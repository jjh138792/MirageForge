// SPDX-License-Identifier: GPL-3.0-only
// Copyright (c) 2025-2026 jjh138792
// MirageForge: https://github.com/jjh138792/MirageForge

#pragma once

#include <cstdio>
#include <cstdint>
#include <cstring>
#include <cinttypes>
#include <string>
#include <vector>
#include <unordered_set>
#include <fcntl.h>
#include <unistd.h>
#include <sys/mman.h>
#include <sys/system_properties.h>
#include "mirage_config.hpp"

namespace mirage {

// 严格对齐 AOSP bionic/libc/system_properties/include/system_properties/prop_area.h & prop_info.h
struct PropAreaHeaderRaw {
    uint32_t bytes_used;
    uint32_t serial;
    uint32_t magic;       // 0x504f5250 ("PROP")
    uint32_t version;     // 0xfc6ed0ab
    uint32_t reserved[28];
};
static_assert(sizeof(PropAreaHeaderRaw) == 128, "PropAreaHeaderRaw must be 128 bytes");

struct PropInfoCowRaw {
    uint32_t serial;
    union {
        char value[PROP_VALUE_MAX];
        struct {
            char error_message[56];
            uint32_t offset;
        } long_property;
    };
    char name[1];
};
static_assert(offsetof(PropInfoCowRaw, value) == 4, "PropInfoCowRaw::value offset mismatch");
static_assert(offsetof(PropInfoCowRaw, name) == 96, "PropInfoCowRaw::name offset mismatch");

constexpr uint32_t kPropAreaMagic   = 0x504f5250u;
constexpr uint32_t kPropAreaVersion = 0xfc6ed0abu;
constexpr uint32_t kPropLongFlag    = 1u << 16;

struct CowSpoofStats {
    uint32_t patched_props = 0;
    uint32_t remapped_vmas = 0;
    uint32_t skipped_props = 0;
};

// 记录已通过私有 COW 原地覆写的属性名：
// 1. 仅开 COW 时实现 0 PLT Hook 隐蔽伪装
// 2. 与常规属性伪装同开时，已 COW 覆写的属性直接交由原生 Bionic 零开销读取真实 /dev/__properties__ 指针
inline std::unordered_set<std::string> g_cow_patched_keys;

inline bool is_cow_patched_key(const char *name) noexcept {
    if (!name || g_cow_patched_keys.empty()) return false;
    return g_cow_patched_keys.find(name) != g_cow_patched_keys.end();
}

inline CowSpoofStats apply_cow_prop_spoof(
    const prop_info *(*orig_find_fn)(const char *),
    FILE *(*orig_fopen_fn)(const char *, const char *),
    int (*orig_fclose_fn)(FILE *)) {
    CowSpoofStats stats{};
    if (!g_cfg.build.cow_prop || !orig_find_fn || g_cfg.build.prop_map.empty()) {
        return stats;
    }

    // 绝对 0 改动安全闸门：绝不允许在 Zygote 本体 (ppid <= 1) 执行属性区重映射
    if (getppid() <= 1) {
        return stats;
    }

    ErrnoRestorer errno_guard;

    struct PendingCowPatch {
        uintptr_t pi_addr;
        const std::string *key;
        const std::string *val;
    };

    // 1. 触发 Bionic 懒加载各目标属性对应的 /dev/__properties__/* 映射，并拿到精确 prop_info* 地址
    std::vector<PendingCowPatch> pending;
    pending.reserve(g_cfg.build.prop_map.size());
    for (const auto &kv : g_cfg.build.prop_map) {
        if (kv.first.empty()) continue;
        const prop_info *pi = orig_find_fn(kv.first.c_str());
        if (pi) {
            pending.push_back({reinterpret_cast<uintptr_t>(pi), &kv.first, &kv.second});
        } else {
            ++stats.skipped_props;
        }
    }
    if (pending.empty()) {
        return stats;
    }

    // 2. 扫描 /proc/self/maps，定位包含目标 prop_info* 的 /dev/__properties__/* 映射区域
    struct PropVma {
        uintptr_t start;
        uintptr_t end;
        uintptr_t offset;
        bool is_shared;
        std::string path;
        std::vector<size_t> patch_indices;
    };

    std::vector<PropVma> vmas;
    FILE *fp = orig_fopen_fn ? orig_fopen_fn("/proc/self/maps", "r") : std::fopen("/proc/self/maps", "r");
    if (!fp) return stats;

    char line[1024];
    while (std::fgets(line, sizeof(line), fp)) {
        if (std::strstr(line, "/dev/__properties__/") == nullptr) {
            continue;
        }
        uintptr_t s = 0, e = 0, off = 0;
        char perms[8] = {0};
        int path_pos = 0;
        if (std::sscanf(line, "%" SCNxPTR "-%" SCNxPTR " %7s %" SCNxPTR " %*x:%*x %*lu %n",
                        &s, &e, perms, &off, &path_pos) < 4) {
            continue;
        }
        if (e <= s || (e - s) < sizeof(PropAreaHeaderRaw)) continue;

        char *p = line + path_pos;
        while (*p == ' ' || *p == '\t') ++p;
        size_t len = std::strlen(p);
        while (len > 0 && (p[len - 1] == '\n' || p[len - 1] == '\r')) p[--len] = '\0';
        if (len == 0 || std::strstr(p, " (deleted)") != nullptr) continue;

        // 跳过目录或非属性区文件（如 properties_serial）
        const auto *hdr = reinterpret_cast<const PropAreaHeaderRaw *>(s);
        if (hdr->magic != kPropAreaMagic || hdr->version != kPropAreaVersion) {
            continue;
        }

        vmas.push_back({s, e, off, perms[3] == 's', std::string(p, len), {}});
    }
    if (orig_fclose_fn) orig_fclose_fn(fp); else std::fclose(fp);

    if (vmas.empty()) {
        stats.skipped_props += static_cast<uint32_t>(pending.size());
        return stats;
    }

    for (size_t i = 0; i < pending.size(); ++i) {
        uintptr_t addr = pending[i].pi_addr;
        bool matched = false;
        for (auto &vma : vmas) {
            if (addr >= vma.start + sizeof(PropAreaHeaderRaw) && addr < vma.end) {
                vma.patch_indices.push_back(i);
                matched = true;
                break;
            }
        }
        if (!matched) {
            ++stats.skipped_props;
        }
    }

    // 3. 仅对实际命中目标属性的 VMA 执行 MAP_PRIVATE | MAP_FIXED 私有写时复制重映射
    for (auto &vma : vmas) {
        if (vma.patch_indices.empty()) continue;

        size_t vma_len = static_cast<size_t>(vma.end - vma.start);
        bool writable = false;

        if (vma.is_shared) {
            int pfd = open(vma.path.c_str(), O_RDONLY | O_CLOEXEC);
            if (pfd < 0) {
                stats.skipped_props += static_cast<uint32_t>(vma.patch_indices.size());
                continue;
            }
            void *remapped = mmap(
                reinterpret_cast<void *>(vma.start),
                vma_len,
                PROT_READ | PROT_WRITE,
                MAP_PRIVATE | MAP_FIXED,
                pfd,
                static_cast<off_t>(vma.offset)
            );
            close(pfd);
            if (remapped == MAP_FAILED || reinterpret_cast<uintptr_t>(remapped) != vma.start) {
                stats.skipped_props += static_cast<uint32_t>(vma.patch_indices.size());
                continue;
            }
            writable = true;
        } else {
            if (mprotect(reinterpret_cast<void *>(vma.start), vma_len, PROT_READ | PROT_WRITE) == 0) {
                writable = true;
            }
        }

        if (!writable) {
            stats.skipped_props += static_cast<uint32_t>(vma.patch_indices.size());
            continue;
        }

        ++stats.remapped_vmas;
        uint32_t vma_patched = 0;

        for (size_t idx : vma.patch_indices) {
            const auto &job = pending[idx];
            const std::string &key = *job.key;
            const std::string &val = *job.val;

            if (job.pi_addr + offsetof(PropInfoCowRaw, name) + key.size() + 1 > vma.end) {
                ++stats.skipped_props;
                continue;
            }

            auto *pi = reinterpret_cast<PropInfoCowRaw *>(job.pi_addr);
            // 100% ABI 校验：确保该偏移处确实是目标属性名，防止跨版本偏移错位
            if (std::memcmp(pi->name, key.data(), key.size() + 1) != 0) {
                ++stats.skipped_props;
                continue;
            }

            uint32_t old_serial = __atomic_load_n(&pi->serial, __ATOMIC_RELAXED);
            bool patched = false;

            if (val.size() < PROP_VALUE_MAX) {
                std::memset(pi->value, 0, PROP_VALUE_MAX);
                std::memcpy(pi->value, val.data(), val.size());
                uint32_t counter = (old_serial + 2u) & 0x0000FFFEu;
                uint32_t new_serial = (static_cast<uint32_t>(val.size()) << 24) | counter;
                __atomic_store_n(&pi->serial, new_serial, __ATOMIC_RELEASE);
                patched = true;
            } else if ((old_serial & kPropLongFlag) != 0) {
                uint32_t loff = pi->long_property.offset;
                if (loff > 0 && job.pi_addr + loff < vma.end) {
                    char *long_dst = reinterpret_cast<char *>(job.pi_addr + loff);
                    size_t max_avail = static_cast<size_t>(vma.end - (job.pi_addr + loff));
                    size_t old_len = strnlen(long_dst, max_avail);
                    if (val.size() <= old_len) {
                        std::memcpy(long_dst, val.data(), val.size());
                        long_dst[val.size()] = '\0';
                        uint32_t new_serial = ((old_serial + 2u) & 0x0000FFFEu) | kPropLongFlag;
                        __atomic_store_n(&pi->serial, new_serial, __ATOMIC_RELEASE);
                        patched = true;
                    }
                }
            }

            if (patched) {
                g_cow_patched_keys.insert(key);
                ++stats.patched_props;
                ++vma_patched;
            } else {
                ++stats.skipped_props;
            }
        }

        if (vma_patched > 0) {
            auto *area = reinterpret_cast<PropAreaHeaderRaw *>(vma.start);
            __atomic_fetch_add(&area->serial, 2u, __ATOMIC_RELEASE);
        }

        // 立刻恢复只读权限 (r--p)，不留可写痕迹
        mprotect(reinterpret_cast<void *>(vma.start), vma_len, PROT_READ);
    }

    return stats;
}

} // namespace mirage

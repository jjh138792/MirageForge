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
#include <unordered_map>
#include <unordered_set>
#include <fcntl.h>
#include <unistd.h>
#include <sys/mman.h>
#include <sys/system_properties.h>
#include "mirage_config.hpp"
#include "bionic/prop_trie_patcher.hpp"

namespace mirage {

struct NativePropSpoofStats {
    uint32_t patched_existing = 0;
    uint32_t inserted_new     = 0;
    uint32_t remapped_vmas    = 0;
    uint32_t verified_ok      = 0;
    uint32_t skipped_props    = 0;
};

// 记录已通过原生 Bionic 读回核验的属性键（供纯 COW 与 MountNS 两种模式共用）
inline std::unordered_set<std::string> g_native_prop_patched_keys;

inline bool is_native_prop_patched(const char *name) noexcept {
    if (!name || g_native_prop_patched_keys.empty()) return false;
    return g_native_prop_patched_keys.find(name) != g_native_prop_patched_keys.end();
}

struct PropAreaVmaInfo {
    uintptr_t start  = 0;
    uintptr_t end    = 0;
    uintptr_t offset = 0;
    bool is_shared   = false;
    std::string vma_path;
    std::string ctx_name;
};

inline std::vector<PropAreaVmaInfo> scan_self_prop_area_vmas(
    FILE *(*orig_fopen_fn)(const char *, const char *),
    int (*orig_fclose_fn)(FILE *)) {
    std::vector<PropAreaVmaInfo> vmas;
    FILE *fp = orig_fopen_fn ? orig_fopen_fn("/proc/self/maps", "r") : std::fopen("/proc/self/maps", "r");
    if (!fp) return vmas;

    char line[1024];
    while (std::fgets(line, sizeof(line), fp)) {
        if (std::strstr(line, "/__properties__/") == nullptr) {
            continue;
        }
        uintptr_t s = 0, e = 0, off = 0;
        char perms[8] = {0};
        int path_pos = 0;
        if (std::sscanf(line, "%" SCNxPTR "-%" SCNxPTR " %7s %" SCNxPTR " %*x:%*x %*lu %n",
                        &s, &e, perms, &off, &path_pos) < 4) {
            continue;
        }
        if (e <= s || (e - s) <= sizeof(PropAreaHeaderRaw)) continue;

        char *p = line + path_pos;
        while (*p == ' ' || *p == '\t') ++p;
        size_t len = std::strlen(p);
        while (len > 0 && (p[len - 1] == '\n' || p[len - 1] == '\r')) p[--len] = '\0';
        if (len == 0 || std::strstr(p, " (deleted)") != nullptr) continue;

        const auto *base = reinterpret_cast<const uint8_t *>(s);
        if (!is_valid_prop_area_header(base, static_cast<size_t>(e - s))) {
            continue;
        }

        PropAreaVmaInfo info;
        info.start = s;
        info.end = e;
        info.offset = off;
        info.is_shared = (perms[3] == 's');
        info.vma_path.assign(p, len);
        info.ctx_name = prop_path_basename(info.vma_path);
        vmas.push_back(std::move(info));
    }

    if (orig_fclose_fn) orig_fclose_fn(fp); else std::fclose(fp);
    return vmas;
}

using T_NativeReadCb = void (*)(void *, const char *, const char *, uint32_t);

inline uint32_t verify_native_prop_spoof(
    const prop_info *(*orig_find_fn)(const char *),
    void (*orig_read_cb_fn)(const prop_info *, T_NativeReadCb, void *),
    int (*orig_get_fn)(const char *, char *)) {
    g_native_prop_patched_keys.clear();
    if (!orig_find_fn) return 0;

    uint32_t ok_cnt = 0;
    for (const auto &kv : g_cfg.build.prop_map) {
        const prop_info *pi = orig_find_fn(kv.first.c_str());
        if (!pi) continue;

        std::string actual;
        if (orig_read_cb_fn) {
            auto reader = [](void *c, const char *, const char *val, uint32_t) {
                if (val) *static_cast<std::string *>(c) = val;
            };
            orig_read_cb_fn(pi, reader, &actual);
        } else if (orig_get_fn) {
            char buf[PROP_VALUE_MAX] = {0};
            if (orig_get_fn(kv.first.c_str(), buf) >= 0) actual = buf;
        }

        if (actual == kv.second) {
            g_native_prop_patched_keys.insert(kv.first);
            ++ok_cnt;
        }
    }
    return ok_cnt;
}

// ============================================================================
// 纯 COW Prop 引擎：
// - 已有属性沿用原版精确 pi_addr 直写快路径 + ABI 校验（支持超长属性重分配）
// - 宿主缺失的跨品牌属性通过 property_info 解析归属区并在 prop_trie 尾部原生插入
// ============================================================================
inline NativePropSpoofStats apply_cow_prop_spoof(
    const prop_info *(*orig_find_fn)(const char *),
    void (*orig_read_cb_fn)(const prop_info *, T_NativeReadCb, void *),
    int (*orig_get_fn)(const char *, char *),
    FILE *(*orig_fopen_fn)(const char *, const char *),
    int (*orig_fclose_fn)(FILE *)) {
    NativePropSpoofStats stats{};
    if (!g_cfg.build.enabled || !orig_find_fn || g_cfg.build.prop_map.empty()) {
        return stats;
    }

    // 绝对 0 改动安全闸门：绝不允许在 Zygote 本体 (ppid <= 1) 执行属性区重映射
    if (getppid() <= 1) {
        return stats;
    }

    ErrnoRestorer errno_guard;

    struct ExistingMatch {
        uintptr_t pi_addr;
        std::string key;
        std::string val;
    };
    std::vector<ExistingMatch> existing;
    std::vector<std::pair<std::string, std::string>> missing;
    existing.reserve(g_cfg.build.prop_map.size());

    // 1. 触发 Bionic 懒加载全部目标属性的上下文属性区
    for (const auto &kv : g_cfg.build.prop_map) {
        if (kv.first.empty()) continue;
        const prop_info *pi = orig_find_fn(kv.first.c_str());
        if (pi) {
            existing.push_back({reinterpret_cast<uintptr_t>(pi), kv.first, kv.second});
        } else {
            missing.emplace_back(kv.first, kv.second);
        }
    }

    // 2. 扫描当前进程映射的属性区（同时兼容 /dev/__properties__/ 与云机 /__properties__/）
    std::vector<PropAreaVmaInfo> vmas = scan_self_prop_area_vmas(orig_fopen_fn, orig_fclose_fn);
    if (vmas.empty()) {
        stats.skipped_props = static_cast<uint32_t>(g_cfg.build.prop_map.size());
        return stats;
    }

    std::unordered_map<std::string, size_t> ctx_to_vma_idx;
    for (size_t i = 0; i < vmas.size(); ++i) {
        ctx_to_vma_idx[vmas[i].ctx_name] = i;
    }

    std::vector<std::vector<ExistingMatch>> vma_existing(vmas.size());
    std::vector<std::vector<std::pair<std::string, std::string>>> vma_missing(vmas.size());

    for (auto &ex : existing) {
        bool placed = false;
        for (size_t i = 0; i < vmas.size(); ++i) {
            if (ex.pi_addr >= vmas[i].start + sizeof(PropAreaHeaderRaw) && ex.pi_addr < vmas[i].end) {
                vma_existing[i].push_back(std::move(ex));
                placed = true;
                break;
            }
        }
        if (!placed) ++stats.skipped_props;
    }

    if (!missing.empty()) {
        PropertyInfoContextResolver resolver;
        bool has_resolver = resolver.load_and_verify();

        size_t default_vma_idx = vmas.size();
        for (const char *def_ctx : {"u:object_r:default_prop:s0", "u:object_r:exported_default_prop:s0"}) {
            auto it = ctx_to_vma_idx.find(def_ctx);
            if (it != ctx_to_vma_idx.end()) {
                default_vma_idx = it->second;
                break;
            }
        }

        for (const auto &mk : missing) {
            size_t target_idx = vmas.size();
            if (has_resolver) {
                std::string ctx = resolver.resolve_context(mk.first);
                if (!ctx.empty()) {
                    auto it = ctx_to_vma_idx.find(ctx);
                    if (it != ctx_to_vma_idx.end()) {
                        target_idx = it->second;
                    }
                } else if (default_vma_idx < vmas.size()) {
                    target_idx = default_vma_idx;
                }
            } else if (default_vma_idx < vmas.size()) {
                target_idx = default_vma_idx;
            }

            if (target_idx < vmas.size()) {
                vma_missing[target_idx].emplace_back(mk.first, mk.second);
            } else {
                ++stats.skipped_props;
            }
        }
    }

    // 3. 仅对有任务的 VMA 执行 MAP_PRIVATE | MAP_FIXED -> 原地直写 + 尾部插节点 -> 恢复 r--p
    for (size_t i = 0; i < vmas.size(); ++i) {
        if (vma_existing[i].empty() && vma_missing[i].empty()) continue;

        const auto &vma = vmas[i];
        size_t vma_len = static_cast<size_t>(vma.end - vma.start);
        bool writable = false;

        if (vma.is_shared) {
            int pfd = open_prop_backing_file_ro(vma.vma_path);
            if (pfd < 0) {
                stats.skipped_props += static_cast<uint32_t>(vma_existing[i].size() + vma_missing[i].size());
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
                stats.skipped_props += static_cast<uint32_t>(vma_existing[i].size() + vma_missing[i].size());
                continue;
            }
            writable = true;
        } else {
            if (mprotect(reinterpret_cast<void *>(vma.start), vma_len, PROT_READ | PROT_WRITE) == 0) {
                writable = true;
            }
        }

        if (!writable) {
            stats.skipped_props += static_cast<uint32_t>(vma_existing[i].size() + vma_missing[i].size());
            continue;
        }

        ++stats.remapped_vmas;

        auto *hdr = reinterpret_cast<PropAreaHeaderRaw *>(vma.start);
        uint8_t *area_data = reinterpret_cast<uint8_t *>(vma.start) + sizeof(PropAreaHeaderRaw);
        const size_t data_cap = vma_len - sizeof(PropAreaHeaderRaw);
        uint32_t direct_patched = 0;

        // 3a. 已有属性：精确 pi_addr 原地覆写 + ABI 键名校验
        for (const auto &ex : vma_existing[i]) {
            if (ex.pi_addr < vma.start + sizeof(PropAreaHeaderRaw) ||
                ex.pi_addr + kPropInfoHdrSize + ex.key.size() + 1 > vma.end) {
                ++stats.skipped_props;
                continue;
            }
            auto *pi = reinterpret_cast<PropInfoRaw *>(ex.pi_addr);
            if (std::memcmp(pi->name, ex.key.data(), ex.key.size() + 1) != 0) {
                ++stats.skipped_props;
                continue;
            }
            uint32_t pi_off = static_cast<uint32_t>(ex.pi_addr - (vma.start + sizeof(PropAreaHeaderRaw)));
            if (write_prop_info_value(hdr, area_data, data_cap, pi_off, ex.val, /*allow_alloc_long=*/true)) {
                ++stats.patched_existing;
                ++direct_patched;
            } else {
                ++stats.skipped_props;
            }
        }

        // 3b. 宿主缺失的跨品牌属性：在 128KB 属性区尾部原生插入 prop_bt + prop_info
        PropAreaPatchResult pres{};
        if (!vma_missing[i].empty()) {
            pres = patch_prop_area_buffer(
                reinterpret_cast<uint8_t *>(vma.start),
                vma_len,
                vma_missing[i],
                /*allow_insert=*/true
            );
            stats.patched_existing += pres.patched_existing;
            stats.inserted_new     += pres.inserted_new;
            stats.skipped_props    += pres.skipped;
        }

        if (direct_patched > 0 && pres.patched_existing == 0 && pres.inserted_new == 0) {
            __atomic_fetch_add(&hdr->serial, 2u, __ATOMIC_RELEASE);
        }

        // 立刻恢复只读权限 (r--p)
        mprotect(reinterpret_cast<void *>(vma.start), vma_len, PROT_READ);
    }

    // 4. 真实读回核验
    stats.verified_ok = verify_native_prop_spoof(orig_find_fn, orig_read_cb_fn, orig_get_fn);
    return stats;
}

} // namespace mirage

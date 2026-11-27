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

// 记录已通过原生 Bionic 读回核验的属性键（供 COW 与 MountNS 两种纯原生模式共用）
inline std::unordered_set<std::string> g_native_prop_patched_keys;

inline bool is_native_prop_patched(const char *name) noexcept {
    if (!name || g_native_prop_patched_keys.empty()) return false;
    return g_native_prop_patched_keys.find(name) != g_native_prop_patched_keys.end();
}

struct PropAreaVmaInfo {
    uintptr_t start = 0;
    uintptr_t end   = 0;
    uintptr_t offset = 0;
    bool is_shared  = false;
    std::string vma_path;
    std::string ctx_name;
};

// 扫描当前进程全部合法属性区映射（同时识别标准 /dev/__properties__/ 与云机容器 /__properties__/）
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

// 真实读回核验：通过原生 Bionic 查询每个属性并比对结果，绝不虚报成功
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
// 纯 COW Prop 引擎：支持原地覆写 + prop_trie 尾部原生插入跨品牌新属性
// 绝不分配 [anon:SystemProperties]，绝不污染宿主机或 Zygote
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

    // 绝对 0 改动安全闸门：绝不在 Zygote 本体 (ppid <= 1) 执行属性区私有重映射
    if (getppid() <= 1) {
        return stats;
    }

    ErrnoRestorer errno_guard;

    // 1. 触发 Bionic 懒加载所有目标属性对应的上下文属性区 VMA（即使属性不存在，Bionic 也会映射其归属上下文区）
    struct ExistingMatch {
        uintptr_t pi_addr;
        std::string key;
        std::string val;
    };
    std::vector<ExistingMatch> existing;
    std::vector<std::pair<std::string, std::string>> missing;
    existing.reserve(g_cfg.build.prop_map.size());

    for (const auto &kv : g_cfg.build.prop_map) {
        if (kv.first.empty()) continue;
        const prop_info *pi = orig_find_fn(kv.first.c_str());
        if (pi) {
            existing.push_back({reinterpret_cast<uintptr_t>(pi), kv.first, kv.second});
        } else {
            missing.emplace_back(kv.first, kv.second);
        }
    }

    // 2. 扫描当前进程映射的合法属性区
    std::vector<PropAreaVmaInfo> vmas = scan_self_prop_area_vmas(orig_fopen_fn, orig_fclose_fn);
    if (vmas.empty()) {
        stats.skipped_props = static_cast<uint32_t>(g_cfg.build.prop_map.size());
        return stats;
    }

    std::unordered_map<std::string, size_t> ctx_to_vma_idx;
    for (size_t i = 0; i < vmas.size(); ++i) {
        ctx_to_vma_idx[vmas[i].ctx_name] = i;
    }

    std::vector<std::vector<std::pair<std::string, std::string>>> vma_jobs(vmas.size());

    // 2a. 已有属性直接按地址归组
    for (const auto &ex : existing) {
        bool placed = false;
        for (size_t i = 0; i < vmas.size(); ++i) {
            if (ex.pi_addr >= vmas[i].start + sizeof(PropAreaHeaderRaw) && ex.pi_addr < vmas[i].end) {
                vma_jobs[i].emplace_back(ex.key, ex.val);
                placed = true;
                break;
            }
        }
        if (!placed) ++stats.skipped_props;
    }

    // 2b. 宿主不存在的新增属性：通过 property_info 二进制树解析归属上下文并归组准备尾部插入
    if (!missing.empty()) {
        PropertyInfoContextResolver resolver;
        bool has_resolver = resolver.load_and_verify();

        // 兜底上下文索引（若目标上下文恰好未映射，优先回落至 default_prop）
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
                }
            }
            if (target_idx == vmas.size() && default_vma_idx < vmas.size()) {
                target_idx = default_vma_idx;
            }
            if (target_idx < vmas.size()) {
                vma_jobs[target_idx].emplace_back(mk.first, mk.second);
            } else {
                ++stats.skipped_props;
            }
        }
    }

    // 3. 逐个 VMA 执行 MAP_PRIVATE | MAP_FIXED 私有写时复制 -> 原地覆写 + 尾部插节点 -> 恢复 PROT_READ
    for (size_t i = 0; i < vmas.size(); ++i) {
        if (vma_jobs[i].empty()) continue;

        const auto &vma = vmas[i];
        size_t vma_len = static_cast<size_t>(vma.end - vma.start);
        bool writable = false;

        if (vma.is_shared) {
            int pfd = open_prop_backing_file_ro(vma.vma_path);
            if (pfd < 0) {
                stats.skipped_props += static_cast<uint32_t>(vma_jobs[i].size());
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
                stats.skipped_props += static_cast<uint32_t>(vma_jobs[i].size());
                continue;
            }
            writable = true;
        } else {
            if (mprotect(reinterpret_cast<void *>(vma.start), vma_len, PROT_READ | PROT_WRITE) == 0) {
                writable = true;
            }
        }

        if (!writable) {
            stats.skipped_props += static_cast<uint32_t>(vma_jobs[i].size());
            continue;
        }

        ++stats.remapped_vmas;
        PropAreaPatchResult pres = patch_prop_area_buffer(
            reinterpret_cast<uint8_t *>(vma.start),
            vma_len,
            vma_jobs[i],
            /*allow_insert=*/true
        );

        stats.patched_existing += pres.patched_existing;
        stats.inserted_new     += pres.inserted_new;
        stats.skipped_props    += pres.skipped;

        // 立刻恢复只读权限 (r--p)
        mprotect(reinterpret_cast<void *>(vma.start), vma_len, PROT_READ);
    }

    // 4. 真实读回核验（确保 Bionic 实际读出的值与目标 100% 一致）
    stats.verified_ok = verify_native_prop_spoof(orig_find_fn, orig_read_cb_fn, orig_get_fn);
    return stats;
}

} // namespace mirage

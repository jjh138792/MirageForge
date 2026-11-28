// SPDX-License-Identifier: GPL-3.0-only
// Copyright (c) 2025-2026 jjh138792
// MirageForge: https://github.com/jjh138792/MirageForge

#pragma once

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <string>
#include <vector>
#include <utility>
#include <unordered_map>
#include <unordered_set>
#include <atomic>
#include <dirent.h>
#include <signal.h>
#include <unistd.h>
#include <fcntl.h>
#include <sched.h>
#include <pthread.h>
#include <sys/mount.h>
#include <sys/mman.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <sys/wait.h>
#include <sys/xattr.h>
#include "bionic_prop_engine.hpp"
#include "vfs/io_hooks.hpp"

namespace mirage {

// 中性暂存目录：不含任何模块或 Root 特征字样，挂载完即 MNT_DETACH 蒸发
constexpr const char *NEUTRAL_STAGE_ROOT = "/data/local/tmp/.st_ns";
constexpr uint32_t kMaxVfsMountJobs      = 192;
constexpr uint32_t kMaxPropMountJobs     = 64;
constexpr uint32_t kMaxLiveUpdateNodes   = 12;

enum : uint8_t {
    LIVE_NODE_NONE    = 0,
    LIVE_NODE_MEMINFO = 1,
    LIVE_NODE_CPUFREQ = 2,
};

struct MountNsExecSummary {
    uint32_t vfs_requested  = 0;
    uint32_t vfs_mounted    = 0;
    uint32_t prop_vmas_ok   = 0;
    uint32_t prop_patched   = 0;
    uint32_t prop_inserted  = 0;
    uint32_t prop_verified  = 0;
    uint32_t live_nodes     = 0;
    std::string diag;
};

struct MountNsPropChildReport {
    uint8_t mode;       // 0=fail, 1=tmpfs-xattr, 2=tmpfs-ctx, 3=data-xattr
    uint8_t fail_stage; // 0=ok, 1=open_ns, 2=setns, 3=priv, 4=stage_label
    int32_t last_errno;
    uint32_t prop_patched;
    uint32_t prop_inserted;
    uint8_t prop_results[kMaxPropMountJobs];
};

struct MountNsVfsChildReport {
    uint8_t mode;       // 0=fail, 1=tmpfs-xattr, 2=tmpfs-ctx, 3=data-xattr
    uint8_t fail_stage; // 0=ok, 1=open_ns, 2=setns, 3=priv, 4=stage_label
    int32_t last_errno;
    uint8_t live_count;
    uint8_t live_kinds[kMaxLiveUpdateNodes];
    int32_t live_param_a[kMaxLiveUpdateNodes];
    int32_t live_param_b[kMaxLiveUpdateNodes];
    uint8_t vfs_results[kMaxVfsMountJobs];
};

// ============================================================================
// 固定等宽字节长度的 /proc/meminfo 格式化器（严禁截断、0 读端撕裂）
// ============================================================================
inline std::string format_meminfo_fixed_width(int ram_gb, int jitter_permille) {
    uint64_t total_kb = calc_realistic_memtotal_kb(ram_gb);
    int64_t delta_avail = (static_cast<int64_t>(total_kb) * jitter_permille) / 1000LL;
    uint64_t avail_kb   = static_cast<uint64_t>(static_cast<int64_t>((total_kb * 57ULL) / 100ULL) + delta_avail);
    uint64_t free_kb    = static_cast<uint64_t>(static_cast<int64_t>((total_kb * 23ULL) / 100ULL) + (delta_avail / 2));
    uint64_t cached_kb  = (total_kb * 28ULL) / 100ULL;
    uint64_t active_kb  = static_cast<uint64_t>(static_cast<int64_t>((total_kb * 36ULL) / 100ULL) - (delta_avail / 2));
    uint64_t inactive_kb = (total_kb * 22ULL) / 100ULL;
    uint64_t sreclaim_kb = (total_kb * 4ULL) / 100ULL;
    uint64_t sunreclaim_kb = (total_kb * 3ULL) / 100ULL;
    uint64_t mapped_kb  = (total_kb * 8ULL) / 100ULL;
    uint64_t shmem_kb   = (total_kb * 2ULL) / 100ULL;

    char buf[1024];
    int n = std::snprintf(
        buf, sizeof(buf),
        "MemTotal:     %10llu kB\n"
        "MemFree:      %10llu kB\n"
        "MemAvailable: %10llu kB\n"
        "Buffers:          184320 kB\n"
        "Cached:       %10llu kB\n"
        "SwapCached:            0 kB\n"
        "Active:       %10llu kB\n"
        "Inactive:     %10llu kB\n"
        "Shmem:        %10llu kB\n"
        "KReclaimable: %10llu kB\n"
        "Slab:         %10llu kB\n"
        "SReclaimable: %10llu kB\n"
        "SUnreclaim:   %10llu kB\n"
        "Mapped:       %10llu kB\n"
        "SwapTotal:       8388608 kB\n"
        "SwapFree:        8126464 kB\n",
        static_cast<unsigned long long>(total_kb),
        static_cast<unsigned long long>(free_kb),
        static_cast<unsigned long long>(avail_kb),
        static_cast<unsigned long long>(cached_kb),
        static_cast<unsigned long long>(active_kb),
        static_cast<unsigned long long>(inactive_kb),
        static_cast<unsigned long long>(shmem_kb),
        static_cast<unsigned long long>(sreclaim_kb),
        static_cast<unsigned long long>(sreclaim_kb + sunreclaim_kb),
        static_cast<unsigned long long>(sreclaim_kb),
        static_cast<unsigned long long>(sunreclaim_kb),
        static_cast<unsigned long long>(mapped_kb)
    );
    return (n > 0) ? std::string(buf, static_cast<size_t>(n)) : "";
}

// ============================================================================
// App 侧物理自检（以 untrusted_app 身份验证 bind-mount 仍存活且 SELinux 允许读取）
// - 原生 procfs 文件 st_size 为 0；原生 sysfs 节点 st_size 为 4096
// - 挂载后的 tmpfs 副本 st_size 等于实际写入字节数，且能成功 read 1 字节
// ============================================================================
inline bool verify_mounted_vfs_node_as_app(const std::string &path, size_t expected_len) noexcept {
    if (path.empty() || expected_len == 0) return false;
    int fd = orig_open ? orig_open(path.c_str(), O_RDONLY | O_CLOEXEC, 0)
                       : open(path.c_str(), O_RDONLY | O_CLOEXEC);
    if (fd < 0) return false;

    struct stat st{};
    bool ok = false;
    if (fstat(fd, &st) == 0 && st.st_size > 0) {
        bool size_distinguished = true;
        if (path.rfind("/sys/", 0) == 0 && expected_len != 4096 && st.st_size == 4096) {
            size_distinguished = false;
        }
        long long diff = static_cast<long long>(st.st_size) - static_cast<long long>(expected_len);
        if (diff < 0) diff = -diff;

        if (size_distinguished && diff <= 16) {
            char probe_ch = 0;
            ssize_t nr = read(fd, &probe_ch, 1);
            ok = (nr == 1);
        }
    }

    if (orig_close) orig_close(fd); else close(fd);
    return ok;
}

// ============================================================================
// Phase 1 (App 侧，preAppSpecialize)：仅挂载系统属性区并立即 MAP_SHARED 钉入地址空间
// ============================================================================
inline MountNsExecSummary request_prop_mount_ns_in_pre_specialize(int fd) {
    MountNsExecSummary summary{};
    auto send_skip = [&](const std::string &reason) -> MountNsExecSummary {
        uint8_t zero = 0;
        write_full(fd, &zero, sizeof(zero));
        summary.diag = reason;
        return summary;
    };

    bool want_prop_ns = (g_cfg.build.enabled && g_cfg.build.prop_mode == PropSpoofMode::MOUNT_NS);
    if (fd < 0 || !want_prop_ns || !orig_system_property_find || g_cfg.build.prop_map.empty()) {
        return send_skip("prop_ns_disabled");
    }

    pid_t ppid = getppid();
    if (ppid <= 1) {
        return send_skip("skip=ppid_is_init");
    }

    struct PropMountJob {
        PropAreaVmaInfo vma;
        std::string mount_target;
        std::vector<std::pair<std::string, std::string>> kvs;
    };
    std::vector<PropMountJob> prop_jobs;

    struct ExistingEntry {
        uintptr_t pi_addr;
        std::string key;
        std::string val;
    };
    std::vector<ExistingEntry> existing;
    std::vector<std::pair<std::string, std::string>> missing;
    existing.reserve(g_cfg.build.prop_map.size());

    for (const auto &kv : g_cfg.build.prop_map) {
        if (kv.first.empty()) continue;
        const prop_info *pi = orig_system_property_find(kv.first.c_str());
        if (pi) {
            existing.push_back({reinterpret_cast<uintptr_t>(pi), kv.first, kv.second});
        } else {
            missing.emplace_back(kv.first, kv.second);
        }
    }

    std::vector<PropAreaVmaInfo> vmas = scan_self_prop_area_vmas(orig_fopen, orig_fclose);
    std::unordered_map<std::string, size_t> ctx_to_idx;
    for (size_t i = 0; i < vmas.size(); ++i) {
        ctx_to_idx[vmas[i].ctx_name] = i;
    }
    std::vector<std::vector<std::pair<std::string, std::string>>> grouped(vmas.size());

    for (const auto &ex : existing) {
        for (size_t i = 0; i < vmas.size(); ++i) {
            if (ex.pi_addr >= vmas[i].start + sizeof(PropAreaHeaderRaw) && ex.pi_addr < vmas[i].end) {
                grouped[i].emplace_back(ex.key, ex.val);
                break;
            }
        }
    }

    if (!missing.empty() && !vmas.empty()) {
        PropertyInfoContextResolver resolver;
        bool has_res = resolver.load_and_verify();
        size_t def_idx = vmas.size();
        for (const char *dc : {"u:object_r:default_prop:s0", "u:object_r:exported_default_prop:s0"}) {
            auto it = ctx_to_idx.find(dc);
            if (it != ctx_to_idx.end()) {
                def_idx = it->second;
                break;
            }
        }
        for (const auto &mk : missing) {
            size_t ti = vmas.size();
            if (has_res) {
                std::string ctx = resolver.resolve_context(mk.first);
                if (!ctx.empty()) {
                    auto it = ctx_to_idx.find(ctx);
                    if (it != ctx_to_idx.end()) ti = it->second;
                } else if (def_idx < vmas.size()) {
                    ti = def_idx;
                }
            } else if (def_idx < vmas.size()) {
                ti = def_idx;
            }
            if (ti < vmas.size()) {
                grouped[ti].emplace_back(mk.first, mk.second);
            }
        }
    }

    for (size_t i = 0; i < vmas.size() && prop_jobs.size() < kMaxPropMountJobs; ++i) {
        if (grouped[i].empty()) continue;
        std::string tgt = vmas[i].vma_path;
        if (access(tgt.c_str(), F_OK) != 0 && tgt.rfind("/__properties__/", 0) == 0) {
            std::string dev_p = "/dev" + tgt;
            if (access(dev_p.c_str(), F_OK) == 0) tgt = dev_p;
        }
        if (access(tgt.c_str(), F_OK) != 0) continue;
        prop_jobs.push_back({vmas[i], std::move(tgt), std::move(grouped[i])});
    }

    if (prop_jobs.empty()) {
        return send_skip("prop_ns_no_vmas");
    }

    if (unshare(CLONE_NEWNS) != 0) {
        return send_skip("prop_unshare_failed(errno=" + std::to_string(errno) + ")");
    }

    uint8_t want_flag = 1;
    int32_t ppid_i32 = static_cast<int32_t>(ppid);
    uint32_t prop_count = static_cast<uint32_t>(prop_jobs.size());

    if (!write_full(fd, &want_flag, sizeof(want_flag)) ||
        !write_full(fd, &ppid_i32, sizeof(ppid_i32)) ||
        !write_full(fd, &prop_count, sizeof(prop_count))) {
        summary.diag = "prop_ipc_write_hdr_failed";
        return summary;
    }

    for (const auto &pjob : prop_jobs) {
        uint32_t kvc = static_cast<uint32_t>(pjob.kvs.size());
        if (!write_str(fd, pjob.mount_target) || !write_full(fd, &kvc, sizeof(kvc))) {
            summary.diag = "prop_ipc_write_job_failed";
            return summary;
        }
        for (const auto &kv : pjob.kvs) {
            if (!write_str(fd, kv.first) || !write_str(fd, kv.second)) {
                summary.diag = "prop_ipc_write_kv_failed";
                return summary;
            }
        }
    }

    std::vector<uint8_t> prop_results(prop_count, 0);
    if (!read_full(fd, prop_results.data(), prop_results.size()) ||
        !read_full(fd, &summary.prop_patched, sizeof(summary.prop_patched)) ||
        !read_full(fd, &summary.prop_inserted, sizeof(summary.prop_inserted))) {
        summary.diag = "prop_ipc_read_res_failed";
        return summary;
    }
    summary.diag = read_str(fd);

    // 零脏页原地刷新：以 PROT_READ | MAP_SHARED | MAP_FIXED 替换旧映射（后续 unmount 不影响已建立映射）
    for (size_t i = 0; i < prop_jobs.size(); ++i) {
        if (prop_results[i] != 1) continue;
        const auto &pj = prop_jobs[i];
        size_t vma_len = static_cast<size_t>(pj.vma.end - pj.vma.start);
        int pfd = open(pj.mount_target.c_str(), O_RDONLY | O_CLOEXEC);
        if (pfd < 0) continue;

        struct stat st{};
        bool ok_map = false;
        if (fstat(pfd, &st) == 0 && st.st_size >= static_cast<off_t>(vma_len)) {
            void *m = mmap(
                reinterpret_cast<void *>(pj.vma.start),
                vma_len,
                PROT_READ,
                MAP_SHARED | MAP_FIXED,
                pfd,
                static_cast<off_t>(pj.vma.offset)
            );
            if (m != MAP_FAILED && reinterpret_cast<uintptr_t>(m) == pj.vma.start) {
                ok_map = true;
            }
        }
        close(pfd);
        if (ok_map) {
            g_ns_mounted_prop_paths.insert(pj.mount_target);
            ++summary.prop_vmas_ok;
        }
    }

    if (summary.prop_vmas_ok > 0) {
        g_prop_ns_mounted.store(true, std::memory_order_release);
        summary.prop_verified = verify_native_prop_spoof(
            orig_system_property_find,
            orig_system_property_read_callback,
            orig_system_property_get
        );
    }
    return summary;
}

// ============================================================================
// Phase 2 (App 侧，postAppSpecialize)：在 SpecializeCommon 与 KernelSU/Zygisk Next
// unmount 清扫全部完成后挂载 VFS 节点，记录全部符号链接别名并做可读性物理自检
// ============================================================================
inline MountNsExecSummary request_vfs_mount_ns_in_post_specialize(int fd) {
    MountNsExecSummary summary{};
    auto send_skip = [&](const std::string &reason) -> MountNsExecSummary {
        uint8_t zero = 0;
        write_full(fd, &zero, sizeof(zero));
        summary.diag = reason;
        return summary;
    };

    if (fd < 0 || !g_cfg.vfs.any_enabled() || !g_cfg.vfs.enable_mount_ns) {
        return send_skip("vfs_ns_disabled");
    }

    pid_t ppid = getppid();
    if (ppid <= 1) {
        return send_skip("vfs_skip=ppid_is_init");
    }

    struct VfsMountJob {
        std::string path;
        std::string content;
        uint8_t live_kind = LIVE_NODE_NONE;
        int32_t param_a   = 0;
        int32_t param_b   = 0;
        std::vector<std::string> aliases;
    };
    std::vector<VfsMountJob> vfs_jobs;
    std::unordered_map<std::string, size_t> canonical_to_idx;
    vfs_jobs.reserve(64);

    auto add_vfs_node = [&](const std::string &path, uint8_t live_kind = LIVE_NODE_NONE, int32_t pa = 0, int32_t pb = 0) {
        std::string content;
        if (live_kind == LIVE_NODE_MEMINFO && g_cfg.vfs.ram_gb > 0) {
            content = format_meminfo_fixed_width(g_cfg.vfs.ram_gb, 0);
        } else {
            int dyn = -1;
            if (!synthesize_vfs_node_on_demand(path, &content, &dyn) || content.empty()) {
                return;
            }
        }
        if (access(path.c_str(), F_OK) != 0 && errno == ENOENT) {
            return;
        }

        char resolved[PATH_MAX];
        std::string canonical = (realpath(path.c_str(), resolved) != nullptr) ? std::string(resolved) : path;

        auto it = canonical_to_idx.find(canonical);
        if (it != canonical_to_idx.end()) {
            // 同一物理目标的符号链接别名（如 cpu0..3/cpufreq/* -> cpufreq/policy0/*）：
            // 仅在伪装内容或频率范围一致时注册为别名，避免跨异构簇串台
            VfsMountJob &existing = vfs_jobs[it->second];
            bool compatible = (live_kind == LIVE_NODE_CPUFREQ && existing.live_kind == LIVE_NODE_CPUFREQ &&
                               existing.param_a == pa && existing.param_b == pb) ||
                              (live_kind == LIVE_NODE_NONE && existing.content == content);
            if (compatible && path != canonical) {
                existing.aliases.push_back(path);
            }
            return;
        }

        if (vfs_jobs.size() >= kMaxVfsMountJobs) return;

        VfsMountJob job{};
        job.path = canonical;
        job.content = std::move(content);
        job.live_kind = live_kind;
        job.param_a = pa;
        job.param_b = pb;
        job.aliases.push_back(canonical);
        if (path != canonical) {
            job.aliases.push_back(path);
        }

        canonical_to_idx[canonical] = vfs_jobs.size();
        vfs_jobs.push_back(std::move(job));
    };

    if (g_cfg.vfs.enable_cpu) {
        add_vfs_node("/proc/cpuinfo");
    }
    if (g_cfg.vfs.enable_mem) {
        add_vfs_node("/proc/meminfo", LIVE_NODE_MEMINFO, g_cfg.vfs.ram_gb, 0);
        add_vfs_node("/proc/ddr_info");
        add_vfs_node("/sys/devices/system/chip-info/ddr_info");
        add_vfs_node("/sys/devices/system/chip-info/ddr_type");
        add_vfs_node("/sys/devices/system/chip-info/ddr_size");
        add_vfs_node("/sys/firmware/devicetree/base/memory/ddr_device_type");
    }
    if (g_cfg.vfs.enable_vfs_nodes) {
        static const char *const kSysNodes[] = {
            "/proc/version",
            "/proc/sys/kernel/osrelease",
            "/sys/class/kgsl/kgsl-3d0/gpu_model",
            "/sys/devices/soc0/soc_id",
            "/sys/devices/soc0/family",
            "/sys/devices/soc0/machine",
            "/sys/devices/system/cpu/present",
            "/sys/devices/system/cpu/possible",
            "/sys/devices/system/cpu/online",
            "/sys/devices/system/cpu/offline",
            "/sys/devices/system/cpu/kernel_max",
        };
        for (const char *p : kSysNodes) add_vfs_node(p);

        int cores = (g_cfg.vfs.cpu_cores > 0 && g_cfg.vfs.cpu_cores <= 16) ? g_cfg.vfs.cpu_cores : 0;
        for (int i = 0; i < cores; ++i) {
            const CpuClusterSpec *cl = find_cluster_for_core(i);
            int min_f = cl ? cl->min_freq_khz : 614400;
            int max_f = cl ? cl->max_freq_khz : 3532800;
            std::string pfx = "/sys/devices/system/cpu/cpu" + std::to_string(i) + "/";
            add_vfs_node(pfx + "online");
            add_vfs_node(pfx + "cpu_capacity");
            add_vfs_node(pfx + "regs/identification/midr_el1");
            add_vfs_node(pfx + "topology/core_cpus_list");
            add_vfs_node(pfx + "cpufreq/scaling_cur_freq", LIVE_NODE_CPUFREQ, min_f, max_f);
            add_vfs_node(pfx + "cpufreq/cpuinfo_cur_freq");
            add_vfs_node(pfx + "cpufreq/cpuinfo_max_freq");
            add_vfs_node(pfx + "cpufreq/cpuinfo_min_freq");
            add_vfs_node(pfx + "cpufreq/scaling_max_freq");
            add_vfs_node(pfx + "cpufreq/scaling_min_freq");
            add_vfs_node(pfx + "cpufreq/scaling_governor");
            add_vfs_node(pfx + "cpufreq/scaling_driver");
        }
    }

    summary.vfs_requested = static_cast<uint32_t>(vfs_jobs.size());
    if (vfs_jobs.empty()) {
        return send_skip("vfs_no_nodes");
    }

    uint8_t want_flag = 1;
    int32_t ppid_i32 = static_cast<int32_t>(ppid);
    uint32_t vfs_count = static_cast<uint32_t>(vfs_jobs.size());

    if (!write_full(fd, &want_flag, sizeof(want_flag)) ||
        !write_full(fd, &ppid_i32, sizeof(ppid_i32)) ||
        !write_full(fd, &vfs_count, sizeof(vfs_count))) {
        summary.diag = "vfs_ipc_write_hdr_failed";
        return summary;
    }

    for (const auto &job : vfs_jobs) {
        if (!write_str(fd, job.path) ||
            !write_str(fd, job.content) ||
            !write_full(fd, &job.live_kind, sizeof(job.live_kind)) ||
            !write_full(fd, &job.param_a, sizeof(job.param_a)) ||
            !write_full(fd, &job.param_b, sizeof(job.param_b))) {
            summary.diag = "vfs_ipc_write_body_failed";
            return summary;
        }
    }

    std::vector<uint8_t> vfs_results(vfs_count, 0);
    if (!read_full(fd, vfs_results.data(), vfs_results.size()) ||
        !read_full(fd, &summary.live_nodes, sizeof(summary.live_nodes))) {
        summary.diag = "vfs_ipc_read_res_failed";
        return summary;
    }
    std::string comp_diag = read_str(fd);

    // App 侧真实可读物理自检：仅将确认存活且 untrusted_app 可读的节点（连同符号链接别名）注册为 MountNS
    uint32_t verified_ok = 0;
    uint32_t alias_total = 0;
    for (size_t i = 0; i < vfs_jobs.size(); ++i) {
        if (vfs_results[i] != 1) continue;
        if (!verify_mounted_vfs_node_as_app(vfs_jobs[i].path, vfs_jobs[i].content.size())) {
            continue;
        }
        for (const auto &alias : vfs_jobs[i].aliases) {
            g_ns_mounted_paths.insert(alias);
            ++alias_total;
        }
        ++verified_ok;
    }

    summary.vfs_mounted = verified_ok;
    if (verified_ok > 0) {
        g_vfs_ns_mounted.store(true, std::memory_order_release);
    }

    summary.diag = comp_diag + " verified=" + std::to_string(verified_ok) +
                   "/" + std::to_string(vfs_count) + " aliases=" + std::to_string(alias_total);
    return summary;
}

// ============================================================================
// Companion 侧：活体动态 pwrite 后台线程（仅对纯内存 tmpfs FD 生效，零闪存损耗）
// ============================================================================
struct LiveVfsItem {
    int fd = -1;
    uint8_t kind = LIVE_NODE_NONE;
    int32_t param_a = 0;
    int32_t param_b = 0;
};

struct LiveVfsThreadArgs {
    pid_t app_pid = 0;
    std::vector<LiveVfsItem> items;
};

inline std::atomic<int> g_active_live_threads{0};

inline void *companion_live_vfs_thread(void *arg) {
    auto *ctx = static_cast<LiveVfsThreadArgs *>(arg);
    if (!ctx) return nullptr;

    uint32_t tick = 1;
    while (ctx->app_pid > 0 && kill(ctx->app_pid, 0) == 0) {
        for (int s = 0; s < 5; ++s) {
            usleep(500000);
            if (kill(ctx->app_pid, 0) != 0) goto done;
        }

        ++tick;
        for (size_t i = 0; i < ctx->items.size(); ++i) {
            const auto &it = ctx->items[i];
            if (it.fd < 0) continue;

            if (it.kind == LIVE_NODE_MEMINFO && it.param_a > 0) {
                int jitter = static_cast<int>(((tick * 7u + static_cast<uint32_t>(i) * 5u) % 37u)) - 18;
                std::string next = format_meminfo_fixed_width(it.param_a, jitter);
                if (!next.empty()) {
                    ssize_t r = pwrite(it.fd, next.data(), next.size(), 0);
                    (void)r;
                }
            } else if (it.kind == LIVE_NODE_CPUFREQ && it.param_b > it.param_a && it.param_a > 0) {
                uint32_t step = (tick * 3u + static_cast<uint32_t>(i) * 2u) % 7u;
                long freq = static_cast<long>(it.param_a) +
                            (static_cast<long>(it.param_b - it.param_a) * static_cast<long>(step)) / 6L;
                freq = (freq / 19200L) * 19200L;
                if (freq < it.param_a) freq = it.param_a;
                if (freq > it.param_b) freq = it.param_b;

                char fbuf[32];
                int fn = std::snprintf(fbuf, sizeof(fbuf), "%ld\n", freq);
                if (fn > 0) {
                    ssize_t r = pwrite(it.fd, fbuf, static_cast<size_t>(fn), 0);
                    (void)r;
                    int tr = ftruncate(it.fd, static_cast<off_t>(fn));
                    (void)tr;
                }
            }
        }
    }

done:
    for (const auto &it : ctx->items) {
        if (it.fd >= 0) close(it.fd);
    }
    delete ctx;
    g_active_live_threads.fetch_sub(1, std::memory_order_relaxed);
    return nullptr;
}

inline void cleanup_stale_stage_dirs_throttled() {
    static std::atomic<int64_t> s_last_clean_sec{0};
    int64_t now = static_cast<int64_t>(std::time(nullptr));
    int64_t prev = s_last_clean_sec.load(std::memory_order_relaxed);
    if (now - prev < 60 || !s_last_clean_sec.compare_exchange_strong(prev, now, std::memory_order_acq_rel)) {
        return;
    }

    DIR *dir = opendir(NEUTRAL_STAGE_ROOT);
    if (!dir) return;
    struct dirent *ent = nullptr;
    while ((ent = readdir(dir)) != nullptr) {
        if (ent->d_name[0] < '0' || ent->d_name[0] > '9') continue;
        pid_t old_pid = static_cast<pid_t>(std::atoi(ent->d_name));
        if (old_pid <= 0 || kill(old_pid, 0) == 0) continue;

        std::string sub = std::string(NEUTRAL_STAGE_ROOT) + "/" + ent->d_name;
        DIR *sdir = opendir(sub.c_str());
        if (sdir) {
            struct dirent *fent = nullptr;
            while ((fent = readdir(sdir)) != nullptr) {
                if (fent->d_name[0] == '.') continue;
                unlink((sub + "/" + fent->d_name).c_str());
            }
            closedir(sdir);
        }
        rmdir(sub.c_str());
    }
    closedir(dir);
}

inline bool apply_cloned_selinux_label(const char *src_path, const char *tgt_path, bool force_ctx_mounted) noexcept {
    if (force_ctx_mounted) return true;
    static constexpr const char *kFallbackCtx = "u:object_r:system_file:s0";
    char orig_ctx[192] = {0};
    ssize_t xlen = lgetxattr(tgt_path, "security.selinux", orig_ctx, sizeof(orig_ctx) - 1);
    if (xlen > 0) {
        orig_ctx[xlen] = '\0';
        size_t len_with_nul = std::strlen(orig_ctx) + 1;
        if (lsetxattr(src_path, "security.selinux", orig_ctx, len_with_nul, 0) == 0) {
            return true;
        }
    }
    return lsetxattr(src_path, "security.selinux", kFallbackCtx, std::strlen(kFallbackCtx) + 1, 0) == 0;
}

inline uint8_t prepare_stage_dir_in_worker(
    const char *stage_dir,
    bool root_slave,
    bool *out_need_umount,
    uint8_t *out_fail_stage,
    int32_t *out_errno) noexcept {
    static constexpr const char *kSysFileCtx = "u:object_r:system_file:s0";
    const size_t kCtxLen = std::strlen(kSysFileCtx) + 1;

    *out_need_umount = false;
    mkdir(stage_dir, 0755);

    if (root_slave) {
        if (mount("tmpfs", stage_dir, "tmpfs", MS_NOSUID | MS_NODEV, "size=8M,mode=0755") == 0) {
            mount(nullptr, stage_dir, nullptr, MS_PRIVATE, nullptr);
            char probe_f[192];
            std::snprintf(probe_f, sizeof(probe_f), "%s/.p", stage_dir);
            int pfd = open(probe_f, O_CREAT | O_WRONLY | O_TRUNC | O_CLOEXEC, 0444);
            if (pfd >= 0) {
                close(pfd);
                if (lsetxattr(probe_f, "security.selinux", kSysFileCtx, kCtxLen, 0) == 0) {
                    unlink(probe_f);
                    *out_need_umount = true;
                    return 1; // tmpfs-xattr
                }
                unlink(probe_f);
            }
            umount2(stage_dir, MNT_DETACH);
        }

        if (mount("tmpfs", stage_dir, "tmpfs", MS_NOSUID | MS_NODEV,
                  "size=8M,mode=0755,context=u:object_r:system_file:s0") == 0) {
            mount(nullptr, stage_dir, nullptr, MS_PRIVATE, nullptr);
            *out_need_umount = true;
            return 2; // tmpfs-ctx
        }
    }

    char probe_f[192];
    std::snprintf(probe_f, sizeof(probe_f), "%s/.p", stage_dir);
    int pfd = open(probe_f, O_CREAT | O_WRONLY | O_TRUNC | O_CLOEXEC, 0444);
    if (pfd >= 0) {
        close(pfd);
        if (lsetxattr(probe_f, "security.selinux", kSysFileCtx, kCtxLen, 0) == 0) {
            unlink(probe_f);
            return 3; // data-xattr
        }
        *out_fail_stage = 4;
        *out_errno = errno;
        unlink(probe_f);
    } else {
        *out_fail_stage = 4;
        *out_errno = errno;
    }
    return 0;
}

inline bool check_target_mnt_ns_isolated(int32_t app_pid, int32_t ppid, char *out_app_ns_path, size_t out_sz) noexcept {
    char ppid_ns_path[64];
    char app_ns_id[64] = {0}, ppid_ns_id[64] = {0}, init_ns_id[64] = {0};
    std::snprintf(out_app_ns_path, out_sz, "/proc/%d/ns/mnt", app_pid);
    std::snprintf(ppid_ns_path, sizeof(ppid_ns_path), "/proc/%d/ns/mnt", ppid);

    ssize_t l_app  = readlink(out_app_ns_path, app_ns_id, sizeof(app_ns_id) - 1);
    ssize_t l_ppid = (ppid > 0) ? readlink(ppid_ns_path, ppid_ns_id, sizeof(ppid_ns_id) - 1) : -1;
    ssize_t l_init = readlink("/proc/1/ns/mnt", init_ns_id, sizeof(init_ns_id) - 1);

    return (l_app > 0) &&
           (l_init <= 0 || std::strcmp(app_ns_id, init_ns_id) != 0) &&
           (l_ppid <= 0 || std::strcmp(app_ns_id, ppid_ns_id) != 0);
}

// ============================================================================
// Phase 1 (Companion 侧)：处理系统属性区 MountNS（零脏页 MAP_SHARED 源头）
// ============================================================================
inline MountNsExecSummary companion_handle_prop_mount_ns_sync(int fd, int32_t app_pid) {
    MountNsExecSummary summary{};
    uint8_t want_flag = 0;
    if (!read_full(fd, &want_flag, sizeof(want_flag)) || want_flag == 0) {
        return summary;
    }

    int32_t ppid = 0;
    uint32_t prop_count = 0;
    if (!read_full(fd, &ppid, sizeof(ppid)) ||
        !read_full(fd, &prop_count, sizeof(prop_count))) {
        return summary;
    }
    if (prop_count > kMaxPropMountJobs) prop_count = kMaxPropMountJobs;

    struct CompPropJob {
        std::string tgt_path;
        std::vector<std::pair<std::string, std::string>> kvs;
    };
    std::vector<CompPropJob> prop_jobs;
    prop_jobs.reserve(prop_count);
    for (uint32_t i = 0; i < prop_count; ++i) {
        CompPropJob pj{};
        pj.tgt_path = read_str(fd);
        uint32_t kvc = 0;
        read_full(fd, &kvc, sizeof(kvc));
        if (kvc > 512) kvc = 512;
        pj.kvs.reserve(kvc);
        for (uint32_t k = 0; k < kvc; ++k) {
            std::string key = read_str(fd);
            std::string val = read_str(fd);
            pj.kvs.emplace_back(std::move(key), std::move(val));
        }
        prop_jobs.push_back(std::move(pj));
    }

    std::vector<uint8_t> prop_results(prop_count, 0);
    auto send_reply = [&]() {
        if (prop_count > 0) write_full(fd, prop_results.data(), prop_results.size());
        write_full(fd, &summary.prop_patched, sizeof(summary.prop_patched));
        write_full(fd, &summary.prop_inserted, sizeof(summary.prop_inserted));
        write_str(fd, summary.diag);
    };

    char app_ns_path[64] = {0};
    if (prop_count == 0 || !check_target_mnt_ns_isolated(app_pid, ppid, app_ns_path, sizeof(app_ns_path))) {
        summary.diag = "prop_skip=ns_not_isolated";
        send_reply();
        return summary;
    }

    mkdir(NEUTRAL_STAGE_ROOT, 0755);
    cleanup_stale_stage_dirs_throttled();

    char pid_stage_dir[128];
    std::snprintf(pid_stage_dir, sizeof(pid_stage_dir), "%s/%d_p", NEUTRAL_STAGE_ROOT, app_pid);

    std::vector<const char *> p_paths(prop_count, nullptr);
    for (uint32_t i = 0; i < prop_count; ++i) {
        p_paths[i] = prop_jobs[i].tgt_path.c_str();
    }

    int pfd[2] = {-1, -1};
    if (pipe(pfd) != 0) {
        summary.diag = "prop_fail=pipe";
        send_reply();
        return summary;
    }

    pid_t worker = fork();
    if (worker == 0) {
        close(pfd[0]);
        umask(0);
        MountNsPropChildReport rep{};

        int ns_fd = open(app_ns_path, O_RDONLY | O_CLOEXEC);
        if (ns_fd < 0) {
            rep.fail_stage = 1;
            rep.last_errno = errno;
        } else if (syscall(__NR_setns, ns_fd, CLONE_NEWNS) != 0) {
            rep.fail_stage = 2;
            rep.last_errno = errno;
            close(ns_fd);
        } else {
            close(ns_fd);
            bool root_slave = (mount(nullptr, "/", nullptr, MS_REC | MS_SLAVE, nullptr) == 0);
            bool dev_priv   = (mount(nullptr, "/dev", nullptr, MS_REC | MS_PRIVATE, nullptr) == 0);
            bool prop_dir_priv = (mount(nullptr, "/dev/__properties__", nullptr, MS_REC | MS_PRIVATE, nullptr) == 0) ||
                                 (mount(nullptr, "/__properties__", nullptr, MS_REC | MS_PRIVATE, nullptr) == 0) ||
                                 dev_priv;

            if (!prop_dir_priv) {
                rep.fail_stage = 3;
                rep.last_errno = errno;
            } else {
                bool need_umount = false;
                rep.mode = prepare_stage_dir_in_worker(pid_stage_dir, root_slave, &need_umount,
                                                       &rep.fail_stage, &rep.last_errno);
                if (rep.mode != 0) {
                    const bool is_force_ctx = (rep.mode == 2);
                    for (uint32_t i = 0; i < prop_count; ++i) {
                        const char *tgt = p_paths[i];
                        if (!tgt || tgt[0] != '/') continue;

                        int rfd = open(tgt, O_RDONLY | O_CLOEXEC);
                        if (rfd < 0) continue;

                        struct stat st{};
                        if (fstat(rfd, &st) != 0 || st.st_size <= static_cast<off_t>(sizeof(PropAreaHeaderRaw)) ||
                            st.st_size > 512 * 1024) {
                            close(rfd);
                            continue;
                        }
                        size_t fsz = static_cast<size_t>(st.st_size);

                        char src_path[180];
                        std::snprintf(src_path, sizeof(src_path), "%s/p%u", pid_stage_dir, i);
                        int wfd = open(src_path, O_CREAT | O_RDWR | O_TRUNC | O_CLOEXEC, 0444);
                        if (wfd < 0) {
                            close(rfd);
                            continue;
                        }

                        char cbuf[4096];
                        size_t copied = 0;
                        while (copied < fsz) {
                            ssize_t nr = read(rfd, cbuf, sizeof(cbuf));
                            if (nr <= 0) break;
                            ssize_t nw = write(wfd, cbuf, static_cast<size_t>(nr));
                            if (nw != nr) break;
                            copied += static_cast<size_t>(nw);
                        }
                        close(rfd);

                        bool patched_ok = false;
                        if (copied == fsz) {
                            void *mem = mmap(nullptr, fsz, PROT_READ | PROT_WRITE, MAP_SHARED, wfd, 0);
                            if (mem != MAP_FAILED) {
                                PropAreaPatchResult pres = patch_prop_area_buffer(
                                    static_cast<uint8_t *>(mem),
                                    fsz,
                                    prop_jobs[i].kvs,
                                    /*allow_insert=*/true,
                                    /*out_touched_keys=*/nullptr
                                );
                                munmap(mem, fsz);
                                if (pres.patched_existing > 0 || pres.inserted_new > 0) {
                                    rep.prop_patched  += pres.patched_existing;
                                    rep.prop_inserted += pres.inserted_new;
                                    patched_ok = true;
                                }
                            }
                        }
                        close(wfd);

                        if (patched_ok && apply_cloned_selinux_label(src_path, tgt, is_force_ctx)) {
                            chmod(src_path, 0444);
                            if (mount(src_path, tgt, nullptr, MS_BIND, nullptr) == 0) {
                                mount(nullptr, tgt, nullptr, MS_BIND | MS_REMOUNT | MS_RDONLY, nullptr);
                                rep.prop_results[i] = 1;
                            }
                        }
                        if (rep.mode == 3) unlink(src_path);
                    }

                    if (need_umount) umount2(pid_stage_dir, MNT_DETACH);
                }
                rmdir(pid_stage_dir);
            }
        }

        write_full(pfd[1], &rep, sizeof(rep));
        close(pfd[1]);
        _exit(0);
    }

    close(pfd[1]);
    MountNsPropChildReport rep{};
    if (worker > 0) {
        read_full(pfd[0], &rep, sizeof(rep));
        int status = 0;
        waitpid(worker, &status, 0);
    }
    close(pfd[0]);

    for (uint32_t i = 0; i < prop_count; ++i) {
        prop_results[i] = rep.prop_results[i];
        if (prop_results[i] == 1) ++summary.prop_vmas_ok;
    }
    summary.prop_patched  = rep.prop_patched;
    summary.prop_inserted = rep.prop_inserted;

    const char *mode_name = (rep.mode == 1) ? "tmpfs-xattr" :
                            (rep.mode == 2) ? "tmpfs-ctx" :
                            (rep.mode == 3) ? "data-xattr" : "none";
    char diag_buf[192];
    if (summary.prop_vmas_ok > 0) {
        std::snprintf(diag_buf, sizeof(diag_buf),
                      "prop_mode=%s prop_vmas=%u/%u patched=%u inserted=%u",
                      mode_name, summary.prop_vmas_ok, prop_count,
                      summary.prop_patched, summary.prop_inserted);
    } else {
        std::snprintf(diag_buf, sizeof(diag_buf),
                      "prop_fallback stage=%u mode=%s errno=%d",
                      static_cast<unsigned>(rep.fail_stage), mode_name, rep.last_errno);
    }
    summary.diag = diag_buf;

    send_reply();
    return summary;
}

// ============================================================================
// Phase 2 (Companion 侧)：在 postAppSpecialize 阶段处理 VFS 节点挂载与活体线程
// ============================================================================
inline MountNsExecSummary companion_handle_vfs_mount_ns_sync(int fd, int32_t app_pid) {
    MountNsExecSummary summary{};
    uint8_t want_flag = 0;
    if (!read_full(fd, &want_flag, sizeof(want_flag)) || want_flag == 0) {
        return summary;
    }

    int32_t ppid = 0;
    uint32_t vfs_count = 0;
    if (!read_full(fd, &ppid, sizeof(ppid)) ||
        !read_full(fd, &vfs_count, sizeof(vfs_count))) {
        return summary;
    }
    if (vfs_count > kMaxVfsMountJobs) vfs_count = kMaxVfsMountJobs;

    struct CompVfsJob {
        std::string path;
        std::string content;
        uint8_t live_kind;
        int32_t param_a;
        int32_t param_b;
    };
    std::vector<CompVfsJob> vfs_jobs;
    vfs_jobs.reserve(vfs_count);
    for (uint32_t i = 0; i < vfs_count; ++i) {
        CompVfsJob j{};
        j.path = read_str(fd);
        j.content = read_str(fd);
        read_full(fd, &j.live_kind, sizeof(j.live_kind));
        read_full(fd, &j.param_a, sizeof(j.param_a));
        read_full(fd, &j.param_b, sizeof(j.param_b));
        vfs_jobs.push_back(std::move(j));
    }

    summary.vfs_requested = vfs_count;
    std::vector<uint8_t> vfs_results(vfs_count, 0);

    auto send_reply = [&]() {
        if (vfs_count > 0) write_full(fd, vfs_results.data(), vfs_results.size());
        write_full(fd, &summary.live_nodes, sizeof(summary.live_nodes));
        write_str(fd, summary.diag);
    };

    char app_ns_path[64] = {0};
    if (vfs_count == 0 || !check_target_mnt_ns_isolated(app_pid, ppid, app_ns_path, sizeof(app_ns_path))) {
        summary.diag = "vfs_skip=ns_not_isolated";
        send_reply();
        return summary;
    }

    mkdir(NEUTRAL_STAGE_ROOT, 0755);
    cleanup_stale_stage_dirs_throttled();

    char pid_stage_dir[128];
    std::snprintf(pid_stage_dir, sizeof(pid_stage_dir), "%s/%d_v", NEUTRAL_STAGE_ROOT, app_pid);

    std::vector<const char *> v_paths(vfs_count, nullptr);
    std::vector<const char *> v_datas(vfs_count, nullptr);
    std::vector<size_t>       v_lens(vfs_count, 0);
    std::vector<uint8_t>      v_live(vfs_count, LIVE_NODE_NONE);
    std::vector<int32_t>      v_pa(vfs_count, 0);
    std::vector<int32_t>      v_pb(vfs_count, 0);
    for (uint32_t i = 0; i < vfs_count; ++i) {
        v_paths[i] = vfs_jobs[i].path.c_str();
        v_datas[i] = vfs_jobs[i].content.data();
        v_lens[i]  = vfs_jobs[i].content.size();
        v_live[i]  = vfs_jobs[i].live_kind;
        v_pa[i]    = vfs_jobs[i].param_a;
        v_pb[i]    = vfs_jobs[i].param_b;
    }

    int sp[2] = {-1, -1};
    if (socketpair(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0, sp) != 0) {
        summary.diag = "vfs_fail=socketpair";
        send_reply();
        return summary;
    }

    pid_t worker = fork();
    if (worker == 0) {
        close(sp[0]);
        umask(0);
        MountNsVfsChildReport rep{};
        int live_fds[kMaxLiveUpdateNodes];
        for (uint32_t i = 0; i < kMaxLiveUpdateNodes; ++i) live_fds[i] = -1;

        int ns_fd = open(app_ns_path, O_RDONLY | O_CLOEXEC);
        if (ns_fd < 0) {
            rep.fail_stage = 1;
            rep.last_errno = errno;
        } else if (syscall(__NR_setns, ns_fd, CLONE_NEWNS) != 0) {
            rep.fail_stage = 2;
            rep.last_errno = errno;
            close(ns_fd);
        } else {
            close(ns_fd);
            bool root_slave = (mount(nullptr, "/", nullptr, MS_REC | MS_SLAVE, nullptr) == 0);
            bool proc_priv  = (mount(nullptr, "/proc", nullptr, MS_REC | MS_PRIVATE, nullptr) == 0);
            bool sys_priv   = (mount(nullptr, "/sys", nullptr, MS_REC | MS_PRIVATE, nullptr) == 0);

            if (!proc_priv && !sys_priv) {
                rep.fail_stage = 3;
                rep.last_errno = errno;
            } else {
                bool need_umount = false;
                rep.mode = prepare_stage_dir_in_worker(pid_stage_dir, root_slave, &need_umount,
                                                       &rep.fail_stage, &rep.last_errno);
                if (rep.mode != 0) {
                    const bool is_force_ctx = (rep.mode == 2);
                    const bool allow_live   = (rep.mode == 1 || rep.mode == 2);

                    for (uint32_t i = 0; i < vfs_count; ++i) {
                        const char *tgt = v_paths[i];
                        if (!tgt || tgt[0] != '/') continue;
                        bool is_proc = (std::strncmp(tgt, "/proc/", 6) == 0);
                        bool is_sys  = (std::strncmp(tgt, "/sys/", 5) == 0);
                        if ((is_proc && !proc_priv) || (is_sys && !sys_priv) || (!is_proc && !is_sys)) {
                            continue;
                        }
                        if (access(tgt, F_OK) != 0) continue;

                        char src_path[180];
                        std::snprintf(src_path, sizeof(src_path), "%s/v%u", pid_stage_dir, i);
                        int sfd = open(src_path, O_CREAT | O_RDWR | O_TRUNC | O_CLOEXEC, 0444);
                        if (sfd < 0) continue;

                        size_t written = 0;
                        while (written < v_lens[i]) {
                            ssize_t n = write(sfd, v_datas[i] + written, v_lens[i] - written);
                            if (n <= 0) break;
                            written += static_cast<size_t>(n);
                        }

                        bool keep_live_fd = false;
                        if (written == v_lens[i] && apply_cloned_selinux_label(src_path, tgt, is_force_ctx)) {
                            if (mount(src_path, tgt, nullptr, MS_BIND, nullptr) == 0) {
                                mount(nullptr, tgt, nullptr, MS_BIND | MS_REMOUNT | MS_RDONLY, nullptr);
                                rep.vfs_results[i] = 1;
                                if (allow_live &&
                                    v_live[i] != LIVE_NODE_NONE &&
                                    rep.live_count < kMaxLiveUpdateNodes) {
                                    uint8_t slot = rep.live_count++;
                                    live_fds[slot] = sfd;
                                    rep.live_kinds[slot] = v_live[i];
                                    rep.live_param_a[slot] = v_pa[i];
                                    rep.live_param_b[slot] = v_pb[i];
                                    keep_live_fd = true;
                                }
                            }
                        }

                        if (!keep_live_fd) close(sfd);
                        if (rep.mode == 3) unlink(src_path);
                    }

                    if (need_umount) umount2(pid_stage_dir, MNT_DETACH);
                }
                rmdir(pid_stage_dir);
            }
        }

        struct msghdr msg{};
        struct iovec iov{};
        iov.iov_base = &rep;
        iov.iov_len  = sizeof(rep);
        msg.msg_iov    = &iov;
        msg.msg_iovlen = 1;

        union {
            char buf[CMSG_SPACE(sizeof(int) * kMaxLiveUpdateNodes)];
            struct cmsghdr align;
        } cmsg_u{};

        if (rep.live_count > 0) {
            msg.msg_control = cmsg_u.buf;
            msg.msg_controllen = CMSG_SPACE(sizeof(int) * rep.live_count);
            struct cmsghdr *cmsg = CMSG_FIRSTHDR(&msg);
            cmsg->cmsg_level = SOL_SOCKET;
            cmsg->cmsg_type  = SCM_RIGHTS;
            cmsg->cmsg_len   = CMSG_LEN(sizeof(int) * rep.live_count);
            std::memcpy(CMSG_DATA(cmsg), live_fds, sizeof(int) * rep.live_count);
        }

        ssize_t sr = sendmsg(sp[1], &msg, MSG_NOSIGNAL);
        (void)sr;

        for (uint8_t i = 0; i < rep.live_count; ++i) {
            if (live_fds[i] >= 0) close(live_fds[i]);
        }
        close(sp[1]);
        _exit(0);
    }

    close(sp[1]);
    MountNsVfsChildReport rep{};
    int recv_fds[kMaxLiveUpdateNodes];
    for (uint32_t i = 0; i < kMaxLiveUpdateNodes; ++i) recv_fds[i] = -1;
    uint32_t recv_fd_cnt = 0;

    if (worker > 0) {
        struct msghdr msg{};
        struct iovec iov{};
        iov.iov_base = &rep;
        iov.iov_len  = sizeof(rep);
        msg.msg_iov    = &iov;
        msg.msg_iovlen = 1;

        union {
            char buf[CMSG_SPACE(sizeof(int) * kMaxLiveUpdateNodes)];
            struct cmsghdr align;
        } cmsg_u{};
        msg.msg_control = cmsg_u.buf;
        msg.msg_controllen = sizeof(cmsg_u.buf);

        ssize_t nr = recvmsg(sp[0], &msg, MSG_WAITALL);
        if (nr == static_cast<ssize_t>(sizeof(rep))) {
            for (struct cmsghdr *cmsg = CMSG_FIRSTHDR(&msg); cmsg != nullptr; cmsg = CMSG_NXTHDR(&msg, cmsg)) {
                if (cmsg->cmsg_level == SOL_SOCKET && cmsg->cmsg_type == SCM_RIGHTS) {
                    size_t data_len = cmsg->cmsg_len - CMSG_LEN(0);
                    uint32_t nfd = static_cast<uint32_t>(data_len / sizeof(int));
                    if (nfd > kMaxLiveUpdateNodes) nfd = kMaxLiveUpdateNodes;
                    std::memcpy(recv_fds, CMSG_DATA(cmsg), nfd * sizeof(int));
                    recv_fd_cnt = nfd;
                    break;
                }
            }
        }
        int status = 0;
        waitpid(worker, &status, 0);
    }
    close(sp[0]);

    for (uint32_t i = 0; i < vfs_count; ++i) {
        vfs_results[i] = rep.vfs_results[i];
        if (vfs_results[i] == 1) ++summary.vfs_mounted;
    }

    if (recv_fd_cnt > 0 && rep.live_count > 0 &&
        g_active_live_threads.load(std::memory_order_relaxed) < 24) {
        uint32_t usable = recv_fd_cnt < rep.live_count ? recv_fd_cnt : rep.live_count;
        auto *targs = new LiveVfsThreadArgs();
        targs->app_pid = app_pid;
        targs->items.reserve(usable);
        for (uint32_t i = 0; i < usable; ++i) {
            targs->items.push_back({recv_fds[i], rep.live_kinds[i], rep.live_param_a[i], rep.live_param_b[i]});
            recv_fds[i] = -1;
        }
        g_active_live_threads.fetch_add(1, std::memory_order_relaxed);
        pthread_t tid;
        if (pthread_create(&tid, nullptr, companion_live_vfs_thread, targs) == 0) {
            pthread_detach(tid);
            summary.live_nodes = usable;
        } else {
            g_active_live_threads.fetch_sub(1, std::memory_order_relaxed);
            for (const auto &it : targs->items) {
                if (it.fd >= 0) close(it.fd);
            }
            delete targs;
        }
    }
    for (uint32_t i = 0; i < recv_fd_cnt; ++i) {
        if (recv_fds[i] >= 0) close(recv_fds[i]);
    }

    const char *mode_name = (rep.mode == 1) ? "tmpfs-xattr" :
                            (rep.mode == 2) ? "tmpfs-ctx" :
                            (rep.mode == 3) ? "data-xattr" : "none";
    char diag_buf[192];
    if (summary.vfs_mounted > 0) {
        std::snprintf(diag_buf, sizeof(diag_buf),
                      "post_vfs_mode=%s mounted=%u/%u live=%u",
                      mode_name, summary.vfs_mounted, vfs_count, summary.live_nodes);
    } else {
        std::snprintf(diag_buf, sizeof(diag_buf),
                      "post_vfs_fallback stage=%u mode=%s errno=%d",
                      static_cast<unsigned>(rep.fail_stage), mode_name, rep.last_errno);
    }
    summary.diag = diag_buf;

    send_reply();
    return summary;
}

} // namespace mirage

#pragma once

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <sstream>
#include <unistd.h>
#include <fcntl.h>
#include <sys/syscall.h>
#include <sys/sysinfo.h>
#include <sys/utsname.h>
#include <sys/stat.h>
#include <linux/memfd.h>
#include "mirage_shared.hpp"
#include "fd_cloak_tracker.hpp"

#ifndef MFD_CLOEXEC
#define MFD_CLOEXEC 0x0001U
#endif

namespace mirage {

inline int (*orig_open)(const char *pathname, int flags, mode_t mode) = nullptr;
inline int (*orig_open64)(const char *pathname, int flags, mode_t mode) = nullptr;
inline int (*orig_open_2)(const char *pathname, int flags) = nullptr;
inline int (*orig_openat)(int dirfd, const char *pathname, int flags, mode_t mode) = nullptr;
inline int (*orig_openat64)(int dirfd, const char *pathname, int flags, mode_t mode) = nullptr;
inline int (*orig_openat_2)(int dirfd, const char *pathname, int flags) = nullptr;
inline int (*orig_openat64_2)(int dirfd, const char *pathname, int flags) = nullptr;
inline FILE *(*orig_fopen)(const char *pathname, const char *mode) = nullptr;
inline FILE *(*orig_fopen64)(const char *pathname, const char *mode) = nullptr;
inline int (*orig_close)(int fd) = nullptr;
inline off_t (*orig_lseek)(int fd, off_t offset, int whence) = nullptr;
inline off64_t (*orig_lseek64)(int fd, off64_t offset, int whence) = nullptr;

inline int (*orig_access)(const char *pathname, int mode) = nullptr;
inline int (*orig_faccessat)(int dirfd, const char *pathname, int mode, int flags) = nullptr;
inline int (*orig_stat)(const char *pathname, struct stat *statbuf) = nullptr;
inline int (*orig_stat64)(const char *pathname, struct stat64 *statbuf) = nullptr;
inline int (*orig_lstat)(const char *pathname, struct stat *statbuf) = nullptr;
inline int (*orig_lstat64)(const char *pathname, struct stat64 *statbuf) = nullptr;
inline int (*orig_fstatat)(int dirfd, const char *pathname, struct stat *statbuf, int flags) = nullptr;
inline int (*orig_fstatat64)(int dirfd, const char *pathname, struct stat64 *statbuf, int flags) = nullptr;
inline int (*orig_fstat)(int fd, struct stat *statbuf) = nullptr;
inline int (*orig_fstat64)(int fd, struct stat64 *statbuf) = nullptr;

inline ssize_t (*orig_readlink)(const char *pathname, char *buf, size_t bufsiz) = nullptr;
inline ssize_t (*orig_readlinkat)(int dirfd, const char *pathname, char *buf, size_t bufsiz) = nullptr;
inline FILE *(*orig_popen)(const char *command, const char *type) = nullptr;
inline int (*orig_uname)(struct utsname *buf) = nullptr;
inline int (*orig_sysinfo)(struct sysinfo *info) = nullptr;
inline long (*orig_sysconf)(int name) = nullptr;

inline uint64_t calc_realistic_memtotal_kb(int ram_gb) {
    uint64_t phys_kb = static_cast<uint64_t>(ram_gb > 0 ? ram_gb : 16) * 1024ULL * 1024ULL;
    return (phys_kb * 935ULL) / 1000ULL;
}

inline std::vector<int> build_opp_steps(int min_khz, int max_khz) {
    std::vector<int> steps;
    if (max_khz <= min_khz) {
        steps.push_back(max_khz);
        return steps;
    }
    constexpr int kNumSteps = 8;
    for (int i = 0; i < kNumSteps; ++i) {
        long raw = min_khz + ((long)(max_khz - min_khz) * i) / (kNumSteps - 1);
        if (i > 0 && i < kNumSteps - 1) {
            raw = (raw / 19200L) * 19200L;
        }
        steps.push_back(static_cast<int>(raw));
    }
    return steps;
}

inline void build_cpu_clusters() {
    g_ctx.clusters.clear();

    if (!g_ctx.cpu_clusters_spec.empty()) {
        auto segs = split_str(g_ctx.cpu_clusters_spec, ';');
        for (const auto &seg : segs) {
            auto parts = split_str(seg, ':');
            if (parts.size() >= 3) {
                auto range = split_str(parts[0], '-');
                int c_start = std::atoi(range[0].c_str());
                int c_end = (range.size() >= 2) ? std::atoi(range[1].c_str()) : c_start;
                int f_min = std::atoi(parts[1].c_str());
                int f_max = std::atoi(parts[2].c_str());
                if (c_start >= 0 && c_end >= c_start && f_max > 0) {
                    CpuClusterSpec cl;
                    cl.first_core = c_start;
                    cl.last_core = c_end;
                    cl.min_freq_khz = f_min > 0 ? f_min : 614400;
                    cl.max_freq_khz = f_max;
                    cl.cpu_part = (c_end >= 6) ? 0xd87 : 0xd85;
                    cl.capacity = (c_end >= 6) ? 1024 : 768;
                    cl.opp_freqs_khz = build_opp_steps(cl.min_freq_khz, cl.max_freq_khz);
                    g_ctx.clusters.push_back(cl);
                }
            }
        }
    }

    if (!g_ctx.clusters.empty()) return;

    std::string token = g_ctx.soc_machine + " " + g_ctx.cpu_hardware;
    for (char &c : token) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));

    if (token.find("mt6991") != std::string::npos || token.find("6991") != std::string::npos) {
        g_ctx.clusters.push_back({0, 3, 800000,  2400000, 0xd81, 612,  build_opp_steps(800000,  2400000)});
        g_ctx.clusters.push_back({4, 6, 1000000, 3300000, 0xd82, 896,  build_opp_steps(1000000, 3300000)});
        g_ctx.clusters.push_back({7, 7, 1200000, 3620000, 0xd85, 1024, build_opp_steps(1200000, 3620000)});
    } else if (token.find("sm8650") != std::string::npos || token.find("pineapple") != std::string::npos) {
        g_ctx.clusters.push_back({0, 1, 556800, 2265600, 0xd80, 384,  build_opp_steps(556800, 2265600)});
        g_ctx.clusters.push_back({2, 6, 691200, 3148800, 0xd81, 845,  build_opp_steps(691200, 3148800)});
        g_ctx.clusters.push_back({7, 7, 806400, 3302400, 0xd82, 1024, build_opp_steps(806400, 3302400)});
    } else {
        int top_freq = g_ctx.cpu_max_freq_khz > 2000000 ? g_ctx.cpu_max_freq_khz : 4320000;
        int perf_freq = (top_freq == 4320000) ? 3532800 : (top_freq * 82 / 100);
        g_ctx.clusters.push_back({0, 5, 614400, perf_freq, 0xd85, 788,  build_opp_steps(614400, perf_freq)});
        g_ctx.clusters.push_back({6, 7, 806400, top_freq,  0xd87, 1024, build_opp_steps(806400, top_freq)});
    }
}

inline const CpuClusterSpec *find_cluster_for_core(int core_idx) {
    for (const auto &cl : g_ctx.clusters) {
        if (core_idx >= cl.first_core && core_idx <= cl.last_core) {
            return &cl;
        }
    }
    return g_ctx.clusters.empty() ? nullptr : &g_ctx.clusters.back();
}

inline int sample_live_core_freq_khz(int core_idx) {
    const CpuClusterSpec *cl = find_cluster_for_core(core_idx);
    if (!cl || cl->opp_freqs_khz.empty()) return 2400000;

    struct timespec ts{};
    clock_gettime(CLOCK_MONOTONIC, &ts);
    uint64_t tick = static_cast<uint64_t>(ts.tv_sec) * 8ULL + static_cast<uint64_t>(ts.tv_nsec / 120000000L);
    uint64_t mix = (tick + static_cast<uint64_t>(core_idx) * 3ULL) ^ (tick >> 2);

    size_t n = cl->opp_freqs_khz.size();
    size_t idx = static_cast<size_t>((mix + static_cast<uint64_t>(core_idx)) % n);
    return cl->opp_freqs_khz[idx];
}

// 匹配单核或策略目录的实时频率节点，返回实时频率文本与对应的 core_id
inline bool resolve_dynamic_cpufreq_node(const std::string &norm_path, std::string *out_val, int *out_core_id = nullptr) {
    if (!out_val) return false;

    // 1. /sys/devices/system/cpu/cpu<N>/cpufreq/scaling_cur_freq 或 cpuinfo_cur_freq
    // 以及 /sys/bus/cpu/devices/cpu<N>/cpufreq/...
    const char *cpu_prefix = nullptr;
    if (norm_path.rfind("/sys/devices/system/cpu/cpu", 0) == 0) {
        cpu_prefix = norm_path.c_str() + 27;
    } else if (norm_path.rfind("/sys/bus/cpu/devices/cpu", 0) == 0) {
        cpu_prefix = norm_path.c_str() + 24;
    }
    if (cpu_prefix && *cpu_prefix >= '0' && *cpu_prefix <= '9') {
        char *endptr = nullptr;
        int core_id = static_cast<int>(std::strtol(cpu_prefix, &endptr, 10));
        if (endptr && (std::strcmp(endptr, "/cpufreq/scaling_cur_freq") == 0 ||
                       std::strcmp(endptr, "/cpufreq/cpuinfo_cur_freq") == 0)) {
            int freq = sample_live_core_freq_khz(core_id);
            *out_val = std::to_string(freq) + "\n";
            if (out_core_id) *out_core_id = core_id;
            return true;
        }
    }

    // 2. /sys/devices/system/cpu/cpufreq/policy<P>/scaling_cur_freq 或 cpuinfo_cur_freq
    if (norm_path.rfind("/sys/devices/system/cpu/cpufreq/policy", 0) == 0) {
        const char *p = norm_path.c_str() + 38;
        if (*p >= '0' && *p <= '9') {
            char *endptr = nullptr;
            int pol_core = static_cast<int>(std::strtol(p, &endptr, 10));
            if (endptr && (std::strcmp(endptr, "/scaling_cur_freq") == 0 ||
                           std::strcmp(endptr, "/cpuinfo_cur_freq") == 0)) {
                int freq = sample_live_core_freq_khz(pol_core);
                *out_val = std::to_string(freq) + "\n";
                if (out_core_id) *out_core_id = pol_core;
                return true;
            }
        }
    }

    return false;
}

inline void build_vfs_topology() {
    auto &files = g_ctx.vfs_files;
    files.clear();

    build_cpu_clusters();

    // 1. 全分区 build.prop
    {
        std::ostringstream ss;
        ss << "# Generated by MirageForge v" << MIRAGE_ENGINE_VERSION << " VFS Hypervisor\n";
        for (const auto &kv : g_ctx.prop_map) {
            ss << kv.first << "=" << kv.second << "\n";
        }
        std::string prop_blob = ss.str();
        files["/system/build.prop"] = prop_blob;
        files["/system/etc/build.prop"] = prop_blob;
        files["/vendor/build.prop"] = prop_blob;
        files["/vendor/etc/build.prop"] = prop_blob;
        files["/product/build.prop"] = prop_blob;
        files["/product/etc/build.prop"] = prop_blob;
        files["/odm/etc/build.prop"] = prop_blob;
        files["/odm/build.prop"] = prop_blob;
    }

    // 2. /proc/cpuinfo & 全套单核 cpufreq + 全编号 policy0..policy15 节点
    {
        int cores = (g_ctx.cpu_cores > 0 && g_ctx.cpu_cores <= 16) ? g_ctx.cpu_cores : 8;
        std::ostringstream ss;
        ss << "Processor\t: AArch64 Processor rev 1 (aarch64)\n";

        for (int i = 0; i < cores; ++i) {
            const CpuClusterSpec *cl = find_cluster_for_core(i);
            uint32_t part_id = cl ? cl->cpu_part : (i < 6 ? 0xd85 : 0xd87);
            ss << "processor\t: " << i << "\n"
               << "BogoMIPS\t: 38.40\n"
               << "Features\t: fp asimd evtstrm aes pmull sha1 sha2 crc32 atomics fphp asimdhp cpuid asimdrdm jscvt fcma lrcpc dcpop sha3 sm3 sm4 asimddp sha512 sve asimdfhm dit uscat ilrcpc flagm ssbs sb paca pacg dcpodp sve2 sveaes svepmull svebitperm svesha3 svesm4 flagm2 frint svei8mm svebf16 i8mm bf16 dgh bti\n"
               << "CPU implementer\t: 0x41\n"
               << "CPU architecture: 8\n"
               << "CPU variant\t: 0x1\n"
               << "CPU part\t: 0x" << std::hex << part_id << std::dec << "\n"
               << "CPU revision\t: 1\n\n";
        }
        ss << "Hardware\t: " << g_ctx.cpu_hardware << "\n";
        files["/proc/cpuinfo"] = ss.str();

        std::string core_range = "0-" + std::to_string(cores - 1) + "\n";
        files["/sys/devices/system/cpu/present"] = core_range;
        files["/sys/devices/system/cpu/possible"] = core_range;
        files["/sys/devices/system/cpu/online"] = core_range;
        files["/sys/devices/system/cpu/offline"] = "\n";
        files["/sys/devices/system/cpu/kernel_max"] = std::to_string(cores - 1) + "\n";

        std::string midr = g_ctx.cpu_midr_el1 + "\n";

        // 为每一颗核心 i (0..cores-1) 生成其所属簇的参数，并同时填充:
        // - /sys/devices/system/cpu/cpu<i>/cpufreq/*
        // - /sys/bus/cpu/devices/cpu<i>/cpufreq/*
        // - /sys/devices/system/cpu/cpufreq/policy<i>/* (覆盖真机可能存在的任意 policy0..policy7 编号！)
        for (int c = 0; c < cores; ++c) {
            const CpuClusterSpec *cl = find_cluster_for_core(c);
            if (!cl) continue;

            std::ostringstream avail_ss, tis_ss, rel_ss;
            for (size_t idx = 0; idx < cl->opp_freqs_khz.size(); ++idx) {
                if (idx > 0) avail_ss << " ";
                avail_ss << cl->opp_freqs_khz[idx];
                tis_ss << cl->opp_freqs_khz[idx] << " " << ((idx + 1) * 14200) << "\n";
            }
            avail_ss << "\n";

            uint32_t cpumask_bits = 0;
            for (int k = cl->first_core; k <= cl->last_core && k < cores; ++k) {
                if (k > cl->first_core) rel_ss << " ";
                rel_ss << k;
                cpumask_bits |= (1u << k);
            }
            rel_ss << "\n";

            char mask_buf[16];
            std::snprintf(mask_buf, sizeof(mask_buf), "%02x\n", cpumask_bits);

            std::string min_s = std::to_string(cl->min_freq_khz) + "\n";
            std::string max_s = std::to_string(cl->max_freq_khz) + "\n";
            std::string cur_s = std::to_string(sample_live_core_freq_khz(c)) + "\n";
            std::string avail_s = avail_ss.str();
            std::string tis_s = tis_ss.str();
            std::string rel_s = rel_ss.str();
            std::string cap_s = std::to_string(cl->capacity) + "\n";

            auto populate_cpufreq_dir = [&](const std::string &dir_prefix) {
                files[dir_prefix + "/cpuinfo_min_freq"] = min_s;
                files[dir_prefix + "/scaling_min_freq"] = min_s;
                files[dir_prefix + "/cpuinfo_max_freq"] = max_s;
                files[dir_prefix + "/scaling_max_freq"] = max_s;
                files[dir_prefix + "/scaling_cur_freq"] = cur_s;
                files[dir_prefix + "/cpuinfo_cur_freq"] = cur_s;
                files[dir_prefix + "/scaling_available_frequencies"] = avail_s;
                files[dir_prefix + "/scaling_boost_frequencies"] = max_s;
                files[dir_prefix + "/scaling_governor"] = "walt\n";
                files[dir_prefix + "/scaling_available_governors"] = "walt schedutil performance powersave\n";
                files[dir_prefix + "/scaling_driver"] = "qcom-cpufreq-hw\n";
                files[dir_prefix + "/related_cpus"] = rel_s;
                files[dir_prefix + "/affected_cpus"] = rel_s;
                files[dir_prefix + "/cpuinfo_transition_latency"] = "1000\n";
                files[dir_prefix + "/stats/time_in_state"] = tis_s;
            };

            std::string cpu_dir = "/sys/devices/system/cpu/cpu" + std::to_string(c);
            files[cpu_dir + "/online"] = "1\n";
            files[cpu_dir + "/cpu_capacity"] = cap_s;
            files[cpu_dir + "/regs/identification/midr_el1"] = midr;
            files[cpu_dir + "/regs/identification/revidr_el1"] = "0x0000000000000000\n";
            files[cpu_dir + "/topology/physical_package_id"] = "0\n";
            files[cpu_dir + "/topology/cluster_id"] = std::to_string(cl->first_core) + "\n";
            files[cpu_dir + "/topology/core_id"] = std::to_string(c) + "\n";
            files[cpu_dir + "/topology/core_cpus_list"] = std::to_string(cl->first_core) + "-" + std::to_string(cl->last_core) + "\n";
            files[cpu_dir + "/topology/package_cpus"] = "ff\n";
            files[cpu_dir + "/topology/core_siblings"] = mask_buf;

            populate_cpufreq_dir(cpu_dir + "/cpufreq");
            populate_cpufreq_dir("/sys/bus/cpu/devices/cpu" + std::to_string(c) + "/cpufreq");
            populate_cpufreq_dir("/sys/devices/system/cpu/cpufreq/policy" + std::to_string(c));
        }
    }

    // 3. /proc/meminfo & 物理 DDR 硬件节点对称合成
    {
        int ram_gb = g_ctx.ram_gb > 0 ? g_ctx.ram_gb : 16;
        uint64_t total_kb = calc_realistic_memtotal_kb(ram_gb);
        uint64_t free_kb  = (total_kb * 24ULL) / 100ULL;
        uint64_t avail_kb = (total_kb * 58ULL) / 100ULL;
        uint64_t cached_kb = (total_kb * 28ULL) / 100ULL;
        uint64_t active_kb = (total_kb * 36ULL) / 100ULL;
        uint64_t inactive_kb = (total_kb * 22ULL) / 100ULL;

        std::ostringstream ss;
        ss << "MemTotal:       " << total_kb << " kB\n"
           << "MemFree:        " << free_kb << " kB\n"
           << "MemAvailable:   " << avail_kb << " kB\n"
           << "Buffers:          184320 kB\n"
           << "Cached:         " << cached_kb << " kB\n"
           << "SwapCached:            0 kB\n"
           << "Active:         " << active_kb << " kB\n"
           << "Inactive:       " << inactive_kb << " kB\n"
           << "SwapTotal:       8388608 kB\n"
           << "SwapFree:        8126464 kB\n";
        files["/proc/meminfo"] = ss.str();

        std::string ddr_desc = std::to_string(ram_gb) + "GB " + g_ctx.ddr_type + " (" + g_ctx.ddr_vendor + ")\n";
        files["/proc/ddr_info"] = ddr_desc;
        files["/sys/devices/system/chip-info/ddr_info"] = ddr_desc;
        files["/sys/devices/system/chip-info/ddr_type"] = g_ctx.ddr_type + "\n";
        files["/sys/devices/system/chip-info/ddr_size"] = std::to_string(ram_gb) + "GB\n";
        files["/sys/firmware/devicetree/base/memory/ddr_device_type"] = g_ctx.ddr_type + "\n";
    }

    // 4. /proc/version & /proc/sys/kernel/osrelease
    {
        files["/proc/sys/kernel/osrelease"] = g_ctx.kernel_release + "\n";
        files["/proc/version"] = "Linux version " + g_ctx.kernel_release + " (kleaf@build-host) " + g_ctx.kernel_version + "\n";
    }

    // 5. SoC & Adreno KGSL 硬件节点
    {
        files["/sys/class/kgsl/kgsl-3d0/gpu_model"] = g_ctx.gl_renderer + "\n";
        files["/sys/class/kgsl/kgsl-3d0/gpu_available_frequencies"] = "900000000 810000000 720000000 600000000 450000000 315000000\n";
        files["/sys/class/kgsl/kgsl-3d0/max_gpuclk"] = "900000000\n";
        files["/sys/devices/soc0/soc_id"] = g_ctx.soc_id + "\n";
        files["/sys/devices/soc0/family"] = g_ctx.soc_family + "\n";
        files["/sys/devices/soc0/machine"] = g_ctx.soc_machine + "\n";
        files["/sys/devices/soc0/vendor"] = g_ctx.gl_vendor + "\n";
    }
}

inline std::string normalize_vfs_path(const char *raw_path) {
    if (!raw_path) return "";
    std::string out;
    out.reserve(std::strlen(raw_path));
    bool prev_slash = false;
    for (const char *p = raw_path; *p != '\0'; ++p) {
        if (*p == '/') {
            if (!prev_slash) out.push_back('/');
            prev_slash = true;
        } else {
            out.push_back(*p);
            prev_slash = false;
        }
    }
    while (out.size() > 1 && out.back() == '/') {
        out.pop_back();
    }
    return out;
}

// 当 openat/fstatat 传入相对路径 (如 dirfd 指向 /sys/devices/system/cpu/cpu0/cpufreq, pathname 为 "scaling_cur_freq") 时还原完整路径
inline std::string resolve_dirfd_path(int dirfd, const char *pathname) {
    if (!pathname) return "";
    if (pathname[0] == '/') return normalize_vfs_path(pathname);
    if (dirfd < 0 || dirfd == AT_FDCWD) return "";

    char link_target[256] = {0};
    char proc_fd_path[64];
    std::snprintf(proc_fd_path, sizeof(proc_fd_path), "/proc/self/fd/%d", dirfd);
    ssize_t len = orig_readlink
                      ? orig_readlink(proc_fd_path, link_target, sizeof(link_target) - 1)
                      : readlink(proc_fd_path, link_target, sizeof(link_target) - 1);
    if (len <= 0) return "";
    link_target[len] = '\0';

    std::string combined = std::string(link_target) + "/" + pathname;
    return normalize_vfs_path(combined.c_str());
}

inline bool is_sensitive_proc_line(const char *line) {
    return std::strstr(line, "mirage_forge") != nullptr ||
           std::strstr(line, "zygisk") != nullptr ||
           std::strstr(line, "magisk") != nullptr ||
           std::strstr(line, "/data/adb") != nullptr ||
           std::strstr(line, "kernelsu") != nullptr ||
           std::strstr(line, "/memfd:") != nullptr ||
           std::strstr(line, "apatch") != nullptr ||
           std::strstr(line, "lsposed") != nullptr;
}

inline bool sanitize_live_proc_file(const std::string &norm_path, std::string *out_clean) {
    if (!g_ctx.enable_maps_cloak || !out_clean) return false;

    bool is_maps = (norm_path == "/proc/self/maps" ||
                    norm_path == "/proc/thread-self/maps" ||
                    norm_path == "/proc/self/smaps");
    bool is_mounts = (norm_path == "/proc/self/mountinfo" ||
                      norm_path == "/proc/self/mounts" ||
                      norm_path == "/proc/mounts");

    if (!is_maps && !is_mounts) return false;

    FILE *real_fp = orig_fopen ? orig_fopen(norm_path.c_str(), "r") : nullptr;
    if (!real_fp) return false;

    std::ostringstream clean_ss;
    char line[1024];
    size_t stripped = 0;
    while (std::fgets(line, sizeof(line), real_fp)) {
        if (is_sensitive_proc_line(line)) {
            ++stripped;
            continue;
        }
        clean_ss << line;
    }
    std::fclose(real_fp);

    *out_clean = clean_ss.str();
    emit_probe("MAPS_CLOAK", norm_path, "Stripped " + std::to_string(stripped) + " traces");
    return true;
}

inline bool is_virtual_vfs_node_norm(const std::string &norm, size_t *out_size = nullptr) {
    if (norm.empty() || !g_ctx.enable_vfs_shield) return false;

    std::string dyn_freq;
    if (resolve_dynamic_cpufreq_node(norm, &dyn_freq)) {
        if (out_size) *out_size = dyn_freq.size();
        return true;
    }
    auto it = g_ctx.vfs_files.find(norm);
    if (it != g_ctx.vfs_files.end()) {
        if (out_size) *out_size = it->second.size();
        return true;
    }
    return false;
}

inline int open_cloaked_vfs_fd_norm(const std::string &norm) {
    if (norm.empty() || !g_ctx.enable_vfs_shield) return -1;

    const std::string *target_content = nullptr;
    std::string dynamic_content;
    int dyn_core_id = -1;

    if (resolve_dynamic_cpufreq_node(norm, &dynamic_content, &dyn_core_id)) {
        target_content = &dynamic_content;
    } else {
        auto it = g_ctx.vfs_files.find(norm);
        if (it != g_ctx.vfs_files.end()) {
            target_content = &it->second;
        } else if (sanitize_live_proc_file(norm, &dynamic_content)) {
            target_content = &dynamic_content;
        } else {
            return -1;
        }
    }

    int fd = static_cast<int>(syscall(__NR_memfd_create, "", MFD_CLOEXEC));
    if (fd < 0) return -1;

    if (!target_content->empty()) {
        write_full(fd, target_content->data(), target_content->size());
        if (orig_lseek) orig_lseek(fd, 0, SEEK_SET); else lseek(fd, 0, SEEK_SET);
    }

    g_fd_cloak.register_fd(fd, norm, target_content->size(), dyn_core_id);
    emit_probe("VFS_OPEN", norm, (dyn_core_id >= 0) ? ("LiveFreq:" + *target_content) : "Cloaked");
    return fd;
}

inline int open_cloaked_vfs_fd(const char *raw_path) {
    if (!raw_path) return -1;
    return open_cloaked_vfs_fd_norm(normalize_vfs_path(raw_path));
}

// ============================================================================
// Bionic I/O & Fortify 全家族 Hook 实现
// ============================================================================
inline int hook_open(const char *pathname, int flags, mode_t mode) {
    ScopedHookGuard guard;
    if (guard) {
        int fake_fd = open_cloaked_vfs_fd(pathname);
        if (fake_fd >= 0) return fake_fd;
    }
    return orig_open ? orig_open(pathname, flags, mode) : -1;
}

inline int hook_open64(const char *pathname, int flags, mode_t mode) {
    ScopedHookGuard guard;
    if (guard) {
        int fake_fd = open_cloaked_vfs_fd(pathname);
        if (fake_fd >= 0) return fake_fd;
    }
    return orig_open64 ? orig_open64(pathname, flags, mode) : (orig_open ? orig_open(pathname, flags, mode) : -1);
}

inline int hook_open_2(const char *pathname, int flags) {
    ScopedHookGuard guard;
    if (guard) {
        int fake_fd = open_cloaked_vfs_fd(pathname);
        if (fake_fd >= 0) return fake_fd;
    }
    return orig_open_2 ? orig_open_2(pathname, flags) : (orig_open ? orig_open(pathname, flags, 0) : -1);
}

inline int hook_openat(int dirfd, const char *pathname, int flags, mode_t mode) {
    ScopedHookGuard guard;
    if (guard && pathname) {
        int fake_fd = open_cloaked_vfs_fd_norm(resolve_dirfd_path(dirfd, pathname));
        if (fake_fd >= 0) return fake_fd;
    }
    return orig_openat ? orig_openat(dirfd, pathname, flags, mode) : -1;
}

inline int hook_openat64(int dirfd, const char *pathname, int flags, mode_t mode) {
    ScopedHookGuard guard;
    if (guard && pathname) {
        int fake_fd = open_cloaked_vfs_fd_norm(resolve_dirfd_path(dirfd, pathname));
        if (fake_fd >= 0) return fake_fd;
    }
    return orig_openat64 ? orig_openat64(dirfd, pathname, flags, mode) : (orig_openat ? orig_openat(dirfd, pathname, flags, mode) : -1);
}

inline int hook_openat_2(int dirfd, const char *pathname, int flags) {
    ScopedHookGuard guard;
    if (guard && pathname) {
        int fake_fd = open_cloaked_vfs_fd_norm(resolve_dirfd_path(dirfd, pathname));
        if (fake_fd >= 0) return fake_fd;
    }
    return orig_openat_2 ? orig_openat_2(dirfd, pathname, flags) : (orig_openat ? orig_openat(dirfd, pathname, flags, 0) : -1);
}

inline int hook_openat64_2(int dirfd, const char *pathname, int flags) {
    ScopedHookGuard guard;
    if (guard && pathname) {
        int fake_fd = open_cloaked_vfs_fd_norm(resolve_dirfd_path(dirfd, pathname));
        if (fake_fd >= 0) return fake_fd;
    }
    return orig_openat64_2 ? orig_openat64_2(dirfd, pathname, flags) : (orig_openat_2 ? orig_openat_2(dirfd, pathname, flags) : -1);
}

inline FILE *hook_fopen(const char *pathname, const char *mode) {
    ScopedHookGuard guard;
    if (guard) {
        int fake_fd = open_cloaked_vfs_fd(pathname);
        if (fake_fd >= 0) {
            FILE *fp = fdopen(fake_fd, "r");
            if (fp) return fp;
            g_fd_cloak.unregister_fd(fake_fd);
            if (orig_close) orig_close(fake_fd); else close(fake_fd);
        }
    }
    return orig_fopen ? orig_fopen(pathname, mode) : nullptr;
}

inline FILE *hook_fopen64(const char *pathname, const char *mode) {
    return hook_fopen(pathname, mode);
}

inline int hook_close(int fd) {
    g_fd_cloak.unregister_fd(fd);
    return orig_close ? orig_close(fd) : close(fd);
}

// 当应用持有单核频率 fd 循环调用 lseek(fd, 0, SEEK_SET) 刷新时，动态写入最新微抖动频点
inline void refresh_dynamic_freq_fd_on_rewind(int fd) {
    int core_id = -1;
    if (g_fd_cloak.get_dynamic_core_id(fd, &core_id)) {
        std::string next_freq = std::to_string(sample_live_core_freq_khz(core_id)) + "\n";
        if (orig_lseek) orig_lseek(fd, 0, SEEK_SET);
        write_full(fd, next_freq.data(), next_freq.size());
        ftruncate(fd, static_cast<off_t>(next_freq.size()));
    }
}

inline off_t hook_lseek(int fd, off_t offset, int whence) {
    if (offset == 0 && whence == SEEK_SET) {
        refresh_dynamic_freq_fd_on_rewind(fd);
    }
    return orig_lseek ? orig_lseek(fd, offset, whence) : lseek(fd, offset, whence);
}

inline off64_t hook_lseek64(int fd, off64_t offset, int whence) {
    if (offset == 0 && whence == SEEK_SET) {
        refresh_dynamic_freq_fd_on_rewind(fd);
    }
    return orig_lseek64 ? orig_lseek64(fd, offset, whence) : lseek64(fd, offset, whence);
}

// ============================================================================
// access / faccessat / stat / stat64 / lstat / fstatat / fstat 家族 Hook
// ============================================================================
inline int hook_access(const char *pathname, int mode) {
    ScopedHookGuard guard;
    if (guard && pathname && is_virtual_vfs_node_norm(normalize_vfs_path(pathname))) {
        return 0;
    }
    return orig_access ? orig_access(pathname, mode) : -1;
}

inline int hook_faccessat(int dirfd, const char *pathname, int mode, int flags) {
    ScopedHookGuard guard;
    if (guard && pathname && is_virtual_vfs_node_norm(resolve_dirfd_path(dirfd, pathname))) {
        return 0;
    }
    return orig_faccessat ? orig_faccessat(dirfd, pathname, mode, flags) : -1;
}

inline int hook_stat(const char *pathname, struct stat *statbuf) {
    ScopedHookGuard guard;
    if (guard && pathname && statbuf) {
        std::string norm = normalize_vfs_path(pathname);
        size_t sz = 0;
        if (is_virtual_vfs_node_norm(norm, &sz)) {
            populate_virtual_stat(statbuf, norm, sz);
            return 0;
        }
    }
    return orig_stat ? orig_stat(pathname, statbuf) : -1;
}

inline int hook_stat64(const char *pathname, struct stat64 *statbuf) {
    ScopedHookGuard guard;
    if (guard && pathname && statbuf) {
        std::string norm = normalize_vfs_path(pathname);
        size_t sz = 0;
        if (is_virtual_vfs_node_norm(norm, &sz)) {
            populate_virtual_stat(statbuf, norm, sz);
            return 0;
        }
    }
    return orig_stat64 ? orig_stat64(pathname, statbuf) : -1;
}

inline int hook_lstat(const char *pathname, struct stat *statbuf) {
    ScopedHookGuard guard;
    if (guard && pathname && statbuf) {
        std::string norm = normalize_vfs_path(pathname);
        size_t sz = 0;
        if (is_virtual_vfs_node_norm(norm, &sz)) {
            populate_virtual_stat(statbuf, norm, sz);
            return 0;
        }
    }
    return orig_lstat ? orig_lstat(pathname, statbuf) : -1;
}

inline int hook_lstat64(const char *pathname, struct stat64 *statbuf) {
    ScopedHookGuard guard;
    if (guard && pathname && statbuf) {
        std::string norm = normalize_vfs_path(pathname);
        size_t sz = 0;
        if (is_virtual_vfs_node_norm(norm, &sz)) {
            populate_virtual_stat(statbuf, norm, sz);
            return 0;
        }
    }
    return orig_lstat64 ? orig_lstat64(pathname, statbuf) : -1;
}

inline int hook_fstatat(int dirfd, const char *pathname, struct stat *statbuf, int flags) {
    ScopedHookGuard guard;
    if (guard && pathname && statbuf) {
        std::string norm = resolve_dirfd_path(dirfd, pathname);
        size_t sz = 0;
        if (is_virtual_vfs_node_norm(norm, &sz)) {
            populate_virtual_stat(statbuf, norm, sz);
            return 0;
        }
    }
    return orig_fstatat ? orig_fstatat(dirfd, pathname, statbuf, flags) : -1;
}

inline int hook_fstatat64(int dirfd, const char *pathname, struct stat64 *statbuf, int flags) {
    ScopedHookGuard guard;
    if (guard && pathname && statbuf) {
        std::string norm = resolve_dirfd_path(dirfd, pathname);
        size_t sz = 0;
        if (is_virtual_vfs_node_norm(norm, &sz)) {
            populate_virtual_stat(statbuf, norm, sz);
            return 0;
        }
    }
    return orig_fstatat64 ? orig_fstatat64(dirfd, pathname, statbuf, flags) : -1;
}

inline int hook_fstat(int fd, struct stat *statbuf) {
    if (g_ctx.enable_vfs_shield && g_fd_cloak.get_cloaked_stat(fd, statbuf)) {
        return 0;
    }
    return orig_fstat ? orig_fstat(fd, statbuf) : -1;
}

inline int hook_fstat64(int fd, struct stat64 *statbuf) {
    if (g_ctx.enable_vfs_shield && g_fd_cloak.get_cloaked_stat(fd, statbuf)) {
        return 0;
    }
    return orig_fstat64 ? orig_fstat64(fd, statbuf) : -1;
}

inline ssize_t hook_readlink(const char *pathname, char *buf, size_t bufsiz) {
    ScopedHookGuard guard;
    std::string cloaked_path;
    if (guard && g_ctx.enable_vfs_shield && buf && bufsiz > 0 &&
        g_fd_cloak.resolve_proc_fd_link(pathname, &cloaked_path)) {
        size_t copy_len = std::min(bufsiz, cloaked_path.size());
        std::memcpy(buf, cloaked_path.data(), copy_len);
        emit_probe("FD_CLOAK", pathname, cloaked_path);
        return static_cast<ssize_t>(copy_len);
    }
    return orig_readlink ? orig_readlink(pathname, buf, bufsiz) : -1;
}

inline ssize_t hook_readlinkat(int dirfd, const char *pathname, char *buf, size_t bufsiz) {
    ScopedHookGuard guard;
    std::string cloaked_path;
    if (guard && g_ctx.enable_vfs_shield && buf && bufsiz > 0 &&
        g_fd_cloak.resolve_proc_fd_link(pathname, &cloaked_path)) {
        size_t copy_len = std::min(bufsiz, cloaked_path.size());
        std::memcpy(buf, cloaked_path.data(), copy_len);
        emit_probe("FD_CLOAK", pathname, cloaked_path);
        return static_cast<ssize_t>(copy_len);
    }
    return orig_readlinkat ? orig_readlinkat(dirfd, pathname, buf, bufsiz) : -1;
}

inline int hook_sysinfo(struct sysinfo *info) {
    int ret = orig_sysinfo ? orig_sysinfo(info) : sysinfo(info);
    ScopedHookGuard guard;
    if (guard && ret == 0 && info && g_ctx.enable_vfs_shield && g_ctx.ram_gb > 0) {
        uint64_t total_bytes = calc_realistic_memtotal_kb(g_ctx.ram_gb) * 1024ULL;
        uint32_t unit = info->mem_unit > 0 ? info->mem_unit : 1;
        info->totalram = static_cast<unsigned long>(total_bytes / unit);
        info->freeram  = (info->totalram * 58UL) / 100UL;
        info->bufferram = (info->totalram * 4UL) / 100UL;
        emit_probe("SYSINFO", "totalram", std::to_string(g_ctx.ram_gb) + "GB (" + g_ctx.ddr_type + ")");
    }
    return ret;
}

inline long hook_sysconf(int name) {
    ScopedHookGuard guard;
    if (guard && g_ctx.enable_vfs_shield) {
        if (name == _SC_NPROCESSORS_CONF || name == _SC_NPROCESSORS_ONLN) {
            emit_probe("SYSCONF", "_SC_NPROCESSORS", std::to_string(g_ctx.cpu_cores));
            return g_ctx.cpu_cores > 0 ? g_ctx.cpu_cores : 8;
        }
        if (name == _SC_PHYS_PAGES && g_ctx.ram_gb > 0) {
            long page_sz = orig_sysconf ? orig_sysconf(_SC_PAGESIZE) : 4096;
            if (page_sz <= 0) page_sz = 4096;
            uint64_t total_bytes = calc_realistic_memtotal_kb(g_ctx.ram_gb) * 1024ULL;
            emit_probe("SYSCONF", "_SC_PHYS_PAGES", std::to_string(g_ctx.ram_gb) + "GB");
            return static_cast<long>(total_bytes / static_cast<uint64_t>(page_sz));
        }
    }
    return orig_sysconf ? orig_sysconf(name) : sysconf(name);
}

inline int hook_uname(struct utsname *buf) {
    int ret = orig_uname ? orig_uname(buf) : uname(buf);
    ScopedHookGuard guard;
    if (guard && ret == 0 && buf && g_ctx.enable_vfs_shield) {
        if (!g_ctx.cpu_arch.empty()) {
            std::strncpy(buf->machine, g_ctx.cpu_arch.c_str(), sizeof(buf->machine) - 1);
            buf->machine[sizeof(buf->machine) - 1] = '\0';
        }
        if (!g_ctx.kernel_release.empty()) {
            std::strncpy(buf->release, g_ctx.kernel_release.c_str(), sizeof(buf->release) - 1);
            buf->release[sizeof(buf->release) - 1] = '\0';
        }
        if (!g_ctx.kernel_version.empty()) {
            std::strncpy(buf->version, g_ctx.kernel_version.c_str(), sizeof(buf->version) - 1);
            buf->version[sizeof(buf->version) - 1] = '\0';
        }
        emit_probe("UNAME", "release/machine", std::string(buf->release) + " / " + buf->machine);
    }
    return ret;
}

inline FILE *hook_popen(const char *command, const char *type) {
    ScopedHookGuard guard;
    if (guard && command && g_ctx.enable_build_props) {
        std::string cmd(command);
        size_t first = cmd.find_first_not_of(" \t");
        if (first != std::string::npos) cmd = cmd.substr(first);

        if (cmd.rfind("getprop", 0) == 0 || cmd.rfind("/system/bin/getprop", 0) == 0) {
            std::string output;
            size_t sp = cmd.find(' ');
            if (sp != std::string::npos) {
                std::string arg = cmd.substr(sp + 1);
                size_t a_start = arg.find_first_not_of(" \t\"'");
                size_t a_end = arg.find_last_not_of(" \t\"'\r\n");
                if (a_start != std::string::npos && a_end != std::string::npos && a_end >= a_start) {
                    std::string key = arg.substr(a_start, a_end - a_start + 1);
                    auto it = g_ctx.prop_map.find(key);
                    if (it != g_ctx.prop_map.end()) {
                        output = it->second + "\n";
                        emit_probe("POPEN_GETPROP", key, it->second);
                    }
                }
            } else {
                std::ostringstream ss;
                for (const auto &kv : g_ctx.prop_map) {
                    ss << "[" << kv.first << "]: [" << kv.second << "]\n";
                }
                output = ss.str();
                emit_probe("POPEN_GETPROP", "ALL", "Synthesized");
            }

            if (!output.empty()) {
                int fd = static_cast<int>(syscall(__NR_memfd_create, "", MFD_CLOEXEC));
                if (fd >= 0) {
                    write_full(fd, output.data(), output.size());
                    if (orig_lseek) orig_lseek(fd, 0, SEEK_SET); else lseek(fd, 0, SEEK_SET);
                    FILE *fp = fdopen(fd, "r");
                    if (fp) return fp;
                    if (orig_close) orig_close(fd); else close(fd);
                }
            }
        }
    }
    return orig_popen ? orig_popen(command, type) : nullptr;
}

} // namespace mirage

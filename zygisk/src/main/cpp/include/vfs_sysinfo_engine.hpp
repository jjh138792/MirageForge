#pragma once

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <sstream>
#include <unistd.h>
#include <fcntl.h>
#include <dlfcn.h>
#include <sys/syscall.h>
#include <sys/sysinfo.h>
#include <sys/utsname.h>
#include <sys/stat.h>
#include <linux/memfd.h>
#include "mirage_config.hpp"
#include "mirage_logger.hpp"
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

// 仅在启用 VFS 或 Build(popen) 时按需解析底层符号
inline void init_vfs_symbols() {
    if (orig_openat) return;
    orig_open = reinterpret_cast<decltype(orig_open)>(dlsym(RTLD_DEFAULT, "open"));
    orig_open64 = reinterpret_cast<decltype(orig_open64)>(dlsym(RTLD_DEFAULT, "open64"));
    orig_open_2 = reinterpret_cast<decltype(orig_open_2)>(dlsym(RTLD_DEFAULT, "__open_2"));
    orig_openat = reinterpret_cast<decltype(orig_openat)>(dlsym(RTLD_DEFAULT, "openat"));
    orig_openat64 = reinterpret_cast<decltype(orig_openat64)>(dlsym(RTLD_DEFAULT, "openat64"));
    orig_openat_2 = reinterpret_cast<decltype(orig_openat_2)>(dlsym(RTLD_DEFAULT, "__openat_2"));
    orig_openat64_2 = reinterpret_cast<decltype(orig_openat64_2)>(dlsym(RTLD_DEFAULT, "__openat64_2"));
    orig_fopen = reinterpret_cast<decltype(orig_fopen)>(dlsym(RTLD_DEFAULT, "fopen"));
    orig_fopen64 = reinterpret_cast<decltype(orig_fopen64)>(dlsym(RTLD_DEFAULT, "fopen64"));
    orig_close = reinterpret_cast<decltype(orig_close)>(dlsym(RTLD_DEFAULT, "close"));
    orig_lseek = reinterpret_cast<decltype(orig_lseek)>(dlsym(RTLD_DEFAULT, "lseek"));
    orig_lseek64 = reinterpret_cast<decltype(orig_lseek64)>(dlsym(RTLD_DEFAULT, "lseek64"));

    orig_access = reinterpret_cast<decltype(orig_access)>(dlsym(RTLD_DEFAULT, "access"));
    orig_faccessat = reinterpret_cast<decltype(orig_faccessat)>(dlsym(RTLD_DEFAULT, "faccessat"));
    orig_stat = reinterpret_cast<decltype(orig_stat)>(dlsym(RTLD_DEFAULT, "stat"));
    orig_stat64 = reinterpret_cast<decltype(orig_stat64)>(dlsym(RTLD_DEFAULT, "stat64"));
    orig_lstat = reinterpret_cast<decltype(orig_lstat)>(dlsym(RTLD_DEFAULT, "lstat"));
    orig_lstat64 = reinterpret_cast<decltype(orig_lstat64)>(dlsym(RTLD_DEFAULT, "lstat64"));
    orig_fstatat = reinterpret_cast<decltype(orig_fstatat)>(dlsym(RTLD_DEFAULT, "fstatat"));
    orig_fstatat64 = reinterpret_cast<decltype(orig_fstatat64)>(dlsym(RTLD_DEFAULT, "fstatat64"));
    orig_fstat = reinterpret_cast<decltype(orig_fstat)>(dlsym(RTLD_DEFAULT, "fstat"));
    orig_fstat64 = reinterpret_cast<decltype(orig_fstat64)>(dlsym(RTLD_DEFAULT, "fstat64"));

    orig_readlink = reinterpret_cast<decltype(orig_readlink)>(dlsym(RTLD_DEFAULT, "readlink"));
    orig_readlinkat = reinterpret_cast<decltype(orig_readlinkat)>(dlsym(RTLD_DEFAULT, "readlinkat"));
    orig_popen = reinterpret_cast<decltype(orig_popen)>(dlsym(RTLD_DEFAULT, "popen"));
    orig_uname = reinterpret_cast<decltype(orig_uname)>(dlsym(RTLD_DEFAULT, "uname"));
    orig_sysinfo = reinterpret_cast<decltype(orig_sysinfo)>(dlsym(RTLD_DEFAULT, "sysinfo"));
    orig_sysconf = reinterpret_cast<decltype(orig_sysconf)>(dlsym(RTLD_DEFAULT, "sysconf"));
}

// 3 指令极速前缀过滤：立即放行 /data, /apex, /dev, /storage, /mnt 等 99.5% 的普通路径
inline bool is_vfs_candidate_prefix(const char *p) noexcept {
    if (!p || p[0] != '/') return false;
    while (*p == '/') ++p;
    // 仅拦截 proc, sys, system, vendor, product, odm
    return (p[0] == 'p' || p[0] == 's' || p[0] == 'v' || p[0] == 'o');
}

// 快速判断 openat/fstatat 传入的相对文件名是否可能是 cpufreq 子节点，避免对普通相对路径调 readlink
inline bool is_cpufreq_relative_candidate(const char *rel) noexcept {
    if (!rel || rel[0] == '/' || rel[0] == '.') return false;
    char c = rel[0];
    // scaling_*, cpuinfo_*, online, cpu_capacity, related_cpus, affected_cpus, time_in_state, midr_el1, revidr_el1, topology/*, cpufreq/*
    return (c == 's' || c == 'c' || c == 'o' || c == 'r' || c == 'a' || c == 't' || c == 'm' || c == 'p');
}

inline uint64_t calc_realistic_memtotal_kb(int ram_gb) noexcept {
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

// 仅初始化轻量级分簇参数表（不预生成任何文件字符串，0 内存浪费）
inline void init_vfs_clusters_lightweight() {
    if (!g_cfg.vfs.enabled) return;
    g_cfg.vfs.clusters.clear();

    if (!g_cfg.vfs.cpu_clusters_spec.empty()) {
        auto segs = split_str(g_cfg.vfs.cpu_clusters_spec, ';');
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
                    g_cfg.vfs.clusters.push_back(cl);
                }
            }
        }
    }

    if (!g_cfg.vfs.clusters.empty()) return;

    std::string token = g_cfg.vfs.soc_machine + " " + g_cfg.vfs.cpu_hardware;
    for (char &c : token) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));

    if (token.find("mt6991") != std::string::npos || token.find("6991") != std::string::npos) {
        g_cfg.vfs.clusters.push_back({0, 3, 800000,  2400000, 0xd81, 612,  build_opp_steps(800000,  2400000)});
        g_cfg.vfs.clusters.push_back({4, 6, 1000000, 3300000, 0xd82, 896,  build_opp_steps(1000000, 3300000)});
        g_cfg.vfs.clusters.push_back({7, 7, 1200000, 3620000, 0xd85, 1024, build_opp_steps(1200000, 3620000)});
    } else if (token.find("sm8650") != std::string::npos || token.find("pineapple") != std::string::npos) {
        g_cfg.vfs.clusters.push_back({0, 1, 556800, 2265600, 0xd80, 384,  build_opp_steps(556800, 2265600)});
        g_cfg.vfs.clusters.push_back({2, 6, 691200, 3148800, 0xd81, 845,  build_opp_steps(691200, 3148800)});
        g_cfg.vfs.clusters.push_back({7, 7, 806400, 3302400, 0xd82, 1024, build_opp_steps(806400, 3302400)});
    } else {
        int top_freq = g_cfg.vfs.cpu_max_freq_khz > 2000000 ? g_cfg.vfs.cpu_max_freq_khz : 4320000;
        int perf_freq = (top_freq == 4320000) ? 3532800 : (top_freq * 82 / 100);
        g_cfg.vfs.clusters.push_back({0, 5, 614400, perf_freq, 0xd85, 788,  build_opp_steps(614400, perf_freq)});
        g_cfg.vfs.clusters.push_back({6, 7, 806400, top_freq,  0xd87, 1024, build_opp_steps(806400, top_freq)});
    }
}

inline const CpuClusterSpec *find_cluster_for_core(int core_idx) noexcept {
    for (const auto &cl : g_cfg.vfs.clusters) {
        if (core_idx >= cl.first_core && core_idx <= cl.last_core) {
            return &cl;
        }
    }
    return g_cfg.vfs.clusters.empty() ? nullptr : &g_cfg.vfs.clusters.back();
}

inline int sample_live_core_freq_khz(int core_idx) noexcept {
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

// 算式级按需解析任意单核 cpufreq 或 policy* 叶子节点（零预生成内存占用）
inline bool synthesize_cpufreq_leaf(int core_id, const char *leaf, std::string *out_content, int *out_dyn_core) {
    const CpuClusterSpec *cl = find_cluster_for_core(core_id);
    if (!cl || !leaf || !out_content) return false;

    if (std::strcmp(leaf, "scaling_cur_freq") == 0 || std::strcmp(leaf, "cpuinfo_cur_freq") == 0) {
        *out_content = std::to_string(sample_live_core_freq_khz(core_id)) + "\n";
        if (out_dyn_core) *out_dyn_core = core_id;
        return true;
    }
    if (std::strcmp(leaf, "cpuinfo_max_freq") == 0 || std::strcmp(leaf, "scaling_max_freq") == 0 ||
        std::strcmp(leaf, "scaling_boost_frequencies") == 0) {
        *out_content = std::to_string(cl->max_freq_khz) + "\n";
        return true;
    }
    if (std::strcmp(leaf, "cpuinfo_min_freq") == 0 || std::strcmp(leaf, "scaling_min_freq") == 0) {
        *out_content = std::to_string(cl->min_freq_khz) + "\n";
        return true;
    }
    if (std::strcmp(leaf, "scaling_available_frequencies") == 0) {
        std::ostringstream ss;
        for (size_t i = 0; i < cl->opp_freqs_khz.size(); ++i) {
            if (i > 0) ss << " ";
            ss << cl->opp_freqs_khz[i];
        }
        ss << "\n";
        *out_content = ss.str();
        return true;
    }
    if (std::strcmp(leaf, "related_cpus") == 0 || std::strcmp(leaf, "affected_cpus") == 0) {
        std::ostringstream ss;
        for (int c = cl->first_core; c <= cl->last_core; ++c) {
            if (c > cl->first_core) ss << " ";
            ss << c;
        }
        ss << "\n";
        *out_content = ss.str();
        return true;
    }
    if (std::strcmp(leaf, "scaling_governor") == 0) {
        *out_content = "walt\n";
        return true;
    }
    if (std::strcmp(leaf, "scaling_available_governors") == 0) {
        *out_content = "walt schedutil performance powersave\n";
        return true;
    }
    if (std::strcmp(leaf, "scaling_driver") == 0) {
        *out_content = "qcom-cpufreq-hw\n";
        return true;
    }
    if (std::strcmp(leaf, "cpuinfo_transition_latency") == 0) {
        *out_content = "1000\n";
        return true;
    }
    if (std::strcmp(leaf, "stats/time_in_state") == 0) {
        std::ostringstream ss;
        for (size_t i = 0; i < cl->opp_freqs_khz.size(); ++i) {
            ss << cl->opp_freqs_khz[i] << " " << ((i + 1) * 14200) << "\n";
        }
        *out_content = ss.str();
        return true;
    }
    return false;
}

inline bool is_sensitive_proc_line(const char *line) noexcept {
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

// 核心：On-Demand 按需实时合成 VFS 节点内容（读哪个节点才生成哪个，未读节点 0 开销）
inline bool synthesize_vfs_node_on_demand(const std::string &norm, std::string *out_content, int *out_dyn_core = nullptr) {
    if (!g_cfg.vfs.enabled || norm.empty() || !out_content) return false;
    if (out_dyn_core) *out_dyn_core = -1;

    // 1. /sys/devices/system/cpu/* 或 /sys/bus/cpu/devices/*
    const char *cpu_sub = nullptr;
    if (norm.rfind("/sys/devices/system/cpu/", 0) == 0) {
        cpu_sub = norm.c_str() + 24;
    } else if (norm.rfind("/sys/bus/cpu/devices/", 0) == 0) {
        cpu_sub = norm.c_str() + 21;
    }

    if (cpu_sub) {
        int cores = (g_cfg.vfs.cpu_cores > 0 && g_cfg.vfs.cpu_cores <= 16) ? g_cfg.vfs.cpu_cores : 8;
        if (std::strcmp(cpu_sub, "present") == 0 ||
            std::strcmp(cpu_sub, "possible") == 0 ||
            std::strcmp(cpu_sub, "online") == 0) {
            *out_content = "0-" + std::to_string(cores - 1) + "\n";
            return true;
        }
        if (std::strcmp(cpu_sub, "offline") == 0) {
            *out_content = "\n";
            return true;
        }
        if (std::strcmp(cpu_sub, "kernel_max") == 0) {
            *out_content = std::to_string(cores - 1) + "\n";
            return true;
        }

        // 匹配 cpu<N>/...
        if (std::strncmp(cpu_sub, "cpu", 3) == 0 && cpu_sub[3] >= '0' && cpu_sub[3] <= '9') {
            char *endp = nullptr;
            int core_id = static_cast<int>(std::strtol(cpu_sub + 3, &endp, 10));
            if (core_id >= 0 && core_id < cores && endp && *endp == '/') {
                const char *rest = endp + 1;
                if (std::strncmp(rest, "cpufreq/", 8) == 0) {
                    return synthesize_cpufreq_leaf(core_id, rest + 8, out_content, out_dyn_core);
                }
                const CpuClusterSpec *cl = find_cluster_for_core(core_id);
                if (std::strcmp(rest, "online") == 0) { *out_content = "1\n"; return true; }
                if (std::strcmp(rest, "cpu_capacity") == 0 && cl) { *out_content = std::to_string(cl->capacity) + "\n"; return true; }
                if (std::strcmp(rest, "regs/identification/midr_el1") == 0) { *out_content = g_cfg.vfs.cpu_midr_el1 + "\n"; return true; }
                if (std::strcmp(rest, "regs/identification/revidr_el1") == 0) { *out_content = "0x0000000000000000\n"; return true; }
                if (std::strcmp(rest, "topology/core_id") == 0) { *out_content = std::to_string(core_id) + "\n"; return true; }
                if (std::strcmp(rest, "topology/cluster_id") == 0 && cl) { *out_content = std::to_string(cl->first_core) + "\n"; return true; }
                if (std::strcmp(rest, "topology/physical_package_id") == 0) { *out_content = "0\n"; return true; }
                if (std::strcmp(rest, "topology/core_cpus_list") == 0 && cl) {
                    *out_content = std::to_string(cl->first_core) + "-" + std::to_string(cl->last_core) + "\n";
                    return true;
                }
            }
        }

        // 匹配 cpufreq/policy<P>/...
        if (std::strncmp(cpu_sub, "cpufreq/policy", 14) == 0 && cpu_sub[14] >= '0' && cpu_sub[14] <= '9') {
            char *endp = nullptr;
            int pol_id = static_cast<int>(std::strtol(cpu_sub + 14, &endp, 10));
            if (pol_id >= 0 && pol_id < cores && endp && *endp == '/') {
                return synthesize_cpufreq_leaf(pol_id, endp + 1, out_content, out_dyn_core);
            }
        }
        return false;
    }

    // 2. /proc/* 节点按需合成
    if (norm.rfind("/proc/", 0) == 0) {
        if (norm == "/proc/meminfo") {
            int ram_gb = g_cfg.vfs.ram_gb > 0 ? g_cfg.vfs.ram_gb : 16;
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
            *out_content = ss.str();
            return true;
        }
        if (norm == "/proc/cpuinfo") {
            int cores = (g_cfg.vfs.cpu_cores > 0 && g_cfg.vfs.cpu_cores <= 16) ? g_cfg.vfs.cpu_cores : 8;
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
            ss << "Hardware\t: " << g_cfg.vfs.cpu_hardware << "\n";
            *out_content = ss.str();
            return true;
        }
        if (norm == "/proc/ddr_info") {
            *out_content = std::to_string(g_cfg.vfs.ram_gb) + "GB " + g_cfg.vfs.ddr_type + " (" + g_cfg.vfs.ddr_vendor + ")\n";
            return true;
        }
        if (norm == "/proc/version") {
            *out_content = "Linux version " + g_cfg.vfs.kernel_release + " (kleaf@build-host) " + g_cfg.vfs.kernel_version + "\n";
            return true;
        }
        if (norm == "/proc/sys/kernel/osrelease") {
            *out_content = g_cfg.vfs.kernel_release + "\n";
            return true;
        }
        return sanitize_live_proc_file(norm, out_content);
    }

    // 3. /sys/class/kgsl & /sys/devices/soc0 & /sys/devices/system/chip-info
    if (norm == "/sys/class/kgsl/kgsl-3d0/gpu_model") {
        *out_content = (g_cfg.gl.enabled && !g_cfg.gl.gl_renderer.empty() ? g_cfg.gl.gl_renderer : "Adreno (TM) 830") + "\n";
        return true;
    }
    if (norm == "/sys/devices/soc0/soc_id") { *out_content = g_cfg.vfs.soc_id + "\n"; return true; }
    if (norm == "/sys/devices/soc0/family") { *out_content = g_cfg.vfs.soc_family + "\n"; return true; }
    if (norm == "/sys/devices/soc0/machine") { *out_content = g_cfg.vfs.soc_machine + "\n"; return true; }
    if (norm == "/sys/devices/system/chip-info/ddr_info") {
        *out_content = std::to_string(g_cfg.vfs.ram_gb) + "GB " + g_cfg.vfs.ddr_type + " (" + g_cfg.vfs.ddr_vendor + ")\n";
        return true;
    }
    if (norm == "/sys/devices/system/chip-info/ddr_type" ||
        norm == "/sys/firmware/devicetree/base/memory/ddr_device_type") {
        *out_content = g_cfg.vfs.ddr_type + "\n";
        return true;
    }
    if (norm == "/sys/devices/system/chip-info/ddr_size") {
        *out_content = std::to_string(g_cfg.vfs.ram_gb) + "GB\n";
        return true;
    }

    // 4. 全分区 build.prop 按需合成 (仅当同时开启了 build 属性池时才生成虚拟 build.prop)
    if (g_cfg.build.enabled &&
        (norm == "/system/build.prop" || norm == "/system/etc/build.prop" ||
         norm == "/vendor/build.prop" || norm == "/vendor/etc/build.prop" ||
         norm == "/product/build.prop" || norm == "/product/etc/build.prop" ||
         norm == "/odm/etc/build.prop" || norm == "/odm/build.prop")) {
        std::ostringstream ss;
        for (const auto &kv : g_cfg.build.prop_map) {
            ss << kv.first << "=" << kv.second << "\n";
        }
        *out_content = ss.str();
        return true;
    }

    return false;
}

inline std::string normalize_vfs_path(const char *raw_path) {
    if (!is_vfs_candidate_prefix(raw_path)) return "";
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

inline std::string resolve_dirfd_path_fast(int dirfd, const char *pathname) {
    if (!pathname) return "";
    if (pathname[0] == '/') return normalize_vfs_path(pathname);
    if (dirfd < 0 || dirfd == AT_FDCWD || !is_cpufreq_relative_candidate(pathname)) return "";

    char link_target[256] = {0};
    char proc_fd_path[64];
    std::snprintf(proc_fd_path, sizeof(proc_fd_path), "/proc/self/fd/%d", dirfd);
    ssize_t len = orig_readlink
                      ? orig_readlink(proc_fd_path, link_target, sizeof(link_target) - 1)
                      : readlink(proc_fd_path, link_target, sizeof(link_target) - 1);
    if (len <= 0 || !is_vfs_candidate_prefix(link_target)) return "";
    link_target[len] = '\0';

    std::string combined = std::string(link_target) + "/" + pathname;
    return normalize_vfs_path(combined.c_str());
}

inline int open_cloaked_vfs_fd_norm(const std::string &norm) {
    if (norm.empty() || !g_cfg.vfs.enabled) return -1;

    std::string content;
    int dyn_core_id = -1;
    if (!synthesize_vfs_node_on_demand(norm, &content, &dyn_core_id)) {
        return -1;
    }

    int fd = static_cast<int>(syscall(__NR_memfd_create, "", MFD_CLOEXEC));
    if (fd < 0) return -1;

    if (!content.empty()) {
        write_full(fd, content.data(), content.size());
        if (orig_lseek) orig_lseek(fd, 0, SEEK_SET); else lseek(fd, 0, SEEK_SET);
    }

    g_fd_cloak.register_fd(fd, norm, content.size(), dyn_core_id);
    emit_probe("VFS_OPEN", norm, (dyn_core_id >= 0) ? ("LiveFreq:" + content.substr(0, content.size() - 1)) : "Cloaked");
    return fd;
}

inline int open_cloaked_vfs_fd(const char *raw_path) {
    if (__builtin_expect(!is_vfs_candidate_prefix(raw_path), 1)) return -1;
    return open_cloaked_vfs_fd_norm(normalize_vfs_path(raw_path));
}

// ============================================================================
// Bionic I/O & Fortify 全家族极速 Hook 实现
// ============================================================================
inline int hook_open(const char *pathname, int flags, mode_t mode) {
    if (__builtin_expect(is_vfs_candidate_prefix(pathname), 0)) {
        ScopedHookGuard guard;
        if (guard) {
            int fake_fd = open_cloaked_vfs_fd(pathname);
            if (fake_fd >= 0) return fake_fd;
        }
    }
    return orig_open ? orig_open(pathname, flags, mode) : -1;
}

inline int hook_open64(const char *pathname, int flags, mode_t mode) {
    if (__builtin_expect(is_vfs_candidate_prefix(pathname), 0)) {
        ScopedHookGuard guard;
        if (guard) {
            int fake_fd = open_cloaked_vfs_fd(pathname);
            if (fake_fd >= 0) return fake_fd;
        }
    }
    return orig_open64 ? orig_open64(pathname, flags, mode) : (orig_open ? orig_open(pathname, flags, mode) : -1);
}

inline int hook_open_2(const char *pathname, int flags) {
    if (__builtin_expect(is_vfs_candidate_prefix(pathname), 0)) {
        ScopedHookGuard guard;
        if (guard) {
            int fake_fd = open_cloaked_vfs_fd(pathname);
            if (fake_fd >= 0) return fake_fd;
        }
    }
    return orig_open_2 ? orig_open_2(pathname, flags) : (orig_open ? orig_open(pathname, flags, 0) : -1);
}

inline int hook_openat(int dirfd, const char *pathname, int flags, mode_t mode) {
    if (__builtin_expect(is_vfs_candidate_prefix(pathname) || is_cpufreq_relative_candidate(pathname), 0)) {
        ScopedHookGuard guard;
        if (guard) {
            int fake_fd = open_cloaked_vfs_fd_norm(resolve_dirfd_path_fast(dirfd, pathname));
            if (fake_fd >= 0) return fake_fd;
        }
    }
    return orig_openat ? orig_openat(dirfd, pathname, flags, mode) : -1;
}

inline int hook_openat64(int dirfd, const char *pathname, int flags, mode_t mode) {
    if (__builtin_expect(is_vfs_candidate_prefix(pathname) || is_cpufreq_relative_candidate(pathname), 0)) {
        ScopedHookGuard guard;
        if (guard) {
            int fake_fd = open_cloaked_vfs_fd_norm(resolve_dirfd_path_fast(dirfd, pathname));
            if (fake_fd >= 0) return fake_fd;
        }
    }
    return orig_openat64 ? orig_openat64(dirfd, pathname, flags, mode) : (orig_openat ? orig_openat(dirfd, pathname, flags, mode) : -1);
}

inline int hook_openat_2(int dirfd, const char *pathname, int flags) {
    if (__builtin_expect(is_vfs_candidate_prefix(pathname) || is_cpufreq_relative_candidate(pathname), 0)) {
        ScopedHookGuard guard;
        if (guard) {
            int fake_fd = open_cloaked_vfs_fd_norm(resolve_dirfd_path_fast(dirfd, pathname));
            if (fake_fd >= 0) return fake_fd;
        }
    }
    return orig_openat_2 ? orig_openat_2(dirfd, pathname, flags) : (orig_openat ? orig_openat(dirfd, pathname, flags, 0) : -1);
}

inline int hook_openat64_2(int dirfd, const char *pathname, int flags) {
    if (__builtin_expect(is_vfs_candidate_prefix(pathname) || is_cpufreq_relative_candidate(pathname), 0)) {
        ScopedHookGuard guard;
        if (guard) {
            int fake_fd = open_cloaked_vfs_fd_norm(resolve_dirfd_path_fast(dirfd, pathname));
            if (fake_fd >= 0) return fake_fd;
        }
    }
    return orig_openat64_2 ? orig_openat64_2(dirfd, pathname, flags) : (orig_openat_2 ? orig_openat_2(dirfd, pathname, flags) : -1);
}

inline FILE *hook_fopen(const char *pathname, const char *mode) {
    if (__builtin_expect(is_vfs_candidate_prefix(pathname), 0)) {
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
    }
    return orig_fopen ? orig_fopen(pathname, mode) : nullptr;
}

inline FILE *hook_fopen64(const char *pathname, const char *mode) {
    return hook_fopen(pathname, mode);
}

inline int hook_close(int fd) {
    g_fd_cloak.unregister_fd(fd); // 内部使用 O(1) 无锁位图预检，普通 fd 1 周期返回
    return orig_close ? orig_close(fd) : close(fd);
}

inline void refresh_dynamic_freq_fd_on_rewind(int fd) {
    int core_id = -1;
    if (__builtin_expect(g_fd_cloak.get_dynamic_core_id(fd, &core_id), 0)) {
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

inline int hook_access(const char *pathname, int mode) {
    if (__builtin_expect(is_vfs_candidate_prefix(pathname), 0)) {
        ScopedHookGuard guard;
        std::string content;
        if (guard && synthesize_vfs_node_on_demand(normalize_vfs_path(pathname), &content)) {
            return 0;
        }
    }
    return orig_access ? orig_access(pathname, mode) : -1;
}

inline int hook_faccessat(int dirfd, const char *pathname, int mode, int flags) {
    if (__builtin_expect(is_vfs_candidate_prefix(pathname) || is_cpufreq_relative_candidate(pathname), 0)) {
        ScopedHookGuard guard;
        std::string content;
        if (guard && synthesize_vfs_node_on_demand(resolve_dirfd_path_fast(dirfd, pathname), &content)) {
            return 0;
        }
    }
    return orig_faccessat ? orig_faccessat(dirfd, pathname, mode, flags) : -1;
}

inline int hook_stat(const char *pathname, struct stat *statbuf) {
    if (__builtin_expect(is_vfs_candidate_prefix(pathname), 0)) {
        ScopedHookGuard guard;
        std::string norm = normalize_vfs_path(pathname);
        std::string content;
        if (guard && statbuf && synthesize_vfs_node_on_demand(norm, &content)) {
            populate_virtual_stat(statbuf, norm, content.size());
            return 0;
        }
    }
    return orig_stat ? orig_stat(pathname, statbuf) : -1;
}

inline int hook_stat64(const char *pathname, struct stat64 *statbuf) {
    if (__builtin_expect(is_vfs_candidate_prefix(pathname), 0)) {
        ScopedHookGuard guard;
        std::string norm = normalize_vfs_path(pathname);
        std::string content;
        if (guard && statbuf && synthesize_vfs_node_on_demand(norm, &content)) {
            populate_virtual_stat(statbuf, norm, content.size());
            return 0;
        }
    }
    return orig_stat64 ? orig_stat64(pathname, statbuf) : -1;
}

inline int hook_lstat(const char *pathname, struct stat *statbuf) {
    return hook_stat(pathname, statbuf);
}

inline int hook_lstat64(const char *pathname, struct stat64 *statbuf) {
    return hook_stat64(pathname, statbuf);
}

inline int hook_fstatat(int dirfd, const char *pathname, struct stat *statbuf, int flags) {
    if (__builtin_expect(is_vfs_candidate_prefix(pathname) || is_cpufreq_relative_candidate(pathname), 0)) {
        ScopedHookGuard guard;
        std::string norm = resolve_dirfd_path_fast(dirfd, pathname);
        std::string content;
        if (guard && statbuf && synthesize_vfs_node_on_demand(norm, &content)) {
            populate_virtual_stat(statbuf, norm, content.size());
            return 0;
        }
    }
    return orig_fstatat ? orig_fstatat(dirfd, pathname, statbuf, flags) : -1;
}

inline int hook_fstatat64(int dirfd, const char *pathname, struct stat64 *statbuf, int flags) {
    if (__builtin_expect(is_vfs_candidate_prefix(pathname) || is_cpufreq_relative_candidate(pathname), 0)) {
        ScopedHookGuard guard;
        std::string norm = resolve_dirfd_path_fast(dirfd, pathname);
        std::string content;
        if (guard && statbuf && synthesize_vfs_node_on_demand(norm, &content)) {
            populate_virtual_stat(statbuf, norm, content.size());
            return 0;
        }
    }
    return orig_fstatat64 ? orig_fstatat64(dirfd, pathname, statbuf, flags) : -1;
}

inline int hook_fstat(int fd, struct stat *statbuf) {
    if (__builtin_expect(g_fd_cloak.get_cloaked_stat(fd, statbuf), 0)) {
        return 0;
    }
    return orig_fstat ? orig_fstat(fd, statbuf) : -1;
}

inline int hook_fstat64(int fd, struct stat64 *statbuf) {
    if (__builtin_expect(g_fd_cloak.get_cloaked_stat(fd, statbuf), 0)) {
        return 0;
    }
    return orig_fstat64 ? orig_fstat64(fd, statbuf) : -1;
}

inline ssize_t hook_readlink(const char *pathname, char *buf, size_t bufsiz) {
    std::string cloaked_path;
    if (__builtin_expect(g_fd_cloak.resolve_proc_fd_link(pathname, &cloaked_path), 0) && buf && bufsiz > 0) {
        size_t copy_len = std::min(bufsiz, cloaked_path.size());
        std::memcpy(buf, cloaked_path.data(), copy_len);
        ScopedHookGuard guard;
        if (guard) emit_probe("FD_CLOAK", pathname, cloaked_path);
        return static_cast<ssize_t>(copy_len);
    }
    return orig_readlink ? orig_readlink(pathname, buf, bufsiz) : -1;
}

inline ssize_t hook_readlinkat(int dirfd, const char *pathname, char *buf, size_t bufsiz) {
    std::string cloaked_path;
    if (__builtin_expect(g_fd_cloak.resolve_proc_fd_link(pathname, &cloaked_path), 0) && buf && bufsiz > 0) {
        size_t copy_len = std::min(bufsiz, cloaked_path.size());
        std::memcpy(buf, cloaked_path.data(), copy_len);
        ScopedHookGuard guard;
        if (guard) emit_probe("FD_CLOAK", pathname, cloaked_path);
        return static_cast<ssize_t>(copy_len);
    }
    return orig_readlinkat ? orig_readlinkat(dirfd, pathname, buf, bufsiz) : -1;
}

inline int hook_sysinfo(struct sysinfo *info) {
    int ret = orig_sysinfo ? orig_sysinfo(info) : sysinfo(info);
    if (ret == 0 && info && g_cfg.vfs.enabled && g_cfg.vfs.ram_gb > 0) {
        uint64_t total_bytes = calc_realistic_memtotal_kb(g_cfg.vfs.ram_gb) * 1024ULL;
        uint32_t unit = info->mem_unit > 0 ? info->mem_unit : 1;
        info->totalram = static_cast<unsigned long>(total_bytes / unit);
        info->freeram  = (info->totalram * 58UL) / 100UL;
        info->bufferram = (info->totalram * 4UL) / 100UL;
        ScopedHookGuard guard;
        if (guard) emit_probe("SYSINFO", "totalram", std::to_string(g_cfg.vfs.ram_gb) + "GB");
    }
    return ret;
}

inline long hook_sysconf(int name) {
    if (g_cfg.vfs.enabled) {
        if (name == _SC_NPROCESSORS_CONF || name == _SC_NPROCESSORS_ONLN) {
            ScopedHookGuard guard;
            if (guard) emit_probe("SYSCONF", "_SC_NPROCESSORS", std::to_string(g_cfg.vfs.cpu_cores));
            return g_cfg.vfs.cpu_cores > 0 ? g_cfg.vfs.cpu_cores : 8;
        }
        if (name == _SC_PHYS_PAGES && g_cfg.vfs.ram_gb > 0) {
            long page_sz = orig_sysconf ? orig_sysconf(_SC_PAGESIZE) : 4096;
            if (page_sz <= 0) page_sz = 4096;
            uint64_t total_bytes = calc_realistic_memtotal_kb(g_cfg.vfs.ram_gb) * 1024ULL;
            ScopedHookGuard guard;
            if (guard) emit_probe("SYSCONF", "_SC_PHYS_PAGES", std::to_string(g_cfg.vfs.ram_gb) + "GB");
            return static_cast<long>(total_bytes / static_cast<uint64_t>(page_sz));
        }
    }
    return orig_sysconf ? orig_sysconf(name) : sysconf(name);
}

inline int hook_uname(struct utsname *buf) {
    int ret = orig_uname ? orig_uname(buf) : uname(buf);
    if (ret == 0 && buf && g_cfg.vfs.enabled) {
        if (!g_cfg.vfs.cpu_arch.empty()) {
            std::strncpy(buf->machine, g_cfg.vfs.cpu_arch.c_str(), sizeof(buf->machine) - 1);
            buf->machine[sizeof(buf->machine) - 1] = '\0';
        }
        if (!g_cfg.vfs.kernel_release.empty()) {
            std::strncpy(buf->release, g_cfg.vfs.kernel_release.c_str(), sizeof(buf->release) - 1);
            buf->release[sizeof(buf->release) - 1] = '\0';
        }
        if (!g_cfg.vfs.kernel_version.empty()) {
            std::strncpy(buf->version, g_cfg.vfs.kernel_version.c_str(), sizeof(buf->version) - 1);
            buf->version[sizeof(buf->version) - 1] = '\0';
        }
        ScopedHookGuard guard;
        if (guard) emit_probe("UNAME", "release/machine", std::string(buf->release) + " / " + buf->machine);
    }
    return ret;
}

inline FILE *hook_popen(const char *command, const char *type) {
    ScopedHookGuard guard;
    if (guard && command && g_cfg.build.enabled) {
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
                    auto it = g_cfg.build.prop_map.find(key);
                    if (it != g_cfg.build.prop_map.end()) {
                        output = it->second + "\n";
                        emit_probe("POPEN_GETPROP", key, it->second);
                    }
                }
            } else {
                std::ostringstream ss;
                for (const auto &kv : g_cfg.build.prop_map) {
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

#pragma once

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <cinttypes>
#include <sstream>
#include <unistd.h>
#include <fcntl.h>
#include <dlfcn.h>
#include <signal.h>
#include <sys/prctl.h>
#include <sys/syscall.h>
#include <sys/sysinfo.h>
#include <sys/utsname.h>
#include <sys/stat.h>
#include <linux/memfd.h>
#include <linux/filter.h>
#include <linux/seccomp.h>
#include <sys/mman.h>
#include <linux/audit.h>
#include <ucontext.h>
#include "mirage_config.hpp"
#include "mirage_logger.hpp"
#include "fd_cloak_tracker.hpp"
#include "jvm_art_engine.hpp"

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
inline int (*orig_fclose)(FILE *fp) = nullptr;
inline int (*orig_close)(int fd) = nullptr;
inline int (*orig_fdsan_close)(int fd, uint64_t tag) = nullptr;
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

inline void init_vfs_symbols() {
    if (orig_openat) return;
    ErrnoRestorer errno_guard;
    orig_open = reinterpret_cast<decltype(orig_open)>(dlsym(RTLD_DEFAULT, "open"));
    orig_open64 = reinterpret_cast<decltype(orig_open64)>(dlsym(RTLD_DEFAULT, "open64"));
    orig_open_2 = reinterpret_cast<decltype(orig_open_2)>(dlsym(RTLD_DEFAULT, "__open_2"));
    orig_openat = reinterpret_cast<decltype(orig_openat)>(dlsym(RTLD_DEFAULT, "openat"));
    orig_openat64 = reinterpret_cast<decltype(orig_openat64)>(dlsym(RTLD_DEFAULT, "openat64"));
    orig_openat_2 = reinterpret_cast<decltype(orig_openat_2)>(dlsym(RTLD_DEFAULT, "__openat_2"));
    orig_openat64_2 = reinterpret_cast<decltype(orig_openat64_2)>(dlsym(RTLD_DEFAULT, "__openat64_2"));
    orig_fopen = reinterpret_cast<decltype(orig_fopen)>(dlsym(RTLD_DEFAULT, "fopen"));
    orig_fopen64 = reinterpret_cast<decltype(orig_fopen64)>(dlsym(RTLD_DEFAULT, "fopen64"));
    orig_fclose = reinterpret_cast<decltype(orig_fclose)>(dlsym(RTLD_DEFAULT, "fclose"));
    orig_close = reinterpret_cast<decltype(orig_close)>(dlsym(RTLD_DEFAULT, "close"));
    orig_fdsan_close = reinterpret_cast<decltype(orig_fdsan_close)>(dlsym(RTLD_DEFAULT, "android_fdsan_close_with_tag"));
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

inline bool is_vfs_candidate_prefix(const char *p) noexcept {
    if (!p || p[0] != '/') return false;
    while (*p == '/') ++p;
    return (p[0] == 'p' || p[0] == 's' || p[0] == 'v' || p[0] == 'o');
}

inline bool is_cpufreq_relative_candidate(const char *rel) noexcept {
    if (!rel || rel[0] == '/' || rel[0] == '.') return false;
    char c = rel[0];
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
    if (orig_fclose) orig_fclose(real_fp); else std::fclose(real_fp);

    *out_clean = clean_ss.str();
    emit_probe("MAPS_CLOAK", norm_path, "Stripped " + std::to_string(stripped) + " traces");
    return true;
}

inline bool synthesize_vfs_node_on_demand(const std::string &norm, std::string *out_content, int *out_dyn_core = nullptr) {
    if (!g_cfg.vfs.enabled || norm.empty() || !out_content) return false;
    if (out_dyn_core) *out_dyn_core = -1;

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

        if (std::strncmp(cpu_sub, "cpufreq/policy", 14) == 0 && cpu_sub[14] >= '0' && cpu_sub[14] <= '9') {
            char *endp = nullptr;
            int pol_id = static_cast<int>(std::strtol(cpu_sub + 14, &endp, 10));
            if (pol_id >= 0 && pol_id < cores && endp && *endp == '/') {
                return synthesize_cpufreq_leaf(pol_id, endp + 1, out_content, out_dyn_core);
            }
        }
        return false;
    }

    if (norm.rfind("/proc/", 0) == 0) {
        if (norm == "/proc/meminfo") {
            int ram_gb = g_cfg.vfs.ram_gb > 0 ? g_cfg.vfs.ram_gb : 16;
            uint64_t total_kb = calc_realistic_memtotal_kb(ram_gb);
            uint64_t free_kb  = (total_kb * 24ULL) / 100ULL;
            uint64_t avail_kb = (total_kb * 58ULL) / 100ULL;
            uint64_t cached_kb = (total_kb * 28ULL) / 100ULL;
            uint64_t active_kb = (total_kb * 36ULL) / 100ULL;
            uint64_t inactive_kb = (total_kb * 22ULL) / 100ULL;
            uint64_t sreclaim_kb = (total_kb * 4ULL) / 100ULL;
            uint64_t sunreclaim_kb = (total_kb * 3ULL) / 100ULL;
            uint64_t mapped_kb = (total_kb * 8ULL) / 100ULL;
            uint64_t shmem_kb = (total_kb * 2ULL) / 100ULL;

            std::ostringstream ss;
            ss << "MemTotal:       " << total_kb << " kB\n"
               << "MemFree:        " << free_kb << " kB\n"
               << "MemAvailable:   " << avail_kb << " kB\n"
               << "Buffers:          184320 kB\n"
               << "Cached:         " << cached_kb << " kB\n"
               << "SwapCached:            0 kB\n"
               << "Active:         " << active_kb << " kB\n"
               << "Inactive:       " << inactive_kb << " kB\n"
               << "Shmem:          " << shmem_kb << " kB\n"
               << "KReclaimable:   " << sreclaim_kb << " kB\n"
               << "Slab:           " << (sreclaim_kb + sunreclaim_kb) << " kB\n"
               << "SReclaimable:   " << sreclaim_kb << " kB\n"
               << "SUnreclaim:     " << sunreclaim_kb << " kB\n"
               << "Mapped:         " << mapped_kb << " kB\n"
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

    ErrnoRestorer errno_guard;
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
    ErrnoRestorer errno_guard;

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

// 关键修复 1：拦截 fclose，在关闭 FILE* 前先解绑底层 fileno(fp)，防止 FD 残留污染
inline int hook_fclose(FILE *fp) {
    if (fp) {
        int fd = fileno(fp);
        if (fd >= 0) g_fd_cloak.unregister_fd(fd);
    }
    return orig_fclose ? orig_fclose(fp) : std::fclose(fp);
}

inline int hook_close(int fd) {
    g_fd_cloak.unregister_fd(fd);
    return orig_close ? orig_close(fd) : close(fd);
}

// 关键修复 2：拦截 Android 10+ libjavacore.so 使用的 android_fdsan_close_with_tag
inline int hook_android_fdsan_close_with_tag(int fd, uint64_t tag) {
    g_fd_cloak.unregister_fd(fd);
    return orig_fdsan_close ? orig_fdsan_close(fd, tag) : (orig_close ? orig_close(fd) : close(fd));
}

inline void refresh_dynamic_freq_fd_on_rewind(int fd) {
    int core_id = -1;
    if (__builtin_expect(g_fd_cloak.get_dynamic_core_id(fd, &core_id), 0)) {
        ErrnoRestorer errno_guard;
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
        if ((name == _SC_PHYS_PAGES || name == _SC_AVPHYS_PAGES) && g_cfg.vfs.ram_gb > 0) {
            long page_sz = orig_sysconf ? orig_sysconf(_SC_PAGESIZE) : 4096;
            if (page_sz <= 0) page_sz = 4096;
            uint64_t total_bytes = calc_realistic_memtotal_kb(g_cfg.vfs.ram_gb) * 1024ULL;
            uint64_t target_bytes = (name == _SC_AVPHYS_PAGES) ? ((total_bytes * 58ULL) / 100ULL) : total_bytes;
            ScopedHookGuard guard;
            if (guard) {
                emit_probe("SYSCONF",
                           name == _SC_AVPHYS_PAGES ? "_SC_AVPHYS_PAGES" : "_SC_PHYS_PAGES",
                           std::to_string(g_cfg.vfs.ram_gb) + "GB");
            }
            return static_cast<long>(target_bytes / static_cast<uint64_t>(page_sz));
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

// ============================================================================
// Binder IPC 内存伪装引擎：拦截 ActivityManager.getMemoryInfo(MemoryInfo)
// 彻底解决 Android 14–17 全软件通过 Binder 向 system_server 查询可用内存导致 VFS 失效的问题
// ============================================================================
using PFN_BpBinder_transact = int32_t (*)(void *self, uint32_t code, const void *data, void *reply, uint32_t flags);
using PFN_Parcel_data = const uint8_t *(*)(const void *parcel);
using PFN_Parcel_dataSize = size_t (*)(const void *parcel);

inline PFN_BpBinder_transact orig_BpBinder_transact = nullptr;
inline PFN_Parcel_data orig_Parcel_data = nullptr;
inline PFN_Parcel_dataSize orig_Parcel_dataSize = nullptr;
inline bool g_binder_meminfo_hooked = false;

inline bool parcel_contains_iam_token(const uint8_t *buf, size_t sz) noexcept {
    // Parcel 中 UTF-16LE 编码的 "android.app.IActivityManager" 关键子串 "IActivityManager"
    static constexpr uint8_t kNeedle[] = {
        'I',0,'A',0,'c',0,'t',0,'i',0,'v',0,'i',0,'t',0,
        'y',0,'M',0,'a',0,'n',0,'a',0,'g',0,'e',0,'r',0
    };
    if (!buf || sz < sizeof(kNeedle)) return false;
    size_t limit = (sz < 128U) ? sz : 128U;
    for (size_t i = 0; i + sizeof(kNeedle) <= limit; i += 2) {
        if (std::memcmp(buf + i, kNeedle, sizeof(kNeedle)) == 0) {
            return true;
        }
    }
    return false;
}

inline int32_t hook_BpBinder_transact(void *self, uint32_t code, const void *data, void *reply, uint32_t flags) {
    int32_t ret = orig_BpBinder_transact ? orig_BpBinder_transact(self, code, data, reply, flags) : -1;
    if (__builtin_expect(
            ret != 0 || !g_cfg.vfs.enabled || g_cfg.vfs.ram_gb <= 0 ||
            !data || !reply || g_am_get_memory_info_code == 0 ||
            code != g_am_get_memory_info_code,
            1)) {
        return ret;
    }

    if (!orig_Parcel_data || !orig_Parcel_dataSize) return ret;

    const uint8_t *req_buf = orig_Parcel_data(data);
    size_t req_sz = orig_Parcel_dataSize(data);
    if (!parcel_contains_iam_token(req_buf, req_sz)) {
        return ret;
    }

    auto *rep_buf = const_cast<uint8_t *>(orig_Parcel_data(reply));
    size_t rep_sz = orig_Parcel_dataSize(reply);
    // Reply Parcel 布局: [0..3] exception(0), [4..7] nonNull(1), [8..15] availMem, [16..23] totalMem, [24..31] threshold, [32..35] lowMemory
    if (!rep_buf || rep_sz < 36U) return ret;

    int32_t exc = -1, non_null = 0;
    std::memcpy(&exc, rep_buf, sizeof(exc));
    std::memcpy(&non_null, rep_buf + 4, sizeof(non_null));
    if (exc != 0 || non_null != 1) return ret;

    int64_t real_avail = 0, real_total = 0;
    std::memcpy(&real_avail, rep_buf + 8, sizeof(real_avail));
    std::memcpy(&real_total, rep_buf + 16, sizeof(real_total));

    // 二次合法性校验：确认真机值在合理物理内存范围内 (512MB ~ 128GB)
    constexpr int64_t kMinRam = 512LL * 1024LL * 1024LL;
    constexpr int64_t kMaxRam = 128LL * 1024LL * 1024LL * 1024LL;
    if (real_total < kMinRam || real_total > kMaxRam || real_avail < 0 || real_avail > real_total) {
        return ret;
    }

    int64_t fake_total = static_cast<int64_t>(calc_realistic_memtotal_kb(g_cfg.vfs.ram_gb) * 1024ULL);
    // 按真机当前空闲比例同比缩放可用内存（若比例过低则保底 45%，保证观感自然且会随系统实时波动）
    double free_ratio = static_cast<double>(real_avail) / static_cast<double>(real_total);
    if (free_ratio < 0.20) free_ratio = 0.45;
    if (free_ratio > 0.85) free_ratio = 0.72;
    int64_t fake_avail = static_cast<int64_t>(static_cast<double>(fake_total) * free_ratio);
    int32_t fake_low_mem = 0;

    std::memcpy(rep_buf + 8, &fake_avail, sizeof(fake_avail));
    std::memcpy(rep_buf + 16, &fake_total, sizeof(fake_total));
    std::memcpy(rep_buf + 32, &fake_low_mem, sizeof(fake_low_mem));

    ScopedHookGuard guard;
    if (guard) {
        emit_probe("BINDER_MEMINFO", "ActivityManager.getMemoryInfo",
                   "avail=" + std::to_string(fake_avail / (1024LL * 1024LL)) +
                   "MB / total=" + std::to_string(fake_total / (1024LL * 1024LL)) + "MB");
    }
    return ret;
}

inline void install_binder_meminfo_hook() {
    if (g_binder_meminfo_hooked || !g_cfg.vfs.enabled || g_cfg.vfs.ram_gb <= 0) return;
    if (g_am_get_memory_info_code == 0) return;
    g_binder_meminfo_hooked = true;

    ErrnoRestorer errno_guard;
    void *binder_h = dlopen("libbinder.so", RTLD_NOW | RTLD_NOLOAD);
    if (!binder_h) binder_h = RTLD_DEFAULT;

    orig_BpBinder_transact = reinterpret_cast<PFN_BpBinder_transact>(
        dlsym(binder_h, "_ZN7android8BpBinder8transactEjRKNS_6ParcelEPS1_j"));
    orig_Parcel_data = reinterpret_cast<PFN_Parcel_data>(
        dlsym(binder_h, "_ZNK7android6Parcel4dataEv"));
    orig_Parcel_dataSize = reinterpret_cast<PFN_Parcel_dataSize>(
        dlsym(binder_h, "_ZNK7android6Parcel8dataSizeEv"));
    auto **vtable = reinterpret_cast<void **>(
        dlsym(binder_h, "_ZTVN7android8BpBinderE"));

    if (!orig_BpBinder_transact || !orig_Parcel_data || !orig_Parcel_dataSize || !vtable) {
        return;
    }

    // 动态扫描 vtable 定位 transact 槽位，0 硬编码偏移，支持 Android 10–17 与 16KB 页大小
    long page_sz = sysconf(_SC_PAGESIZE);
    if (page_sz <= 0) page_sz = 4096;
    uintptr_t page_mask = ~static_cast<uintptr_t>(page_sz - 1);

    for (size_t i = 0; i < 20; ++i) {
        if (vtable[i] == reinterpret_cast<void *>(orig_BpBinder_transact)) {
            void *page_start = reinterpret_cast<void *>(reinterpret_cast<uintptr_t>(&vtable[i]) & page_mask);
            if (mprotect(page_start, static_cast<size_t>(page_sz), PROT_READ | PROT_WRITE) == 0) {
                vtable[i] = reinterpret_cast<void *>(hook_BpBinder_transact);
                mprotect(page_start, static_cast<size_t>(page_sz), PROT_READ);
                emit_probe("BINDER_MEMINFO", "BpBinder::transact",
                           "Hooked (tx=" + std::to_string(g_am_get_memory_info_code) + ")");
            }
            break;
        }
    }
}

// ============================================================================
// 按需 Seccomp-BPF 内联 svc #0 拦截器
// 关键修复：绝对优先放行本模块、libc.so、linker64、libart.so、[vdso]，绝不被上限截断！
// ============================================================================
inline struct sigaction g_old_sigsys{};
inline bool g_seccomp_installed = false;
inline int g_seccomp_probe_pipe[2] = {-1, -1};
inline std::atomic_flag g_seccomp_pipe_lock = ATOMIC_FLAG_INIT;

#if defined(__aarch64__)
__attribute__((noinline))
inline int64_t mirage_seccomp_bypass_syscall(
    long nr, long a0, long a1, long a2, long a3, long a4, long a5) noexcept {
    register long x8 __asm__("x8") = nr;
    register long x0 __asm__("x0") = a0;
    register long x1 __asm__("x1") = a1;
    register long x2 __asm__("x2") = a2;
    register long x3 __asm__("x3") = a3;
    register long x4 __asm__("x4") = a4;
    register long x5 __asm__("x5") = a5;
    __asm__ volatile(
        "svc #0\n"
        : "+r"(x0)
        : "r"(x8), "r"(x1), "r"(x2), "r"(x3), "r"(x4), "r"(x5)
        : "memory", "cc"
    );
    return static_cast<int64_t>(x0);
}

inline bool safe_copy_user_proc_or_sys_path(const char *user_ptr, char *out_buf, size_t out_cap) noexcept {
    if (!user_ptr || !out_buf || out_cap < 8) return false;
    if (g_seccomp_probe_pipe[0] < 0 || g_seccomp_probe_pipe[1] < 0) return false;
    if (g_seccomp_pipe_lock.test_and_set(std::memory_order_acquire)) return false;

    size_t copied = 0;
    bool nul_found = false;
    bool valid_prefix = true;

    while (copied + 1 < out_cap) {
        uintptr_t cur = reinterpret_cast<uintptr_t>(user_ptr) + copied;
        size_t to_page_end = 4096U - (cur & 4095U);
        size_t remain = (out_cap - 1) - copied;
        size_t chunk = (to_page_end < remain) ? to_page_end : remain;
        if (chunk > 64U) chunk = 64U;

        int64_t wr = mirage_seccomp_bypass_syscall(
            __NR_write, g_seccomp_probe_pipe[1],
            static_cast<long>(cur), static_cast<long>(chunk), 0, 0, 0);
        if (wr <= 0) {
            valid_prefix = false;
            break;
        }
        int64_t rd = mirage_seccomp_bypass_syscall(
            __NR_read, g_seccomp_probe_pipe[0],
            reinterpret_cast<long>(out_buf + copied), static_cast<long>(wr), 0, 0, 0);
        if (rd <= 0) {
            valid_prefix = false;
            break;
        }

        for (int64_t i = 0; i < rd; ++i) {
            if (out_buf[copied + static_cast<size_t>(i)] == '\0') {
                nul_found = true;
                break;
            }
        }
        copied += static_cast<size_t>(rd);

        if (copied >= 1 && out_buf[0] != '/') {
            valid_prefix = false;
            break;
        }
        if (copied >= 6) {
            if (std::strncmp(out_buf, "/proc/", 6) != 0 && std::strncmp(out_buf, "/sys/", 5) != 0) {
                valid_prefix = false;
                break;
            }
        }
        if (nul_found) break;
    }

    g_seccomp_pipe_lock.clear(std::memory_order_release);
    if (!valid_prefix || !nul_found || copied < 6) return false;
    return std::strncmp(out_buf, "/proc/", 6) == 0 || std::strncmp(out_buf, "/sys/", 5) == 0;
}

inline void sigsys_seccomp_handler(int sig, siginfo_t *info, void *ucontext_raw) {
    if (sig != SIGSYS || !info || !ucontext_raw) return;

    int nr = info->si_syscall;
    if (nr != __NR_openat && nr != __NR_readlinkat && nr != __NR_uname && nr != __NR_sysinfo) {
        if ((g_old_sigsys.sa_flags & SA_SIGINFO) && g_old_sigsys.sa_sigaction) {
            g_old_sigsys.sa_sigaction(sig, info, ucontext_raw);
        } else if (g_old_sigsys.sa_handler != SIG_DFL && g_old_sigsys.sa_handler != SIG_IGN) {
            g_old_sigsys.sa_handler(sig);
        } else {
            auto *uc = static_cast<ucontext_t *>(ucontext_raw);
            uc->uc_mcontext.regs[0] = static_cast<uint64_t>(-ENOSYS);
        }
        return;
    }

    auto *uc = static_cast<ucontext_t *>(ucontext_raw);
    long a0 = static_cast<long>(uc->uc_mcontext.regs[0]);
    long a1 = static_cast<long>(uc->uc_mcontext.regs[1]);
    long a2 = static_cast<long>(uc->uc_mcontext.regs[2]);
    long a3 = static_cast<long>(uc->uc_mcontext.regs[3]);

    // 若同线程正在执行 Hook 或重入 SIGSYS，零堆分配直接直通裸 svc #0
    if (__builtin_expect(g_tls_in_sigsys || g_tls_in_hook, 0)) {
        uc->uc_mcontext.regs[0] = static_cast<uint64_t>(
            mirage_seccomp_bypass_syscall(nr, a0, a1, a2, a3, 0, 0));
        return;
    }

    int saved_errno = errno;
    g_tls_in_sigsys = true;

    if (nr == __NR_openat) {
        char safe_path[256];
        int64_t raw_ret = -1;
        if (safe_copy_user_proc_or_sys_path(reinterpret_cast<const char *>(a1), safe_path, sizeof(safe_path))) {
            int fake_fd = open_cloaked_vfs_fd(safe_path);
            raw_ret = (fake_fd >= 0) ? fake_fd : mirage_seccomp_bypass_syscall(__NR_openat, a0, a1, a2, a3, 0, 0);
        } else {
            raw_ret = mirage_seccomp_bypass_syscall(__NR_openat, a0, a1, a2, a3, 0, 0);
        }
        uc->uc_mcontext.regs[0] = static_cast<uint64_t>(raw_ret);
    } else if (nr == __NR_readlinkat) {
        char safe_path[256];
        char *buf = reinterpret_cast<char *>(a2);
        size_t bufsiz = static_cast<size_t>(a3);
        int64_t raw_ret = -1;
        if (buf && bufsiz > 0 &&
            safe_copy_user_proc_or_sys_path(reinterpret_cast<const char *>(a1), safe_path, sizeof(safe_path)) &&
            std::strncmp(safe_path, "/proc/", 6) == 0) {
            std::string cloaked_path;
            if (g_fd_cloak.resolve_proc_fd_link(safe_path, &cloaked_path)) {
                size_t copy_len = std::min(bufsiz, cloaked_path.size());
                std::memcpy(buf, cloaked_path.data(), copy_len);
                raw_ret = static_cast<int64_t>(copy_len);
            } else {
                raw_ret = mirage_seccomp_bypass_syscall(__NR_readlinkat, a0, a1, a2, a3, 0, 0);
            }
        } else {
            raw_ret = mirage_seccomp_bypass_syscall(__NR_readlinkat, a0, a1, a2, a3, 0, 0);
        }
        uc->uc_mcontext.regs[0] = static_cast<uint64_t>(raw_ret);
    } else if (nr == __NR_uname) {
        int64_t raw_ret = mirage_seccomp_bypass_syscall(__NR_uname, a0, 0, 0, 0, 0, 0);
        auto *buf = reinterpret_cast<struct utsname *>(a0);
        if (raw_ret == 0 && buf && g_cfg.vfs.enabled) {
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
        }
        uc->uc_mcontext.regs[0] = static_cast<uint64_t>(raw_ret);
    } else if (nr == __NR_sysinfo) {
        int64_t raw_ret = mirage_seccomp_bypass_syscall(__NR_sysinfo, a0, 0, 0, 0, 0, 0);
        auto *s_info = reinterpret_cast<struct sysinfo *>(a0);
        if (raw_ret == 0 && s_info && g_cfg.vfs.enabled && g_cfg.vfs.ram_gb > 0) {
            uint64_t total_bytes = calc_realistic_memtotal_kb(g_cfg.vfs.ram_gb) * 1024ULL;
            uint32_t unit = s_info->mem_unit > 0 ? s_info->mem_unit : 1;
            s_info->totalram = static_cast<unsigned long>(total_bytes / unit);
            s_info->freeram  = (s_info->totalram * 58UL) / 100UL;
            s_info->bufferram = (s_info->totalram * 4UL) / 100UL;
        }
        uc->uc_mcontext.regs[0] = static_cast<uint64_t>(raw_ret);
    }

    g_tls_in_sigsys = false;
    errno = saved_errno;
}
#endif

inline void install_on_demand_seccomp_bpf() {
    if (!g_cfg.vfs.enabled || !g_cfg.vfs.enable_seccomp || g_seccomp_installed) return;
    g_seccomp_installed = true;

#if defined(__aarch64__)
    ErrnoRestorer errno_guard;
    if (pipe2(g_seccomp_probe_pipe, O_NONBLOCK | O_CLOEXEC) != 0) {
        g_seccomp_probe_pipe[0] = -1;
        g_seccomp_probe_pipe[1] = -1;
    }

    struct ExecRange {
        uint32_t hi;
        uint32_t lo_start;
        uint32_t lo_end;
    };
    std::vector<ExecRange> allow_ranges;
    uintptr_t self_pc = reinterpret_cast<uintptr_t>(&mirage_seccomp_bypass_syscall);
    bool self_vma_found = false;
    bool libc_found = false;

    FILE *fp = orig_fopen ? orig_fopen("/proc/self/maps", "r") : std::fopen("/proc/self/maps", "r");
    if (fp) {
        char line[1024];
        while (std::fgets(line, sizeof(line), fp)) {
            uintptr_t s = 0, e = 0;
            char perms[8] = {0};
            int path_pos = 0;
            if (std::sscanf(line, "%" SCNxPTR "-%" SCNxPTR " %7s %*s %*s %*s %n", &s, &e, perms, &path_pos) < 3) {
                continue;
            }
            if (perms[2] != 'x' || e <= s || (s >> 32) != (e >> 32)) continue;

            const char *path = line + path_pos;
            while (*path == ' ' || *path == '\t') ++path;

            bool is_self   = (self_pc >= s && self_pc < e);
            bool is_libc   = (std::strstr(path, "/libc.so") != nullptr);
            bool is_linker = (std::strstr(path, "/linker64") != nullptr);
            bool is_art    = (std::strstr(path, "/libart.so") != nullptr ||
                              std::strstr(path, "/libartbase.so") != nullptr);
            bool is_vdso   = (std::strncmp(path, "[vdso]", 6) == 0);

            // 只放行真正会执行合法系统调用的 5 类核心运行时代码段（通常仅 6~10 段，永不被截断！）
            if (!is_self && !is_libc && !is_linker && !is_art && !is_vdso) continue;

            if (is_self) self_vma_found = true;
            if (is_libc) libc_found = true;

            allow_ranges.push_back({
                static_cast<uint32_t>(s >> 32),
                static_cast<uint32_t>(s & 0xFFFFFFFFULL),
                static_cast<uint32_t>(e & 0xFFFFFFFFULL),
            });
        }
        if (orig_fclose) orig_fclose(fp); else std::fclose(fp);
    }

    // 安全守卫：若未能定位到 libc.so 或本模块自身代码段，绝不安装 BPF，防止误杀！
    if (!libc_found) return;
    if (!self_vma_found) {
        if ((self_pc >> 32) != ((self_pc + 256) >> 32)) return;
        allow_ranges.insert(allow_ranges.begin(), {
            static_cast<uint32_t>(self_pc >> 32),
            static_cast<uint32_t>(self_pc & 0xFFFFFFFFULL),
            static_cast<uint32_t>((self_pc + 256) & 0xFFFFFFFFULL),
        });
    }

    sigset_t unblock_set;
    sigemptyset(&unblock_set);
    sigaddset(&unblock_set, SIGSYS);
    sigprocmask(SIG_UNBLOCK, &unblock_set, nullptr);

    struct sigaction sa{};
    sa.sa_sigaction = sigsys_seccomp_handler;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = SA_SIGINFO | SA_NODEFER;
    if (sigaction(SIGSYS, &sa, &g_old_sigsys) != 0) return;

    std::vector<struct sock_filter> filter;
    filter.reserve(10 + allow_ranges.size() * 6);

    // [0..1] 严格校验架构为 AUDIT_ARCH_AARCH64，非 AArch64 直接放行
    filter.push_back(BPF_STMT(BPF_LD | BPF_W | BPF_ABS, offsetof(struct seccomp_data, arch)));
    filter.push_back(BPF_JUMP(BPF_JMP | BPF_JEQ | BPF_K, AUDIT_ARCH_AARCH64, 0, 5));

    // [2..7] 仅检查 4 个目标系统调用，其余系统调用立即 ALLOW
    filter.push_back(BPF_STMT(BPF_LD | BPF_W | BPF_ABS, offsetof(struct seccomp_data, nr)));
    filter.push_back(BPF_JUMP(BPF_JMP | BPF_JEQ | BPF_K, __NR_openat,     4, 0));
    filter.push_back(BPF_JUMP(BPF_JMP | BPF_JEQ | BPF_K, __NR_readlinkat, 3, 0));
    filter.push_back(BPF_JUMP(BPF_JMP | BPF_JEQ | BPF_K, __NR_uname,      2, 0));
    filter.push_back(BPF_JUMP(BPF_JMP | BPF_JEQ | BPF_K, __NR_sysinfo,    1, 0));
    filter.push_back(BPF_STMT(BPF_RET | BPF_K, SECCOMP_RET_ALLOW));

    for (const auto &r : allow_ranges) {
        filter.push_back(BPF_STMT(BPF_LD | BPF_W | BPF_ABS, offsetof(struct seccomp_data, instruction_pointer) + 4));
        filter.push_back(BPF_JUMP(BPF_JMP | BPF_JEQ | BPF_K, r.hi, 0, 4));
        filter.push_back(BPF_STMT(BPF_LD | BPF_W | BPF_ABS, offsetof(struct seccomp_data, instruction_pointer)));
        filter.push_back(BPF_JUMP(BPF_JMP | BPF_JGE | BPF_K, r.lo_start, 0, 2));
        filter.push_back(BPF_JUMP(BPF_JMP | BPF_JGE | BPF_K, r.lo_end, 1, 0));
        filter.push_back(BPF_STMT(BPF_RET | BPF_K, SECCOMP_RET_ALLOW));
    }

    filter.push_back(BPF_STMT(BPF_RET | BPF_K, SECCOMP_RET_TRAP));

    struct sock_fprog prog = {
        .len = static_cast<unsigned short>(filter.size()),
        .filter = filter.data(),
    };

    prctl(PR_SET_NO_NEW_PRIVS, 1, 0, 0, 0);
    if (prctl(PR_SET_SECCOMP, SECCOMP_MODE_FILTER, &prog, 0, 0) == 0) {
        emit_probe("SECCOMP_SVC", "BPF_Filter_Active", "CoreRanges=" + std::to_string(allow_ranges.size()));
    }
#endif
}

} // namespace mirage
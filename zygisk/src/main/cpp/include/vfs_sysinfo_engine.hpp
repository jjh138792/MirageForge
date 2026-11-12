#pragma once

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cstdarg>
#include <ctime>
#include <string>
#include <vector>
#include <sstream>
#include <fstream>
#include <mutex>
#include <algorithm>
#include <fcntl.h>
#include <unistd.h>
#include <dlfcn.h>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <sys/sysinfo.h>
#include <sys/utsname.h>
#include "mirage_config.hpp"
#include "mirage_logger.hpp"
#include "fd_cloak_tracker.hpp"

#ifndef MFD_CLOEXEC
#define MFD_CLOEXEC 0x0001U
#endif

namespace mirage {

// 原始函数指针前置声明
inline int (*orig_open)(const char *pathname, int flags, ...) = nullptr;
inline int (*orig_open64)(const char *pathname, int flags, ...) = nullptr;
inline int (*orig_open_2)(const char *pathname, int flags) = nullptr;
inline int (*orig_openat)(int dirfd, const char *pathname, int flags, ...) = nullptr;
inline int (*orig_openat64)(int dirfd, const char *pathname, int flags, ...) = nullptr;
inline int (*orig_openat_2)(int dirfd, const char *pathname, int flags) = nullptr;
inline FILE *(*orig_fopen)(const char *pathname, const char *mode) = nullptr;
inline FILE *(*orig_fopen64)(const char *pathname, const char *mode) = nullptr;
inline int (*orig_fclose)(FILE *fp) = nullptr;
inline int (*orig_close)(int fd) = nullptr;
inline int (*orig_android_fdsan_close_with_tag)(int fd, uint64_t tag) = nullptr;

inline ssize_t (*orig_read)(int fd, void *buf, size_t count) = nullptr;
inline ssize_t (*orig_pread64)(int fd, void *buf, size_t count, off64_t offset) = nullptr;
inline off_t (*orig_lseek)(int fd, off_t offset, int whence) = nullptr;
inline off64_t (*orig_lseek64)(int fd, off64_t offset, int whence) = nullptr;
inline ssize_t (*orig_readlink)(const char *pathname, char *buf, size_t bufsiz) = nullptr;
inline ssize_t (*orig_readlinkat)(int dirfd, const char *pathname, char *buf, size_t bufsiz) = nullptr;
inline int (*orig_access)(const char *pathname, int mode) = nullptr;
inline int (*orig_faccessat)(int dirfd, const char *pathname, int mode, int flags) = nullptr;

inline int (*orig_stat)(const char *pathname, struct stat *statbuf) = nullptr;
inline int (*orig_stat64)(const char *pathname, struct stat64 *statbuf) = nullptr;
inline int (*orig_lstat)(const char *pathname, struct stat *statbuf) = nullptr;
inline int (*orig_lstat64)(const char *pathname, struct stat64 *statbuf) = nullptr;
inline int (*orig_fstat)(int fd, struct stat *statbuf) = nullptr;
inline int (*orig_fstat64)(int fd, struct stat64 *statbuf) = nullptr;
inline int (*orig_fstatat)(int dirfd, const char *pathname, struct stat *statbuf, int flags) = nullptr;
inline int (*orig_fstatat64)(int dirfd, const char *pathname, struct stat64 *statbuf, int flags) = nullptr;

inline int (*orig_uname)(struct utsname *buf) = nullptr;
inline int (*orig_sysinfo)(struct sysinfo *info) = nullptr;
inline long (*orig_sysconf)(int name) = nullptr;
inline FILE *(*orig_popen)(const char *command, const char *type) = nullptr;

inline std::mutex g_vfs_cache_mtx;
inline std::unordered_map<std::string, std::string> g_vfs_static_cache;

// 创建匿名内存文件并绑定真实内核 Inode，防止 FD 关闭复用后污染系统字体/资源文件
inline int create_memfd_buffer(const std::string &fake_path, const std::string &content, int dyn_core_id = -1) {
    int fd = static_cast<int>(syscall(__NR_memfd_create, "sysfs_node", MFD_CLOEXEC));
    if (fd < 0) return -1;
    if (!content.empty()) {
        write_full(fd, content.data(), content.size());
    }
    lseek(fd, 0, SEEK_SET);

    uint64_t real_ino = 0;
    struct stat st{};
    if (orig_fstat && orig_fstat(fd, &st) == 0) {
        real_ino = static_cast<uint64_t>(st.st_ino);
    }
    g_fd_cloak.register_fd(fd, fake_path, content.size(), dyn_core_id, real_ino);
    return fd;
}

// 异构 CPU 分簇解析器 (僅初始化轻量级结构体，不提前生成文本)
inline void init_vfs_clusters_lightweight() {
    if (!g_cfg.vfs.enabled) return;
    g_cfg.vfs.clusters.clear();

    int total_cores = g_cfg.vfs.cpu_cores > 0 ? g_cfg.vfs.cpu_cores : 8;
    int global_max = g_cfg.vfs.cpu_max_freq_khz > 0 ? g_cfg.vfs.cpu_max_freq_khz : 4320000;
    bool is_mtk = (g_cfg.vfs.cpu_hardware.find("MT") != std::string::npos ||
                   g_cfg.vfs.soc_machine.find("MT") != std::string::npos);

    std::string spec = g_cfg.vfs.cpu_clusters_spec;
    if (!spec.empty() && spec.find(':') != std::string::npos) {
        for (const auto &seg : split_str(spec, ';')) {
            auto parts = split_str(seg, ':');
            if (parts.size() >= 3) {
                auto range = split_str(parts[0], '-');
                int first = std::atoi(range[0].c_str());
                int last = range.size() > 1 ? std::atoi(range[1].c_str()) : first;
                int min_f = std::atoi(parts[1].c_str());
                int max_f = std::atoi(parts[2].c_str());
                if (first >= 0 && last >= first && max_f > min_f) {
                    CpuClusterSpec c{};
                    c.first_core = first;
                    c.last_core = last;
                    c.min_freq_khz = min_f;
                    c.max_freq_khz = max_f;
                    g_cfg.vfs.clusters.push_back(c);
                }
            }
        }
    }

    if (g_cfg.vfs.clusters.empty()) {
        if (total_cores >= 8) {
            g_cfg.vfs.clusters.push_back({0, 5, 614400, static_cast<int>(global_max * 0.80), 0xd80, 768, {}});
            g_cfg.vfs.clusters.push_back({6, total_cores - 1, 806400, global_max, 0xd85, 1024, {}});
        } else {
            g_cfg.vfs.clusters.push_back({0, total_cores - 1, 614400, global_max, 0xd85, 1024, {}});
        }
    }

    int highest_max = 0;
    for (const auto &c : g_cfg.vfs.clusters) {
        if (c.max_freq_khz > highest_max) highest_max = c.max_freq_khz;
    }
    if (highest_max > 0) g_cfg.vfs.cpu_max_freq_khz = highest_max;

    for (size_t idx = 0; idx < g_cfg.vfs.clusters.size(); ++idx) {
        auto &c = g_cfg.vfs.clusters[idx];
        bool is_prime = (idx + 1 == g_cfg.vfs.clusters.size());
        bool is_little = (idx == 0 && g_cfg.vfs.clusters.size() >= 2);
        c.cpu_part = is_mtk ? (is_prime ? 0xd85 : (is_little ? 0xd80 : 0xd81))
                            : (is_prime ? 0xd85 : (is_little ? 0xd80 : 0xd83));
        c.capacity = is_prime ? 1024 : (is_little ? 512 : 820);

        constexpr int kStepKhz = 19200;
        int span = c.max_freq_khz - c.min_freq_khz;
        int steps = 14;
        c.opp_freqs_khz.push_back(c.min_freq_khz);
        for (int s = 1; s < steps; ++s) {
            int raw = c.min_freq_khz + (span * s) / steps;
            int aligned = (raw / kStepKhz) * kStepKhz;
            if (aligned > c.opp_freqs_khz.back() && aligned < c.max_freq_khz) {
                c.opp_freqs_khz.push_back(aligned);
            }
        }
        if (c.opp_freqs_khz.back() != c.max_freq_khz) {
            c.opp_freqs_khz.push_back(c.max_freq_khz);
        }
    }
}

inline const CpuClusterSpec *find_cluster_for_core(int core_id) noexcept {
    for (const auto &c : g_cfg.vfs.clusters) {
        if (core_id >= c.first_core && core_id <= c.last_core) return &c;
    }
    return g_cfg.vfs.clusters.empty() ? nullptr : &g_cfg.vfs.clusters.front();
}

inline int compute_live_core_freq_khz(int core_id) noexcept {
    const CpuClusterSpec *c = find_cluster_for_core(core_id);
    if (!c || c->opp_freqs_khz.empty()) return 1804800;

    struct timespec ts{};
    clock_gettime(CLOCK_MONOTONIC, &ts);
    uint64_t tick = static_cast<uint64_t>(ts.tv_sec) * 7ULL +
                    static_cast<uint64_t>(ts.tv_nsec / 180000000ULL) +
                    static_cast<uint64_t>(core_id * 5);

    size_t n = c->opp_freqs_khz.size();
    bool is_prime = (c == &g_cfg.vfs.clusters.back());
    size_t idx = is_prime
                     ? (n / 3 + (tick % std::max<size_t>(1, n - n / 3)))
                     : ((tick + static_cast<uint64_t>(core_id)) % std::max<size_t>(1, (n * 4) / 5));
    if (idx >= n) idx = n - 1;
    return c->opp_freqs_khz[idx];
}

// 计算与物理内存档位严格匹配的可用总内存 (字节)
// 预留约 4.5% 的硬件 Carveout，确保检测工具向上取整时 100% 识别为对应的物理内存档位 (如 16GB / 24GB)
inline uint64_t get_carveout_total_ram_bytes() noexcept {
    uint64_t phys_gb = static_cast<uint64_t>(g_cfg.vfs.ram_gb > 0 ? g_cfg.vfs.ram_gb : 16);
    uint64_t raw_bytes = phys_gb * 1024ULL * 1024ULL * 1024ULL;
    uint64_t usable_bytes = (raw_bytes * 955ULL) / 1000ULL;
    return (usable_bytes / 4096ULL) * 4096ULL;
}

inline std::string build_cpuinfo_text() {
    int cores = g_cfg.vfs.cpu_cores > 0 ? g_cfg.vfs.cpu_cores : 8;
    std::string hw = g_cfg.vfs.cpu_hardware.empty() ? "Qualcomm Technologies, Inc SM8750" : g_cfg.vfs.cpu_hardware;
    bool is_mtk = (hw.find("MT") != std::string::npos || g_cfg.vfs.soc_machine.find("MT") != std::string::npos);

    std::ostringstream ss;
    ss << "Processor\t: AArch64 Processor rev 1 (aarch64)\n";
    for (int i = 0; i < cores; ++i) {
        const CpuClusterSpec *c = find_cluster_for_core(i);
        uint32_t part = c ? c->cpu_part : 0xd85;
        char part_hex[16];
        std::snprintf(part_hex, sizeof(part_hex), "0x%03x", part);

        ss << "processor\t: " << i << "\n"
           << "BogoMIPS\t: 38.40\n"
           << "Features\t: fp asimd evtstrm aes pmull sha1 sha2 crc32 atomics fphp asimdhp cpuid asimdrdm jscvt fcma lrcpc dcpop sha3 sm3 sm4 asimddp sha512 sve asimdfhm dit uscat ilrcpc flagm ssbs sb pauth dcpodp flagm2 frint svei8mm svebf16 i8mm bf16 dgh bti\n"
           << "CPU implementer\t: " << (is_mtk ? "0x41" : "0x51") << "\n"
           << "CPU architecture: 8\n"
           << "CPU variant\t: 0x1\n"
           << "CPU part\t: " << part_hex << "\n"
           << "CPU revision\t: 1\n\n";
    }
    ss << "Hardware\t: " << hw << "\n";
    ss << "Revision\t: 0000\n";
    ss << "Serial\t\t: 0000000000000000\n";
    return ss.str();
}

inline std::string build_meminfo_text() {
    uint64_t total_kb = get_carveout_total_ram_bytes() / 1024ULL;
    uint64_t free_kb = (total_kb * 36ULL) / 100ULL;
    uint64_t avail_kb = (total_kb * 66ULL) / 100ULL;
    uint64_t cached_kb = (total_kb * 24ULL) / 100ULL;
    uint64_t buffers_kb = (total_kb * 3ULL) / 100ULL;
    uint64_t swap_total_kb = (total_kb / 2ULL / 4ULL) * 4ULL;

    std::ostringstream ss;
    ss << "MemTotal:       " << total_kb << " kB\n"
       << "MemFree:        " << free_kb << " kB\n"
       << "MemAvailable:   " << avail_kb << " kB\n"
       << "Buffers:        " << buffers_kb << " kB\n"
       << "Cached:         " << cached_kb << " kB\n"
       << "SwapCached:            0 kB\n"
       << "Active:         " << (total_kb * 25ULL) / 100ULL << " kB\n"
       << "Inactive:       " << (total_kb * 18ULL) / 100ULL << " kB\n"
       << "SwapTotal:      " << swap_total_kb << " kB\n"
       << "SwapFree:       " << swap_total_kb << " kB\n"
       << "Dirty:                64 kB\n"
       << "Writeback:             0 kB\n"
       << "AnonPages:      " << (total_kb * 19ULL) / 100ULL << " kB\n"
       << "Mapped:         " << (total_kb * 8ULL) / 100ULL << " kB\n"
       << "Shmem:             45280 kB\n"
       << "KReclaimable:     368640 kB\n"
       << "SReclaimable:     245760 kB\n"
       << "SUnreclaim:       212992 kB\n";
    return ss.str();
}

// 按需生成虚拟硬件节点内容
inline bool generate_vfs_node_on_demand(const std::string &path, std::string *out_content, int *out_dyn_core) {
    if (out_dyn_core) *out_dyn_core = -1;

    // 1. 单核实时动态频率节点
    if (path.rfind("/sys/devices/system/cpu/cpu", 0) == 0 &&
        (path.find("/cpufreq/scaling_cur_freq") != std::string::npos ||
         path.find("/cpufreq/cpuinfo_cur_freq") != std::string::npos)) {
        int core_id = std::atoi(path.c_str() + 27);
        int cores = g_cfg.vfs.cpu_cores > 0 ? g_cfg.vfs.cpu_cores : 8;
        if (core_id >= 0 && core_id < cores) {
            if (out_dyn_core) *out_dyn_core = core_id;
            if (out_content) *out_content = std::to_string(compute_live_core_freq_khz(core_id)) + "\n";
            return true;
        }
    }
    if (path.rfind("/sys/devices/system/cpu/cpufreq/policy", 0) == 0 &&
        (path.find("/scaling_cur_freq") != std::string::npos ||
         path.find("/cpuinfo_cur_freq") != std::string::npos)) {
        int core_id = std::atoi(path.c_str() + 38);
        int cores = g_cfg.vfs.cpu_cores > 0 ? g_cfg.vfs.cpu_cores : 8;
        if (core_id >= 0 && core_id < cores) {
            if (out_dyn_core) *out_dyn_core = core_id;
            if (out_content) *out_content = std::to_string(compute_live_core_freq_khz(core_id)) + "\n";
            return true;
        }
    }

    // 2. 命中静态缓存直接返回
    {
        std::lock_guard<std::mutex> lock(g_vfs_cache_mtx);
        auto it = g_vfs_static_cache.find(path);
        if (it != g_vfs_static_cache.end()) {
            if (out_content) *out_content = it->second;
            return true;
        }
    }

    // 3. 首次访问时动态合成
    std::string generated;
    int cores = g_cfg.vfs.cpu_cores > 0 ? g_cfg.vfs.cpu_cores : 8;

    if (path == "/proc/cpuinfo") {
        generated = build_cpuinfo_text();
    } else if (path == "/proc/meminfo") {
        generated = build_meminfo_text();
    } else if (path == "/proc/version") {
        std::string krel = g_cfg.vfs.kernel_release.empty() ? "6.6.30-android15-8-g94b8f12ab4c1" : g_cfg.vfs.kernel_release;
        std::string kver = g_cfg.vfs.kernel_version.empty() ? "#1 SMP PREEMPT Fri Nov 15 12:00:00 UTC 2025" : g_cfg.vfs.kernel_version;
        generated = "Linux version " + krel + " (build-user@android-build) (Android clang version 18.0.1) " + kver + "\n";
    } else if (path == "/proc/device-tree/model" || path == "/sys/firmware/devicetree/base/model") {
        // 拦截 DevCheck 读取设备树真实 SoC 型号
        std::string soc = g_cfg.vfs.soc_machine.empty() ? "SM8750" : g_cfg.vfs.soc_machine;
        bool is_mtk = (soc.find("MT") != std::string::npos);
        generated = (is_mtk ? ("MediaTek " + soc + " SoC") : ("Qualcomm Technologies, Inc. " + soc + " SoC"));
        generated.push_back('\0');
    } else if (path == "/sys/devices/system/cpu/present" ||
               path == "/sys/devices/system/cpu/possible" ||
               path == "/sys/devices/system/cpu/online") {
        generated = "0-" + std::to_string(cores - 1) + "\n";
    } else if (path == "/sys/devices/system/cpu/kernel_max") {
        generated = std::to_string(cores - 1) + "\n";
    } else if (path == "/sys/devices/soc0/soc_id" || path == "/sys/devices/system/soc/soc0/id") {
        generated = (g_cfg.vfs.soc_id.empty() ? "618" : g_cfg.vfs.soc_id) + "\n";
    } else if (path == "/sys/devices/soc0/family") {
        bool is_mtk = (g_cfg.vfs.soc_machine.find("MT") != std::string::npos);
        generated = std::string(is_mtk ? "MediaTek" : "Snapdragon") + "\n";
    } else if (path == "/sys/devices/soc0/machine") {
        generated = (g_cfg.vfs.soc_machine.empty() ? "SM8750" : g_cfg.vfs.soc_machine) + "\n";
    } else if (path == "/sys/devices/soc0/vendor") {
        bool is_mtk = (g_cfg.vfs.soc_machine.find("MT") != std::string::npos);
        generated = std::string(is_mtk ? "MediaTek" : "Qualcomm") + "\n";
    } else if (path == "/proc/ddr_info" ||
               path == "/proc/driver/ddr_info" ||
               path == "/sys/devices/system/chip-info/ddr_info" ||
               path == "/sys/bus/platform/drivers/ddr_type/ddr_type") {
        int ram_gb = g_cfg.vfs.ram_gb > 0 ? g_cfg.vfs.ram_gb : 16;
        std::string dtype = g_cfg.vfs.ddr_type.empty() ? "LPDDR5X" : g_cfg.vfs.ddr_type;
        std::string dvend = g_cfg.vfs.ddr_vendor.empty() ? "SKhynix" : g_cfg.vfs.ddr_vendor;
        generated = "ddr_type: " + dtype + "\nddr_size: " + std::to_string(ram_gb) + "GB\nddr_vendor: " + dvend + "\n";
    } else if (path == "/sys/class/kgsl/kgsl-3d0/gpu_model" ||
               path == "/sys/devices/platform/kgsl-3d0.0/kgsl/kgsl-3d0/gpu_model") {
        if (g_cfg.gl.enabled && !g_cfg.gl.gl_renderer.empty()) {
            generated = g_cfg.gl.gl_renderer + "\n";
        }
    } else if (path.rfind("/sys/devices/system/cpu/", 0) == 0) {
        for (int i = 0; i < cores; ++i) {
            const CpuClusterSpec *c = find_cluster_for_core(i);
            if (!c) continue;
            std::string prefix = "/sys/devices/system/cpu/cpu" + std::to_string(i) + "/";
            std::string pol_prefix = "/sys/devices/system/cpu/cpufreq/policy" + std::to_string(c->first_core) + "/";

            if (path == prefix + "cpufreq/cpuinfo_max_freq" || path == prefix + "cpufreq/scaling_max_freq" ||
                path == pol_prefix + "cpuinfo_max_freq" || path == pol_prefix + "scaling_max_freq") {
                generated = std::to_string(c->max_freq_khz) + "\n";
                break;
            }
            if (path == prefix + "cpufreq/cpuinfo_min_freq" || path == prefix + "cpufreq/scaling_min_freq" ||
                path == pol_prefix + "cpuinfo_min_freq" || path == pol_prefix + "scaling_min_freq") {
                generated = std::to_string(c->min_freq_khz) + "\n";
                break;
            }
            if (path == prefix + "cpufreq/scaling_available_frequencies" ||
                path == pol_prefix + "scaling_available_frequencies") {
                std::ostringstream oss;
                for (size_t k = 0; k < c->opp_freqs_khz.size(); ++k) {
                    if (k > 0) oss << " ";
                    oss << c->opp_freqs_khz[k];
                }
                oss << "\n";
                generated = oss.str();
                break;
            }
            if (path == prefix + "cpufreq/scaling_governor" || path == pol_prefix + "scaling_governor") {
                generated = "walt\n";
                break;
            }
            if (path == prefix + "cpufreq/affected_cpus" || path == prefix + "cpufreq/related_cpus" ||
                path == pol_prefix + "affected_cpus" || path == pol_prefix + "related_cpus") {
                std::ostringstream cpus_ss;
                for (int m = c->first_core; m <= c->last_core; ++m) {
                    if (m > c->first_core) cpus_ss << " ";
                    cpus_ss << m;
                }
                cpus_ss << "\n";
                generated = cpus_ss.str();
                break;
            }
            if (path == prefix + "topology/core_cpus_list" || path == prefix + "topology/package_cpus_list") {
                generated = std::to_string(c->first_core) + "-" + std::to_string(c->last_core) + "\n";
                break;
            }
            if (path == prefix + "topology/physical_package_id") {
                generated = "0\n";
                break;
            }
            if (path == prefix + "topology/cluster_id") {
                generated = std::to_string(c->first_core) + "\n";
                break;
            }
            if (path == prefix + "cpu_capacity") {
                generated = std::to_string(c->capacity) + "\n";
                break;
            }
        }
    }

    if (generated.empty()) return false;

    {
        std::lock_guard<std::mutex> lock(g_vfs_cache_mtx);
        g_vfs_static_cache[path] = generated;
    }
    if (out_content) *out_content = std::move(generated);
    return true;
}

inline void init_vfs_symbols() {
    if (orig_openat) return;
    orig_open = reinterpret_cast<decltype(orig_open)>(dlsym(RTLD_DEFAULT, "open"));
    orig_open64 = reinterpret_cast<decltype(orig_open64)>(dlsym(RTLD_DEFAULT, "open64"));
    orig_open_2 = reinterpret_cast<decltype(orig_open_2)>(dlsym(RTLD_DEFAULT, "__open_2"));
    orig_openat = reinterpret_cast<decltype(orig_openat)>(dlsym(RTLD_DEFAULT, "openat"));
    orig_openat64 = reinterpret_cast<decltype(orig_openat64)>(dlsym(RTLD_DEFAULT, "openat64"));
    orig_openat_2 = reinterpret_cast<decltype(orig_openat_2)>(dlsym(RTLD_DEFAULT, "__openat_2"));
    orig_fopen = reinterpret_cast<decltype(orig_fopen)>(dlsym(RTLD_DEFAULT, "fopen"));
    orig_fopen64 = reinterpret_cast<decltype(orig_fopen64)>(dlsym(RTLD_DEFAULT, "fopen64"));
    orig_fclose = reinterpret_cast<decltype(orig_fclose)>(dlsym(RTLD_DEFAULT, "fclose"));
    orig_close = reinterpret_cast<decltype(orig_close)>(dlsym(RTLD_DEFAULT, "close"));
    orig_android_fdsan_close_with_tag = reinterpret_cast<decltype(orig_android_fdsan_close_with_tag)>(
        dlsym(RTLD_DEFAULT, "android_fdsan_close_with_tag"));

    orig_read = reinterpret_cast<decltype(orig_read)>(dlsym(RTLD_DEFAULT, "read"));
    orig_pread64 = reinterpret_cast<decltype(orig_pread64)>(dlsym(RTLD_DEFAULT, "pread64"));
    orig_lseek = reinterpret_cast<decltype(orig_lseek)>(dlsym(RTLD_DEFAULT, "lseek"));
    orig_lseek64 = reinterpret_cast<decltype(orig_lseek64)>(dlsym(RTLD_DEFAULT, "lseek64"));
    orig_readlink = reinterpret_cast<decltype(orig_readlink)>(dlsym(RTLD_DEFAULT, "readlink"));
    orig_readlinkat = reinterpret_cast<decltype(orig_readlinkat)>(dlsym(RTLD_DEFAULT, "readlinkat"));
    orig_access = reinterpret_cast<decltype(orig_access)>(dlsym(RTLD_DEFAULT, "access"));
    orig_faccessat = reinterpret_cast<decltype(orig_faccessat)>(dlsym(RTLD_DEFAULT, "faccessat"));

    orig_stat = reinterpret_cast<decltype(orig_stat)>(dlsym(RTLD_DEFAULT, "stat"));
    orig_stat64 = reinterpret_cast<decltype(orig_stat64)>(dlsym(RTLD_DEFAULT, "stat64"));
    orig_lstat = reinterpret_cast<decltype(orig_lstat)>(dlsym(RTLD_DEFAULT, "lstat"));
    orig_lstat64 = reinterpret_cast<decltype(orig_lstat64)>(dlsym(RTLD_DEFAULT, "lstat64"));
    orig_fstat = reinterpret_cast<decltype(orig_fstat)>(dlsym(RTLD_DEFAULT, "fstat"));
    orig_fstat64 = reinterpret_cast<decltype(orig_fstat64)>(dlsym(RTLD_DEFAULT, "fstat64"));
    orig_fstatat = reinterpret_cast<decltype(orig_fstatat)>(dlsym(RTLD_DEFAULT, "fstatat"));
    orig_fstatat64 = reinterpret_cast<decltype(orig_fstatat64)>(dlsym(RTLD_DEFAULT, "fstatat64"));

    orig_uname = reinterpret_cast<decltype(orig_uname)>(dlsym(RTLD_DEFAULT, "uname"));
    orig_sysinfo = reinterpret_cast<decltype(orig_sysinfo)>(dlsym(RTLD_DEFAULT, "sysinfo"));
    orig_sysconf = reinterpret_cast<decltype(orig_sysconf)>(dlsym(RTLD_DEFAULT, "sysconf"));
    orig_popen = reinterpret_cast<decltype(orig_popen)>(dlsym(RTLD_DEFAULT, "popen"));
}

inline std::string normalize_vfs_path(const char *pathname) {
    if (!pathname) return "";
    std::string p(pathname);
    while (p.size() > 1 && p.back() == '/') p.pop_back();
    if (p.rfind("/proc/self/", 0) == 0) return p;
    std::string pid_prefix = "/proc/" + std::to_string(getpid()) + "/";
    if (p.rfind(pid_prefix, 0) == 0) {
        return "/proc/self/" + p.substr(pid_prefix.size());
    }
    return p;
}

inline int filter_proc_maps_stream(const char *real_path) {
    if (!orig_openat) return -1;
    int real_fd = orig_openat(AT_FDCWD, real_path, O_RDONLY, 0);
    if (real_fd < 0) return -1;

    std::string raw;
    char buf[4096];
    ssize_t n;
    while ((n = orig_read ? orig_read(real_fd, buf, sizeof(buf)) : read(real_fd, buf, sizeof(buf))) > 0) {
        raw.append(buf, static_cast<size_t>(n));
    }
    if (orig_close) orig_close(real_fd); else close(real_fd);

    std::istringstream iss(raw);
    std::ostringstream oss;
    std::string line;
    while (std::getline(iss, line)) {
        if (line.find("mirage_forge") != std::string::npos ||
            line.find("zygisk") != std::string::npos ||
            line.find("magisk") != std::string::npos ||
            line.find("kernelsu") != std::string::npos ||
            line.find("/data/adb") != std::string::npos ||
            line.find("memfd:sysfs_node") != std::string::npos) {
            continue;
        }
        oss << line << "\n";
    }
    return create_memfd_buffer(real_path, oss.str());
}

inline int try_intercept_vfs_open(const char *pathname) {
    if (!g_cfg.vfs.enabled || !pathname) return -1;
    ScopedHookGuard guard;
    if (!guard) return -1;

    std::string norm = normalize_vfs_path(pathname);
    if (norm == "/proc/self/maps" || norm == "/proc/self/smaps" || norm == "/proc/self/mountinfo") {
        int mfd = filter_proc_maps_stream(pathname);
        if (mfd >= 0) {
            emit_probe("VFS_MAPS", norm, "Filtered Stealth Stream");
            return mfd;
        }
    }

    std::string content;
    int dyn_core = -1;
    if (generate_vfs_node_on_demand(norm, &content, &dyn_core)) {
        int mfd = create_memfd_buffer(norm, content, dyn_core);
        if (mfd >= 0) {
            if (dyn_core >= 0) {
                emit_probe("VFS_OPEN", norm, "LiveFreq:" + content.substr(0, content.size() - 1));
            } else {
                emit_probe("VFS_OPEN", norm, "Spoofed (" + std::to_string(content.size()) + "B)");
            }
            return mfd;
        }
    }
    return -1;
}

inline int hook_openat(int dirfd, const char *pathname, int flags, ...) {
    mode_t mode = 0;
    if (flags & (O_CREAT | O_TMPFILE)) {
        va_list args;
        va_start(args, flags);
        mode = static_cast<mode_t>(va_arg(args, int));
        va_end(args);
    }
    int vfd = try_intercept_vfs_open(pathname);
    if (vfd >= 0) return vfd;
    return orig_openat ? orig_openat(dirfd, pathname, flags, mode) : -1;
}

inline int hook_openat64(int dirfd, const char *pathname, int flags, ...) {
    mode_t mode = 0;
    if (flags & (O_CREAT | O_TMPFILE)) {
        va_list args;
        va_start(args, flags);
        mode = static_cast<mode_t>(va_arg(args, int));
        va_end(args);
    }
    int vfd = try_intercept_vfs_open(pathname);
    if (vfd >= 0) return vfd;
    return orig_openat64 ? orig_openat64(dirfd, pathname, flags, mode) : -1;
}

inline int hook_openat_2(int dirfd, const char *pathname, int flags) {
    int vfd = try_intercept_vfs_open(pathname);
    if (vfd >= 0) return vfd;
    return orig_openat_2 ? orig_openat_2(dirfd, pathname, flags) : -1;
}

inline int hook_open(const char *pathname, int flags, ...) {
    mode_t mode = 0;
    if (flags & (O_CREAT | O_TMPFILE)) {
        va_list args;
        va_start(args, flags);
        mode = static_cast<mode_t>(va_arg(args, int));
        va_end(args);
    }
    int vfd = try_intercept_vfs_open(pathname);
    if (vfd >= 0) return vfd;
    return orig_open ? orig_open(pathname, flags, mode) : -1;
}

inline int hook_open64(const char *pathname, int flags, ...) {
    mode_t mode = 0;
    if (flags & (O_CREAT | O_TMPFILE)) {
        va_list args;
        va_start(args, flags);
        mode = static_cast<mode_t>(va_arg(args, int));
        va_end(args);
    }
    int vfd = try_intercept_vfs_open(pathname);
    if (vfd >= 0) return vfd;
    return orig_open64 ? orig_open64(pathname, flags, mode) : -1;
}

inline int hook_open_2(const char *pathname, int flags) {
    int vfd = try_intercept_vfs_open(pathname);
    if (vfd >= 0) return vfd;
    return orig_open_2 ? orig_open_2(pathname, flags) : -1;
}

// 接管 stdio fopen / fopen64：拦截不走外部 openat PLT 的标准 C 文件流读取 (如 AIDA64 读取 /proc/meminfo)
inline FILE *hook_fopen(const char *pathname, const char *mode) {
    if (pathname && mode && mode[0] == 'r') {
        int vfd = try_intercept_vfs_open(pathname);
        if (vfd >= 0) {
            FILE *fp = fdopen(vfd, mode);
            if (fp) return fp;
            g_fd_cloak.unregister_fd(vfd);
            if (orig_close) orig_close(vfd); else close(vfd);
        }
    }
    return orig_fopen ? orig_fopen(pathname, mode) : nullptr;
}

inline FILE *hook_fopen64(const char *pathname, const char *mode) {
    if (pathname && mode && mode[0] == 'r') {
        int vfd = try_intercept_vfs_open(pathname);
        if (vfd >= 0) {
            FILE *fp = fdopen(vfd, mode);
            if (fp) return fp;
            g_fd_cloak.unregister_fd(vfd);
            if (orig_close) orig_close(vfd); else close(vfd);
        }
    }
    return orig_fopen64 ? orig_fopen64(pathname, mode) : nullptr;
}

inline int hook_fclose(FILE *fp) {
    if (fp) {
        int fd = fileno(fp);
        if (fd >= 0) g_fd_cloak.unregister_fd(fd);
    }
    return orig_fclose ? orig_fclose(fp) : -1;
}

inline int hook_close(int fd) {
    g_fd_cloak.unregister_fd(fd);
    return orig_close ? orig_close(fd) : -1;
}

// 关键修复：接管 Android 10+ 的 android_fdsan_close_with_tag，防止 libjavacore 关闭虚拟 fd 后残留脏位图
inline int hook_android_fdsan_close_with_tag(int fd, uint64_t tag) {
    g_fd_cloak.unregister_fd(fd);
    return orig_android_fdsan_close_with_tag
               ? orig_android_fdsan_close_with_tag(fd, tag)
               : (orig_close ? orig_close(fd) : -1);
}

inline void refresh_dynamic_freq_fd_if_needed(int fd, off64_t offset) {
    if (offset != 0 || !g_fd_cloak.is_tracked_fast(fd)) return;
    int core_id = -1;
    if (g_fd_cloak.get_dynamic_core_id(fd, &core_id) && core_id >= 0) {
        std::string fresh = std::to_string(compute_live_core_freq_khz(core_id)) + "\n";
        (void)ftruncate(fd, 0);
        if (orig_lseek) orig_lseek(fd, 0, SEEK_SET);
        write_full(fd, fresh.data(), fresh.size());
        if (orig_lseek) orig_lseek(fd, 0, SEEK_SET);
    }
}

inline off_t hook_lseek(int fd, off_t offset, int whence) {
    if (whence == SEEK_SET && offset == 0) refresh_dynamic_freq_fd_if_needed(fd, 0);
    return orig_lseek ? orig_lseek(fd, offset, whence) : -1;
}

inline off64_t hook_lseek64(int fd, off64_t offset, int whence) {
    if (whence == SEEK_SET && offset == 0) refresh_dynamic_freq_fd_if_needed(fd, 0);
    return orig_lseek64 ? orig_lseek64(fd, offset, whence) : -1;
}

inline ssize_t hook_pread64(int fd, void *buf, size_t count, off64_t offset) {
    if (offset == 0) refresh_dynamic_freq_fd_if_needed(fd, 0);
    return orig_pread64 ? orig_pread64(fd, buf, count, offset) : -1;
}

inline ssize_t hook_readlink(const char *pathname, char *buf, size_t bufsiz) {
    if (g_cfg.vfs.enabled && pathname && buf && bufsiz > 0) {
        std::string target;
        if (g_fd_cloak.resolve_proc_fd_link(pathname, &target)) {
            size_t copy_len = std::min(bufsiz, target.size());
            std::memcpy(buf, target.data(), copy_len);
            return static_cast<ssize_t>(copy_len);
        }
    }
    return orig_readlink ? orig_readlink(pathname, buf, bufsiz) : -1;
}

inline ssize_t hook_readlinkat(int dirfd, const char *pathname, char *buf, size_t bufsiz) {
    if (g_cfg.vfs.enabled && pathname && buf && bufsiz > 0) {
        std::string target;
        if (g_fd_cloak.resolve_proc_fd_link(pathname, &target)) {
            size_t copy_len = std::min(bufsiz, target.size());
            std::memcpy(buf, target.data(), copy_len);
            return static_cast<ssize_t>(copy_len);
        }
    }
    return orig_readlinkat ? orig_readlinkat(dirfd, pathname, buf, bufsiz) : -1;
}

inline int hook_access(const char *pathname, int mode) {
    if (g_cfg.vfs.enabled && pathname && (mode == F_OK || mode == R_OK)) {
        std::string norm = normalize_vfs_path(pathname);
        if (generate_vfs_node_on_demand(norm, nullptr, nullptr)) return 0;
    }
    return orig_access ? orig_access(pathname, mode) : -1;
}

inline int hook_faccessat(int dirfd, const char *pathname, int mode, int flags) {
    if (g_cfg.vfs.enabled && pathname && (mode == F_OK || mode == R_OK)) {
        std::string norm = normalize_vfs_path(pathname);
        if (generate_vfs_node_on_demand(norm, nullptr, nullptr)) return 0;
    }
    return orig_faccessat ? orig_faccessat(dirfd, pathname, mode, flags) : -1;
}

// 关键自愈：先调用真实 fstat 获取当前内核 st_ino，若该 fd 已被复用给系统字体文件，自动剔除脏记录并放行真实 stat！
inline int hook_fstat(int fd, struct stat *statbuf) {
    int ret = orig_fstat ? orig_fstat(fd, statbuf) : -1;
    if (ret == 0 && g_cfg.vfs.enabled && statbuf && g_fd_cloak.is_tracked_fast(fd)) {
        uint64_t cur_ino = static_cast<uint64_t>(statbuf->st_ino);
        g_fd_cloak.get_cloaked_stat(fd, statbuf, cur_ino);
    }
    return ret;
}

inline int hook_fstat64(int fd, struct stat64 *statbuf) {
    int ret = orig_fstat64 ? orig_fstat64(fd, statbuf) : -1;
    if (ret == 0 && g_cfg.vfs.enabled && statbuf && g_fd_cloak.is_tracked_fast(fd)) {
        uint64_t cur_ino = static_cast<uint64_t>(statbuf->st_ino);
        g_fd_cloak.get_cloaked_stat(fd, statbuf, cur_ino);
    }
    return ret;
}

inline int hook_stat(const char *pathname, struct stat *statbuf) {
    if (g_cfg.vfs.enabled && pathname && statbuf) {
        std::string norm = normalize_vfs_path(pathname);
        std::string content;
        if (generate_vfs_node_on_demand(norm, &content, nullptr)) {
            populate_virtual_stat(statbuf, norm, content.size());
            return 0;
        }
    }
    return orig_stat ? orig_stat(pathname, statbuf) : -1;
}

inline int hook_stat64(const char *pathname, struct stat64 *statbuf) {
    if (g_cfg.vfs.enabled && pathname && statbuf) {
        std::string norm = normalize_vfs_path(pathname);
        std::string content;
        if (generate_vfs_node_on_demand(norm, &content, nullptr)) {
            populate_virtual_stat(statbuf, norm, content.size());
            return 0;
        }
    }
    return orig_stat64 ? orig_stat64(pathname, statbuf) : -1;
}

inline int hook_lstat(const char *pathname, struct stat *statbuf) {
    if (g_cfg.vfs.enabled && pathname && statbuf) {
        std::string norm = normalize_vfs_path(pathname);
        std::string content;
        if (generate_vfs_node_on_demand(norm, &content, nullptr)) {
            populate_virtual_stat(statbuf, norm, content.size());
            return 0;
        }
    }
    return orig_lstat ? orig_lstat(pathname, statbuf) : -1;
}

inline int hook_lstat64(const char *pathname, struct stat64 *statbuf) {
    if (g_cfg.vfs.enabled && pathname && statbuf) {
        std::string norm = normalize_vfs_path(pathname);
        std::string content;
        if (generate_vfs_node_on_demand(norm, &content, nullptr)) {
            populate_virtual_stat(statbuf, norm, content.size());
            return 0;
        }
    }
    return orig_lstat64 ? orig_lstat64(pathname, statbuf) : -1;
}

inline int hook_fstatat(int dirfd, const char *pathname, struct stat *statbuf, int flags) {
    if (g_cfg.vfs.enabled && pathname && statbuf) {
        std::string norm = normalize_vfs_path(pathname);
        std::string content;
        if (generate_vfs_node_on_demand(norm, &content, nullptr)) {
            populate_virtual_stat(statbuf, norm, content.size());
            return 0;
        }
    }
    return orig_fstatat ? orig_fstatat(dirfd, pathname, statbuf, flags) : -1;
}

inline int hook_fstatat64(int dirfd, const char *pathname, struct stat64 *statbuf, int flags) {
    if (g_cfg.vfs.enabled && pathname && statbuf) {
        std::string norm = normalize_vfs_path(pathname);
        std::string content;
        if (generate_vfs_node_on_demand(norm, &content, nullptr)) {
            populate_virtual_stat(statbuf, norm, content.size());
            return 0;
        }
    }
    return orig_fstatat64 ? orig_fstatat64(dirfd, pathname, statbuf, flags) : -1;
}

inline int hook_uname(struct utsname *buf) {
    int ret = orig_uname ? orig_uname(buf) : -1;
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
        if (guard) emit_probe("UNAME", "release", buf->release);
    }
    return ret;
}

inline int hook_sysinfo(struct sysinfo *info) {
    int ret = orig_sysinfo ? orig_sysinfo(info) : -1;
    if (ret == 0 && info && g_cfg.vfs.enabled && g_cfg.vfs.ram_gb > 0) {
        uint64_t unit = info->mem_unit > 0 ? info->mem_unit : 1;
        uint64_t total_bytes = get_carveout_total_ram_bytes();
        info->totalram = static_cast<unsigned long>(total_bytes / unit);
        info->freeram = static_cast<unsigned long>((total_bytes * 42ULL / 100ULL) / unit);
        info->bufferram = static_cast<unsigned long>((total_bytes * 6ULL / 100ULL) / unit);
        info->totalswap = static_cast<unsigned long>((total_bytes / 2ULL) / unit);
        info->freeswap = info->totalswap;
        ScopedHookGuard guard;
        if (guard) emit_probe("SYSINFO", "totalram_gb", std::to_string(g_cfg.vfs.ram_gb));
    }
    return ret;
}

inline long hook_sysconf(int name) {
    if (g_cfg.vfs.enabled) {
        if ((name == _SC_NPROCESSORS_CONF || name == _SC_NPROCESSORS_ONLN) && g_cfg.vfs.cpu_cores > 0) {
            return static_cast<long>(g_cfg.vfs.cpu_cores);
        }
        if (name == _SC_PHYS_PAGES && g_cfg.vfs.ram_gb > 0) {
            long page_sz = orig_sysconf ? orig_sysconf(_SC_PAGESIZE) : 4096;
            if (page_sz <= 0) page_sz = 4096;
            uint64_t total_bytes = get_carveout_total_ram_bytes();
            return static_cast<long>(total_bytes / static_cast<uint64_t>(page_sz));
        }
    }
    return orig_sysconf ? orig_sysconf(name) : -1;
}

inline FILE *hook_popen(const char *command, const char *type) {
    if (command && type && type[0] == 'r') {
        std::string cmd(command);
        if (g_cfg.build.enabled && cmd.find("getprop") != std::string::npos) {
            ScopedHookGuard guard;
            if (guard) emit_probe("SHELL_POPEN", "getprop", cmd);

            size_t pos = cmd.find("getprop");
            std::string arg = cmd.substr(pos + 7);
            size_t first = arg.find_first_not_of(" \t\r\n\"'");
            if (first != std::string::npos) {
                size_t last = arg.find_last_not_of(" \t\r\n\"'");
                std::string prop_key = arg.substr(first, last - first + 1);
                auto it = g_cfg.build.prop_map.find(prop_key);
                if (it != g_cfg.build.prop_map.end()) {
                    int mfd = create_memfd_buffer("/proc/self/environ", it->second + "\n");
                    if (mfd >= 0) return fdopen(mfd, "r");
                }
            } else {
                std::ostringstream oss;
                for (const auto &kv : g_cfg.build.prop_map) {
                    oss << "[" << kv.first << "]: [" << kv.second << "]\n";
                }
                int mfd = create_memfd_buffer("/proc/self/environ", oss.str());
                if (mfd >= 0) return fdopen(mfd, "r");
            }
        }
        if (g_cfg.vfs.enabled && cmd.find("/proc/cpuinfo") != std::string::npos) {
            int mfd = create_memfd_buffer("/proc/cpuinfo", build_cpuinfo_text());
            if (mfd >= 0) return fdopen(mfd, "r");
        }
        if (g_cfg.vfs.enabled && cmd.find("/proc/meminfo") != std::string::npos) {
            int mfd = create_memfd_buffer("/proc/meminfo", build_meminfo_text());
            if (mfd >= 0) return fdopen(mfd, "r");
        }
    }
    return orig_popen ? orig_popen(command, type) : nullptr;
}

} // namespace mirage

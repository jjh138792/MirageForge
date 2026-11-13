#pragma once

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <cstdarg>
#include <sstream>
#include <fstream>
#include <mutex>
#include <fcntl.h>
#include <unistd.h>
#include <dlfcn.h>
#include <sys/stat.h>
#include <sys/sysinfo.h>
#include <sys/utsname.h>
#include <sys/mman.h>
#include <linux/memfd.h>
#include <sys/syscall.h>
#include "mirage_config.hpp"
#include "mirage_logger.hpp"
#include "fd_cloak_tracker.hpp"
#include "jvm_art_engine.hpp"

namespace mirage {

inline int create_ cloaked_memfd(const char *tag, const std::string &content, const char *orig_path) {
    int fd = static_cast<int>(syscall(__NR_memfd_create, tag, MFD_CLOEXEC));
    if (fd < 0) return -1;
    if (!content.empty()) {
        write_full(fd, content.data(), content.size());
    }
    lseek(fd, 0, SEEK_SET);
    g_fd_cloak.track(fd, orig_path);
    return fd;
}

inline int parse_cpu_core_index(const char *path, std::string &out_leaf) {
    const char *p = nullptr;
    if (std::strncmp(path, "/sys/devices/system/cpu/cpu", 27) == 0) {
        p = path + 27;
    } else if (std::strncmp(path, "/sys/devices/system/cpu/cpufreq/policy", 38) == 0) {
        p = path + 38;
    } else if (std::strncmp(path, "/sys/bus/cpu/devices/cpu", 24) == 0) {
        p = path + 24;
    }
    if (!p || *p < '0' || *p > '9') return -1;

    int core_idx = 0;
    while (*p >= '0' && *p <= '9') {
        core_idx = core_idx * 10 + (*p - '0');
        ++p;
    }
    if (*p != '/') return -1;
    ++p;
    if (std::strncmp(p, "cpufreq/", 8) == 0) {
        p += 8;
    }
    out_leaf = p;
    return core_idx;
}

// 轻量级解析异构 CPU 分簇配置
inline void init_vfs_clusters_lightweight() {
    if (!g_cfg.vfs.enabled || !g_cfg.vfs.clusters.empty()) return;

    int total_cores = g_cfg.vfs.cpu_cores > 0 ? g_cfg.vfs.cpu_cores : 8;
    int global_max = g_cfg.vfs.cpu_max_freq_khz > 0 ? g_cfg.vfs.cpu_max_freq_khz : 4320000;
    bool is_mtk = (g_cfg.vfs.soc_machine.find("MT") == 0 || g_cfg.vfs.cpu_hardware.find("MT") != std::string::npos);

    std::string spec_str = g_cfg.vfs.cpu_clusters_spec;
    if (spec_str.empty()) {
        if (is_mtk) {
            spec_str = "0-3:800000:2400000;4-6:1000000:3300000;7-" +
                       std::to_string(total_cores - 1) + ":1200000:" + std::to_string(global_max);
        } else {
            spec_str = "0-5:614400:3532800;6-" +
                       std::to_string(total_cores - 1) + ":806400:" + std::to_string(global_max);
        }
    }

    auto segments = split_str(spec_str, ';');
    size_t seg_idx = 0;
    for (const auto &seg : segments) {
        auto parts = split_str(seg, ':');
        if (parts.size() >= 3) {
            CpuClusterSpec c{};
            size_t dash = parts[0].find('-');
            if (dash != std::string::npos) {
                c.first_core = std::atoi(parts[0].substr(0, dash).c_str());
                c.last_core = std::atoi(parts[0].substr(dash + 1).c_str());
            } else {
                c.first_core = c.last_core = std::atoi(parts[0].c_str());
            }
            c.min_freq_khz = std::atoi(parts[1].c_str());
            c.max_freq_khz = std::atoi(parts[2].c_str());

            bool is_prime = (seg_idx + 1 == segments.size());
            bool is_little = (seg_idx == 0 && segments.size() >= 3);
            if (is_mtk) {
                c.cpu_part = is_prime ? 0xd85 : (is_little ? 0xd80 : 0xd81);
            } else {
                c.cpu_part = is_prime ? 0xd85 : 0xd84;
            }
            c.capacity = is_prime ? 1024 : (is_little ? 446 : 768);

            constexpr int kStepKhz = 192000;
            for (int f = c.min_freq_khz; f < c.max_freq_khz; f += kStepKhz) {
                c.opp_freqs_khz.push_back(f);
            }
            c.opp_freqs_khz.push_back(c.max_freq_khz);
            g_cfg.vfs.clusters.push_back(std::move(c));
        }
        ++seg_idx;
    }

    if (g_cfg.vfs.clusters.empty()) {
        CpuClusterSpec fallback{0, total_cores - 1, 614400, global_max, 0xd85, 1024, {614400, 1804800, 2803200, global_max}};
        g_cfg.vfs.clusters.push_back(std::move(fallback));
    }
}

inline const CpuClusterSpec &get_cluster_for_core(int core_idx) {
    init_vfs_clusters_lightweight();
    for (const auto &c : g_cfg.vfs.clusters) {
        if (core_idx >= c.first_core && core_idx <= c.last_core) {
            return c;
        }
    }
    return g_cfg.vfs.clusters.back();
}

inline int compute_live_core_freq_khz(int core_idx) {
    const CpuClusterSpec &c = get_cluster_for_core(core_idx);
    if (c.opp_freqs_khz.empty()) return c.max_freq_khz;

    struct timespec ts{};
    clock_gettime(CLOCK_MONOTONIC, &ts);
    uint64_t tick = static_cast<uint64_t>(ts.tv_sec) * 7ULL +
                    static_cast<uint64_t>(ts.tv_nsec / 140000000ULL) +
                    static_cast<uint64_t>(core_idx * 19 + 5);

    size_t n = c.opp_freqs_khz.size();
    bool is_prime = (c.capacity >= 1024);
    size_t idx = 0;
    if (is_prime) {
        size_t span = (n > 2) ? (n - 1) : n;
        idx = (tick * 3ULL) % span;
    } else {
        size_t low = n / 5;
        size_t span = (n > low) ? (n - low) : 1;
        idx = low + (tick % span);
    }
    if (idx >= n) idx = n - 1;
    return c.opp_freqs_khz[idx];
}

inline std::string synthesize_Static_cpu_leaf(int core_idx, const std::string &leaf) {
    const CpuClusterSpec &c = get_cluster_for_core(core_idx);

    if (leaf == "cpuinfo_max_freq" || leaf == "scaling_max_freq") {
        return std::to_string(c.max_freq_khz) + "\n";
    }
    if (leaf == "cpuinfo_min_freq" || leaf == "scaling_min_freq") {
        return std::to_string(c.min_freq_khz) + "\n";
    }
    if (leaf == "scaling_available_frequencies") {
        std::ostringstream ss;
        for (size_t i = 0; i < c.opp_freqs_khz.size(); ++i) {
            if (i > 0) ss << " ";
            ss << c.opp_freqs_khz[i];
        }
        ss << "\n";
        return ss.str();
    }
    if (leaf == "scaling_governor") return "walt\n";
    if (leaf == "scaling_driver") return "cpufreq-dt\n";
    if (leaf == "related_cpus" || leaf == "affected_cpus") {
        std::ostringstream ss;
        for (int i = c.first_core; i <= c.last_core; ++i) {
            if (i > c.first_core) ss << " ";
            ss << i;
        }
        ss << "\n";
        return ss.str();
    }
    if (leaf == "cpu_capacity") return std::to_string(c.capacity) + "\n";
    if (leaf == "online") return "1\n";
    if (leaf == "topology/physical_package_id") return "0\n";
    if (leaf == "topology/cluster_id" || leaf == "topology/core_siblings_list") {
        return std::to_string(c.first_core) + "-" + std::to_string(c.last_core) + "\n";
    }
    if (leaf == "regs/identification/midr_el1") {
        char hex_midr[32];
        std::snprintf(hex_midr, sizeof(hex_midr), "0x00000000410fd%02x0\n", c.cpu_part & 0xff);
        return hex_midr;
    }
    return "";
}

inline std::once_flag g_cpuinfo_once;
inline std::string g_cached_cpuinfo;

inline const std::string &get_synthesized_cpuinfo() {
    std::call_once(g_cpuinfo_once, []() {
        init_vfs_clusters_lightweight();
        int cores = g_cfg.vfs.cpu_cores > 0 ? g_cfg.vfs.cpu_cores : 8;
        std::string hw = g_cfg.vfs.cpu_hardware.empty()
                             ? "Qualcomm Technologies, Inc SM8750"
                             : g_cfg.vfs.cpu_hardware;
        std::string soc = g_cfg.vfs.soc_machine.empty() ? "SM8750" : g_cfg.vfs.soc_machine;

        std::ostringstream ss;
        ss << "Processor\t: AArch64 Processor rev 0 (aarch64)\n";
        for (int i = 0; i < cores; ++i) {
            const CpuClusterSpec &c = get_cluster_for_core(i);
            char part_hex[16];
            std::snprintf(part_hex, sizeof(part_hex), "0x%03x", c.cpu_part);

            ss << "processor\t: " << i << "\n"
               << "BogoMIPS\t: 38.40\n"
               << "Features\t: fp asimd evtstrm aes pmull sha1 sha2 crc32 atomics fphp asimdhp cpuid asimdrdm jscvt fcma lrcpc dcpop sha3 sm3 sm4 asimddp sha512 asimdfhm dit uscat ilrcpc flagm ssbs sb paca pacg dcpodp flagm2 frint i8mm bf16 bti\n"
               << "CPU implementer\t: 0x41\n"
               << "CPU architecture: 8\n"
               << "CPU variant\t: 0x1\n"
               << "CPU part\t: " << part_hex << "\n"
               << "CPU revision\t: 0\n\n";
        }
        ss << "Hardware\t: " << hw << "\n";
        ss << "Qualcomm Technologies, Inc " << soc << "\n";
        g_cached_cpuinfo = ss.str();
    });
    return g_cached_cpuinfo;
}

inline uint64_t compute_carveout_total_kb() {
    uint64_t installed_kb = static_cast<uint64_t>(g_cfg.vfs.ram_gb > 0 ? g_cfg.vfs.ram_gb : 16) * 1024ULL * 1024ULL;
    uint64_t carveout_kb = static_cast<uint64_t>(g_cfg.vfs.ram_gb > 0 ? g_cfg.vfs.ram_gb : 16) * 49152ULL;
    return (installed_kb > carveout_kb) ? (installed_kb - carveout_kb) : installed_kb;
}

inline std::once_flag g_meminfo_once;
inline std::string g_cached_meminfo;

inline const std::string &get_synthesized_meminfo() {
    std::call_once(g_meminfo_once, []() {
        uint64_t total_kb = compute_carveout_total_kb();
        uint64_t avail_kb = (total_kb * 64ULL) / 100ULL;
        uint64_t free_kb  = (total_kb * 24ULL) / 100ULL;
        uint64_t cached_kb = (total_kb * 30ULL) / 100ULL;
        uint64_t swap_total_kb = (total_kb / 2ULL);

        std::ostringstream ms;
        ms << "MemTotal:       " << total_kb << " kB\n"
           << "MemFree:        " << free_kb << " kB\n"
           << "MemAvailable:   " << avail_kb << " kB\n"
           << "Buffers:          184320 kB\n"
           << "Cached:         " << cached_kb << " kB\n"
           << "SwapCached:        24576 kB\n"
           << "Active:          4821400 kB\n"
           << "Inactive:        3194880 kB\n"
           << "SwapTotal:      " << swap_total_kb << " kB\n"
           << "SwapFree:       " << (swap_total_kb - 131072ULL) << " kB\n"
           << "Zram:             524288 kB\n"
           << "Dirty:               512 kB\n"
           << "Writeback:             0 kB\n"
           << "AnonPages:       3620000 kB\n"
           << "Mapped:          1420000 kB\n"
           << "Shmem:             48200 kB\n"
           << "KReclaimable:     412000 kB\n"
           << "Slab:             580000 kB\n"
           << "SReclaimable:     240000 kB\n"
           << "SUnreclaim:       340000 kB\n";
        g_cached_meminfo = ms.str();
    });
    return g_cached_meminfo;
}

inline int (*orig_open)(const char *pathname, int flags, ...) = nullptr;
inline int (*orig_open64)(const char *pathname, int flags, ...) = nullptr;
inline int (*orig_openat)(int dirfd, const char *pathname, int flags, ...) = nullptr;
inline int (*orig_openat64)(int dirfd, const char *pathname, int flags, ...) = nullptr;
inline FILE *(*orig_fopen)(const char *pathname, const char *mode) = nullptr;
inline FILE *(*orig_fopen64)(const char *pathname, const char *mode) = nullptr;

inline ssize_t (*orig_readlink)(const char *pathname, char *buf, size_t bufsiz) = nullptr;
inline ssize_t (*orig_readlinkat)(int dirfd, const char *pathname, char *buf, size_t bufsiz) = nullptr;
inline int (*orig_fstat)(int fd, struct stat *statbuf) = nullptr;
inline int (*orig_fstat64)(int fd, struct stat64 *statbuf) = nullptr;
inline int (*orig_stat)(const char *pathname, struct stat *statbuf) = nullptr;
inline int (*orig_stat64)(const char *pathname, struct stat64 *statbuf) = nullptr;
inline int (*orig_lstat)(const char *pathname, struct stat *statbuf) = nullptr;
inline int (*orig_lstat64)(const char *pathname, struct stat64 *statbuf) = nullptr;
inline int (*orig_fstatat)(int dirfd, const char *pathname, struct stat *statbuf, int flags) = nullptr;
inline int (*orig_fstatat64)(int dirfd, const char *pathname, struct stat64 *statbuf, int flags) = nullptr;
inline int (*orig_access)(const char *pathname, int mode) = nullptr;
inline int (*orig_faccessat)(int dirfd, const char *pathname, int mode, int flags) = nullptr;
inline int (*orig_close)(int fd) = nullptr;

inline int (*orig_uname)(struct utsname *buf) = nullptr;
inline int (*orig_sysinfo)(struct sysinfo *info) = nullptr;
inline long (*orig_sysconf)(int name) = nullptr;
inline FILE *(*orig_popen)(const char *command, const char *type) = nullptr;

inline void init_vfs_symbols() {
    if (orig_openat) return;
    orig_open       = reinterpret_cast<decltype(orig_open)>(dlsym(RTLD_DEFAULT, "open"));
    orig_open64     = reinterpret_cast<decltype(orig_open64)>(dlsym(RTLD_DEFAULT, "open64"));
    orig_openat     = reinterpret_cast<decltype(orig_openat)>(dlsym(RTLD_DEFAULT, "openat"));
    orig_openat64   = reinterpret_cast<decltype(orig_openat64)>(dlsym(RTLD_DEFAULT, "openat64"));
    orig_fopen      = reinterpret_cast<decltype(orig_fopen)>(dlsym(RTLD_DEFAULT, "fopen"));
    orig_fopen64    = reinterpret_cast<decltype(orig_fopen64)>(dlsym(RTLD_DEFAULT, "fopen64"));
    orig_readlink   = reinterpret_cast<decltype(orig_readlink)>(dlsym(RTLD_DEFAULT, "readlink"));
    orig_readlinkat = reinterpret_cast<decltype(orig_readlinkat)>(dlsym(RTLD_DEFAULT, "readlinkat"));
    orig_fstat      = reinterpret_cast<decltype(orig_fstat)>(dlsym(RTLD_DEFAULT, "fstat"));
    orig_fstat64    = reinterpret_cast<decltype(orig_fstat64)>(dlsym(RTLD_DEFAULT, "fstat64"));
    orig_stat       = reinterpret_cast<decltype(orig_stat)>(dlsym(RTLD_DEFAULT, "stat"));
    orig_stat64     = reinterpret_cast<decltype(orig_stat64)>(dlsym(RTLD_DEFAULT, "stat64"));
    orig_lstat      = reinterpret_cast<decltype(orig_lstat)>(dlsym(RTLD_DEFAULT, "lstat"));
    orig_lstat64    = reinterpret_cast<decltype(orig_lstat64)>(dlsym(RTLD_DEFAULT, "lstat64"));
    orig_fstatat    = reinterpret_cast<decltype(orig_fstatat)>(dlsym(RTLD_DEFAULT, "fstatat"));
    orig_fstatat64  = reinterpret_cast<decltype(orig_fstatat64)>(dlsym(RTLD_DEFAULT, "fstatat64"));
    orig_access     = reinterpret_cast<decltype(orig_access)>(dlsym(RTLD_DEFAULT, "access"));
    orig_faccessat  = reinterpret_cast<decltype(orig_faccessat)>(dlsym(RTLD_DEFAULT, "faccessat"));
    orig_close      = reinterpret_cast<decltype(orig_close)>(dlsym(RTLD_DEFAULT, "close"));
    orig_uname      = reinterpret_cast<decltype(orig_uname)>(dlsym(RTLD_DEFAULT, "uname"));
    orig_sysinfo    = reinterpret_cast<decltype(orig_sysinfo)>(dlsym(RTLD_DEFAULT, "sysinfo"));
    orig_sysconf    = reinterpret_cast<decltype(orig_sysconf)>(dlsym(RTLD_DEFAULT, "sysconf"));
    orig_popen      = reinterpret_cast<decltype(orig_popen)>(dlsym(RTLD_DEFAULT, "popen"));
}

inline int open_sanitized_proc_self_file(const char *real_path, const char *orig_path) {
    int real_fd = orig_openat ? orig_openat(AT_FDCWD, real_path, O_RDONLY, 0) : -1;
    if (real_fd < 0) return -1;

    std::string raw;
    char buf[4096];
    ssize_t n;
    while ((n = read(real_fd, buf, sizeof(buf))) > 0) {
        raw.append(buf, static_cast<size_t>(n));
    }
    if (orig_close) orig_close(real_fd); else close(real_fd);

    std::ostringstream clean;
    std::istringstream iss(raw);
    std::string line;
    while (std::getline(iss, line)) {
        if (line.find("mirage_forge") != std::string::npos ||
            line.find("zygisk") != std::string::npos ||
            line.find("magisk") != std::string::npos ||
            line.find("kernelsu") != std::string::npos ||
            line.find("/data/adb") != std::string::npos ||
            line.find("memfd:") != std::string::npos) {
            continue;
        }
        clean << line << "\n";
    }
    return create_cloaked_memfd("anon_proc", clean.str(), orig_path);
}

inline bool is_virtualized_vfs_path(const char *path) {
    if (!path || !g_cfg.vfs.enabled) return false;
    if (std::strcmp(path, "/proc/cpuinfo") == 0 ||
        std::strcmp(path, "/proc/meminfo") == 0 ||
        std::strcmp(path, "/proc/version") == 0 ||
        std::strcmp(path, "/proc/ddr_info") == 0 ||
        std::strcmp(path, "/proc/device-tree/model") == 0 ||
        std::strcmp(path, "/sys/firmware/devicetree/base/model") == 0 ||
        std::strcmp(path, "/proc/device-tree/compatible") == 0 ||
        std::strcmp(path, "/sys/firmware/devicetree/base/compatible") == 0 ||
        std::strcmp(path, "/sys/class/kgsl/kgsl-3d0/gpu_model") == 0 ||
        std::strcmp(path, "/sys/devices/system/chip-info/ddr_info") == 0 ||
        std::strcmp(path, "/sys/devices/soc0/soc_id") == 0 ||
        std::strcmp(path, "/sys/devices/soc0/family") == 0 ||
        std::strcmp(path, "/sys/devices/soc0/machine") == 0 ||
        std::strcmp(path, "/sys/devices/soc0/revision") == 0 ||
        std::strcmp(path, "/sys/devices/soc0/vendor") == 0 ||
        std::strcmp(path, "/sys/devices/system/cpu/present") == 0 ||
        std::strcmp(path, "/sys/devices/system/cpu/possible") == 0 ||
        std::strcmp(path, "/sys/devices/system/cpu/online") == 0 ||
        std::strcmp(path, "/sys/devices/system/cpu/kernel_max") == 0) {
        return true;
    }
    std::string leaf;
    int core_idx = parse_cpu_core_index(path, leaf);
    if (core_idx >= 0 && core_idx < (g_cfg.vfs.cpu_cores > 0 ? g_cfg.vfs.cpu_cores : 8)) {
        if (leaf == "scaling_cur_freq" || leaf == "cpuinfo_cur_freq" ||
            !synthesize_static_cpu_leaf(core_idx, leaf).empty()) {
            return true;
        }
    }
    return false;
}

inline int intercept_vfs_open_on_demand(const char *path) {
    if (!path || path[0] != '/') return -1;

    // 顺手在主线程尝试锁定 Application DPI (一旦锁定即 1 周期原子返回)
    if (g_cfg.build.app_dpi > 0) {
        try_lock_application_dpi_if_needed();
    }

    if (g_cfg.vfs.enabled) {
        if (std::strcmp(path, "/proc/cpuinfo") == 0) {
            emit_probe("VFS_OPEN", path, "Cloaked");
            return create_cloaked_memfd("cpuinfo", get_synthesized_cpuinfo(), path);
        }
        if (std::strcmp(path, "/proc/meminfo") == 0) {
            emit_probe("VFS_OPEN", path, "Cloaked");
            return create_cloaked_memfd("meminfo", get_synthesized_meminfo(), path);
        }
        if (std::strcmp(path, "/proc/version") == 0) {
            std::string ver = "Linux version " + g_cfg.vfs.kernel_release +
                              " (build-user@android-build) " + g_cfg.vfs.kernel_version + "\n";
            emit_probe("VFS_OPEN", path, "Cloaked");
            return create_cloaked_memfd("version", ver, path);
        }
        // 拦截真实真机上的 DeviceTree Model 与 Compatible (DevCheck 读取真实 SoC 的核心路径！)
        if (std::strcmp(path, "/proc/device-tree/model") == 0 ||
            std::strcmp(path, "/sys/firmware/devicetree/base/model") == 0) {
            std::string dt_model = (g_cfg.vfs.cpu_hardware.empty() ? "Qualcomm Technologies, Inc. SM8750" : g_cfg.vfs.cpu_hardware) + " SoC";
            dt_model.push_back('\0'); // DeviceTree 字符串以 \0 结尾
            emit_probe("VFS_OPEN", path, dt_model.c_str());
            return create_cloaked_memfd("dt_model", dt_model, path);
        }
        if (std::strcmp(path, "/proc/device-tree/compatible") == 0 ||
            std::strcmp(path, "/sys/firmware/devicetree/base/compatible") == 0) {
            bool is_mtk = (g_cfg.vfs.soc_machine.find("MT") == 0);
            std::string compat = (is_mtk ? "mediatek," : "qcom,") + g_cfg.vfs.soc_machine;
            for (char &ch : compat) {
                if (ch >= 'A' && ch <= 'Z') ch = static_cast<char>(ch - 'A' + 'a');
            }
            compat.push_back('\0');
            emit_probe("VFS_OPEN", path, compat.c_str());
            return create_cloaked_memfd("dt_compat", compat, path);
        }
        // 拦截真机高通 KGSL GPU 型号节点
        if (std::strcmp(path, "/sys/class/kgsl/kgsl-3d0/gpu_model") == 0) {
            std::string gpu_m = (g_cfg.gl.gl_renderer.empty() ? "Adreno (TM) 830" : g_cfg.gl.gl_renderer) + "\n";
            emit_probe("VFS_OPEN", path, gpu_m);
            return create_cloaked_memfd("kgsl_gpu", gpu_m, path);
        }
        if (std::strcmp(path, "/proc/ddr_info") == 0 ||
            std::strcmp(path, "/sys/devices/system/chip-info/ddr_info") == 0) {
            std::string ddr = std::to_string(g_cfg.vfs.ram_gb) + "GB " +
                              g_cfg.vfs.ddr_vendor + " " + g_cfg.vfs.ddr_type + "\n";
            emit_probe("VFS_OPEN", path, "Cloaked");
            return create_cloaked_memfd("ddr_info", ddr, path);
        }
        if (std::strcmp(path, "/sys/devices/soc0/soc_id") == 0) {
            emit_probe("VFS_OPEN", path, g_cfg.vfs.soc_id);
            return create_cloaked_memfd("soc_id", g_cfg.vfs.soc_id + "\n", path);
        }
        if (std::strcmp(path, "/sys/devices/soc0/family") == 0) {
            emit_probe("VFS_OPEN", path, g_cfg.vfs.soc_family);
            return create_cloaked_memfd("soc_fam", g_cfg.vfs.soc_family + "\n", path);
        }
        if (std::strcmp(path, "/sys/devices/soc0/machine") == 0) {
            emit_probe("VFS_OPEN", path, g_cfg.vfs.soc_machine);
            return create_cloaked_memfd("soc_mach", g_cfg.vfs.soc_machine + "\n", path);
        }
        if (std::strcmp(path, "/sys/devices/soc0/revision") == 0) {
            return create_cloaked_memfd("soc_rev", "2.0\n", path);
        }
        if (std::strcmp(path, "/sys/devices/soc0/vendor") == 0) {
            bool is_mtk = (g_cfg.vfs.soc_machine.find("MT") == 0);
            return create_cloaked_memfd("soc_ven", is_mtk ? "MediaTek\n" : "Qualcomm\n", path);
        }

        int cores = g_cfg.vfs.cpu_cores > 0 ? g_cfg.vfs.cpu_cores : 8;
        if (std::strcmp(path, "/sys/devices/system/cpu/present") == 0 ||
            std::strcmp(path, "/sys/devices/system/cpu/possible") == 0 ||
            std::strcmp(path, "/sys/devices/system/cpu/online") == 0) {
            std::string range = "0-" + std::to_string(cores - 1) + "\n";
            return create_cloaked_memfd("cpu_range", range, path);
        }
        if (std::strcmp(path, "/sys/devices/system/cpu/kernel_max") == 0) {
            return create_cloaked_memfd("cpu_kmax", std::to_string(cores - 1) + "\n", path);
        }

        std::string leaf;
        int core_idx = parse_cpu_core_index(path, leaf);
        if (core_idx >= 0 && core_idx < cores) {
            if (leaf == "scaling_cur_freq" || leaf == "cpuinfo_cur_freq") {
                int live_khz = compute_live_core_freq_khz(core_idx);
                std::string freq_str = std::to_string(live_khz) + "\n";
                emit_probe("VFS_OPEN", path, "LiveFreq:" + std::to_string(live_khz));
                return create_cloaked_memfd("cur_freq", freq_str, path);
            }
            std::string static_val = synthesize_static_cpu_leaf(core_idx, leaf);
            if (!static_val.empty()) {
                emit_probe("VFS_OPEN", path, "Cloaked");
                return create_cloaked_memfd("cpu_node", static_val, path);
            }
        }
    }

    if (g_cfg.vfs.enabled || g_cfg.build.enabled) {
        if (std::strcmp(path, "/proc/self/maps") == 0 || std::strcmp(path, "/proc/thread-self/maps") == 0) {
            return open_sanitized_proc_self_file("/proc/self/maps", path);
        }
        if (std::strcmp(path, "/proc/self/smaps") == 0) {
            return open_sanitized_proc_self_file("/proc/self/smaps", path);
        }
        if (std::strcmp(path, "/proc/self/mountinfo") == 0 || std::strcmp(path, "/proc/mounts") == 0) {
            return open_sanitized_proc_self_file("/proc/self/mountinfo", path);
        }
    }

    if (__builtin_expect(g_cfg.probe.enabled, 0)) {
        if (std::strncmp(path, "/proc/", 6) == 0 || std::strncmp(path, "/sys/", 5) == 0) {
            if (std::strncmp(path, "/proc/self/", 11) != 0 &&
                std::strncmp(path, "/proc/thread-self/", 18) != 0) {
                emit_probe("VFS_UNMAPPED", path, "<passthrough>");
            }
        }
    }
    return -1;
}

inline int hook_openat(int dirfd, const char *pathname, int flags, ...) {
    mode_t mode = 0;
    if ((flags & O_CREAT) || (flags & O_TMPFILE) == O_TMPFILE) {
        va_list args;
        va_start(args, flags);
        mode = static_cast<mode_t>(va_arg(args, int));
        va_end(args);
    }
    ScopedHookGuard guard;
    if (guard && pathname) {
        int fake_fd = intercept_vfs_open_on_demand(pathname);
        if (fake_fd >= 0) return fake_fd;
    }
    return orig_openat ? orig_openat(dirfd, pathname, flags, mode) : -1;
}

inline int hook_openat64(int dirfd, const char *pathname, int flags, ...) {
    mode_t mode = 0;
    if ((flags & O_CREAT) || (flags & O_TMPFILE) == O_TMPFILE) {
        va_list args;
        va_start(args, flags);
        mode = static_cast<mode_t>(va_arg(args, int));
        va_end(args);
    }
    ScopedHookGuard guard;
    if (guard && pathname) {
        int fake_fd = intercept_vfs_open_on_demand(pathname);
        if (fake_fd >= 0) return fake_fd;
    }
    return orig_openat64 ? orig_openat64(dirfd, pathname, flags, mode)
                         : (orig_openat ? orig_openat(dirfd, pathname, flags, mode) : -1);
}

inline int hook_open(const char *pathname, int flags, ...) {
    mode_t mode = 0;
    if ((flags & O_CREAT) || (flags & O_TMPFILE) == O_TMPFILE) {
        va_list args;
        va_start(args, flags);
        mode = static_cast<mode_t>(va_arg(args, int));
        va_end(args);
    }
    ScopedHookGuard guard;
    if (guard && pathname) {
        int fake_fd = intercept_vfs_open_on_demand(pathname);
        if (fake_fd >= 0) return fake_fd;
    }
    return orig_open ? orig_open(pathname, flags, mode) : -1;
}

inline int hook_open64(const char *pathname, int flags, ...) {
    mode_t mode = 0;
    if ((flags & O_CREAT) || (flags & O_TMPFILE) == O_TMPFILE) {
        va_list args;
        va_start(args, flags);
        mode = static_cast<mode_t>(va_arg(args, int));
        va_end(args);
    }
    ScopedHookGuard guard;
    if (guard && pathname) {
        int fake_fd = intercept_vfs_open_on_demand(pathname);
        if (fake_fd >= 0) return fake_fd;
    }
    return orig_open64 ? orig_open64(pathname, flags, mode)
                       : (orig_open ? orig_open(pathname, flags, mode) : -1);
}

inline FILE *hook_fopen(const char *pathname, const char *mode) {
    ScopedHookGuard guard;
    if (guard && pathname) {
        int fake_fd = intercept_vfs_open_on_demand(pathname);
        if (fake_fd >= 0) {
            FILE *fp = fdopen(fake_fd, mode ? mode : "r");
            if (fp) return fp;
            if (orig_close) orig_close(fake_fd); else close(fake_fd);
        }
    }
    return orig_fopen ? orig_fopen(pathname, mode) : nullptr;
}

inline FILE *hook_fopen64(const char *pathname, const char *mode) {
    ScopedHookGuard guard;
    if (guard && pathname) {
        int fake_fd = intercept_vfs_open_on_demand(pathname);
        if (fake_fd >= 0) {
            FILE *fp = fdopen(fake_fd, mode ? mode : "r");
            if (fp) return fp;
            if (orig_close) orig_close(fake_fd); else close(fake_fd);
        }
    }
    return orig_fopen64 ? orig_fopen64(pathname, mode)
                        : (orig_fopen ? orig_fopen(pathname, mode) : nullptr);
}

inline int hook_close(int fd) {
    g_fd_cloak.untrack(fd);
    return orig_close ? orig_close(fd) : close(fd);
}

inline ssize_t hook_readlinkat(int dirfd, const char *pathname, char *buf, size_t bufsiz) {
    if (pathname && buf && bufsiz > 0) {
        std::string cloaked_target;
        if (g_fd_cloak.resolve_proc_fd_link(pathname, cloaked_target)) {
            size_t copy_len = std::min(bufsiz, cloaked_target.size());
            std::memcpy(buf, cloaked_target.data(), copy_len);
            return static_cast<ssize_t>(copy_len);
        }
    }
    return orig_readlinkat ? orig_readlinkat(dirfd, pathname, buf, bufsiz) : -1;
}

inline ssize_t hook_readlink(const char *pathname, char *buf, size_t bufsiz) {
    if (pathname && buf && bufsiz > 0) {
        std::string cloaked_target;
        if (g_fd_cloak.resolve_proc_fd_link(pathname, cloaked_target)) {
            size_t copy_len = std::min(bufsiz, cloaked_target.size());
            std::memcpy(buf, cloaked_target.data(), copy_len);
            return static_cast<ssize_t>(copy_len);
        }
    }
    return orig_readlink ? orig_readlink(pathname, buf, bufsiz) : -1;
}

template <typename StatT>
inline void fill_synthetic_proc_stat(StatT *st, const char *path) {
    if (!st) return;
    std::memset(st, 0, sizeof(StatT));
    st->st_dev = 0x04;
    st->st_ino = static_cast<decltype(st->st_ino)>(hash_str(path ? path : "/proc/cpuinfo") & 0x7fffffffULL);
    st->st_mode = S_IFREG | 0444;
    st->st_nlink = 1;
    st->st_blksize = 4096;
}

inline int hook_fstat(int fd, struct stat *statbuf) {
    int ret = orig_fstat ? orig_fstat(fd, statbuf) : -1;
    if (ret == 0 && statbuf && g_fd_cloak.is_cloaked(fd)) {
        statbuf->st_mode = S_IFREG | 0444;
        statbuf->st_dev = 0x04;
    }
    return ret;
}

inline int hook_fstat64(int fd, struct stat64 *statbuf) {
    int ret = orig_fstat64 ? orig_fstat64(fd, statbuf) : -1;
    if (ret == 0 && statbuf && g_fd_cloak.is_cloaked(fd)) {
        statbuf->st_mode = S_IFREG | 0444;
        statbuf->st_dev = 0x04;
    }
    return ret;
}

inline int hook_stat(const char *pathname, struct stat *statbuf) {
    int ret = orig_stat ? orig_stat(pathname, statbuf) : -1;
    if (ret != 0 && is_virtualized_vfs_path(pathname)) {
        fill_synthetic_proc_stat(statbuf, pathname);
        errno = 0;
        return 0;
    }
    return ret;
}

inline int hook_stat64(const char *pathname, struct stat64 *statbuf) {
    int ret = orig_stat64 ? orig_stat64(pathname, statbuf) : -1;
    if (ret != 0 && is_virtualized_vfs_path(pathname)) {
        fill_synthetic_proc_stat(statbuf, pathname);
        errno = 0;
        return 0;
    }
    return ret;
}

// 修复：严格调用 orig_lstat 与 orig_lstat64，不误调 orig_stat
inline int hook_lstat(const char *pathname, struct stat *statbuf) {
    int ret = orig_lstat ? orig_lstat(pathname, statbuf) : -1;
    if (ret != 0 && is_virtualized_vfs_path(pathname)) {
        fill_synthetic_proc_stat(statbuf, pathname);
        errno = 0;
        return 0;
    }
    return ret;
}

inline int hook_lstat64(const char *pathname, struct stat64 *statbuf) {
    int ret = orig_lstat64 ? orig_lstat64(pathname, statbuf) : -1;
    if (ret != 0 && is_virtualized_vfs_path(pathname)) {
        fill_synthetic_proc_stat(statbuf, pathname);
        errno = 0;
        return 0;
    }
    return ret;
}

inline int hook_fstatat(int dirfd, const char *pathname, struct stat *statbuf, int flags) {
    int ret = orig_fstatat ? orig_fstatat(dirfd, pathname, statbuf, flags) : -1;
    if (ret != 0 && is_virtualized_vfs_path(pathname)) {
        fill_synthetic_proc_stat(statbuf, pathname);
        errno = 0;
        return 0;
    }
    return ret;
}

inline int hook_fstatat64(int dirfd, const char *pathname, struct stat64 *statbuf, int flags) {
    int ret = orig_fstatat64 ? orig_fstatat64(dirfd, pathname, statbuf, flags) : -1;
    if (ret != 0 && is_virtualized_vfs_path(pathname)) {
        fill_synthetic_proc_stat(statbuf, pathname);
        errno = 0;
        return 0;
    }
    return ret;
}

inline int hook_access(const char *pathname, int mode) {
    int ret = orig_access ? orig_access(pathname, mode) : -1;
    if (ret != 0 && is_virtualized_vfs_path(pathname)) {
        errno = 0;
        return 0;
    }
    return ret;
}

inline int hook_faccessat(int dirfd, const char *pathname, int mode, int flags) {
    int ret = orig_faccessat ? orig_faccessat(dirfd, pathname, mode, flags) : -1;
    if (ret != 0 && is_virtualized_vfs_path(pathname)) {
        errno = 0;
        return 0;
    }
    return ret;
}

inline int hook_uname(struct utsname *buf) {
    if (!orig_uname) return -1;
    int ret = orig_uname(buf);
    if (ret == 0 && buf && g_cfg.vfs.enabled) {
        if (!g_cfg.vfs.kernel_release.empty()) {
            std::strncpy(buf->release, g_cfg.vfs.kernel_release.c_str(), sizeof(buf->release) - 1);
            buf->release[sizeof(buf->release) - 1] = '\0';
        }
        if (!g_cfg.vfs.kernel_version.empty()) {
            std::strncpy(buf->version, g_cfg.vfs.kernel_version.c_str(), sizeof(buf->version) - 1);
            buf->version[sizeof(buf->version) - 1] = '\0';
        }
        if (!g_cfg.vfs.cpu_arch.empty()) {
            std::strncpy(buf->machine, g_cfg.vfs.cpu_arch.c_str(), sizeof(buf->machine) - 1);
            buf->machine[sizeof(buf->machine) - 1] = '\0';
        }
        ScopedHookGuard guard;
        if (guard) emit_probe("UNAME", buf->release, buf->machine);
    }
    return ret;
}

inline int hook_sysinfo(struct sysinfo *info) {
    if (!orig_sysinfo) return -1;
    int ret = orig_sysinfo(info);
    if (ret == 0 && info && g_cfg.vfs.enabled && g_cfg.vfs.ram_gb > 0) {
        uint64_t unit = info->mem_unit ? info->mem_unit : 1;
        uint64_t total_bytes = compute_carveout_total_kb() * 1024ULL;
        info->totalram = static_cast<unsigned long>(total_bytes / unit);
        info->freeram  = static_cast<unsigned long>((total_bytes * 28ULL / 100ULL) / unit);
        info->bufferram = static_cast<unsigned long>((total_bytes * 6ULL / 100ULL) / unit);
        ScopedHookGuard guard;
        if (guard) emit_probe("SYSINFO", "totalram_gb", std::to_string(g_cfg.vfs.ram_gb));
    }
    return ret;
}

inline long hook_sysconf(int name) {
    if (g_cfg.vfs.enabled) {
        if ((name == _SC_NPROCESSORS_CONF || name == _SC_NPROCESSORS_ONLN) && g_cfg.vfs.cpu_cores > 0) {
            ScopedHookGuard guard;
            if (guard) emit_probe("SYSCONF", "_SC_NPROCESSORS_ONLN", std::to_string(g_cfg.vfs.cpu_cores));
            return g_cfg.vfs.cpu_cores;
        }
        if (name == _SC_PHYS_PAGES && g_cfg.vfs.ram_gb > 0) {
            long page_sz = orig_sysconf ? orig_sysconf(_SC_PAGESIZE) : 4096;
            if (page_sz <= 0) page_sz = 4096;
            uint64_t total_bytes = compute_carveout_total_kb() * 1024ULL;
            return static_cast<long>(total_bytes / static_cast<uint64_t>(page_sz));
        }
    }
    return orig_sysconf ? orig_sysconf(name) : -1;
}

inline FILE *hook_popen(const char *command, const char *type) {
    if (command && g_cfg.build.enabled) {
        std::string cmd(command);
        if (cmd.find("getprop") != std::string::npos) {
            ScopedHookGuard guard;
            if (guard) emit_probe("POPEN", "getprop", cmd);

            size_t pos = cmd.find("getprop");
            std::string arg = cmd.substr(pos + 7);
            size_t first = arg.find_first_not_of(" \t\r\n\"'");
            if (first != std::string::npos) {
                size_t last = arg.find_last_not_of(" \t\r\n\"'");
                std::string prop_key = arg.substr(first, last - first + 1);
                auto it = g_cfg.build.prop_map.find(prop_key);
                if (it != g_cfg.build.prop_map.end()) {
                    std::string out = it->second + "\n";
                    int fd = create_cloaked_memfd("getprop_pipe", out, "/dev/null");
                    if (fd >= 0) {
                        FILE *fp = fdopen(fd, "r");
                        if (fp) return fp;
                        if (orig_close) orig_close(fd); else close(fd);
                    }
                }
            }
        }
    }
    return orig_popen ? orig_popen(command, type) : nullptr;
}

} // namespace mirage

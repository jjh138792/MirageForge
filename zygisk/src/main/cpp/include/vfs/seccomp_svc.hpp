#pragma once

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cinttypes>
#include <string>
#include <vector>
#include <atomic>
#include <signal.h>
#include <ucontext.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/prctl.h>
#include <sys/syscall.h>
#include <linux/filter.h>
#include <linux/seccomp.h>
#include <linux/audit.h>
#include "vfs/io_hooks.hpp"

namespace mirage {

struct PrebakedVfsEntry {
    std::string path;
    std::string content;
};

// AArch64 内核 rt_sigaction 精确 ABI 布局 (sigsetsize == 8)
struct KernelSigactionAArch64 {
    uintptr_t k_sa_handler;
    uint64_t  k_sa_flags;
    uintptr_t k_sa_restorer;
    uint64_t  k_sa_mask;
};

inline struct sigaction g_old_sigsys{};
inline KernelSigactionAArch64 g_old_k_sigsys{};
inline int g_seccomp_probe_pipe[2] = {-1, -1};
inline std::atomic_flag g_seccomp_pipe_lock = ATOMIC_FLAG_INIT;

// 预烘焙只读静态 VFS 缓存：仅在安装 BPF 前的单线程阶段构造，安装后永不修改，
// 从而在 SIGSYS 信号处理函数内实现 0 malloc、0 锁、100% 异步信号安全！
inline std::vector<PrebakedVfsEntry> g_seccomp_vfs_cache;
inline std::atomic<bool> g_seccomp_cache_ready{false};

inline void prebake_seccomp_vfs_cache() {
    g_seccomp_vfs_cache.clear();
    if (!g_cfg.vfs.enabled) return;

    auto bake = [&](const std::string &path) {
        std::string content;
        int dyn = -1;
        if (synthesize_vfs_node_on_demand(path, &content, &dyn) && !content.empty()) {
            g_seccomp_vfs_cache.push_back({path, std::move(content)});
        }
    };

    static const char *const kCorePaths[] = {
        "/proc/cpuinfo",
        "/proc/meminfo",
        "/proc/ddr_info",
        "/proc/version",
        "/proc/sys/kernel/osrelease",
        "/sys/class/kgsl/kgsl-3d0/gpu_model",
        "/sys/devices/soc0/soc_id",
        "/sys/devices/soc0/family",
        "/sys/devices/soc0/machine",
        "/sys/devices/system/chip-info/ddr_info",
        "/sys/devices/system/chip-info/ddr_type",
        "/sys/devices/system/chip-info/ddr_size",
        "/sys/firmware/devicetree/base/memory/ddr_device_type",
        "/sys/devices/system/cpu/present",
        "/sys/devices/system/cpu/possible",
        "/sys/devices/system/cpu/online",
        "/sys/devices/system/cpu/offline",
        "/sys/devices/system/cpu/kernel_max",
    };
    for (const char *p : kCorePaths) bake(p);

    int cores = (g_cfg.vfs.cpu_cores > 0 && g_cfg.vfs.cpu_cores <= 16) ? g_cfg.vfs.cpu_cores : 8;
    for (int i = 0; i < cores; ++i) {
        std::string pfx = "/sys/devices/system/cpu/cpu" + std::to_string(i) + "/";
        bake(pfx + "online");
        bake(pfx + "cpu_capacity");
        bake(pfx + "regs/identification/midr_el1");
        bake(pfx + "topology/core_cpus_list");
        bake(pfx + "cpufreq/cpuinfo_max_freq");
        bake(pfx + "cpufreq/cpuinfo_min_freq");
        bake(pfx + "cpufreq/scaling_max_freq");
        bake(pfx + "cpufreq/scaling_min_freq");
        bake(pfx + "cpufreq/scaling_governor");
        bake(pfx + "cpufreq/scaling_driver");
    }

    if (g_cfg.build.enabled) {
        bake("/system/build.prop");
        bake("/vendor/build.prop");
        bake("/product/build.prop");
        bake("/odm/build.prop");
    }

    g_seccomp_cache_ready.store(true, std::memory_order_release);
}

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

inline void drain_probe_pipe(size_t bytes) noexcept {
    char trash[64];
    while (bytes > 0) {
        size_t step = bytes > sizeof(trash) ? sizeof(trash) : bytes;
        int64_t r = mirage_seccomp_bypass_syscall(
            __NR_read, g_seccomp_probe_pipe[0],
            reinterpret_cast<long>(trash), static_cast<long>(step), 0, 0, 0);
        if (r <= 0) break;
        bytes -= static_cast<size_t>(r);
    }
}

// 通过内核 pipe copy_from_user 安全读取任意用户态内存，遇到 PROT_NONE 陷阱页绝不 SIGSEGV
inline bool safe_read_user_bytes(const void *user_src, void *dst, size_t len) noexcept {
    if (!user_src || !dst || len == 0 || len > 512) return false;
    if (g_seccomp_probe_pipe[0] < 0 || g_seccomp_probe_pipe[1] < 0) return false;
    if (g_seccomp_pipe_lock.test_and_set(std::memory_order_acquire)) return false;

    int64_t wr = mirage_seccomp_bypass_syscall(
        __NR_write, g_seccomp_probe_pipe[1],
        reinterpret_cast<long>(user_src), static_cast<long>(len), 0, 0, 0);
    bool ok = false;
    if (wr == static_cast<int64_t>(len)) {
        int64_t rd = mirage_seccomp_bypass_syscall(
            __NR_read, g_seccomp_probe_pipe[0],
            reinterpret_cast<long>(dst), static_cast<long>(len), 0, 0, 0);
        ok = (rd == static_cast<int64_t>(len));
        if (!ok && rd >= 0) drain_probe_pipe(static_cast<size_t>(wr - rd));
    } else if (wr > 0) {
        drain_probe_pipe(static_cast<size_t>(wr));
    }
    g_seccomp_pipe_lock.clear(std::memory_order_release);
    return ok;
}

// 通过内核 pipe copy_to_user 安全写入任意用户态内存，遇到只读/无效页绝不 SIGSEGV
inline bool safe_write_user_bytes(void *user_dst, const void *src, size_t len) noexcept {
    if (!user_dst || !src || len == 0 || len > 512) return false;
    if (g_seccomp_probe_pipe[0] < 0 || g_seccomp_probe_pipe[1] < 0) return false;
    if (g_seccomp_pipe_lock.test_and_set(std::memory_order_acquire)) return false;

    int64_t wr = mirage_seccomp_bypass_syscall(
        __NR_write, g_seccomp_probe_pipe[1],
        reinterpret_cast<long>(src), static_cast<long>(len), 0, 0, 0);
    bool ok = false;
    if (wr == static_cast<int64_t>(len)) {
        int64_t rd = mirage_seccomp_bypass_syscall(
            __NR_read, g_seccomp_probe_pipe[0],
            reinterpret_cast<long>(user_dst), static_cast<long>(len), 0, 0, 0);
        ok = (rd == static_cast<int64_t>(len));
        if (!ok) drain_probe_pipe(static_cast<size_t>(wr));
    } else if (wr > 0) {
        drain_probe_pipe(static_cast<size_t>(wr));
    }
    g_seccomp_pipe_lock.clear(std::memory_order_release);
    return ok;
}

inline bool safe_copy_user_vfs_path(const char *user_ptr, char *out_buf, size_t out_cap) noexcept {
    if (!user_ptr || !out_buf || out_cap < 8) return false;
    if (g_seccomp_probe_pipe[0] < 0 || g_seccomp_probe_pipe[1] < 0) return false;
    if (g_seccomp_pipe_lock.test_and_set(std::memory_order_acquire)) return false;

    size_t copied = 0;
    bool nul_found = false;
    bool valid = true;

    while (copied + 1 < out_cap) {
        uintptr_t cur = reinterpret_cast<uintptr_t>(user_ptr) + copied;
        size_t to_page_end = 4096U - (cur & 4095U);
        size_t remain = (out_cap - 1) - copied;
        size_t chunk = (to_page_end < remain) ? to_page_end : remain;
        if (chunk > 64U) chunk = 64U;

        int64_t wr = mirage_seccomp_bypass_syscall(
            __NR_write, g_seccomp_probe_pipe[1],
            static_cast<long>(cur), static_cast<long>(chunk), 0, 0, 0);
        if (wr <= 0) { valid = false; break; }

        int64_t rd = mirage_seccomp_bypass_syscall(
            __NR_read, g_seccomp_probe_pipe[0],
            reinterpret_cast<long>(out_buf + copied), static_cast<long>(wr), 0, 0, 0);
        if (rd <= 0) {
            drain_probe_pipe(static_cast<size_t>(wr));
            valid = false;
            break;
        }
        if (rd < wr) drain_probe_pipe(static_cast<size_t>(wr - rd));

        for (int64_t i = 0; i < rd; ++i) {
            if (out_buf[copied + static_cast<size_t>(i)] == '\0') {
                nul_found = true;
                break;
            }
        }
        copied += static_cast<size_t>(rd);

        if (copied >= 1 && out_buf[0] != '/') { valid = false; break; }
        if (copied >= 6) {
            if (std::strncmp(out_buf, "/proc/", 6) != 0 &&
                std::strncmp(out_buf, "/sys/", 5) != 0 &&
                std::strncmp(out_buf, "/system/", 8) != 0 &&
                std::strncmp(out_buf, "/vendor/", 8) != 0 &&
                std::strncmp(out_buf, "/product/", 9) != 0 &&
                std::strncmp(out_buf, "/odm/", 5) != 0) {
                valid = false;
                break;
            }
        }
        if (nul_found) break;
    }

    g_seccomp_pipe_lock.clear(std::memory_order_release);
    return valid && nul_found && copied >= 6;
}

// 零 malloc、零锁，通过裸 svc #0 创建只读 memfd 并填入静态数据
inline int create_raw_memfd_from_buffer(const char *data, size_t len, const char *static_path) noexcept {
    int64_t fd = mirage_seccomp_bypass_syscall(
        __NR_memfd_create, reinterpret_cast<long>(""), MFD_CLOEXEC, 0, 0, 0, 0);
    if (fd < 0) return -1;

    size_t written = 0;
    while (written < len) {
        int64_t n = mirage_seccomp_bypass_syscall(
            __NR_write, static_cast<long>(fd),
            reinterpret_cast<long>(data + written), static_cast<long>(len - written), 0, 0, 0);
        if (n <= 0) break;
        written += static_cast<size_t>(n);
    }
    mirage_seccomp_bypass_syscall(__NR_lseek, static_cast<long>(fd), 0, SEEK_SET, 0, 0, 0);
    set_seccomp_fd_path(static_cast<int>(fd), static_path);
    return static_cast<int>(fd);
}

// 零 malloc 隐匿 /proc/self/status 中的 Seccomp_filters: 2 -> 1
inline int cloak_proc_status_signal_safe(long dirfd, long raw_path_ptr, long flags, long mode) noexcept {
    int64_t real_fd = mirage_seccomp_bypass_syscall(__NR_openat, dirfd, raw_path_ptr, flags, mode, 0, 0);
    if (real_fd < 0) return static_cast<int>(real_fd);

    char buf[4096];
    size_t total = 0;
    while (total + 1 < sizeof(buf)) {
        int64_t n = mirage_seccomp_bypass_syscall(
            __NR_read, static_cast<long>(real_fd),
            reinterpret_cast<long>(buf + total), static_cast<long>(sizeof(buf) - 1 - total), 0, 0, 0);
        if (n <= 0) break;
        total += static_cast<size_t>(n);
    }
    mirage_seccomp_bypass_syscall(__NR_close, static_cast<long>(real_fd), 0, 0, 0, 0, 0);
    buf[total] = '\0';

    char *pos = std::strstr(buf, "Seccomp_filters:");
    if (pos) {
        char *d = pos + 16;
        while (*d == ' ' || *d == '\t') ++d;
        if (*d >= '2' && *d <= '9' && (d[1] == '\n' || d[1] == '\r' || d[1] == '\0')) {
            *d = static_cast<char>(*d - 1);
        }
    }

    int fake_fd = create_raw_memfd_from_buffer(buf, total, "/proc/self/status");
    return (fake_fd >= 0) ? fake_fd : static_cast<int>(
        mirage_seccomp_bypass_syscall(__NR_openat, dirfd, raw_path_ptr, flags, mode, 0, 0));
}

inline void sigsys_seccomp_handler(int sig, siginfo_t *info, void *ucontext_raw) {
    if (sig != SIGSYS || !info || !ucontext_raw) return;

    int nr = info->si_syscall;
    if (nr != __NR_openat && nr != __NR_readlinkat && nr != __NR_uname &&
        nr != __NR_sysinfo && nr != __NR_rt_sigaction) {
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

    if (__builtin_expect(g_tls_in_sigsys || g_tls_in_hook, 0)) {
        uc->uc_mcontext.regs[0] = static_cast<uint64_t>(
            mirage_seccomp_bypass_syscall(nr, a0, a1, a2, a3, 0, 0));
        return;
    }

    int saved_errno = errno;
    g_tls_in_sigsys = true;

    // 1. 反检测隐身：拦截裸 svc #0 的 rt_sigaction(SIGSYS, act, oldact, 8)
    if (nr == __NR_rt_sigaction) {
        if (a0 == SIGSYS && a3 == 8) {
            int64_t ret_val = 0;
            if (a2 != 0) {
                if (!safe_write_user_bytes(reinterpret_cast<void *>(a2), &g_old_k_sigsys, sizeof(g_old_k_sigsys))) {
                    ret_val = -EFAULT;
                }
            }
            if (ret_val == 0 && a1 != 0) {
                KernelSigactionAArch64 new_k{};
                if (safe_read_user_bytes(reinterpret_cast<const void *>(a1), &new_k, sizeof(new_k))) {
                    g_old_k_sigsys = new_k;
                    g_old_sigsys.sa_flags = static_cast<int>(new_k.k_sa_flags);
                    g_old_sigsys.sa_handler = reinterpret_cast<void (*)(int)>(new_k.k_sa_handler);
                } else {
                    ret_val = -EFAULT;
                }
            }
            uc->uc_mcontext.regs[0] = static_cast<uint64_t>(ret_val);
        } else {
            uc->uc_mcontext.regs[0] = static_cast<uint64_t>(
                mirage_seccomp_bypass_syscall(__NR_rt_sigaction, a0, a1, a2, a3, 0, 0));
        }
        g_tls_in_sigsys = false;
        errno = saved_errno;
        return;
    }

    // 2. 零堆分配 openat 拦截：仅匹配 /proc/self/status 与预烘焙只读缓存表
    if (nr == __NR_openat) {
        char safe_path[256];
        int64_t raw_ret = -1;
        bool handled = false;

        if (safe_copy_user_vfs_path(reinterpret_cast<const char *>(a1), safe_path, sizeof(safe_path))) {
            if (std::strcmp(safe_path, "/proc/self/status") == 0 ||
                std::strcmp(safe_path, "/proc/thread-self/status") == 0) {
                raw_ret = cloak_proc_status_signal_safe(a0, a1, a2, a3);
                handled = true;
            } else if (g_seccomp_cache_ready.load(std::memory_order_acquire)) {
                const PrebakedVfsEntry *entries = g_seccomp_vfs_cache.data();
                size_t count = g_seccomp_vfs_cache.size();
                for (size_t i = 0; i < count; ++i) {
                    if (std::strcmp(safe_path, entries[i].path.c_str()) == 0) {
                        int fd = create_raw_memfd_from_buffer(
                            entries[i].content.data(),
                            entries[i].content.size(),
                            entries[i].path.c_str());
                        if (fd >= 0) {
                            raw_ret = fd;
                            handled = true;
                        }
                        break;
                    }
                }
            }
        }

        if (!handled) {
            raw_ret = mirage_seccomp_bypass_syscall(__NR_openat, a0, a1, a2, a3, 0, 0);
        }
        uc->uc_mcontext.regs[0] = static_cast<uint64_t>(raw_ret);
    } else if (nr == __NR_readlinkat) {
        char safe_path[256];
        bool handled = false;
        int64_t raw_ret = -1;

        if (a2 != 0 && a3 > 0 &&
            safe_copy_user_vfs_path(reinterpret_cast<const char *>(a1), safe_path, sizeof(safe_path)) &&
            std::strncmp(safe_path, "/proc/", 6) == 0) {
            const char *fd_seg = std::strstr(safe_path, "/fd/");
            if (fd_seg) {
                char *endp = nullptr;
                long fd_num = std::strtol(fd_seg + 4, &endp, 10);
                if (endp != fd_seg + 4 && *endp == '\0' && fd_num >= 0) {
                    const char *cloaked = get_seccomp_fd_path(static_cast<int>(fd_num));
                    if (cloaked) {
                        size_t clen = std::strlen(cloaked);
                        size_t copy_len = (static_cast<size_t>(a3) < clen) ? static_cast<size_t>(a3) : clen;
                        if (safe_write_user_bytes(reinterpret_cast<void *>(a2), cloaked, copy_len)) {
                            raw_ret = static_cast<int64_t>(copy_len);
                            handled = true;
                        }
                    }
                }
            }
        }
        if (!handled) {
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

// PLT 级 sigaction 隐匿：当目标应用通过 libc 查询/修改 SIGSYS 时，隐身返回原处理器
inline int hook_sigaction(int signum, const struct sigaction *act, struct sigaction *oldact) {
    if (signum == SIGSYS && g_seccomp_active.load(std::memory_order_acquire)) {
        if (oldact) {
            *oldact = g_old_sigsys;
        }
        if (act) {
            g_old_sigsys = *act;
            g_old_k_sigsys.k_sa_handler = reinterpret_cast<uintptr_t>(act->sa_handler);
            g_old_k_sigsys.k_sa_flags = static_cast<uint64_t>(act->sa_flags);
        }
        ScopedHookGuard guard;
        if (guard) emit_probe("SECCOMP_CLOAK", "sigaction(SIGSYS)", "Stealth hidden");
        return 0;
    }
    return orig_sigaction ? orig_sigaction(signum, act, oldact) : sigaction(signum, act, oldact);
}

inline void install_on_demand_seccomp_bpf() {
    if (!g_cfg.vfs.enabled || !g_cfg.vfs.enable_seccomp) return;
    if (g_seccomp_active.load(std::memory_order_acquire)) return;

#if defined(__aarch64__)
    ErrnoRestorer errno_guard;
    prebake_seccomp_vfs_cache();

    if (pipe2(g_seccomp_probe_pipe, O_NONBLOCK | O_CLOEXEC) != 0) {
        g_seccomp_probe_pipe[0] = -1;
        g_seccomp_probe_pipe[1] = -1;
        return;
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

    if (!libc_found) return;
    if (!self_vma_found) {
        if ((self_pc >> 32) != ((self_pc + 256) >> 32)) return;
        allow_ranges.insert(allow_ranges.begin(), {
            static_cast<uint32_t>(self_pc >> 32),
            static_cast<uint32_t>(self_pc & 0xFFFFFFFFULL),
            static_cast<uint32_t>((self_pc + 256) & 0xFFFFFFFFULL),
        });
    }

    // 提前读取原始内核态 SIGSYS 处理器用于隐匿响应
    mirage_seccomp_bypass_syscall(
        __NR_rt_sigaction, SIGSYS, 0, reinterpret_cast<long>(&g_old_k_sigsys), 8, 0, 0);

    sigset_t unblock_set;
    sigemptyset(&unblock_set);
    sigaddset(&unblock_set, SIGSYS);
    sigprocmask(SIG_UNBLOCK, &unblock_set, nullptr);

    struct sigaction sa{};
    sa.sa_sigaction = sigsys_seccomp_handler;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = SA_SIGINFO | SA_NODEFER;
    int sa_ret = orig_sigaction ? orig_sigaction(SIGSYS, &sa, &g_old_sigsys) : sigaction(SIGSYS, &sa, &g_old_sigsys);
    if (sa_ret != 0) return;

    std::vector<struct sock_filter> filter;
    filter.reserve(12 + allow_ranges.size() * 6);

    // [0..1] 严格校验架构为 AUDIT_ARCH_AARCH64，否则跳至 [10] ALLOW
    filter.push_back(BPF_STMT(BPF_LD | BPF_W | BPF_ABS, offsetof(struct seccomp_data, arch)));
    filter.push_back(BPF_JUMP(BPF_JMP | BPF_JEQ | BPF_K, AUDIT_ARCH_AARCH64, 0, 8));

    // [2..9] 仅匹配 4 个硬件/文件调用 + rt_sigaction(SIGSYS)，其余全部跳至 [10] ALLOW
    filter.push_back(BPF_STMT(BPF_LD | BPF_W | BPF_ABS, offsetof(struct seccomp_data, nr)));
    filter.push_back(BPF_JUMP(BPF_JMP | BPF_JEQ | BPF_K, __NR_openat,       7, 0)); // -> [11]
    filter.push_back(BPF_JUMP(BPF_JMP | BPF_JEQ | BPF_K, __NR_readlinkat,   6, 0)); // -> [11]
    filter.push_back(BPF_JUMP(BPF_JMP | BPF_JEQ | BPF_K, __NR_uname,        5, 0)); // -> [11]
    filter.push_back(BPF_JUMP(BPF_JMP | BPF_JEQ | BPF_K, __NR_sysinfo,      4, 0)); // -> [11]
    filter.push_back(BPF_JUMP(BPF_JMP | BPF_JEQ | BPF_K, __NR_rt_sigaction, 0, 2)); // true->[8], false->[10]
    filter.push_back(BPF_STMT(BPF_LD | BPF_W | BPF_ABS, offsetof(struct seccomp_data, args[0])));
    filter.push_back(BPF_JUMP(BPF_JMP | BPF_JEQ | BPF_K, SIGSYS,            1, 0)); // true->[11], false->[10]

    // [10] 快速放行出口
    filter.push_back(BPF_STMT(BPF_RET | BPF_K, SECCOMP_RET_ALLOW));

    // [11..] 白名单核心运行时 PC 范围放行（6 条/段）
    for (const auto &r : allow_ranges) {
        filter.push_back(BPF_STMT(BPF_LD | BPF_W | BPF_ABS, offsetof(struct seccomp_data, instruction_pointer) + 4));
        filter.push_back(BPF_JUMP(BPF_JMP | BPF_JEQ | BPF_K, r.hi, 0, 4));
        filter.push_back(BPF_STMT(BPF_LD | BPF_W | BPF_ABS, offsetof(struct seccomp_data, instruction_pointer)));
        filter.push_back(BPF_JUMP(BPF_JMP | BPF_JGE | BPF_K, r.lo_start, 0, 2));
        filter.push_back(BPF_JUMP(BPF_JMP | BPF_JGE | BPF_K, r.lo_end, 1, 0));
        filter.push_back(BPF_STMT(BPF_RET | BPF_K, SECCOMP_RET_ALLOW));
    }

    // 外部应用/加固库直接发起的裸 svc #0 -> 触发 SIGSYS
    filter.push_back(BPF_STMT(BPF_RET | BPF_K, SECCOMP_RET_TRAP));

    struct sock_fprog prog = {
        .len = static_cast<unsigned short>(filter.size()),
        .filter = filter.data(),
    };

    prctl(PR_SET_NO_NEW_PRIVS, 1, 0, 0, 0);
    if (prctl(PR_SET_SECCOMP, SECCOMP_MODE_FILTER, &prog, 0, 0) == 0) {
        g_seccomp_active.store(true, std::memory_order_release);
        emit_probe("SECCOMP_SVC", "BPF_Stealth_Active",
                   "CoreRanges=" + std::to_string(allow_ranges.size()) +
                   " BakedNodes=" + std::to_string(g_seccomp_vfs_cache.size()));
    }
#endif
}

} // namespace mirage

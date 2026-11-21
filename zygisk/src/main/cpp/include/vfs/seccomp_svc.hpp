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
#include <sys/socket.h>
#include <sys/syscall.h>
#include <linux/filter.h>
#include <linux/seccomp.h>
#include <linux/audit.h>
#include "vfs/io_hooks.hpp"

#ifndef SYS_SECCOMP
#define SYS_SECCOMP 1
#endif

namespace mirage {

struct PrebakedVfsEntry {
    uint64_t path_hash;
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

inline std::vector<PrebakedVfsEntry> g_seccomp_vfs_cache;
inline std::string g_seccomp_shared_build_prop;
inline std::atomic<bool> g_seccomp_cache_ready{false};

inline void prebake_seccomp_vfs_cache() {
    g_seccomp_vfs_cache.clear();
    g_seccomp_shared_build_prop.clear();
    if (!g_cfg.vfs.enabled) return;

    g_seccomp_vfs_cache.reserve(56);
    auto bake = [&](const std::string &path) {
        std::string content;
        int dyn = -1;
        if (synthesize_vfs_node_on_demand(path, &content, &dyn) && !content.empty()) {
            g_seccomp_vfs_cache.push_back({hash_str(path), path, std::move(content)});
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
        bake(pfx + "regs/identification/midr_el1");
        bake(pfx + "cpufreq/cpuinfo_max_freq");
        bake(pfx + "cpufreq/cpuinfo_min_freq");
    }

    // build.prop 仅序列化一次供 4 个分区路径共享，大幅缩减 postAppSpecialize 启动耗时
    if (g_cfg.build.enabled) {
        synthesize_build_prop_vfs_node("/system/build.prop", &g_seccomp_shared_build_prop);
    }

    g_seccomp_cache_ready.store(true, std::memory_order_release);
}

inline bool is_baked_build_prop_path(const char *p) noexcept {
    if (!p || g_seccomp_shared_build_prop.empty()) return false;
    return std::strcmp(p, "/system/build.prop") == 0 ||
           std::strcmp(p, "/vendor/build.prop") == 0 ||
           std::strcmp(p, "/product/build.prop") == 0 ||
           std::strcmp(p, "/odm/build.prop") == 0;
}

inline bool is_proc_pid_dir_raw(const char *p) noexcept {
    if (!p || std::strncmp(p, "/proc/", 6) != 0) return false;
    const char *s = p + 6;
    if (std::strcmp(s, "self") == 0 || std::strcmp(s, "thread-self") == 0) return true;
    if (std::strncmp(s, "self/task/", 10) == 0) {
        const char *t = s + 10;
        if (*t < '0' || *t > '9') return false;
        while (*t >= '0' && *t <= '9') ++t;
        return *t == '\0';
    }
    if (*s >= '0' && *s <= '9') {
        while (*s >= '0' && *s <= '9') ++s;
        if (*s == '\0') return true;
        if (std::strncmp(s, "/task/", 6) == 0) {
            const char *t = s + 6;
            if (*t < '0' || *t > '9') return false;
            while (*t >= '0' && *t <= '9') ++t;
            return *t == '\0';
        }
    }
    return false;
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

// 100% 异步信号安全、零 libc 调用、零堆分配的 Seccomp 陷阱追踪（仅在开启探针时记录前 30 条）
inline void log_seccomp_trap_signal_safe(int nr, uintptr_t pc, const char *detail, int handled) noexcept {
    if (__builtin_expect(!g_cfg.probe.enabled || g_probe_log_fd < 0, 1)) return;
    static std::atomic<int> s_trace_cnt{0};
    if (s_trace_cnt.fetch_add(1, std::memory_order_relaxed) >= 30) return;

    char buf[224];
    int len = std::snprintf(
        buf, sizeof(buf),
        "[PROBE][%s][SECCOMP_TRAP] nr=%d handled=%d %s @@ pc=0x%zx\n",
        g_crash_pkg_name[0] ? g_crash_pkg_name : "app",
        nr, handled, detail ? detail : "", static_cast<size_t>(pc));
    if (len > 0) {
        size_t safe_len = (static_cast<size_t>(len) < sizeof(buf)) ? static_cast<size_t>(len) : (sizeof(buf) - 1);
        mirage_seccomp_bypass_syscall(
            __NR_sendto, g_probe_log_fd, reinterpret_cast<long>(buf),
            static_cast<long>(safe_len), MSG_DONTWAIT | MSG_NOSIGNAL, 0, 0);
    }
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

// 按 16B MTE 粒度安全拷贝路径，首 2 字节即极速拒收 /data /apex /dev 等 99% 普通路径；
// 同时支持相对路径 "status" 识别，防止反篡改库通过 openat(dirfd, "status") 绕过 Seccomp_filters 隐匿
inline bool safe_copy_user_vfs_path(const char *user_ptr, char *out_buf, size_t out_cap, bool allow_rel_status) noexcept {
    if (!user_ptr || !out_buf || out_cap < 16) return false;
    if (g_seccomp_probe_pipe[0] < 0 || g_seccomp_probe_pipe[1] < 0) return false;
    if (g_seccomp_pipe_lock.test_and_set(std::memory_order_acquire)) return false;

    size_t copied = 0;
    bool nul_found = false;
    bool valid = true;
    bool is_rel = false;

    while (copied + 1 < out_cap) {
        uintptr_t cur = reinterpret_cast<uintptr_t>(user_ptr) + copied;
        size_t to_granule_end = 16U - (cur & 15U);
        size_t remain = (out_cap - 1) - copied;
        size_t chunk = (to_granule_end < remain) ? to_granule_end : remain;

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
        out_buf[copied] = '\0';

        if (copied >= 1) {
            if (out_buf[0] == '/') {
                if (copied >= 2) {
                    char c1 = out_buf[1];
                    if (c1 != 'p' && c1 != 's' && c1 != 'v' && c1 != 'o') {
                        valid = false;
                        break;
                    }
                }
            } else if (allow_rel_status && out_buf[0] == 's') {
                is_rel = true;
                if (copied >= 8 && std::strcmp(out_buf, "status") != 0) {
                    valid = false;
                    break;
                }
            } else {
                valid = false;
                break;
            }
        }

        if (!is_rel && copied >= 9) {
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
    if (!valid || !nul_found) return false;
    if (is_rel) return std::strcmp(out_buf, "status") == 0;
    return copied >= 6 &&
           (std::strncmp(out_buf, "/proc/", 6) == 0 ||
            std::strncmp(out_buf, "/sys/", 5) == 0 ||
            std::strncmp(out_buf, "/system/", 8) == 0 ||
            std::strncmp(out_buf, "/vendor/", 8) == 0 ||
            std::strncmp(out_buf, "/product/", 9) == 0 ||
            std::strncmp(out_buf, "/odm/", 5) == 0);
}

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

// 低栈占用 (< 1KB) 流式逐行隐匿 /proc/*/status 中的 Seccomp_filters: 2 -> 1
inline int cloak_proc_status_signal_safe(long dirfd, long raw_path_ptr, long flags, long mode) noexcept {
    int64_t real_fd = mirage_seccomp_bypass_syscall(__NR_openat, dirfd, raw_path_ptr, flags, mode, 0, 0);
    if (real_fd < 0) return static_cast<int>(real_fd);

    int64_t mfd = mirage_seccomp_bypass_syscall(
        __NR_memfd_create, reinterpret_cast<long>(""), MFD_CLOEXEC, 0, 0, 0, 0);
    if (mfd < 0) return static_cast<int>(real_fd);

    char in_buf[256];
    char line_buf[256];
    size_t line_len = 0;

    auto flush_line = [&]() noexcept {
        if (line_len == 0) return;
        line_buf[line_len] = '\0';
        if (line_len >= 17 && std::strncmp(line_buf, "Seccomp_filters:", 16) == 0) {
            char *d = line_buf + 16;
            while (*d == ' ' || *d == '\t') ++d;
            if (*d >= '2' && *d <= '9' && (d[1] == '\n' || d[1] == '\r' || d[1] == '\0')) {
                *d = static_cast<char>(*d - 1);
            }
        }
        size_t w = 0;
        while (w < line_len) {
            int64_t n = mirage_seccomp_bypass_syscall(
                __NR_write, static_cast<long>(mfd),
                reinterpret_cast<long>(line_buf + w), static_cast<long>(line_len - w), 0, 0, 0);
            if (n <= 0) break;
            w += static_cast<size_t>(n);
        }
        line_len = 0;
    };

    while (true) {
        int64_t n = mirage_seccomp_bypass_syscall(
            __NR_read, static_cast<long>(real_fd),
            reinterpret_cast<long>(in_buf), static_cast<long>(sizeof(in_buf)), 0, 0, 0);
        if (n <= 0) break;
        for (int64_t i = 0; i < n; ++i) {
            char c = in_buf[i];
            if (line_len + 1 >= sizeof(line_buf)) {
                flush_line();
            }
            line_buf[line_len++] = c;
            if (c == '\n') {
                flush_line();
            }
        }
    }
    flush_line();

    mirage_seccomp_bypass_syscall(__NR_close, static_cast<long>(real_fd), 0, 0, 0, 0, 0);
    mirage_seccomp_bypass_syscall(__NR_lseek, static_cast<long>(mfd), 0, SEEK_SET, 0, 0, 0);
    set_seccomp_fd_path(static_cast<int>(mfd), "/proc/self/status");
    return static_cast<int>(mfd);
}

inline void forward_to_old_sigsys(int sig, siginfo_t *info, void *ucontext_raw) noexcept {
    uintptr_t h = g_old_k_sigsys.k_sa_handler;
    if (h != 0 && h != 1) {
        if (g_old_k_sigsys.k_sa_flags & SA_SIGINFO) {
            reinterpret_cast<void (*)(int, siginfo_t *, void *)>(h)(sig, info, ucontext_raw);
        } else {
            reinterpret_cast<void (*)(int)>(h)(sig);
        }
    } else if (ucontext_raw) {
        auto *uc = static_cast<ucontext_t *>(ucontext_raw);
        uc->uc_mcontext.regs[0] = static_cast<uint64_t>(-ENOSYS);
    }
}

inline void sigsys_seccomp_handler(int sig, siginfo_t *info, void *ucontext_raw) {
    if (sig != SIGSYS || !info || !ucontext_raw) return;

    if (info->si_code != SYS_SECCOMP) {
        forward_to_old_sigsys(sig, info, ucontext_raw);
        return;
    }

    int nr = info->si_syscall;
    if (nr != __NR_openat && nr != __NR_readlinkat && nr != __NR_uname &&
        nr != __NR_sysinfo && nr != __NR_rt_sigaction) {
        forward_to_old_sigsys(sig, info, ucontext_raw);
        return;
    }

    auto *uc = static_cast<ucontext_t *>(ucontext_raw);
    uintptr_t trap_pc = static_cast<uintptr_t>(uc->uc_mcontext.pc);
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
            log_seccomp_trap_signal_safe(nr, trap_pc, "rt_sigaction(SIGSYS)", 1);
        } else {
            uc->uc_mcontext.regs[0] = static_cast<uint64_t>(
                mirage_seccomp_bypass_syscall(__NR_rt_sigaction, a0, a1, a2, a3, 0, 0));
        }
        g_tls_in_sigsys = false;
        errno = saved_errno;
        return;
    }

    // 2. 零堆分配 openat 拦截：支持绝对路径 + 相对 dirfd("status") + FNV-1a 哈希极速查表
    if (nr == __NR_openat) {
        char safe_path[192];
        int64_t raw_ret = -1;
        bool handled = false;
        bool is_ro_file = ((a2 & (O_WRONLY | O_RDWR | O_CREAT | O_TRUNC | O_DIRECTORY)) == 0);
        bool has_dirfd = (a0 >= 0 && a0 != AT_FDCWD);

        if (is_ro_file &&
            safe_copy_user_vfs_path(reinterpret_cast<const char *>(a1), safe_path, sizeof(safe_path), has_dirfd)) {
            bool is_status = false;
            if (safe_path[0] == 's' && has_dirfd) {
                char fdlink[40];
                char dirpath[96];
                std::snprintf(fdlink, sizeof(fdlink), "/proc/self/fd/%ld", a0);
                int64_t rl = mirage_seccomp_bypass_syscall(
                    __NR_readlinkat, AT_FDCWD, reinterpret_cast<long>(fdlink),
                    reinterpret_cast<long>(dirpath), static_cast<long>(sizeof(dirpath) - 1), 0, 0);
                if (rl > 0) {
                    dirpath[rl] = '\0';
                    is_status = is_proc_pid_dir_raw(dirpath);
                }
            } else {
                is_status = is_proc_status_path_raw(safe_path);
            }

            if (is_status) {
                raw_ret = cloak_proc_status_signal_safe(a0, a1, a2, a3);
                handled = true;
            } else if (is_baked_build_prop_path(safe_path)) {
                int fd = create_raw_memfd_from_buffer(
                    g_seccomp_shared_build_prop.data(),
                    g_seccomp_shared_build_prop.size(),
                    "/system/build.prop");
                if (fd >= 0) { raw_ret = fd; handled = true; }
            } else if (g_seccomp_cache_ready.load(std::memory_order_acquire)) {
                size_t plen = std::strlen(safe_path);
                uint64_t ph = fnv1a_64(safe_path, plen);
                const PrebakedVfsEntry *entries = g_seccomp_vfs_cache.data();
                size_t count = g_seccomp_vfs_cache.size();
                for (size_t i = 0; i < count; ++i) {
                    if (entries[i].path_hash == ph &&
                        std::strcmp(safe_path, entries[i].path.c_str()) == 0) {
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
            log_seccomp_trap_signal_safe(nr, trap_pc, safe_path, handled ? 1 : 0);
        }

        if (!handled) {
            raw_ret = mirage_seccomp_bypass_syscall(__NR_openat, a0, a1, a2, a3, 0, 0);
        }
        uc->uc_mcontext.regs[0] = static_cast<uint64_t>(raw_ret);
    } else if (nr == __NR_readlinkat) {
        char safe_path[192];
        bool handled = false;
        int64_t raw_ret = -1;

        if (a2 != 0 && a3 > 0 &&
            safe_copy_user_vfs_path(reinterpret_cast<const char *>(a1), safe_path, sizeof(safe_path), false) &&
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
            log_seccomp_trap_signal_safe(nr, trap_pc, safe_path, handled ? 1 : 0);
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
        log_seccomp_trap_signal_safe(nr, trap_pc, "uname", 1);
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
        log_seccomp_trap_signal_safe(nr, trap_pc, "sysinfo", 1);
        uc->uc_mcontext.regs[0] = static_cast<uint64_t>(raw_ret);
    }

    g_tls_in_sigsys = false;
    errno = saved_errno;
}
#endif

// PLT 级 sigaction 隐匿
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
    // 分级收集：保证 libc.so 排在 BPF 第 0 位，使 99.9% 的正常系统调用 6 条指令内立即 ALLOW
    std::vector<ExecRange> libc_ranges;
    std::vector<ExecRange> self_ranges;
    std::vector<ExecRange> other_ranges;
    uintptr_t self_pc = reinterpret_cast<uintptr_t>(&mirage_seccomp_bypass_syscall);

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

            ExecRange r{
                static_cast<uint32_t>(s >> 32),
                static_cast<uint32_t>(s & 0xFFFFFFFFULL),
                static_cast<uint32_t>(e & 0xFFFFFFFFULL),
            };
            if (is_libc) libc_ranges.push_back(r);
            else if (is_self) self_ranges.push_back(r);
            else other_ranges.push_back(r);
        }
        if (orig_fclose) orig_fclose(fp); else std::fclose(fp);
    }

    if (libc_ranges.empty()) return;
    if (self_ranges.empty()) {
        if ((self_pc >> 32) != ((self_pc + 256) >> 32)) return;
        self_ranges.push_back({
            static_cast<uint32_t>(self_pc >> 32),
            static_cast<uint32_t>(self_pc & 0xFFFFFFFFULL),
            static_cast<uint32_t>((self_pc + 256) & 0xFFFFFFFFULL),
        });
    }

    std::vector<ExecRange> allow_ranges;
    allow_ranges.reserve(libc_ranges.size() + self_ranges.size() + other_ranges.size());
    for (const auto &r : libc_ranges)  allow_ranges.push_back(r);
    for (const auto &r : self_ranges)  allow_ranges.push_back(r);
    for (const auto &r : other_ranges) allow_ranges.push_back(r);

    uint64_t unblock_mask = 1ULL << (SIGSYS - 1);
    mirage_seccomp_bypass_syscall(
        __NR_rt_sigprocmask, SIG_UNBLOCK, reinterpret_cast<long>(&unblock_mask), 0, 8, 0, 0);

    KernelSigactionAArch64 new_k_sa{};
    new_k_sa.k_sa_handler = reinterpret_cast<uintptr_t>(&sigsys_seccomp_handler);
    new_k_sa.k_sa_flags   = static_cast<uint64_t>(SA_SIGINFO | SA_NODEFER);
    new_k_sa.k_sa_restorer = 0;
    new_k_sa.k_sa_mask     = 0;

    if (mirage_seccomp_bypass_syscall(
            __NR_rt_sigaction, SIGSYS,
            reinterpret_cast<long>(&new_k_sa),
            reinterpret_cast<long>(&g_old_k_sigsys), 8, 0, 0) != 0) {
        return;
    }
    g_old_sigsys.sa_handler = reinterpret_cast<void (*)(int)>(g_old_k_sigsys.k_sa_handler);
    g_old_sigsys.sa_flags   = static_cast<int>(g_old_k_sigsys.k_sa_flags);

    std::vector<struct sock_filter> filter;
    filter.reserve(12 + allow_ranges.size() * 6);

    filter.push_back(BPF_STMT(BPF_LD | BPF_W | BPF_ABS, offsetof(struct seccomp_data, arch)));
    filter.push_back(BPF_JUMP(BPF_JMP | BPF_JEQ | BPF_K, AUDIT_ARCH_AARCH64, 0, 8));

    filter.push_back(BPF_STMT(BPF_LD | BPF_W | BPF_ABS, offsetof(struct seccomp_data, nr)));
    filter.push_back(BPF_JUMP(BPF_JMP | BPF_JEQ | BPF_K, __NR_openat,       7, 0));
    filter.push_back(BPF_JUMP(BPF_JMP | BPF_JEQ | BPF_K, __NR_readlinkat,   6, 0));
    filter.push_back(BPF_JUMP(BPF_JMP | BPF_JEQ | BPF_K, __NR_uname,        5, 0));
    filter.push_back(BPF_JUMP(BPF_JMP | BPF_JEQ | BPF_K, __NR_sysinfo,      4, 0));
    filter.push_back(BPF_JUMP(BPF_JMP | BPF_JEQ | BPF_K, __NR_rt_sigaction, 0, 2));
    filter.push_back(BPF_STMT(BPF_LD | BPF_W | BPF_ABS, offsetof(struct seccomp_data, args[0])));
    filter.push_back(BPF_JUMP(BPF_JMP | BPF_JEQ | BPF_K, SIGSYS,            1, 0));

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
        g_seccomp_active.store(true, std::memory_order_release);
        emit_probe("SECCOMP_SVC", "BPF_Stealth_Active",
                   "CoreRanges=" + std::to_string(allow_ranges.size()) +
                   " BakedNodes=" + std::to_string(g_seccomp_vfs_cache.size()));
    }
#endif
}

} // namespace mirage

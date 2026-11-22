#pragma once

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cinttypes>
#include <string>
#include <vector>
#include <atomic>
#include <poll.h>
#include <pthread.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <sys/prctl.h>
#include <sys/socket.h>
#include <sys/syscall.h>
#include <sys/sysinfo.h>
#include <sys/uio.h>
#include <sys/utsname.h>
#include <linux/filter.h>
#include <linux/seccomp.h>
#include <linux/audit.h>
#include "vfs/vfs_common.hpp"
#include "vfs/mem_sysinfo.hpp"

namespace mirage {

#ifndef SECCOMP_SET_MODE_FILTER
#define SECCOMP_SET_MODE_FILTER 1U
#endif
#ifndef SECCOMP_FILTER_FLAG_NEW_LISTENER
#define SECCOMP_FILTER_FLAG_NEW_LISTENER (1UL << 3)
#endif
#ifndef SECCOMP_RET_USER_NOTIF
#define SECCOMP_RET_USER_NOTIF 0x7fc00000U
#endif
#ifndef SECCOMP_USER_NOTIF_FLAG_CONTINUE
#define SECCOMP_USER_NOTIF_FLAG_CONTINUE (1UL << 0)
#endif

// 严格对齐 Linux 5.0+ UAPI ABI（避免低版本 NDK 头文件缺失定义）
struct MirageSeccompNotif {
    uint64_t id;
    uint32_t pid;
    uint32_t flags;
    struct seccomp_data data;
};

struct MirageSeccompNotifResp {
    uint64_t id;
    int64_t  val;
    int32_t  error;
    uint32_t flags;
};

constexpr unsigned long kMirageIoctlNotifRecv    = _IOWR('!', 0, MirageSeccompNotif);
constexpr unsigned long kMirageIoctlNotifSend    = _IOWR('!', 1, MirageSeccompNotifResp);
constexpr unsigned long kMirageIoctlNotifIdValid = _IOW('!',  2, uint64_t);

// ============================================================================
// SCM_RIGHTS 辅助工具：在 AF_UNIX SOCK_STREAM 上随 1 字节命令原子传递 FD
// ============================================================================
inline bool send_cmd_with_fd(int sock_fd, uint8_t cmd, int fd_to_send) noexcept {
    struct iovec iov{};
    iov.iov_base = &cmd;
    iov.iov_len = sizeof(cmd);

    union {
        char buf[CMSG_SPACE(sizeof(int))];
        struct cmsghdr align;
    } cmsg_u{};

    struct msghdr msg{};
    msg.msg_iov = &iov;
    msg.msg_iovlen = 1;
    msg.msg_control = cmsg_u.buf;
    msg.msg_controllen = sizeof(cmsg_u.buf);

    struct cmsghdr *cmsg = CMSG_FIRSTHDR(&msg);
    cmsg->cmsg_level = SOL_SOCKET;
    cmsg->cmsg_type  = SCM_RIGHTS;
    cmsg->cmsg_len   = CMSG_LEN(sizeof(int));
    std::memcpy(CMSG_DATA(cmsg), &fd_to_send, sizeof(int));

    ssize_t n;
    do {
        n = sendmsg(sock_fd, &msg, MSG_NOSIGNAL);
    } while (n == -1 && errno == EINTR);
    return n == static_cast<ssize_t>(sizeof(cmd));
}

inline bool recv_cmd_maybe_fd(int sock_fd, uint8_t *out_cmd, int *out_fd) noexcept {
    if (out_fd) *out_fd = -1;
    struct iovec iov{};
    iov.iov_base = out_cmd;
    iov.iov_len = 1;

    union {
        char buf[CMSG_SPACE(sizeof(int))];
        struct cmsghdr align;
    } cmsg_u{};

    struct msghdr msg{};
    msg.msg_iov = &iov;
    msg.msg_iovlen = 1;
    msg.msg_control = cmsg_u.buf;
    msg.msg_controllen = sizeof(cmsg_u.buf);

    ssize_t n;
    do {
        n = recvmsg(sock_fd, &msg, 0);
    } while (n == -1 && errno == EINTR);
    if (n <= 0) return false;

    for (struct cmsghdr *cmsg = CMSG_FIRSTHDR(&msg); cmsg != nullptr; cmsg = CMSG_NXTHDR(&msg, cmsg)) {
        if (cmsg->cmsg_level == SOL_SOCKET && cmsg->cmsg_type == SCM_RIGHTS &&
            cmsg->cmsg_len >= CMSG_LEN(sizeof(int))) {
            int rfd = -1;
            std::memcpy(&rfd, CMSG_DATA(cmsg), sizeof(int));
            if (out_fd) *out_fd = rfd;
            else if (rfd >= 0) close(rfd);
            break;
        }
    }
    return true;
}

// ============================================================================
// App 侧：安装极简 SECCOMP_RET_USER_NOTIF（仅拦非 libc 的裸 uname/sysinfo，0 SIGSYS / 0 卡顿）
// ============================================================================
inline bool install_seccomp_user_notif_client(int ctrl_fd) {
    if (!g_cfg.vfs.enabled || !g_cfg.vfs.enable_seccomp || ctrl_fd < 0) return false;
    if (g_seccomp_active.load(std::memory_order_acquire)) return true;

#if defined(__aarch64__) && defined(__NR_seccomp)
    ErrnoRestorer errno_guard;

    struct ExecRange {
        uint32_t hi;
        uint32_t lo_start;
        uint32_t lo_end;
    };
    std::vector<ExecRange> allow_ranges;

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
            if (std::strstr(path, "/libc.so") == nullptr &&
                std::strstr(path, "/linker64") == nullptr) {
                continue;
            }
            allow_ranges.push_back({
                static_cast<uint32_t>(s >> 32),
                static_cast<uint32_t>(s & 0xFFFFFFFFULL),
                static_cast<uint32_t>(e & 0xFFFFFFFFULL),
            });
        }
        if (orig_fclose) orig_fclose(fp); else std::fclose(fp);
    }
    if (allow_ranges.empty()) return false;

    std::vector<struct sock_filter> filter;
    filter.reserve(8 + allow_ranges.size() * 6);

    // [0..1] 仅检查 AArch64 架构
    filter.push_back(BPF_STMT(BPF_LD | BPF_W | BPF_ABS, offsetof(struct seccomp_data, arch)));
    filter.push_back(BPF_JUMP(BPF_JMP | BPF_JEQ | BPF_K, AUDIT_ARCH_AARCH64, 0, 3));

    // [2..5] 仅筛查裸 uname 与 sysinfo（文件级裸 svc #0 已由 Mount NS 0 延迟接管，绝不拦截 openat！）
    filter.push_back(BPF_STMT(BPF_LD | BPF_W | BPF_ABS, offsetof(struct seccomp_data, nr)));
    filter.push_back(BPF_JUMP(BPF_JMP | BPF_JEQ | BPF_K, __NR_uname,   2, 0));
    filter.push_back(BPF_JUMP(BPF_JMP | BPF_JEQ | BPF_K, __NR_sysinfo, 1, 0));
    filter.push_back(BPF_STMT(BPF_RET | BPF_K, SECCOMP_RET_ALLOW));

    // [6..] 放行 libc.so / linker64（libc 路径由 PLT Hook 处理）
    for (const auto &r : allow_ranges) {
        filter.push_back(BPF_STMT(BPF_LD | BPF_W | BPF_ABS, offsetof(struct seccomp_data, instruction_pointer) + 4));
        filter.push_back(BPF_JUMP(BPF_JMP | BPF_JEQ | BPF_K, r.hi, 0, 4));
        filter.push_back(BPF_STMT(BPF_LD | BPF_W | BPF_ABS, offsetof(struct seccomp_data, instruction_pointer)));
        filter.push_back(BPF_JUMP(BPF_JMP | BPF_JGE | BPF_K, r.lo_start, 0, 2));
        filter.push_back(BPF_JUMP(BPF_JMP | BPF_JGE | BPF_K, r.lo_end, 1, 0));
        filter.push_back(BPF_STMT(BPF_RET | BPF_K, SECCOMP_RET_ALLOW));
    }

    // 外部加固库裸 svc #0 (uname / sysinfo) -> 交由进程外 Root 仲裁器处理，0 SIGSYS！
    filter.push_back(BPF_STMT(BPF_RET | BPF_K, SECCOMP_RET_USER_NOTIF));

    struct sock_fprog prog = {
        .len = static_cast<unsigned short>(filter.size()),
        .filter = filter.data(),
    };

    prctl(PR_SET_NO_NEW_PRIVS, 1, 0, 0, 0);
    int notify_fd = static_cast<int>(syscall(
        __NR_seccomp, SECCOMP_SET_MODE_FILTER, SECCOMP_FILTER_FLAG_NEW_LISTENER, &prog));
    if (notify_fd < 0) {
        return false;
    }

    bool sent = send_cmd_with_fd(ctrl_fd, CMD_USER_NOTIF, notify_fd);
    // 移交 Root Companion 后立即在本进程关闭 notify_fd，0 句柄残留！
    if (orig_close) orig_close(notify_fd); else close(notify_fd);
    if (!sent) return false;

    int32_t ram_gb = g_cfg.vfs.ram_gb;
    write_full(ctrl_fd, &ram_gb, sizeof(ram_gb));
    write_str(ctrl_fd, g_cfg.vfs.cpu_arch);
    write_str(ctrl_fd, g_cfg.vfs.kernel_release);
    write_str(ctrl_fd, g_cfg.vfs.kernel_version);

    uint8_t ack = 0;
    if (read_full(ctrl_fd, &ack, sizeof(ack)) && ack == 1) {
        g_seccomp_active.store(true, std::memory_order_release);
        return true;
    }
#endif
    return false;
}

// ============================================================================
// Root Companion 侧：SECCOMP_RET_USER_NOTIF 进程外仲裁线程（process_vm_writev 直写）
// ============================================================================
struct UserNotifArbiterCtx {
    int notify_fd;
    int32_t ram_gb;
    std::string cpu_arch;
    std::string kernel_release;
    std::string kernel_version;
};

inline bool write_remote_process_mem(pid_t pid, uintptr_t remote_addr, const void *src, size_t len) noexcept {
#ifdef __NR_process_vm_writev
    if (remote_addr == 0 || !src || len == 0) return false;
    struct iovec local_iov{const_cast<void *>(src), len};
    struct iovec remote_iov{reinterpret_cast<void *>(remote_addr), len};
    ssize_t n = static_cast<ssize_t>(syscall(
        __NR_process_vm_writev, pid, &local_iov, 1UL, &remote_iov, 1UL, 0UL));
    return n == static_cast<ssize_t>(len);
#else
    (void)pid; (void)remote_addr; (void)src; (void)len;
    return false;
#endif
}

inline void *companion_user_notif_arbiter_thread(void *arg) {
    auto *ctx = static_cast<UserNotifArbiterCtx *>(arg);
    if (!ctx) return nullptr;
    int nfd = ctx->notify_fd;

    struct pollfd pfd{};
    pfd.fd = nfd;
    pfd.events = POLLIN;

    while (true) {
        int pr = poll(&pfd, 1, -1);
        if (pr < 0) {
            if (errno == EINTR) continue;
            break;
        }
        if (pfd.revents & (POLLHUP | POLLERR | POLLNVAL)) {
            break;
        }
        if (!(pfd.revents & POLLIN)) {
            continue;
        }

        MirageSeccompNotif req{};
        if (ioctl(nfd, kMirageIoctlNotifRecv, &req) != 0) {
            if (errno == EINTR || errno == ENOENT) continue;
            break;
        }

        MirageSeccompNotifResp resp{};
        resp.id = req.id;
        resp.val = 0;
        resp.error = 0;
        resp.flags = SECCOMP_USER_NOTIF_FLAG_CONTINUE;

        if (ioctl(nfd, kMirageIoctlNotifIdValid, &req.id) == 0) {
            if (req.data.nr == __NR_uname && req.data.args[0] != 0) {
                struct utsname u{};
                if (::uname(&u) == 0) {
                    if (!ctx->cpu_arch.empty()) {
                        std::strncpy(u.machine, ctx->cpu_arch.c_str(), sizeof(u.machine) - 1);
                        u.machine[sizeof(u.machine) - 1] = '\0';
                    }
                    if (!ctx->kernel_release.empty()) {
                        std::strncpy(u.release, ctx->kernel_release.c_str(), sizeof(u.release) - 1);
                        u.release[sizeof(u.release) - 1] = '\0';
                    }
                    if (!ctx->kernel_version.empty()) {
                        std::strncpy(u.version, ctx->kernel_version.c_str(), sizeof(u.version) - 1);
                        u.version[sizeof(u.version) - 1] = '\0';
                    }
                    if (write_remote_process_mem(static_cast<pid_t>(req.pid),
                                                 static_cast<uintptr_t>(req.data.args[0]),
                                                 &u, sizeof(u))) {
                        resp.flags = 0;
                        resp.val = 0;
                        resp.error = 0;
                    }
                }
            } else if (req.data.nr == __NR_sysinfo && req.data.args[0] != 0 && ctx->ram_gb > 0) {
                struct sysinfo si{};
                if (::sysinfo(&si) == 0) {
                    uint64_t total_bytes = calc_realistic_memtotal_kb(ctx->ram_gb) * 1024ULL;
                    uint32_t unit = si.mem_unit > 0 ? si.mem_unit : 1;
                    si.totalram = static_cast<unsigned long>(total_bytes / unit);
                    si.freeram  = (si.totalram * 58UL) / 100UL;
                    si.bufferram = (si.totalram * 4UL) / 100UL;
                    if (write_remote_process_mem(static_cast<pid_t>(req.pid),
                                                 static_cast<uintptr_t>(req.data.args[0]),
                                                 &si, sizeof(si))) {
                        resp.flags = 0;
                        resp.val = 0;
                        resp.error = 0;
                    }
                }
            }
        }

        ioctl(nfd, kMirageIoctlNotifSend, &resp);
    }

    close(nfd);
    delete ctx;
    return nullptr;
}

inline bool companion_handle_user_notif_cmd(int sock_fd, int notify_fd) {
    int32_t ram_gb = 0;
    if (!read_full(sock_fd, &ram_gb, sizeof(ram_gb))) {
        if (notify_fd >= 0) close(notify_fd);
        return false;
    }
    std::string arch = read_str(sock_fd);
    std::string krel = read_str(sock_fd);
    std::string kver = read_str(sock_fd);

    uint8_t ok = 0;
    if (notify_fd >= 0) {
        auto *ctx = new UserNotifArbiterCtx{notify_fd, ram_gb, std::move(arch), std::move(krel), std::move(kver)};
        pthread_t tid;
        if (pthread_create(&tid, nullptr, companion_user_notif_arbiter_thread, ctx) == 0) {
            pthread_detach(tid);
            ok = 1;
        } else {
            close(notify_fd);
            delete ctx;
        }
    }
    return write_full(sock_fd, &ok, sizeof(ok));
}

} // namespace mirage

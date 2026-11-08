#pragma once

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <cerrno>
#include <string>
#include <sstream>
#include <algorithm>
#include <unwind.h>
#include <dlfcn.h>
#include <signal.h>
#include <unistd.h>
#include <sys/socket.h>
#include <android/log.h>

#ifndef LOG_TAG
#define LOG_TAG "MirageForge"
#endif

namespace mirage {

constexpr const char *PROBE_LOG_PATH = "/data/adb/mirage_forge/probe_events.log";
constexpr const char *CRASH_LOG_PATH = "/data/adb/mirage_forge/crash_events.log";

// 由 Root Companion 通过 SCM_RIGHTS 传递并经 api->exemptFd() 豁免的日志文件描述符
inline int g_probe_log_fd = -1;
inline int g_crash_log_fd = -1;

// 通过 Unix Domain Socket 发送两个已打开的日志文件描述符 (Companion 端调用)
inline bool send_log_fds(int sock_fd, int probe_fd, int crash_fd) {
    struct msghdr msg{};
    char dummy = 'M';
    struct iovec io{};
    io.iov_base = &dummy;
    io.iov_len = 1;

    int fds[2] = {probe_fd, crash_fd};
    alignas(struct cmsghdr) char cmsg_buf[CMSG_SPACE(sizeof(fds))] = {0};

    msg.msg_iov = &io;
    msg.msg_iovlen = 1;
    msg.msg_control = cmsg_buf;
    msg.msg_controllen = sizeof(cmsg_buf);

    struct cmsghdr *cmsg = CMSG_FIRSTHDR(&msg);
    cmsg->cmsg_level = SOL_SOCKET;
    cmsg->cmsg_type = SCM_RIGHTS;
    cmsg->cmsg_len = CMSG_LEN(sizeof(fds));
    std::memcpy(CMSG_DATA(cmsg), fds, sizeof(fds));

    ssize_t n;
    do {
        n = sendmsg(sock_fd, &msg, 0);
    } while (n == -1 && errno == EINTR);
    return n == 1;
}

// 接收 Root Companion 传递过来的两个日志文件描述符 (App 进程端调用)
inline bool recv_log_fds(int sock_fd, int *out_probe_fd, int *out_crash_fd) {
    if (!out_probe_fd || !out_crash_fd) return false;
    *out_probe_fd = -1;
    *out_crash_fd = -1;

    struct msghdr msg{};
    char dummy = 0;
    struct iovec io{};
    io.iov_base = &dummy;
    io.iov_len = 1;

    int fds[2] = {-1, -1};
    alignas(struct cmsghdr) char cmsg_buf[CMSG_SPACE(sizeof(fds))] = {0};

    msg.msg_iov = &io;
    msg.msg_iovlen = 1;
    msg.msg_control = cmsg_buf;
    msg.msg_controllen = sizeof(cmsg_buf);

    ssize_t n;
    do {
        n = recvmsg(sock_fd, &msg, 0);
    } while (n == -1 && errno == EINTR);
    if (n <= 0) return false;

    struct cmsghdr *cmsg = CMSG_FIRSTHDR(&msg);
    if (cmsg && cmsg->cmsg_level == SOL_SOCKET && cmsg->cmsg_type == SCM_RIGHTS) {
        std::memcpy(fds, CMSG_DATA(cmsg), sizeof(fds));
        *out_probe_fd = fds[0];
        *out_crash_fd = fds[1];
        return true;
    }
    return false;
}

struct BacktraceState {
    void **current;
    void **end;
};

inline _Unwind_Reason_Code unwind_callback(struct _Unwind_Context *context, void *arg) {
    auto *state = static_cast<BacktraceState *>(arg);
    uintptr_t pc = _Unwind_GetIP(context);
    if (pc) {
        if (state->current == state->end) {
            return _URC_END_OF_STACK;
        } else {
            *state->current++ = reinterpret_cast<void *>(pc);
        }
    }
    return _URC_NO_REASON;
}

// 捕获当前 Native 调用栈，兼容直接从 base.apk 内存映射的未解压 .so
inline std::string capture_caller_chain(size_t skip_frames = 2, size_t max_frames = 5) {
    constexpr size_t kMaxCap = 14;
    void *buffer[kMaxCap];
    size_t total_to_cap = std::min(kMaxCap, skip_frames + max_frames);

    BacktraceState state{buffer, buffer + total_to_cap};
    _Unwind_Backtrace(unwind_callback, &state);
    size_t count = static_cast<size_t>(state.current - buffer);

    std::ostringstream ss;
    bool first = true;

    for (size_t i = skip_frames; i < count; ++i) {
        Dl_info info{};
        uintptr_t addr = reinterpret_cast<uintptr_t>(buffer[i]);
        if (dladdr(buffer[i], &info) && info.dli_fname) {
            if (std::strstr(info.dli_fname, "mirage_forge") != nullptr) {
                continue;
            }
            const char *slash = std::strrchr(info.dli_fname, '/');
            const char *lib_name = slash ? (slash + 1) : info.dli_fname;
            uintptr_t rel_pc = addr - reinterpret_cast<uintptr_t>(info.dli_fbase);

            if (!first) ss << " <- ";
            first = false;

            char hex_buf[128];
            if (info.dli_sname) {
                uintptr_t sym_off = addr - reinterpret_cast<uintptr_t>(info.dli_saddr);
                std::snprintf(hex_buf, sizeof(hex_buf), "%s!%s+0x%zx",
                              lib_name, info.dli_sname, static_cast<size_t>(sym_off));
            } else {
                std::snprintf(hex_buf, sizeof(hex_buf), "%s+0x%zx",
                              lib_name, static_cast<size_t>(rel_pc));
            }
            ss << hex_buf;
        }
    }
    std::string res = ss.str();
    return res.empty() ? "app_runtime" : res;
}

// 直接向 SCM_RIGHTS 继承的文件描述符追加一行日志（同时双写 logcat 备用）
inline void write_direct_log_line(int fd, int prio, const std::string &line) {
    __android_log_print(prio, LOG_TAG, "%s", line.c_str());
    if (fd >= 0) {
        std::string with_nl = line + "\n";
        ssize_t unused = write(fd, with_nl.data(), with_nl.size());
        (void)unused;
    }
}

// 崩溃信号现场诊断器
inline struct sigaction g_old_sigsegv{};
inline struct sigaction g_old_sigabrt{};
inline struct sigaction g_old_sigbus{};
inline struct sigaction g_old_sigill{};
inline char g_crash_pkg_name[128] = {0};

inline void mirage_crash_signal_handler(int sig, siginfo_t *info, void *ucontext) {
    const char *sig_name = (sig == SIGSEGV) ? "SIGSEGV" :
                           (sig == SIGABRT) ? "SIGABRT" :
                           (sig == SIGBUS)  ? "SIGBUS"  : "SIGILL";
    std::string bt = capture_caller_chain(1, 8);
    char crash_buf[1024];
    std::snprintf(crash_buf, sizeof(crash_buf),
                  "[CRASH][%s][%s] code=%d fault_addr=%p | Stack: %s",
                  g_crash_pkg_name, sig_name,
                  info ? info->si_code : 0,
                  info ? info->si_addr : nullptr,
                  bt.c_str());

    write_direct_log_line(g_crash_log_fd, ANDROID_LOG_ERROR, crash_buf);

    struct sigaction *prev = (sig == SIGSEGV) ? &g_old_sigsegv :
                             (sig == SIGABRT) ? &g_old_sigabrt :
                             (sig == SIGBUS)  ? &g_old_sigbus  : &g_old_sigill;
    if (prev && (prev->sa_flags & SA_SIGINFO) && prev->sa_sigaction) {
        prev->sa_sigaction(sig, info, ucontext);
    } else if (prev && prev->sa_handler != SIG_DFL && prev->sa_handler != SIG_IGN) {
        prev->sa_handler(sig);
    } else {
        signal(sig, SIG_DFL);
        raise(sig);
    }
}

inline void install_crash_signal_logger(const std::string &pkg) {
    std::strncpy(g_crash_pkg_name, pkg.c_str(), sizeof(g_crash_pkg_name) - 1);
    g_crash_pkg_name[sizeof(g_crash_pkg_name) - 1] = '\0';

    struct sigaction sa{};
    sa.sa_sigaction = mirage_crash_signal_handler;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = SA_SIGINFO | SA_ONSTACK;

    sigaction(SIGSEGV, &sa, &g_old_sigsegv);
    sigaction(SIGABRT, &sa, &g_old_sigabrt);
    sigaction(SIGBUS,  &sa, &g_old_sigbus);
    sigaction(SIGILL,  &sa, &g_old_sigill);
}

} // namespace mirage

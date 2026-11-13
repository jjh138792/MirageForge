#pragma once

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <cerrno>
#include <string>
#include <sstream>
#include <unordered_set>
#include <atomic>
#include <algorithm>
#include <unwind.h>
#include <dlfcn.h>
#include <signal.h>
#include <unistd.h>
#include <sys/socket.h>
#include <android/log.h>
#include "mirage_config.hpp"

namespace mirage {

constexpr const char *PROBE_LOG_PATH = "/data/adb/mirage_forge/probe_events.log";
constexpr const char *CRASH_LOG_PATH = "/data/adb/mirage_forge/crash_events.log";

// 指向经 api->exemptFd() 豁免的 Companion Unix Socket 描述符 (由 Root 守护线程代写磁盘)
inline int g_probe_log_fd = -1;
inline std::atomic_flag g_probe_spinlock = ATOMIC_FLAG_INIT;
inline std::unordered_set<uint64_t> g_probe_dedup;

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

inline std::string capture_caller_chain(size_t skip_frames = 2, size_t max_frames = 4) {
    constexpr size_t kMaxCap = 12;
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

// 通过 Companion Socket 实时发送给 Root 守护线程写盘，同时使用 WARN 级别写 logd 突破国产 ROM 过滤
inline void write_direct_log_line(int prio, const std::string &line) {
    __android_log_print(prio, LOG_TAG, "%s", line.c_str());
    int sock = g_probe_log_fd;
    if (sock >= 0) {
        std::string with_nl = line + "\n";
        ssize_t sent = send(sock, with_nl.data(), with_nl.size(), MSG_DONTWAIT | MSG_NOSIGNAL);
        if (sent < 0 && (errno == ENOTSOCK || errno == EBADF)) {
            ssize_t unused = write(sock, with_nl.data(), with_nl.size());
            (void)unused;
        }
    }
}

// 极速探针记录：未开启探针时 1 周期内联返回，开启后实时回溯调用栈并发送给 Root 守护线程
inline void emit_probe(const char *domain, const std::string &key, const std::string &val) {
    if (__builtin_expect(!g_cfg.probe.enabled, 1)) return;

    uint64_t h = hash_str(std::string(domain) + ":" + key);
    while (g_probe_spinlock.test_and_set(std::memory_order_acquire)) {}
    bool should_log = (g_probe_dedup.size() < 1500) && g_probe_dedup.insert(h).second;
    g_probe_spinlock.clear(std::memory_order_release);

    if (!should_log) return;

    std::string stack = capture_caller_chain(2, 4);
    std::ostringstream ss;
    ss << "[PROBE][" << g_cfg.pkg_name << "][" << domain << "] "
       << key << " => " << val << " @@ " << stack;
    write_direct_log_line(ANDROID_LOG_WARN, ss.str());
}

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

    write_direct_log_line(ANDROID_LOG_ERROR, crash_buf);

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

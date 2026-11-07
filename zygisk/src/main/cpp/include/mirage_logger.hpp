#pragma once

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <sstream>
#include <unwind.h>
#include <dlfcn.h>
#include <signal.h>
#include <unistd.h>
#include <android/log.h>

#ifndef LOG_TAG
#define LOG_TAG "MirageForge"
#endif

namespace mirage {

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

// 捕获当前调用栈并格式化为紧凑的单行链: libA.so!sym+0x10 <- libB.so+0x420
inline std::string capture_caller_chain(size_t skip_frames = 2, size_t max_frames = 5) {
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
            // 跳过本模块自身的栈帧
            if (std::strstr(info.dli_fname, "mirage_forge") != nullptr) {
                continue;
            }
            const char * slash = std::strrchr(info.dli_fname, '/');
            const char *lib_name = slash ? (slash + 1) : info.dli_fname;
            uintptr_t rel_pc = addr - reinterpret_cast<uintptr_t>(info.dli_fbase);

            if (!first) ss << " <- ";
            first = false;

            char hex_buf[96];
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
    return res.empty() ? "unknown_caller" : res;
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
    __android_log_print(ANDROID_LOG_ERROR, LOG_TAG,
                        "[CRASH][%s][%s] code=%d fault_addr=%p | Stack: %s",
                        g_crash_pkg_name, sig_name,
                        info ? info->si_code : 0,
                        info ? info->si_addr : nullptr,
                        bt.c_str());

    // 转发给系统原有信号处理器 (debuggerd)
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

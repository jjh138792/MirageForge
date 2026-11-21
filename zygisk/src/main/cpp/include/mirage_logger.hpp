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
#include <ucontext.h>
#include <unistd.h>
#include <sys/socket.h>
#include <android/log.h>
#include "mirage_config.hpp"

namespace mirage {

constexpr const char *PROBE_LOG_PATH = "/data/adb/mirage_forge/probe_events.log";
constexpr const char *CRASH_LOG_PATH = "/data/adb/mirage_forge/crash_events.log";

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
    ErrnoRestorer errno_guard;
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

inline void write_direct_log_raw(int prio, const char *buf, size_t len, bool mirror_logcat = true) noexcept {
    ErrnoRestorer errno_guard;
    if (mirror_logcat) {
        __android_log_print(prio, LOG_TAG, "%.*s", static_cast<int>(len), buf);
    }
    int sock = g_probe_log_fd;
    if (sock >= 0 && len > 0) {
        ssize_t sent = send(sock, buf, len, MSG_DONTWAIT | MSG_NOSIGNAL);
        if (sent < 0 && (errno == ENOTSOCK || errno == EBADF)) {
            ssize_t unused = write(sock, buf, len);
            (void)unused;
        }
    }
}

inline void write_direct_log_line(int prio, const std::string &line) {
    ErrnoRestorer errno_guard;
    std::string with_nl = line + "\n";
    write_direct_log_raw(prio, with_nl.data(), with_nl.size(), true);
}

inline void emit_probe(const char *domain, const std::string &key, const std::string &val) {
    if (__builtin_expect(!g_cfg.probe.enabled || g_tls_in_sigsys, 1)) return;
    ErrnoRestorer errno_guard;

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
inline struct sigaction g_old_sigfpe{};
inline bool g_crash_via_sigchain = false;
inline char g_crash_pkg_name[128] = {0};

inline struct sigaction *get_old_crash_sa(int sig) noexcept {
    switch (sig) {
        case SIGSEGV: return &g_old_sigsegv;
        case SIGABRT: return &g_old_sigabrt;
        case SIGBUS:  return &g_old_sigbus;
        case SIGILL:  return &g_old_sigill;
        case SIGFPE:  return &g_old_sigfpe;
        default:      return nullptr;
    }
}

inline const char *crash_sig_name(int sig) noexcept {
    switch (sig) {
        case SIGSEGV: return "SIGSEGV";
        case SIGABRT: return "SIGABRT";
        case SIGBUS:  return "SIGBUS";
        case SIGILL:  return "SIGILL";
        case SIGFPE:  return "SIGFPE";
        default:      return "SIGNAL";
    }
}

inline void forward_crash_signal(int sig, siginfo_t *info, void *ucontext, struct sigaction *prev) noexcept {
    if (prev && (prev->sa_flags & SA_SIGINFO) && prev->sa_sigaction) {
        prev->sa_sigaction(sig, info, ucontext);
    } else if (prev && prev->sa_handler != SIG_DFL && prev->sa_handler != SIG_IGN) {
        prev->sa_handler(sig);
    } else {
        signal(sig, SIG_DFL);
        raise(sig);
    }
}

// 双层防误报崩溃捕获器（覆盖 SIGSEGV/SIGABRT/SIGBUS/SIGILL/SIGFPE）：
// 1. 优先通过 libsigchain.so 注册，排在 ART FaultManager 之后，不截获 CheckSuspend / 隐式 NPE
// 2. 发送 [PENDING_CRASH] 给 Root Companion，仅当进程随后真正死亡 (socket EOF) 时才落盘为真实崩溃
inline void mirage_crash_signal_handler(int sig, siginfo_t *info, void *ucontext) {
    int saved_errno = errno;
    struct sigaction *prev = get_old_crash_sa(sig);
    uintptr_t fault_addr = info ? reinterpret_cast<uintptr_t>(info->si_addr) : 0;

    // 兜底快路径：若未能挂载进 libsigchain，直接跳过 ART 的页对齐 CheckSuspend / 隐式 NPE
    if (!g_crash_via_sigchain && sig == SIGSEGV && info &&
        (fault_addr < 0x1000 || (info->si_code == SEGV_ACCERR && (fault_addr & 0xFFFU) == 0))) {
        errno = saved_errno;
        forward_crash_signal(sig, info, ucontext, prev);
        return;
    }

    char reg_buf[256];
    reg_buf[0] = '\0';
#if defined(__aarch64__)
    if (ucontext) {
        auto *uc = static_cast<ucontext_t *>(ucontext);
        uintptr_t pc = static_cast<uintptr_t>(uc->uc_mcontext.pc);
        uintptr_t sp = static_cast<uintptr_t>(uc->uc_mcontext.sp);
        uintptr_t lr = static_cast<uintptr_t>(uc->uc_mcontext.regs[30]);
        Dl_info pc_dli{};
        if (dladdr(reinterpret_cast<void *>(pc), &pc_dli) && pc_dli.dli_fname) {
            const char *sl = std::strrchr(pc_dli.dli_fname, '/');
            const char *pc_lib = sl ? (sl + 1) : pc_dli.dli_fname;
            uintptr_t pc_off = pc - reinterpret_cast<uintptr_t>(pc_dli.dli_fbase);
            std::snprintf(reg_buf, sizeof(reg_buf), "pc=%s+0x%zx sp=0x%zx lr=0x%zx sigsys=%d",
                          pc_lib, static_cast<size_t>(pc_off),
                          static_cast<size_t>(sp), static_cast<size_t>(lr),
                          g_tls_in_sigsys ? 1 : 0);
        } else {
            std::snprintf(reg_buf, sizeof(reg_buf), "pc=0x%zx sp=0x%zx lr=0x%zx sigsys=%d",
                          static_cast<size_t>(pc), static_cast<size_t>(sp),
                          static_cast<size_t>(lr), g_tls_in_sigsys ? 1 : 0);
        }
    }
#endif

    void *frames[10];
    BacktraceState state{frames, frames + 10};
    _Unwind_Backtrace(unwind_callback, &state);
    size_t count = static_cast<size_t>(state.current - frames);

    char stack_buf[512];
    size_t s_pos = 0;
    stack_buf[0] = '\0';

    for (size_t i = 1; i < count && s_pos + 64 < sizeof(stack_buf); ++i) {
        Dl_info dli{};
        uintptr_t addr = reinterpret_cast<uintptr_t>(frames[i]);
        if (dladdr(frames[i], &dli) && dli.dli_fname) {
            const char *slash = std::strrchr(dli.dli_fname, '/');
            const char *lib_name = slash ? (slash + 1) : dli.dli_fname;
            uintptr_t rel_pc = addr - reinterpret_cast<uintptr_t>(dli.dli_fbase);

            if (s_pos > 0) {
                int w = std::snprintf(stack_buf + s_pos, sizeof(stack_buf) - s_pos, " <- ");
                if (w > 0) s_pos += static_cast<size_t>(w);
            }
            int w = 0;
            if (dli.dli_sname) {
                uintptr_t sym_off = addr - reinterpret_cast<uintptr_t>(dli.dli_saddr);
                w = std::snprintf(stack_buf + s_pos, sizeof(stack_buf) - s_pos,
                                  "%s!%s+0x%zx", lib_name, dli.dli_sname, static_cast<size_t>(sym_off));
            } else {
                w = std::snprintf(stack_buf + s_pos, sizeof(stack_buf) - s_pos,
                                  "%s+0x%zx", lib_name, static_cast<size_t>(rel_pc));
            }
            if (w > 0) s_pos += static_cast<size_t>(w);
        }
    }

    char crash_buf[960];
    int total_len = std::snprintf(
        crash_buf, sizeof(crash_buf),
        "[PENDING_CRASH][%s][%s] code=%d fault_addr=%p %s | Stack: %s\n",
        g_crash_pkg_name, crash_sig_name(sig),
        info ? info->si_code : 0,
        info ? info->si_addr : nullptr,
        reg_buf,
        s_pos > 0 ? stack_buf : "unknown"
    );
    if (total_len > 0) {
        size_t safe_len = std::min(static_cast<size_t>(total_len), sizeof(crash_buf) - 1);
        // 仅送往 Root Companion 待死亡核验，不提前刷屏 logcat ERROR
        write_direct_log_raw(ANDROID_LOG_DEBUG, crash_buf, safe_len, false);
    }

    errno = saved_errno;
    forward_crash_signal(sig, info, ucontext, prev);
}

inline void install_crash_signal_logger(const std::string &pkg) {
    std::strncpy(g_crash_pkg_name, pkg.c_str(), sizeof(g_crash_pkg_name) - 1);
    g_crash_pkg_name[sizeof(g_crash_pkg_name) - 1] = '\0';

    using PFN_sa = int (*)(int, const struct sigaction *, struct sigaction *);
    void *sc_h = dlopen("libsigchain.so", RTLD_NOW | RTLD_NOLOAD);
    PFN_sa fn_sa = sc_h ? reinterpret_cast<PFN_sa>(dlsym(sc_h, "sigaction")) : nullptr;
    g_crash_via_sigchain = (fn_sa != nullptr);
    if (!fn_sa) fn_sa = &sigaction;

    struct sigaction sa{};
    sa.sa_sigaction = mirage_crash_signal_handler;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = SA_SIGINFO | SA_ONSTACK;

    fn_sa(SIGSEGV, &sa, &g_old_sigsegv);
    fn_sa(SIGABRT, &sa, &g_old_sigabrt);
    fn_sa(SIGBUS,  &sa, &g_old_sigbus);
    fn_sa(SIGILL,  &sa, &g_old_sigill);
    fn_sa(SIGFPE,  &sa, &g_old_sigfpe);
}

} // namespace mirage

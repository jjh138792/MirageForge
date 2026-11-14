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

// 异步信号安全的原始字节写入函数（零堆内存分配，可在信号处理器内安全调用）
inline void write_raw_log_async_safe(int prio, const char *buf, size_t len) noexcept {
    if (!buf || len == 0) return;
    __android_log_write(prio, LOG_TAG, buf);
    int sock = g_probe_log_fd;
    if (sock >= 0) {
        ssize_t sent = send(sock, buf, len, MSG_DONTWAIT | MSG_NOSIGNAL);
        if (sent < 0 && (errno == ENOTSOCK || errno == EBADF)) {
            ssize_t unused = write(sock, buf, len);
            (void)unused;
        }
    }
}

inline void write_direct_log_line(int prio, const std::string &line) {
    std::string with_nl = line + "\n";
    write_raw_log_async_safe(prio, with_nl.data(), with_nl.size());
}

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

// 纯栈内存实现的崩溃回溯与 ART 隐式空指针判定（0 次 malloc）
inline bool capture_crash_chain_async_safe(char *out_buf, size_t out_cap, bool *out_is_art_managed) noexcept {
    if (!out_buf || out_cap == 0) return false;
    out_buf[0] = '\0';
    if (out_is_art_managed) *out_is_art_managed = false;

    void *buffer[10];
    BacktraceState state{buffer, buffer + 10};
    _Unwind_Backtrace(unwind_callback, &state);
    size_t count = static_cast<size_t>(state.current - buffer);

    size_t pos = 0;
    bool first = true;
    int valid_frames = 0;
    int art_frames = 0;

    for (size_t i = 1; i < count; ++i) {
        Dl_info info{};
        uintptr_t addr = reinterpret_cast<uintptr_t>(buffer[i]);
        if (dladdr(buffer[i], &info) && info.dli_fname) {
            if (std::strstr(info.dli_fname, "mirage_forge") != nullptr ||
                std::strstr(info.dli_fname, "vdso") != nullptr ||
                std::strstr(info.dli_fname, "libsigchain") != nullptr) {
                continue;
            }
            const char *slash = std::strrchr(info.dli_fname, '/');
            const char *lib_name = slash ? (slash + 1) : info.dli_fname;

            ++valid_frames;
            if (std::strcmp(lib_name, "libart.so") == 0 ||
                std::strstr(lib_name, ".odex") != nullptr ||
                std::strstr(lib_name, ".oat") != nullptr) {
                ++art_frames;
            }

            uintptr_t rel_pc = addr - reinterpret_cast<uintptr_t>(info.dli_fbase);
            int n = 0;
            if (info.dli_sname) {
                uintptr_t sym_off = addr - reinterpret_cast<uintptr_t>(info.dli_saddr);
                n = std::snprintf(out_buf + pos, out_cap - pos, "%s%s!%s+0x%zx",
                                  first ? "" : " <- ", lib_name, info.dli_sname, static_cast<size_t>(sym_off));
            } else {
                n = std::snprintf(out_buf + pos, out_cap - pos, "%s%s+0x%zx",
                                  first ? "" : " <- ", lib_name, static_cast<size_t>(rel_pc));
            }
            if (n > 0) {
                pos += static_cast<size_t>(n);
                first = false;
                if (pos >= out_cap - 1) break;
            }
        }
    }

    // 如果所有有效栈帧均位于 libart.so/.oat（或 JIT 匿名页无有效 C++ 栈帧），标记为 ART 托管环境信号
    if (out_is_art_managed && (valid_frames == 0 || art_frames == valid_frames)) {
        *out_is_art_managed = true;
    }
    return pos > 0;
}

inline void forward_to_previous_signal_handler(int sig, siginfo_t *info, void *ucontext) noexcept {
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

inline void mirage_crash_signal_handler(int sig, siginfo_t *info, void *ucontext) {
    char bt_buf[512] = {0};
    bool is_art_managed = false;
    capture_crash_chain_async_safe(bt_buf, sizeof(bt_buf), &is_art_managed);

    // 过滤 Android ART 虚拟机的 Implicit Null Check (fault_addr < 0x10000，如 0x0)，直接移交 ART FaultManager
    uintptr_t fault_addr = info ? reinterpret_cast<uintptr_t>(info->si_addr) : 0;
    if (sig == SIGSEGV && fault_addr < 0x10000U && is_art_managed) {
        forward_to_previous_signal_handler(sig, info, ucontext);
        return;
    }

    const char *sig_name = (sig == SIGSEGV) ? "SIGSEGV" :
                           (sig == SIGABRT) ? "SIGABRT" :
                           (sig == SIGBUS)  ? "SIGBUS"  : "SIGILL";

    char crash_buf[768];
    int len = std::snprintf(crash_buf, sizeof(crash_buf),
                            "[CRASH][%s][%s] code=%d fault_addr=%p | Stack: %s\n",
                            g_crash_pkg_name, sig_name,
                            info ? info->si_code : 0,
                            info ? info->si_addr : nullptr,
                            bt_buf[0] ? bt_buf : "native_runtime");
    if (len > 0) {
        size_t safe_len = static_cast<size_t>(len) < sizeof(crash_buf) ? static_cast<size_t>(len) : (sizeof(crash_buf) - 1);
        write_raw_log_async_safe(ANDROID_LOG_ERROR, crash_buf, safe_len);
    }

    forward_to_previous_signal_handler(sig, info, ucontext);
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

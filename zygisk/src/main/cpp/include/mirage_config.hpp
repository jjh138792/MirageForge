#pragma once

#include <cstdint>
#include <cstddef>
#include <cstdlib>
#include <cstring>
#include <cerrno>
#include <algorithm>
#include <string>
#include <vector>
#include <fstream>
#include <unordered_map>
#include <unistd.h>
#include <android/log.h>

#define LOG_TAG "MirageForge"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, LOG_TAG, __VA_ARGS__)
#define LOGW(...) __android_log_print(ANDROID_LOG_WARN, LOG_TAG, __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, __VA_ARGS__)

namespace mirage {

constexpr const char *MODULE_PROP_PATH = "/data/adb/modules/mirage_forge/module.prop";
constexpr const char *CLI_BIN_PATH     = "/data/adb/modules/mirage_forge/bin/mirageforge_cli";
constexpr const char *DATA_ROOT_DIR    = "/data/adb/mirage_forge";
constexpr const char *ROUTES_DB_PATH   = "/data/adb/mirage_forge/routes.db";
constexpr const char *APPS_CONF_DIR    = "/data/adb/mirage_forge/apps";
constexpr const char *LAUNCH_LOG_PATH  = "/data/adb/mirage_forge/launch_history.log";

// 从 module.prop 动态读取版本号
inline std::string read_module_prop_version() {
    std::ifstream ifs(MODULE_PROP_PATH);
    if (!ifs.is_open()) return "dynamic";
    std::string line;
    while (std::getline(ifs, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (line.rfind("version=", 0) == 0) {
            std::string ver = line.substr(8);
            if (!ver.empty() && ver[0] == 'v') ver.erase(0, 1);
            return ver.empty() ? "dynamic" : ver;
        }
    }
    return "dynamic";
}

// 易失性内存擦除工具
inline void volatile_scrub_string(std::string &s) noexcept {
    if (s.empty()) return;
    volatile char *p = const_cast<volatile char *>(s.data());
    for (size_t i = 0; i < s.size(); ++i) {
        p[i] = 0;
    }
    s.clear();
    s.shrink_to_fit();
}

inline void volatile_scrub_map(std::unordered_map<std::string, std::string> &m) noexcept {
    for (auto &kv : m) {
        volatile_scrub_string(kv.second);
    }
    m.clear();
}

// 严格保护线程 errno 的守卫器：防止探针 send() 或日志调用污染原系统调用的 errno
struct ErrnoRestorer {
    int saved_errno;
    inline ErrnoRestorer() noexcept : saved_errno(errno) {}
    inline ~ErrnoRestorer() noexcept { errno = saved_errno; }
};

// 线程局部重入保护器（自动附带 errno 保护）
inline thread_local bool g_tls_in_hook = false;

struct ScopedHookGuard {
    int saved_errno;
    bool entered;
    inline ScopedHookGuard() noexcept : saved_errno(errno) {
        if (__builtin_expect(g_tls_in_hook, 0)) {
            entered = false;
        } else {
            g_tls_in_hook = true;
            entered = true;
        }
    }
    inline ~ScopedHookGuard() noexcept {
        if (entered) {
            g_tls_in_hook = false;
        }
        errno = saved_errno;
    }
    explicit inline operator bool() const noexcept { return entered; }
};

constexpr uint64_t fnv1a_64(const char *str, size_t len) noexcept {
    uint64_t hash = 14695981039346656037ULL;
    for (size_t i = 0; i < len; ++i) {
        hash ^= static_cast<uint8_t>(str[i]);
        hash *= 1099511628211ULL;
    }
    return hash;
}

inline uint64_t hash_str(const std::string &s) noexcept {
    return fnv1a_64(s.data(), s.size());
}

// ============================================================================
// 微模块 1：Build 反射与受限属性区配置 (已移除 DPI)
// ============================================================================
struct BuildModuleConfig {
    bool enabled = false;
    bool force_spoof_sdk = false;
    std::unordered_map<std::string, std::string> prop_map;
    std::unordered_map<std::string, std::string> build_map;
    std::unordered_map<std::string, std::string> version_map;

    void wipe_volatile_reflection_maps() noexcept {
        volatile_scrub_map(build_map);
        volatile_scrub_map(version_map);
    }
};

// ============================================================================
// 微模块 2：OpenGL ES / Vulkan / MediaDrm / 屏幕高刷配置
// ============================================================================
struct GraphicsModuleConfig {
    bool enabled = false;
    int max_refresh_rate = 0; // 0 = 0改动；120 / 144 / 165 = 伪装屏幕最高刷新率
    std::string gl_vendor;
    std::string gl_renderer;
    std::string gl_version;
    std::string gl_glsl_version = "OpenGL ES GLSL ES 3.20";
    uint32_t vulkan_vendor_id = 0;
    uint32_t vulkan_device_id = 0;
};

// ============================================================================
// 微模块 3：VFS 硬件拓扑与按需 Seccomp-BPF 配置
// ============================================================================
struct CpuClusterSpec {
    int first_core = 0;
    int last_core = 5;
    int min_freq_khz = 614400;
    int max_freq_khz = 3532800;
    uint32_t cpu_part = 0xd85;
    int capacity = 768;
    std::vector<int> opp_freqs_khz;
};

struct VfsModuleConfig {
    bool enabled = false;
    bool enable_seccomp = false; // false = 0改动；true = 开启按需 Seccomp-BPF 拦截裸 svc #0
    std::string cpu_hardware;
    std::string cpu_arch = "aarch64";
    std::string soc_id;
    std::string soc_family = "Snapdragon";
    std::string soc_machine;
    std::string cpu_midr_el1 = "0x00000000511f8000";
    std::string cpu_clusters_spec;
    int cpu_cores = 8;
    int cpu_max_freq_khz = 4320000;
    int ram_gb = 16;
    std::string ddr_type = "LPDDR5X";
    std::string ddr_vendor = "SKhynix";
    std::string kernel_release;
    std::string kernel_version;
    std::vector<CpuClusterSpec> clusters;
};

// ============================================================================
// 微模块 4：幻境探针与崩溃捕获配置
// ============================================================================
struct ProbeModuleConfig {
    bool enabled = false;
};

struct ModularConfig {
    bool active = false;
    std::string module_version = "dynamic";
    std::string pkg_name;

    BuildModuleConfig    build;
    GraphicsModuleConfig gl;
    VfsModuleConfig      vfs;
    ProbeModuleConfig    probe;

    inline bool is_any_enabled() const noexcept {
        return build.enabled || gl.enabled || vfs.enabled || probe.enabled;
    }
};

inline ModularConfig g_cfg;

inline bool write_full(int fd, const void *buf, size_t len) {
    const auto *p = static_cast<const uint8_t *>(buf);
    while (len > 0) {
        ssize_t n;
        do {
            n = write(fd, p, len);
        } while (n == -1 && errno == EINTR);
        if (n <= 0) return false;
        p += n;
        len -= static_cast<size_t>(n);
    }
    return true;
}

inline bool read_full(int fd, void *buf, size_t len) {
    auto *p = static_cast<uint8_t *>(buf);
    while (len > 0) {
        ssize_t n;
        do {
            n = read(fd, p, len);
        } while (n == -1 && errno == EINTR);
        if (n <= 0) return false;
        p += n;
        len -= static_cast<size_t>(n);
    }
    return true;
}

inline void write_str(int fd, const std::string &s) {
    uint32_t len = static_cast<uint32_t>(s.size());
    write_full(fd, &len, sizeof(len));
    if (len > 0) write_full(fd, s.data(), len);
}

inline std::string read_str(int fd) {
    uint32_t len = 0;
    if (!read_full(fd, &len, sizeof(len)) || len == 0 || len > 524288) return "";
    std::string s(len, '\0');
    if (!read_full(fd, &s[0], len)) return "";
    return s;
}

inline std::vector<std::string> split_str(const std::string &s, char delim = ',') {
    std::vector<std::string> res;
    size_t start = 0;
    while (start < s.size()) {
        size_t pos = s.find(delim, start);
        if (pos == std::string::npos) pos = s.size();
        if (pos > start) res.emplace_back(s.substr(start, pos - start));
        start = pos + 1;
    }
    return res;
}

} // namespace mirage

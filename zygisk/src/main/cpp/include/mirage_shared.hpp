#pragma once

#include <cstdint>
#include <cstddef>
#include <cstdlib>
#include <cstring>
#include <cerrno>
#include <algorithm>
#include <string>
#include <vector>
#include <unordered_map>
#include <unordered_set>
#include <mutex>
#include <atomic>
#include <unistd.h>
#include <sys/system_properties.h>
#include <android/log.h>
#include "mirage_logger.hpp"

#define LOG_TAG "MirageForge"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, LOG_TAG, __VA_ARGS__)
#define LOGW(...) __android_log_print(ANDROID_LOG_WARN, LOG_TAG, __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, __VA_ARGS__)

namespace mirage {

constexpr const char *MIRAGE_ENGINE_VERSION = "1.0.1";
constexpr const char *CLI_BIN_PATH          = "/data/adb/modules/mirage_forge/bin/mirageforge_cli";
constexpr const char *DATA_ROOT_DIR         = "/data/adb/mirage_forge";
constexpr const char *ROUTES_DB_PATH        = "/data/adb/mirage_forge/routes.db";
constexpr const char *APPS_CONF_DIR         = "/data/adb/mirage_forge/apps";
constexpr const char *LAUNCH_LOG_PATH       = "/data/adb/mirage_forge/launch_history.log";

// 线程局部重入保护器
inline thread_local bool g_tls_in_hook = false;

struct ScopedHookGuard {
    bool entered;
    ScopedHookGuard() noexcept {
        if (g_tls_in_hook) {
            entered = false;
        } else {
            g_tls_in_hook = true;
            entered = true;
        }
    }
    ~ScopedHookGuard() noexcept {
        if (entered) {
            g_tls_in_hook = false;
        }
    }
    explicit operator bool() const noexcept { return entered; }
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

// AOSP Bionic `prop_info` 1:1 真实二进制内存布局结构
struct alignas(4) BionicPropInfoCompat {
    uint32_t serial;              // 偏移 0..3
    char value[PROP_VALUE_MAX];   // 偏移 4..95 (PROP_VALUE_MAX == 92)
    char name[1];                 // 偏移 96..N
};

class SyntheticPropArena {
public:
    const prop_info *obtain(const std::string &name, const std::string &val) {
        std::lock_guard<std::mutex> lock(mtx_);
        auto it = by_name_.find(name);
        if (it != by_name_.end()) {
            return reinterpret_cast<const prop_info *>(it->second);
        }

        size_t alloc_sz = offsetof(BionicPropInfoCompat, name) + name.size() + 1;
        void *raw = std::calloc(1, alloc_sz);
        if (!raw) return nullptr;

        auto *pi = static_cast<BionicPropInfoCompat *>(raw);
        __atomic_store_n(&pi->serial, 0x02000000u, __ATOMIC_RELAXED);
        std::strncpy(pi->value, val.c_str(), PROP_VALUE_MAX - 1);
        pi->value[PROP_VALUE_MAX - 1] = '\0';
        std::memcpy(pi->name, name.data(), name.size());
        pi->name[name.size()] = '\0';

        by_name_[name] = pi;
        long_values_[pi] = val;
        return reinterpret_cast<const prop_info *>(pi);
    }

    bool is_synthetic(const prop_info *pi, std::string *out_name = nullptr, std::string *out_val = nullptr) {
        if (!pi) return false;
        std::lock_guard<std::mutex> lock(mtx_);
        auto *compat = reinterpret_cast<BionicPropInfoCompat *>(const_cast<prop_info *>(pi));
        auto it = long_values_.find(compat);
        if (it == long_values_.end()) return false;
        if (out_name) *out_name = compat->name;
        if (out_val) *out_val = it->second;
        return true;
    }

private:
    std::mutex mtx_;
    std::unordered_map<std::string, BionicPropInfoCompat *> by_name_;
    std::unordered_map<BionicPropInfoCompat *, std::string> long_values_;
};

inline SyntheticPropArena g_prop_arena;

// 异构 CPU 核心分簇描述结构 (用于伪装每一颗核心的最小/最大/实时跳动频率与 policy* 节点)
struct CpuClusterSpec {
    int first_core = 0;
    int last_core = 5;
    int min_freq_khz = 614400;
    int max_freq_khz = 3532800;
    uint32_t cpu_part = 0xd85;
    int capacity = 768;
    std::vector<int> opp_freqs_khz;
};

struct MirageContext {
    bool active = false;
    bool enable_build_props = true;
    bool enable_gl_vulkan = true;
    bool enable_vfs_shield = true;
    bool enable_maps_cloak = true;
    bool enable_probe = false;
    bool force_spoof_sdk = false;

    std::string pkg_name;

    // GPU / Vulkan / MediaDrm 参数
    std::string gl_vendor = "Qualcomm";
    std::string gl_renderer = "Adreno (TM) 830";
    std::string gl_version = "OpenGL ES 3.2 V@0800.0 (GIT@8f4b2a1, I9b3d7e6c1a, 1731000000)";
    std::string gl_glsl_version = "OpenGL ES GLSL ES 3.20";
    uint32_t vulkan_vendor_id = 0x5143;
    uint32_t vulkan_device_id = 0x43050000;

    // VFS & 异构多簇 CPU & DDR 物理内存对称参数
    std::string cpu_hardware = "Qualcomm Technologies, Inc SM8750";
    std::string cpu_arch = "aarch64";
    std::string soc_id = "618";
    std::string soc_family = "Snapdragon";
    std::string soc_machine = "SM8750";
    std::string cpu_midr_el1 = "0x00000000511f8000";
    std::string cpu_clusters_spec; // 可选自定义分簇配置，如 "0-5:614400:3532800;6-7:806400:4320000"
    int cpu_cores = 8;
    int cpu_max_freq_khz = 4320000;
    int ram_gb = 16;
    std::string ddr_type = "LPDDR5X";
    std::string ddr_vendor = "SKhynix";
    std::string kernel_release = "6.6.30-android15-8-g94b8f12ab4c1";
    std::string kernel_version = "#1 SMP PREEMPT Fri Nov 15 12:00:00 UTC 2025";

    std::vector<CpuClusterSpec> clusters;
    std::unordered_map<std::string, std::string> prop_map;
    std::unordered_map<std::string, std::string> build_map;
    std::unordered_map<std::string, std::string> version_map;
    std::unordered_map<std::string, std::string> vfs_files;
};

inline MirageContext g_ctx;
inline std::mutex g_probe_mtx;
inline std::unordered_set<uint64_t> g_probe_dedup;

// 输出携带实时 Native 调用栈的探针审计日志（直写 SCM_RIGHTS 文件描述符，无视商业 ROM 禁日志）
inline void emit_probe(const char *domain, const std::string &key, const std::string &val) {
    if (!g_ctx.enable_probe) return;
    std::string composite = std::string(domain) + ":" + key;
    uint64_t h = hash_str(composite);
    {
        std::lock_guard<std::mutex> lock(g_probe_mtx);
        if (g_probe_dedup.size() > 1500) return;
        if (!g_probe_dedup.insert(h).second) return;
    }
    std::string stack = capture_caller_chain(2, 5);
    std::ostringstream ss;
    ss << "[PROBE][" << g_ctx.pkg_name << "][" << domain << "] "
       << key << " => " << val << " @@ " << stack;
    write_direct_log_line(g_probe_log_fd, ANDROID_LOG_INFO, ss.str());
}

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

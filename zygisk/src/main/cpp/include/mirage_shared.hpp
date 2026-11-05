#pragma once

#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>
#include <unordered_map>
#include <unordered_set>
#include <mutex>
#include <atomic>
#include <unistd.h>
#include <sys/system_properties.h>
#include <android/log.h>

#define LOG_TAG "MirageForge"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, LOG_TAG, __VA_ARGS__)
#define LOGW(...) __android_log_print(ANDROID_LOG_WARN, LOG_TAG, __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, __VA_ARGS__)

namespace mirage {

// 编译期与运行期 FNV-1a 64位高速哈希
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
// AOSP Bionic `prop_info` 1:1 真实二进制内存布局结构
// 防止风控 SDK 直接通过 `(const char*)pi + 4` 或 `(const char*)pi + 96` 裸读内存导致崩溃或穿帮
// ============================================================================
struct alignas(4) BionicPropInfoCompat {
    std::atomic<uint32_t> serial; // 偏移 0..3 (偶数表示稳定状态)
    char value[PROP_VALUE_MAX];   // 偏移 4..95 (PROP_VALUE_MAX == 92)
    char name[1];                 // 偏移 96..N (柔性数组起始位，存放以 '\0' 结尾的属性名)
};

class SyntheticPropArena {
public:
    const prop_info *obtain(const std::string &name, const std::string &val) {
        std::lock_guard<std::mutex> lock(mtx_);
        auto it = by_name_.find(name);
        if (it != by_name_.end()) {
            update_value(it->second, val);
            return reinterpret_cast<const prop_info *>(it->second);
        }

        // 分配真实 Bionic 布局尺寸: sizeof(serial) + 92 + name.size() + 1 + 额外保存长字符串的扩展区
        size_t alloc_sz = offsetof(BionicPropInfoCompat, name) + name.size() + 1;
        void *raw = std::calloc(1, alloc_sz);
        if (!raw) return nullptr;

        auto *pi = static_cast<BionicPropInfoCompat *>(raw);
        pi->serial.store(0x02000000u, std::memory_order_relaxed); // 偶数序列号
        std::strncpy(pi->value, val.c_str(), PROP_VALUE_MAX - 1);
        pi->value[PROP_VALUE_MAX - 1] = '\0';
        std::memcpy(pi->name, name.data(), name.size());
        pi->name[name.size()] = '\0';

        by_name_[name] = pi;
        long_values_[pi] = val;
        ordered_list_.push_back(pi);
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

    std::vector<const prop_info *> snapshot_all() {
        std::lock_guard<std::mutex> lock(mtx_);
        std::vector<const prop_info *> res;
        res.reserve(ordered_list_.size());
        for (auto *p : ordered_list_) {
            res.push_back(reinterpret_cast<const prop_info *>(p));
        }
        return res;
    }

private:
    void update_value(BionicPropInfoCompat *pi, const std::string &val) {
        std::strncpy(pi->value, val.c_str(), PROP_VALUE_MAX - 1);
        pi->value[PROP_VALUE_MAX - 1] = '\0';
        long_values_[pi] = val;
    }

    std::mutex mtx_;
    std::unordered_map<std::string, BionicPropInfoCompat *> by_name_;
    std::unordered_map<BionicPropInfoCompat *, std::string> long_values_;
    std::vector<BionicPropInfoCompat *> ordered_list_;
};

inline SyntheticPropArena g_prop_arena;

// ============================================================================
// 全局进程内全息配置上下文
// ============================================================================
struct MirageContext {
    bool active = false;
    bool enable_build_props = true;
    bool enable_gl_vulkan = true;
    bool enable_vfs_shield = true;
    bool enable_probe = false;

    std::string pkg_name;

    // GPU & Vulkan 伪装参数
    std::string gl_vendor = "Qualcomm";
    std::string gl_renderer = "Adreno (TM) 830";
    std::string gl_version = "OpenGL ES 3.2 V@0800.0 (GIT@8f4b2a1, I9b3d7e6c1a, 1731000000)";
    std::string gl_glsl_version = "OpenGL ES GLSL ES 3.20";
    uint32_t vulkan_vendor_id = 0x5143; // 0x5143 = Qualcomm, 0x13B5 = ARM
    uint32_t vulkan_device_id = 0x43050000;

    // VFS & Syscall (sysinfo/sysconf/uname) 硬件参数
    std::string cpu_hardware = "Qualcomm Technologies, Inc SM8750";
    std::string cpu_arch = "aarch64";
    std::string soc_id = "618";
    std::string soc_family = "Snapdragon";
    std::string soc_machine = "SM8750";
    int cpu_cores = 8;
    int cpu_max_freq_khz = 4320000;
    int ram_gb = 16;
    std::string kernel_release = "6.6.30-android15-8-g94b8f12ab4c1";
    std::string kernel_version = "#1 SMP PREEMPT Fri Nov 15 12:00:00 UTC 2025";

    // 属性与反射映射表
    std::unordered_map<std::string, std::string> prop_map;
    std::unordered_map<std::string, std::string> build_map;
    std::unordered_map<std::string, std::string> version_map;

    // 预合成的 VFS 节点文本
    std::unordered_map<std::string, std::string> vfs_files;
};

inline MirageContext g_ctx;
inline std::mutex g_probe_mtx;
inline std::unordered_set<uint64_t> g_probe_dedup;

inline void emit_probe(const char *domain, const std::string &key, const std::string &val) {
    if (!g_ctx.enable_probe) return;
    std::string composite = std::string(domain) + ":" + key;
    uint64_t h = hash_str(composite);
    {
        std::lock_guard<std::mutex> lock(g_probe_mtx);
        if (g_probe_dedup.size() > 1024) return;
        if (!g_probe_dedup.insert(h).second) return;
    }
    LOGI("[PROBE][%s][%s] %s => %s", g_ctx.pkg_name.c_str(), domain, key.c_str(), val.c_str());
}

// IPC Socket 读写工具
inline bool write_full(int fd, const void *buf, size_t len) {
    const auto *p = static_cast<const uint8_t *>(buf);
    while (len > 0) {
        ssize_t n = TEMP_FAILURE_RETRY(write(fd, p, len));
        if (n <= 0) return false;
        p += n;
        len -= static_cast<size_t>(n);
    }
    return true;
}

inline bool read_full(int fd, void *buf, size_t len) {
    auto *p = static_cast<uint8_t *>(buf);
    while (len > 0) {
        ssize_t n = TEMP_FAILURE_RETRY(read(fd, p, len));
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

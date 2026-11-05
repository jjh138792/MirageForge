#pragma once

#include <cstdint>
#include <cstring>
#include <string>
#include <vector>
#include <unordered_map>
#include <unordered_set>
#include <mutex>
#include <unistd.h>
#include <sys/system_properties.h>
#include <android/log.h>

#define LOG_TAG "MirageForge"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, LOG_TAG, __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, __VA_ARGS__)

namespace mirage {

// Companion IPC 动作码
enum class IpcAction : uint8_t {
    QUERY_CONFIG = 1,
    REPORT_PROBE = 2
};

// 进程内全局全息配置上下文
struct RuntimeProfile {
    bool active = false;
    bool enable_build_props = true;
    bool enable_gl_spoof = true;
    bool enable_vfs_shield = true;
    bool enable_probe = false;

    std::string pkg_name;

    // GPU 伪装参数
    std::string gl_vendor;
    std::string gl_renderer;
    std::string gl_version;

    // VFS 硬件节点参数
    std::string cpu_hardware;
    std::string cpu_arch;
    int cpu_cores = 8;
    int ram_gb = 16;
    std::string kernel_version;

    // Java 反射与底层属性表
    std::unordered_map<std::string, std::string> prop_map;
    std::unordered_map<std::string, std::string> build_map;
    std::unordered_map<std::string, std::string> version_map;

    // 预合成的 VFS 虚拟文件内容
    std::string vfs_build_prop;
    std::string vfs_cpuinfo;
    std::string vfs_meminfo;
    std::string vfs_proc_version;
    std::string vfs_gpu_model;
};

inline RuntimeProfile g_profile;

// 虚拟 prop_info 句柄结构
struct SyntheticProp {
    std::string name;
    std::string value;
};

inline std::mutex g_syn_mutex;
inline std::unordered_map<std::string, SyntheticProp *> g_syn_by_name;
inline std::unordered_set<const void *> g_syn_ptrs;

// 探针去重缓存（避免同一属性每秒写入上千次日志）
inline std::mutex g_probe_mutex;
inline std::unordered_set<std::string> g_probe_seen;

inline const prop_info *get_or_create_synthetic_prop(const std::string &name, const std::string &val) {
    std::lock_guard<std::mutex> lock(g_syn_mutex);
    auto it = g_syn_by_name.find(name);
    if (it != g_syn_by_name.end()) {
        it->second->value = val;
        return reinterpret_cast<const prop_info *>(it->second);
    }
    auto *sp = new SyntheticProp{name, val};
    g_syn_by_name[name] = sp;
    g_syn_ptrs.insert(static_cast<const void *>(sp));
    return reinterpret_cast<const prop_info *>(sp);
}

inline const SyntheticProp *as_synthetic_prop(const prop_info *pi) {
    if (!pi) return nullptr;
    std::lock_guard<std::mutex> lock(g_syn_mutex);
    if (g_syn_ptrs.count(reinterpret_cast<const void *>(pi)) > 0) {
        return reinterpret_cast<const SyntheticProp *>(pi);
    }
    return nullptr;
}

// 基础 Socket 读写封装
inline bool write_exact(int fd, const void *buf, size_t len) {
    const uint8_t *p = static_cast<const uint8_t *>(buf);
    size_t left = len;
    while (left > 0) {
        ssize_t n = write(fd, p, left);
        if (n <= 0) return false;
        p += n;
        left -= static_cast<size_t>(n);
    }
    return true;
}

inline bool read_exact(int fd, void *buf, size_t len) {
    uint8_t *p = static_cast<uint8_t *>(buf);
    size_t left = len;
    while (left > 0) {
        ssize_t n = read(fd, p, left);
        if (n <= 0) return false;
        p += n;
        left -= static_cast<size_t>(n);
    }
    return true;
}

inline void write_string(int fd, const std::string &str) {
    uint32_t len = static_cast<uint32_t>(str.size());
    write_exact(fd, &len, sizeof(len));
    if (len > 0) write_exact(fd, str.data(), len);
}

inline std::string read_string(int fd) {
    uint32_t len = 0;
    if (!read_exact(fd, &len, sizeof(len)) || len == 0 || len > 262144) return "";
    std::string str(len, '\0');
    if (!read_exact(fd, &str[0], len)) return "";
    return str;
}

// 将 문자열按分隔符切分为 vector
inline std::vector<std::string> split_csv(const std::string &s, char delim = ',') {
    std::vector<std::string> out;
    size_t start = 0;
    while (start < s.size()) {
        size_t end = s.find(delim, start);
        if (end == std::string::npos) end = s.size();
        std::string token = s.substr(start, end - start);
        if (!token.empty()) out.push_back(token);
        start = end + 1;
    }
    return out;
}

// 记录幻境探针事件（先存储在进程内缓冲，或输出到 logcat 供 WebUI 实时抓取）
inline void record_probe_event(const char *category, const std::string &target, const std::string &result) {
    if (!g_profile.enable_probe) return;
    std::string sig = std::string(category) + "|" + target;
    {
        std::lock_guard<std::mutex> lock(g_probe_mutex);
        if (g_probe_seen.size() > 512) return;
        if (!g_probe_seen.insert(sig).second) return;
    }
    LOGI("[PROBE][%s][%s] %s => %s", g_profile.pkg_name.c_str(), category, target.c_str(), result.c_str());
}

} // namespace mirage

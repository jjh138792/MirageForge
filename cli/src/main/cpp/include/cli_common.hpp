#pragma once

#include <cstdint>
#include <cstddef>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cerrno>
#include <cctype>
#include <string>
#include <vector>
#include <map>
#include <unordered_map>
#include <sstream>
#include <fstream>
#include <algorithm>
#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/types.h>

namespace mirage::cli {

constexpr const char *MIRAGE_VERSION      = "1.0.1";
constexpr int         MIRAGE_VERSION_CODE = 101;

constexpr const char *DATA_ROOT_DIR   = "/data/adb/mirage_forge";
constexpr const char *TPL_DIR         = "/data/adb/mirage_forge/templates";
constexpr const char *APPS_DIR        = "/data/adb/mirage_forge/apps";
constexpr const char *ROUTES_DB       = "/data/adb/mirage_forge/routes.db";
constexpr const char *CONFIG_JSON     = "/data/adb/mirage_forge/config.json";
constexpr const char *LAUNCH_LOG_FILE = "/data/adb/mirage_forge/launch_history.log";
constexpr const char *MODULE_DIR      = "/data/adb/modules/mirage_forge";

// 全息机型模板结构体 (v1.0.1 含物理 DDR 对称规格)
struct DeviceTemplate {
    std::string id;
    std::string name;
    std::string model;
    std::string brand;
    std::string manufacturer;
    std::string device;
    std::string product;
    std::string board;
    std::string hardware = "qcom";
    std::string bootloader = "unknown";
    std::string soc_manufacturer = "Qualcomm";
    std::string soc_model;
    std::string supported_abis = "arm64-v8a,armeabi-v7a,armeabi";
    std::string build_id = "AP3A.240905.015.A2";
    std::string build_time = "1736899200000";
    std::string release = "15";
    std::string sdk_int = "35";
    std::string security_patch = "2025-02-01";
    std::string fingerprint;

    // GPU (OpenGL ES & Vulkan)
    std::string gl_vendor = "Qualcomm";
    std::string gl_renderer = "Adreno (TM) 830";
    std::string gl_version = "OpenGL ES 3.2 V@0800.0";

    // VFS & Syscall & 物理内存对称参数
    std::string cpu_hardware = "Qualcomm Technologies, Inc SM8750";
    int cpu_cores = 8;
    int cpu_max_freq = 4320000;
    int ram_gb = 16;
    std::string ddr_type = "LPDDR5X";
    std::string ddr_vendor = "SKhynix";
    std::string soc_id = "618";
    std::string soc_machine = "SM8750";
    std::string kernel_release = "6.6.30-android15-8-g94b8f12ab4c1";
    std::string kernel_version = "#1 SMP PREEMPT Fri Nov 15 12:00:00 UTC 2025";

    // 附加跨厂商属性 (以 \n 分隔的 KEY=VALUE)
    std::string custom_props;
};

// 应用级五维分流路由结构体
struct AppRoute {
    std::string pkg_name;
    std::string tpl_id;
    bool enable_build = true;
    bool enable_gl = true;
    bool enable_vfs = true;
    bool enable_probe = false;
    bool force_spoof_sdk = false; // 默认 false，防止跨 Android 大版本篡改 SDK_INT 引发应用崩溃
};

inline std::string trim(const std::string &s) {
    size_t first = s.find_first_not_of(" \t\r\n");
    if (first == std::string::npos) return "";
    size_t last = s.find_last_not_of(" \t\r\n");
    return s.substr(first, last - first + 1);
}

inline std::vector<std::string> split_lines(const std::string &s) {
    std::vector<std::string> out;
    std::istringstream iss(s);
    std::string line;
    while (std::getline(iss, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        out.push_back(line);
    }
    return out;
}

inline std::vector<std::string> split_char(const std::string &s, char delim) {
    std::vector<std::string> out;
    size_t start = 0;
    while (start <= s.size()) {
        size_t pos = s.find(delim, start);
        if (pos == std::string::npos) {
            out.push_back(s.substr(start));
            break;
        }
        out.push_back(s.substr(start, pos - start));
        start = pos + 1;
    }
    return out;
}

inline bool is_safe_token(const std::string &s) {
    if (s.empty() || s.size() > 128) return false;
    for (char c : s) {
        if (!(std::isalnum(static_cast<unsigned char>(c)) || c == '_' || c == '.' || c == '-')) {
            return false;
        }
    }
    return s.find("..") == std::string::npos;
}

inline std::string json_escape(const std::string &s) {
    std::string out;
    out.reserve(s.size() + 16);
    for (unsigned char c : s) {
        switch (c) {
            case '\"': out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\b': out += "\\b";  break;
            case '\f': out += "\\f";  break;
            case '\n': out += "\\n";  break;
            case '\r': out += "\\r";  break;
            case '\t': out += "\\t";  break;
            default:
                if (c < 0x20) {
                    char buf[8];
                    std::snprintf(buf, sizeof(buf), "\\u%04x", c);
                    out += buf;
                } else {
                    out.push_back(static_cast<char>(c));
                }
                break;
        }
    }
    return out;
}

inline bool ensure_dir(const char *path, mode_t mode = 0700) {
    struct stat st{};
    if (stat(path, &st) == 0) {
        chmod(path, mode);
        return S_ISDIR(st.st_mode);
    }
    if (mkdir(path, mode) == 0) {
        chmod(path, mode);
        return true;
    }
    return false;
}

// 原子级文件写入（先写 .tmp + fsync，再 rename 原子覆盖）
inline bool write_file_atomic(const std::string &path, const std::string &content, mode_t mode = 0600) {
    std::string tmp_path = path + ".tmp." + std::to_string(getpid());
    int fd = open(tmp_path.c_str(), O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, mode);
    if (fd < 0) return false;

    const char *ptr = content.data();
    size_t left = content.size();
    while (left > 0) {
        ssize_t written = write(fd, ptr, left);
        if (written < 0) {
            if (errno == EINTR) continue;
            close(fd);
            unlink(tmp_path.c_str());
            return false;
        }
        ptr += written;
        left -= static_cast<size_t>(written);
    }

    fsync(fd);
    fchmod(fd, mode);
    close(fd);

    if (rename(tmp_path.c_str(), path.c_str()) != 0) {
        unlink(tmp_path.c_str());
        return false;
    }
    return true;
}

inline std::string read_file_to_string(const std::string &path) {
    std::ifstream ifs(path, std::ios::in | std::ios::binary);
    if (!ifs.is_open()) return "";
    std::ostringstream ss;
    ss << ifs.rdbuf();
    return ss.str();
}

inline std::unordered_map<std::string, std::string> parse_flat_json_object(const std::string &json) {
    std::unordered_map<std::string, std::string> res;
    size_t i = 0, n = json.size();

    auto skip_ws = [&]() {
        while (i < n && std::isspace(static_cast<unsigned char>(json[i]))) ++i;
    };

    auto parse_str = [&]() -> std::string {
        std::string out;
        if (i >= n || json[i] != '\"') return out;
        ++i;
        while (i < n) {
            char c = json[i++];
            if (c == '\"') break;
            if (c == '\\' && i < n) {
                char esc = json[i++];
                switch (esc) {
                    case '\"': out.push_back('\"'); break;
                    case '\\': out.push_back('\\'); break;
                    case '/':  out.push_back('/');  break;
                    case 'b':  out.push_back('\b'); break;
                    case 'f':  out.push_back('\f'); break;
                    case 'n':  out.push_back('\n'); break;
                    case 'r':  out.push_back('\r'); break;
                    case 't':  out.push_back('\t'); break;
                    default:   out.push_back(esc);  break;
                }
            } else {
                out.push_back(c);
            }
        }
        return out;
    };

    skip_ws();
    if (i < n && json[i] == '{') ++i;

    while (i < n) {
        skip_ws();
        if (i >= n || json[i] == '}') break;
        if (json[i] == ',') { ++i; continue; }
        if (json[i] != '\"') { ++i; continue; }

        std::string key = parse_str();
        skip_ws();
        if (i >= n || json[i] != ':') break;
        ++i;
        skip_ws();

        if (i >= n) break;
        if (json[i] == '\"') {
            res[key] = parse_str();
        } else {
            size_t val_start = i;
            while (i < n && json[i] != ',' && json[i] != '}' && !std::isspace(static_cast<unsigned char>(json[i]))) {
                ++i;
            }
            res[key] = json.substr(val_start, i - val_start);
        }
    }
    return res;
}

inline std::string template_to_json(const DeviceTemplate &t) {
    std::ostringstream ss;
    ss << "{"
       << "\"id\":\"" << json_escape(t.id) << "\","
       << "\"name\":\"" << json_escape(t.name) << "\","
       << "\"MODEL\":\"" << json_escape(t.model) << "\","
       << "\"BRAND\":\"" << json_escape(t.brand) << "\","
       << "\"MANUFACTURER\":\"" << json_escape(t.manufacturer) << "\","
       << "\"DEVICE\":\"" << json_escape(t.device) << "\","
       << "\"PRODUCT\":\"" << json_escape(t.product) << "\","
       << "\"BOARD\":\"" << json_escape(t.board) << "\","
       << "\"HARDWARE\":\"" << json_escape(t.hardware) << "\","
       << "\"BOOTLOADER\":\"" << json_escape(t.bootloader) << "\","
       << "\"SOC_MANUFACTURER\":\"" << json_escape(t.soc_manufacturer) << "\","
       << "\"SOC_MODEL\":\"" << json_escape(t.soc_model) << "\","
       << "\"SUPPORTED_ABIS\":\"" << json_escape(t.supported_abis) << "\","
       << "\"BUILD_ID\":\"" << json_escape(t.build_id) << "\","
       << "\"BUILD_TIME\":\"" << json_escape(t.build_time) << "\","
       << "\"RELEASE\":\"" << json_escape(t.release) << "\","
       << "\"SDK_INT\":\"" << json_escape(t.sdk_int) << "\","
       << "\"SECURITY_PATCH\":\"" << json_escape(t.security_patch) << "\","
       << "\"FINGERPRINT\":\"" << json_escape(t.fingerprint) << "\","
       << "\"GL_VENDOR\":\"" << json_escape(t.gl_vendor) << "\","
       << "\"GL_RENDERER\":\"" << json_escape(t.gl_renderer) << "\","
       << "\"GL_VERSION\":\"" << json_escape(t.gl_version) << "\","
       << "\"CPU_HARDWARE\":\"" << json_escape(t.cpu_hardware) << "\","
       << "\"CPU_CORES\":" << t.cpu_cores << ","
       << "\"CPU_MAX_FREQ\":" << t.cpu_max_freq << ","
       << "\"RAM_GB\":" << t.ram_gb << ","
       << "\"DDR_TYPE\":\"" << json_escape(t.ddr_type) << "\","
       << "\"DDR_VENDOR\":\"" << json_escape(t.ddr_vendor) << "\","
       << "\"SOC_ID\":\"" << json_escape(t.soc_id) << "\","
       << "\"SOC_MACHINE\":\"" << json_escape(t.soc_machine) << "\","
       << "\"KERNEL_RELEASE\":\"" << json_escape(t.kernel_release) << "\","
       << "\"KERNEL_VERSION\":\"" << json_escape(t.kernel_version) << "\","
       << "\"CUSTOM_PROPS\":\"" << json_escape(t.custom_props) << "\""
       << "}";
    return ss.str();
}

inline DeviceTemplate template_from_kv_map(const std::unordered_map<std::string, std::string> &m) {
    auto get = [&](const char *k, const char *def = "") -> std::string {
        auto it = m.find(k);
        return (it != m.end() && !it->second.empty()) ? it->second : def;
    };
    DeviceTemplate t;
    t.id               = get("id");
    t.name             = get("name");
    t.model            = get("MODEL");
    t.brand            = get("BRAND");
    t.manufacturer     = get("MANUFACTURER", t.brand.c_str());
    t.device           = get("DEVICE");
    t.product          = get("PRODUCT", t.device.c_str());
    t.board            = get("BOARD");
    t.hardware         = get("HARDWARE", "qcom");
    t.bootloader       = get("BOOTLOADER", "unknown");
    t.soc_manufacturer = get("SOC_MANUFACTURER", "Qualcomm");
    t.soc_model        = get("SOC_MODEL");
    t.supported_abis   = get("SUPPORTED_ABIS", "arm64-v8a,armeabi-v7a,armeabi");
    t.build_id         = get("BUILD_ID", "AP3A.240905.015.A2");
    t.build_time       = get("BUILD_TIME", "1736899200000");
    t.release          = get("RELEASE", "15");
    t.sdk_int          = get("SDK_INT", "35");
    t.security_patch   = get("SECURITY_PATCH", "2025-02-01");
    t.fingerprint      = get("FINGERPRINT");
    t.gl_vendor        = get("GL_VENDOR", "Qualcomm");
    t.gl_renderer      = get("GL_RENDERER", "Adreno (TM) 830");
    t.gl_version       = get("GL_VERSION", "OpenGL ES 3.2 V@0800.0");
    t.cpu_hardware     = get("CPU_HARDWARE", "Qualcomm Technologies, Inc SM8750");
    t.cpu_cores        = std::atoi(get("CPU_CORES", "8").c_str());
    t.cpu_max_freq     = std::atoi(get("CPU_MAX_FREQ", "4320000").c_str());
    t.ram_gb           = std::atoi(get("RAM_GB", "16").c_str());
    t.ddr_type         = get("DDR_TYPE", "LPDDR5X");
    t.ddr_vendor       = get("DDR_VENDOR", "SKhynix");
    t.soc_id           = get("SOC_ID", "618");
    t.soc_machine      = get("SOC_MACHINE", "SM8750");
    t.kernel_release   = get("KERNEL_RELEASE", "6.6.30-android15-8-g94b8f12ab4c1");
    t.kernel_version   = get("KERNEL_VERSION", "#1 SMP PREEMPT Fri Nov 15 12:00:00 UTC 2025");
    t.custom_props     = get("CUSTOM_PROPS");
    if (t.cpu_cores <= 0) t.cpu_cores = 8;
    if (t.ram_gb <= 0) t.ram_gb = 16;
    return t;
}

} // namespace mirage::cli

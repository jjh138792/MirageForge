#pragma once

#include <cstdint>
#include <cstddef>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cerrno>
#include <string>
#include <vector>
#include <map>
#include <unordered_map>
#include <sstream>
#include <fstream>
#include <algorithm>
#include <unistd.h>
#include <fcntl.h>
#include <dirent.h>
#include <sys/stat.h>
#include <sys/file.h>

namespace mfcli {

constexpr const char *CLI_VERSION = "1.0.0";
constexpr const char *DATA_ROOT   = "/data/adb/mirage_forge";
constexpr const char *TPL_DIR     = "/data/adb/mirage_forge/templates";
constexpr const char *APPS_DIR    = "/data/adb/mirage_forge/apps";
constexpr const char *ROUTES_FILE = "/data/adb/mirage_forge/routes.db";
constexpr const char *LOCK_FILE   = "/data/adb/mirage_forge/.lock";

// RAII 全局文件排他锁，防止并发写坏配置
class ScopedFileLock {
public:
    ScopedFileLock() {
        mkdir(DATA_ROOT, 0700);
        fd_ = open(LOCK_FILE, O_CREAT | O_RDWR, 0600);
        if (fd_ >= 0) {
            flock(fd_, LOCK_EX);
        }
    }
    ~ScopedFileLock() {
        if (fd_ >= 0) {
            flock(fd_, LOCK_UN);
            close(fd_);
        }
    }
private:
    int fd_ = -1;
};

// 字符串首尾空白裁剪
inline std::string trim(const std::string &s) {
    size_t first = s.find_first_not_of(" \t\r\n");
    if (first == std::string::npos) return "";
    size_t last = s.find_last_not_of(" \t\r\n");
    return s.substr(first, last - first + 1);
}

// 字符串切分
inline std::vector<std::string> split(const std::string &s, char delim) {
    std::vector<std::string> out;
    size_t start = 0;
    while (start <= s.size()) {
        size_t pos = s.find(delim, start);
        if (pos == std::string::npos) pos = s.size();
        out.push_back(s.substr(start, pos - start));
        start = pos + 1;
    }
    return out;
}

// 校验标识符是否合法 (仅允许字母、数字、下划线、点、横杠)
inline bool is_safe_id(const std::string &s) {
    if (s.empty() || s.size() > 128) return false;
    if (s == "." || s == "..") return false;
    for (char c : s) {
        if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
              (c >= '0' && c <= '9') || c == '_' || c == '.' || c == '-')) {
            return false;
        }
    }
    return true;
}

// 标准 JSON 字符串转义
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

// Base64 解码器（供 WebUI 无损传输多行文本与特殊字符）
inline std::string base64_decode(const std::string &in) {
    static const int8_t T[256] = {
        -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
        -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
        -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,62,-1,-1,-1,63,
        52,53,54,55,56,57,58,59,60,61,-1,-1,-1,-1,-1,-1,
        -1, 0, 1, 2, 3, 4, 5, 6, 7, 8, 9,10,11,12,13,14,
        15,16,17,18,19,20,21,22,23,24,25,-1,-1,-1,-1,-1,
        -1,26,27,28,29,30,31,32,33,34,35,36,37,38,39,40,
        41,42,43,44,45,46,47,48,49,50,51,-1,-1,-1,-1,-1,
        -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
        -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
        -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
        -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
        -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
        -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
        -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
        -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1
    };
    std::string out;
    out.reserve(in.size() * 3 / 4);
    int val = 0, valb = -8;
    for (unsigned char c : in) {
        if (T[c] == -1) {
            if (c == '=') break;
            continue;
        }
        val = (val << 6) + T[c];
        valb += 6;
        if (valb >= 0) {
            out.push_back(static_cast<char>((val >> valb) & 0xFF));
            valb -= 8;
        }
    }
    return out;
}

// 原子写文件：先写入 .tmp 并 fsync，再原子 rename 覆盖目标文件，赋予 0600 权限
inline bool atomic_write_file(const std::string &path, const std::string &content, mode_t mode = 0600) {
    std::string tmp_path = path + ".tmp." + std::to_string(getpid());
    int fd = open(tmp_path.c_str(), O_WRONLY | O_CREAT | O_TRUNC, mode);
    if (fd < 0) return false;

    const char *ptr = content.data();
    size_t left = content.size();
    while (left > 0) {
        ssize_t written = write(fd, ptr, left);
        if (written <= 0) {
            if (errno == EINTR) continue;
            close(fd);
            unlink(tmp_path.c_str());
            return false;
        }
        ptr += written;
        left -= static_cast<size_t>(written);
    }
    fsync(fd);
    close(fd);
    chmod(tmp_path.c_str(), mode);

    if (rename(tmp_path.c_str(), path.c_str()) != 0) {
        unlink(tmp_path.c_str());
        return false;
    }
    return true;
}

inline bool read_file_to_string(const std::string &path, std::string *out) {
    std::ifstream ifs(path, std::ios::in | std::ios::binary);
    if (!ifs.is_open()) return false;
    std::ostringstream ss;
    ss << ifs.rdbuf();
    *out = ss.str();
    return true;
}

inline void ensure_directories() {
    mkdir(DATA_ROOT, 0700);
    mkdir(TPL_DIR, 0700);
    mkdir(APPS_DIR, 0700);
    chmod(DATA_ROOT, 0700);
    chmod(TPL_DIR, 0700);
    chmod(APPS_DIR, 0700);
}

} // namespace mfcli

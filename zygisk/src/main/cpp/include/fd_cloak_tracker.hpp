#pragma once

#include <cstdio>
#include <cstring>
#include <string>
#include <unordered_map>
#include <mutex>
#include <unistd.h>
#include <sys/stat.h>
#include "mirage_shared.hpp"

namespace mirage {

struct CloakedFdEntry {
    std::string fake_target_path;
    size_t content_size;
};

template <typename StatT>
inline void populate_virtual_stat(StatT *buf, const std::string &fake_path, size_t content_size) {
    if (!buf) return;
    std::memset(buf, 0, sizeof(StatT));
    buf->st_dev = 0xFE00;
    buf->st_ino = static_cast<decltype(buf->st_ino)>(hash_str(fake_path) & 0x7FFFFFFFull);
    buf->st_mode = S_IFREG | S_IRUSR | S_IRGRP | S_IROTH; // 0100444
    buf->st_nlink = 1;
    buf->st_uid = 0;
    buf->st_gid = 0;
    // Linux /proc 与 /sys 伪文件系统下节点 st_size 为 0 或 4096，普通 build.prop 为真实字节数
    if (fake_path.rfind("/proc/", 0) == 0) {
        buf->st_size = 0;
    } else if (fake_path.rfind("/sys/", 0) == 0) {
        buf->st_size = 4096;
    } else {
        buf->st_size = static_cast<decltype(buf->st_size)>(content_size);
    }
    buf->st_blksize = 4096;
}

class FdCloakTracker {
public:
    void register_fd(int fd, const std::string &original_path, size_t content_size) {
        if (fd < 0) return;
        std::lock_guard<std::mutex> lock(mtx_);
        table_[fd] = CloakedFdEntry{original_path, content_size};
    }

    void unregister_fd(int fd) {
        if (fd < 0) return;
        std::lock_guard<std::mutex> lock(mtx_);
        table_.erase(fd);
    }

    bool resolve_proc_fd_link(const char *pathname, std::string *out_real_path) {
        if (!pathname) return false;
        const char *fd_seg = std::strstr(pathname, "/fd/");
        if (!fd_seg) return false;
        if (std::strncmp(pathname, "/proc/", 6) != 0) return false;

        char *endptr = nullptr;
        long fd_num = std::strtol(fd_seg + 4, &endptr, 10);
        if (endptr == fd_seg + 4 || *endptr != '\0' || fd_num < 0) return false;

        std::lock_guard<std::mutex> lock(mtx_);
        auto it = table_.find(static_cast<int>(fd_num));
        if (it == table_.end()) return false;
        if (out_real_path) *out_real_path = it->second.fake_target_path;
        return true;
    }

    template <typename StatT>
    bool get_cloaked_stat(int fd, StatT *buf) {
        if (fd < 0 || !buf) return false;
        std::lock_guard<std::mutex> lock(mtx_);
        auto it = table_.find(fd);
        if (it == table_.end()) return false;
        populate_virtual_stat(buf, it->second.fake_target_path, it->second.content_size);
        return true;
    }

private:
    std::mutex mtx_;
    std::unordered_map<int, CloakedFdEntry> table_;
};

inline FdCloakTracker g_fd_cloak;

} // namespace mirage

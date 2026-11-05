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
    std::string fake_target_path; // 例如 "/proc/cpuinfo" 或 "/system/build.prop"
    size_t content_size;
};

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

    // 检查如 "/proc/self/fd/42" 或 "/proc/12345/fd/42" 是否指向我们的虚拟 memfd
    bool resolve_proc_fd_link(const char *pathname, std::string *out_real_path) {
        if (!pathname) return false;
        const char *fd_Segment = std::strstr(pathname, "/fd/");
        if (!fd_Segment) return false;
        if (std::strncmp(pathname, "/proc/", 6) != 0) return false;

        char *endptr = nullptr;
        long fd_num = std::strtol(fd_Segment + 4, &endptr, 10);
        if (endptr == fd_Segment + 4 || *endptr != '\0' || fd_num < 0) return false;

        std::lock_guard<std::mutex> lock(mtx_);
        auto it = table_.find(static_cast<int>(fd_num));
        if (it == table_.end()) return false;
        if (out_real_path) *out_real_path = it->second.fake_target_path;
        return true;
    }

    bool get_cloaked_stat(int fd, struct stat *buf) {
        if (fd < 0 || !buf) return false;
        std::lock_guard<std::mutex> lock(mtx_);
        auto it = table_.find(fd);
        if (it == table_.end()) return false;

        std::memset(buf, 0, sizeof(struct stat));
        buf->st_dev = 0xFE00; // 伪装块设备号，隐藏匿名 tmpfs/memfd 特征
        buf->st_ino = static_cast<ino_t>(hash_str(it->second.fake_target_path) & 0x7FFFFFFFull);
        buf->st_mode = S_IFREG | S_IRUSR | S_IRGRP | S_IROTH; // 0100444 只读普通文件
        buf->st_nlink = 1;
        buf->st_uid = 0;
        buf->st_gid = 0;
        buf->st_size = (it->second.fake_target_path.rfind("/proc/", 0) == 0)
                           ? 0 // 真实 Linux /proc 伪文件 st_size 恒为 0！
                           : static_cast<off_t>(it->second.content_size);
        buf->st_blksize = 4096;
        return true;
    }

private:
    std::mutex mtx_;
    std::unordered_map<int, CloakedFdEntry> table_;
};

inline FdCloakTracker g_fd_cloak;

} // namespace mirage

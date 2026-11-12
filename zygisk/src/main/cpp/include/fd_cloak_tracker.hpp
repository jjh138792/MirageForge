#pragma once

#include <cstdio>
#include <cstring>
#include <string>
#include <unordered_map>
#include <atomic>
#include <unistd.h>
#include <sys/stat.h>
#include "mirage_config.hpp"

namespace mirage {

struct CloakedFdEntry {
    std::string fake_target_path;
    size_t content_size;
    int dynamic_core_id;
    uint64_t memfd_ino; // 记录虚拟 memfd 的真实内核 inode，防止 fd 关闭复用后误伤真实文件
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
    buf->st_size = (fake_path.rfind("/proc/", 0) == 0)
                       ? 0
                       : static_cast<decltype(buf->st_size)>(content_size);
    buf->st_blksize = 4096;
}

class FdCloakTracker {
public:
    static constexpr int kFastBitmapSize = 4096;

    inline bool is_tracked_fast(int fd) const noexcept {
        if (__builtin_expect(active_count_.load(std::memory_order_relaxed) == 0, 1)) {
            return false;
        }
        if (fd < 0) return false;
        if (__builtin_expect(fd < kFastBitmapSize, 1)) {
            return fast_bitmap_[fd].load(std::memory_order_relaxed);
        }
        return true;
    }

    void register_fd(int fd, const std::string &original_path, size_t content_size,
                     int dyn_core_id = -1, uint64_t real_memfd_ino = 0) {
        if (fd < 0) return;
        lock();
        bool is_new = (table_.find(fd) == table_.end());
        table_[fd] = CloakedFdEntry{original_path, content_size, dyn_core_id, real_memfd_ino};
        if (fd < kFastBitmapSize) {
            fast_bitmap_[fd].store(true, std::memory_order_release);
        }
        if (is_new) {
            active_count_.fetch_add(1, std::memory_order_release);
        }
        unlock();
    }

    void unregister_fd(int fd) noexcept {
        if (__builtin_expect(!is_tracked_fast(fd), 1)) return;
        lock();
        auto it = table_.find(fd);
        if (it != table_.end()) {
            table_.erase(it);
            if (fd < kFastBitmapSize) {
                fast_bitmap_[fd].store(false, std::memory_order_release);
            }
            active_count_.fetch_sub(1, std::memory_order_release);
        }
        unlock();
    }

    bool get_dynamic_core_id(int fd, int *out_core_id, uint64_t cur_real_ino = 0) noexcept {
        if (__builtin_expect(!is_tracked_fast(fd), 1)) return false;
        lock();
        auto it = table_.find(fd);
        if (it == table_.end()) {
            unlock();
            return false;
        }
        // 若传入了当前内核 inode 且与注册时不符，说明该 fd 已被复用，立即自愈剔除
        if (cur_real_ino != 0 && it->second.memfd_ino != 0 && cur_real_ino != it->second.memfd_ino) {
            table_.erase(it);
            if (fd < kFastBitmapSize) {
                fast_bitmap_[fd].store(false, std::memory_order_release);
            }
            active_count_.fetch_sub(1, std::memory_order_release);
            unlock();
            return false;
        }
        bool ok = (it->second.dynamic_core_id >= 0);
        if (ok && out_core_id) *out_core_id = it->second.dynamic_core_id;
        unlock();
        return ok;
    }

    bool resolve_proc_fd_link(const char *pathname, std::string *out_real_path) {
        if (__builtin_expect(active_count_.load(std::memory_order_relaxed) == 0, 1)) return false;
        if (!pathname) return false;
        const char *fd_seg = std::strstr(pathname, "/fd/");
        if (!fd_seg || std::strncmp(pathname, "/proc/", 6) != 0) return false;

        char *endptr = nullptr;
        long fd_num = std::strtol(fd_seg + 4, &endptr, 10);
        if (endptr == fd_seg + 4 || *endptr != '\0' || fd_num < 0) return false;

        int fd = static_cast<int>(fd_num);
        if (!is_tracked_fast(fd)) return false;

        lock();
        auto it = table_.find(fd);
        bool ok = (it != table_.end());
        if (ok && out_real_path) *out_real_path = it->second.fake_target_path;
        unlock();
        return ok;
    }

    template <typename StatT>
    bool get_cloaked_stat(int fd, StatT *buf, uint64_t cur_real_ino = 0) {
        if (__builtin_expect(!is_tracked_fast(fd), 1) || !buf) return false;
        std::string fake_path;
        size_t sz = 0;
        lock();
        auto it = table_.find(fd);
        if (it == table_.end()) {
            unlock();
            return false;
        }
        // 核心自愈校验：一旦发现当前 fd 的真实 inode 与 memfd 创建时的 inode 不同，
        // 说明该 fd 已被关闭并被系统复用给字体/资源文件，立即剔除脏记录并放行！
        if (cur_real_ino != 0 && it->second.memfd_ino != 0 && cur_real_ino != it->second.memfd_ino) {
            table_.erase(it);
            if (fd < kFastBitmapSize) {
                fast_bitmap_[fd].store(false, std::memory_order_release);
            }
            active_count_.fetch_sub(1, std::memory_order_release);
            unlock();
            return false;
        }
        fake_path = it->second.fake_target_path;
        sz = it->second.content_size;
        unlock();

        populate_virtual_stat(buf, fake_path, sz);
        return true;
    }

private:
    inline void lock() noexcept {
        while (spin_.test_and_set(std::memory_order_acquire)) {}
    }
    inline void unlock() noexcept {
        spin_.clear(std::memory_order_release);
    }

    std::atomic_flag spin_ = ATOMIC_FLAG_INIT;
    std::atomic<uint32_t> active_count_{0};
    std::atomic<bool> fast_bitmap_[kFastBitmapSize]{};
    std::unordered_map<int, CloakedFdEntry> table_;
};

inline FdCloakTracker g_fd_cloak;

} // namespace mirage

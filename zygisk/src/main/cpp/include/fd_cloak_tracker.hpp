#pragma once

#include <cstdio>
#include <cstring>
#include <string>
#include <unordered_map>
#include <atomic>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/syscall.h>
#include "mirage_config.hpp"

namespace mirage {

struct CloakedFdEntry {
    std::string fake_target_path;
    size_t content_size;
    int dynamic_core_id;
    dev_t memfd_dev;
    ino_t memfd_ino;
};

// 直接通过内核 fstat 系统调用获取当前 fd 的真实 (dev, ino)，不经过任何 PLT Hook
inline bool query_raw_fd_identity(int fd, dev_t *out_dev, ino_t *out_ino) noexcept {
    if (fd < 0) return false;
    struct stat st{};
#if defined(__NR_fstat)
    if (syscall(__NR_fstat, fd, &st) == 0) {
        if (out_dev) *out_dev = st.st_dev;
        if (out_ino) *out_ino = st.st_ino;
        return true;
    }
#endif
    return false;
}

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

    void register_fd(int fd, const std::string &original_path, size_t content_size, int dyn_core_id = -1) {
        if (fd < 0) return;
        dev_t dev = 0;
        ino_t ino = 0;
        query_raw_fd_identity(fd, &dev, &ino);

        lock();
        bool is_new = (table_.find(fd) == table_.end());
        table_[fd] = CloakedFdEntry{original_path, content_size, dyn_core_id, dev, ino};
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
        erase_locked(fd);
        unlock();
    }

    bool get_dynamic_core_id(int fd, int *out_core_id) noexcept {
        if (__builtin_expect(!is_tracked_fast(fd), 1)) return false;
        dev_t cur_dev = 0;
        ino_t cur_ino = 0;
        bool has_id = query_raw_fd_identity(fd, &cur_dev, &cur_ino);

        lock();
        auto it = table_.find(fd);
        if (it == table_.end()) {
            unlock();
            return false;
        }
        // 校验内核 inode 指纹：若该 fd 已被关闭并复用作其他真实文件，立即自我清理！
        if (has_id && it->second.memfd_ino != 0 &&
            (it->second.memfd_dev != cur_dev || it->second.memfd_ino != cur_ino)) {
            erase_locked(fd);
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

        dev_t cur_dev = 0;
        ino_t cur_ino = 0;
        bool has_id = query_raw_fd_identity(fd, &cur_dev, &cur_ino);

        lock();
        auto it = table_.find(fd);
        if (it == table_.end()) {
            unlock();
            return false;
        }
        if (has_id && it->second.memfd_ino != 0 &&
            (it->second.memfd_dev != cur_dev || it->second.memfd_ino != cur_ino)) {
            erase_locked(fd);
            unlock();
            return false;
        }
        if (out_real_path) *out_real_path = it->second.fake_target_path;
        unlock();
        return true;
    }

    template <typename StatT>
    bool get_cloaked_stat(int fd, StatT *buf) {
        if (__builtin_expect(!is_tracked_fast(fd), 1) || !buf) return false;
        dev_t cur_dev = 0;
        ino_t cur_ino = 0;
        bool has_id = query_raw_fd_identity(fd, &cur_dev, &cur_ino);

        std::string fake_path;
        size_t sz = 0;
        lock();
        auto it = table_.find(fd);
        if (it == table_.end()) {
            unlock();
            return false;
        }
        // 核心防闪退保护：如果 fd 已经被底层关闭并重新分配给字体/资源文件，立即注销并放行！
        if (has_id && it->second.memfd_ino != 0 &&
            (it->second.memfd_dev != cur_dev || it->second.memfd_ino != cur_ino)) {
            erase_locked(fd);
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
    inline void erase_locked(int fd) noexcept {
        auto it = table_.find(fd);
        if (it != table_.end()) {
            table_.erase(it);
            if (fd >= 0 && fd < kFastBitmapSize) {
                fast_bitmap_[fd].store(false, std::memory_order_release);
            }
            active_count_.fetch_sub(1, std::memory_order_release);
        }
    }

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

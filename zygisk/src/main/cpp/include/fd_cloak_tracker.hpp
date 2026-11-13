#pragma once

#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <string>
#include <atomic>
#include <unistd.h>

namespace mirage {

// O(1) 无锁位图 memfd 隐身追踪器：追踪虚拟文件描述符并还原 /proc/self/fd/<N> 真实路径
class FdCloakTracker {
public:
    static constexpr int kMaxTrackedFds = 1024;
    static constexpr int kBitmapWords = kMaxTrackedFds / 64;

    FdCloakTracker() noexcept {
        for (int i = 0; i < kBitmapWords; ++i) {
            bitmap_[i].store(0ULL, std::memory_order_relaxed);
        }
    }

    inline void track(int fd, const char *real_path) noexcept {
        if (fd < 0 || fd >= kMaxTrackedFds || !real_path) return;

        while (spinlock_.test_and_set(std::memory_order_acquire)) {}
        std::strncpy(real_paths_[fd], real_path, sizeof(real_paths_[fd]) - 1);
        real_paths_[fd][sizeof(real_paths_[fd]) - 1] = '\0';
        spinlock_.clear(std::memory_order_release);

        int word = fd >> 6;
        uint64_t bit = 1ULL << (fd & 63);
        bitmap_[word].fetch_or(bit, std::memory_order_release);
    }

    inline void register_fd(int fd, const std::string &real_path) noexcept {
        track(fd, real_path.c_str());
    }

    inline void untrack(int fd) noexcept {
        if (fd < 0 || fd >= kMaxTrackedFds) return;
        int word = fd >> 6;
        uint64_t bit = 1ULL << (fd & 63);
        if ((bitmap_[word].load(std::memory_order_relaxed) & bit) == 0) {
            return;
        }
        bitmap_[word].fetch_and(~bit, std::memory_order_release);

        while (spinlock_.test_and_set(std::memory_order_acquire)) {}
        real_paths_[fd][0] = '\0';
        spinlock_.clear(std::memory_order_release);
    }

    inline void unregister_fd(int fd) noexcept {
        untrack(fd);
    }

    inline bool is_cloaked(int fd) const noexcept {
        if (fd < 0 || fd >= kMaxTrackedFds) return false;
        int word = fd >> 6;
        uint64_t bit = 1ULL << (fd & 63);
        return (bitmap_[word].load(std::memory_order_acquire) & bit) != 0;
    }

    inline bool is_tracked(int fd) const noexcept {
        return is_cloaked(fd);
    }

    // 指针版重载
    inline bool resolve_proc_fd_link(const char *pathname, std::string *out_real_path) const {
        if (!pathname || !out_real_path) return false;
        int fd = parse_proc_fd_path(pathname);
        if (!is_cloaked(fd)) return false;

        while (spinlock_.test_and_set(std::memory_order_acquire)) {}
        const char *saved = real_paths_[fd];
        bool ok = (saved[0] != '\0');
        if (ok) {
            *out_real_path = saved;
        }
        spinlock_.clear(std::memory_order_release);
        return ok;
    }

    // 引用版重载 (适配 vfs_sysinfo_engine.hpp 直接传入 std::string&)
    inline bool resolve_proc_fd_link(const char *pathname, std::string &out_real_path) const {
        return resolve_proc_fd_link(pathname, &out_real_path);
    }

private:
    static inline int parse_proc_fd_path(const char *path) noexcept {
        const char *p = nullptr;
        if (std::strncmp(path, "/proc/self/fd/", 14) == 0) {
            p = path + 14;
        } else if (std::strncmp(path, "/proc/thread-self/fd/", 21) == 0) {
            p = path + 21;
        } else if (std::strncmp(path, "/proc/", 6) == 0) {
            const char *q = path + 6;
            while (*q >= '0' && *q <= '9') ++q;
            if (std::strncmp(q, "/fd/", 4) == 0) {
                p = q + 4;
            }
        }
        if (!p || *p < '0' || *p > '9') return -1;

        int fd = 0;
        while (*p >= '0' && *p <= '9') {
            fd = fd * 10 + (*p - '0');
            if (fd >= kMaxTrackedFds) return -1;
            ++p;
        }
        return (*p == '\0') ? fd : -1;
    }

    std::atomic<uint64_t> bitmap_[kBitmapWords]{};
    mutable std::atomic_flag spinlock_ = ATOMIC_FLAG_INIT;
    char real_paths_[kMaxTrackedFds][128]{};
};

inline FdCloakTracker g_fd_cloak;

} // namespace mirage

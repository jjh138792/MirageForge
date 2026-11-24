// SPDX-License-Identifier: GPL-3.0-only
// Copyright (c) 2025-2026 jjh138792
// MirageForge: https://github.com/jjh138792/MirageForge

#pragma once

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <unordered_set>
#include <atomic>
#include <unistd.h>
#include <fcntl.h>
#include <dlfcn.h>
#include <sys/stat.h>
#include <sys/sysinfo.h>
#include <sys/utsname.h>
#include <sys/syscall.h>
#include <linux/memfd.h>
#include "mirage_config.hpp"
#include "mirage_logger.hpp"
#include "fd_cloak_tracker.hpp"

#ifndef MFD_CLOEXEC
#define MFD_CLOEXEC 0x0001U
#endif

namespace mirage {

// 私有 Mount NS 生效状态与已挂载路径表
inline std::atomic<bool> g_vfs_ns_mounted{false};
inline std::unordered_set<std::string> g_ns_mounted_paths;

inline bool is_ns_mounted_path(const std::string &norm) noexcept {
    if (__builtin_expect(!g_vfs_ns_mounted.load(std::memory_order_acquire), 1)) {
        return false;
    }
    return g_ns_mounted_paths.find(norm) != g_ns_mounted_paths.end();
}

// 原始 Bionic I/O & 系统信息函数指针
inline int (*orig_open)(const char *pathname, int flags, mode_t mode) = nullptr;
inline int (*orig_open64)(const char *pathname, int flags, mode_t mode) = nullptr;
inline int (*orig_open_2)(const char *pathname, int flags) = nullptr;
inline int (*orig_openat)(int dirfd, const char *pathname, int flags, mode_t mode) = nullptr;
inline int (*orig_openat64)(int dirfd, const char *pathname, int flags, mode_t mode) = nullptr;
inline int (*orig_openat_2)(int dirfd, const char *pathname, int flags) = nullptr;
inline int (*orig_openat64_2)(int dirfd, const char *pathname, int flags) = nullptr;
inline FILE *(*orig_fopen)(const char *pathname, const char *mode) = nullptr;
inline FILE *(*orig_fopen64)(const char *pathname, const char *mode) = nullptr;
inline int (*orig_fclose)(FILE *fp) = nullptr;
inline int (*orig_close)(int fd) = nullptr;
inline int (*orig_fdsan_close)(int fd, uint64_t tag) = nullptr;
inline off_t (*orig_lseek)(int fd, off_t offset, int whence) = nullptr;
inline off64_t (*orig_lseek64)(int fd, off64_t offset, int whence) = nullptr;

inline int (*orig_access)(const char *pathname, int mode) = nullptr;
inline int (*orig_faccessat)(int dirfd, const char *pathname, int mode, int flags) = nullptr;
inline int (*orig_stat)(const char *pathname, struct stat *statbuf) = nullptr;
inline int (*orig_stat64)(const char *pathname, struct stat64 *statbuf) = nullptr;
inline int (*orig_lstat)(const char *pathname, struct stat *statbuf) = nullptr;
inline int (*orig_lstat64)(const char *pathname, struct stat64 *statbuf) = nullptr;
inline int (*orig_fstatat)(int dirfd, const char *pathname, struct stat *statbuf, int flags) = nullptr;
inline int (*orig_fstatat64)(int dirfd, const char *pathname, struct stat64 *statbuf, int flags) = nullptr;
inline int (*orig_fstat)(int fd, struct stat *statbuf) = nullptr;
inline int (*orig_fstat64)(int fd, struct stat64 *statbuf) = nullptr;

inline ssize_t (*orig_readlink)(const char *pathname, char *buf, size_t bufsiz) = nullptr;
inline ssize_t (*orig_readlinkat)(int dirfd, const char *pathname, char *buf, size_t bufsiz) = nullptr;
inline FILE *(*orig_popen)(const char *command, const char *type) = nullptr;
inline int (*orig_uname)(struct utsname *buf) = nullptr;
inline int (*orig_sysinfo)(struct sysinfo *info) = nullptr;
inline long (*orig_sysconf)(int name) = nullptr;

inline void init_vfs_symbols() {
    if (orig_openat) return;
    ErrnoRestorer errno_guard;
    orig_open = reinterpret_cast<decltype(orig_open)>(dlsym(RTLD_DEFAULT, "open"));
    orig_open64 = reinterpret_cast<decltype(orig_open64)>(dlsym(RTLD_DEFAULT, "open64"));
    orig_open_2 = reinterpret_cast<decltype(orig_open_2)>(dlsym(RTLD_DEFAULT, "__open_2"));
    orig_openat = reinterpret_cast<decltype(orig_openat)>(dlsym(RTLD_DEFAULT, "openat"));
    orig_openat64 = reinterpret_cast<decltype(orig_openat64)>(dlsym(RTLD_DEFAULT, "openat64"));
    orig_openat_2 = reinterpret_cast<decltype(orig_openat_2)>(dlsym(RTLD_DEFAULT, "__openat_2"));
    orig_openat64_2 = reinterpret_cast<decltype(orig_openat64_2)>(dlsym(RTLD_DEFAULT, "__openat64_2"));
    orig_fopen = reinterpret_cast<decltype(orig_fopen)>(dlsym(RTLD_DEFAULT, "fopen"));
    orig_fopen64 = reinterpret_cast<decltype(orig_fopen64)>(dlsym(RTLD_DEFAULT, "fopen64"));
    orig_fclose = reinterpret_cast<decltype(orig_fclose)>(dlsym(RTLD_DEFAULT, "fclose"));
    orig_close = reinterpret_cast<decltype(orig_close)>(dlsym(RTLD_DEFAULT, "close"));
    orig_fdsan_close = reinterpret_cast<decltype(orig_fdsan_close)>(dlsym(RTLD_DEFAULT, "android_fdsan_close_with_tag"));
    orig_lseek = reinterpret_cast<decltype(orig_lseek)>(dlsym(RTLD_DEFAULT, "lseek"));
    orig_lseek64 = reinterpret_cast<decltype(orig_lseek64)>(dlsym(RTLD_DEFAULT, "lseek64"));

    orig_access = reinterpret_cast<decltype(orig_access)>(dlsym(RTLD_DEFAULT, "access"));
    orig_faccessat = reinterpret_cast<decltype(orig_faccessat)>(dlsym(RTLD_DEFAULT, "faccessat"));
    orig_stat = reinterpret_cast<decltype(orig_stat)>(dlsym(RTLD_DEFAULT, "stat"));
    orig_stat64 = reinterpret_cast<decltype(orig_stat64)>(dlsym(RTLD_DEFAULT, "stat64"));
    orig_lstat = reinterpret_cast<decltype(orig_lstat)>(dlsym(RTLD_DEFAULT, "lstat"));
    orig_lstat64 = reinterpret_cast<decltype(orig_lstat64)>(dlsym(RTLD_DEFAULT, "lstat64"));
    orig_fstatat = reinterpret_cast<decltype(orig_fstatat)>(dlsym(RTLD_DEFAULT, "fstatat"));
    orig_fstatat64 = reinterpret_cast<decltype(orig_fstatat64)>(dlsym(RTLD_DEFAULT, "fstatat64"));
    orig_fstat = reinterpret_cast<decltype(orig_fstat)>(dlsym(RTLD_DEFAULT, "fstat"));
    orig_fstat64 = reinterpret_cast<decltype(orig_fstat64)>(dlsym(RTLD_DEFAULT, "fstat64"));

    orig_readlink = reinterpret_cast<decltype(orig_readlink)>(dlsym(RTLD_DEFAULT, "readlink"));
    orig_readlinkat = reinterpret_cast<decltype(orig_readlinkat)>(dlsym(RTLD_DEFAULT, "readlinkat"));
    orig_popen = reinterpret_cast<decltype(orig_popen)>(dlsym(RTLD_DEFAULT, "popen"));
    orig_uname = reinterpret_cast<decltype(orig_uname)>(dlsym(RTLD_DEFAULT, "uname"));
    orig_sysinfo = reinterpret_cast<decltype(orig_sysinfo)>(dlsym(RTLD_DEFAULT, "sysinfo"));
    orig_sysconf = reinterpret_cast<decltype(orig_sysconf)>(dlsym(RTLD_DEFAULT, "sysconf"));
}

inline bool is_vfs_candidate_prefix(const char *p) noexcept {
    if (!p || p[0] != '/') return false;
    while (*p == '/') ++p;
    return (p[0] == 'p' || p[0] == 's' || p[0] == 'v' || p[0] == 'o');
}

inline bool is_cpufreq_relative_candidate(const char *rel) noexcept {
    if (!rel || rel[0] == '/' || rel[0] == '.') return false;
    char c = rel[0];
    return (c == 's' || c == 'c' || c == 'o' || c == 'r' || c == 'a' || c == 't' || c == 'm' || c == 'p');
}

inline std::string normalize_vfs_path(const char *raw_path) {
    if (!is_vfs_candidate_prefix(raw_path)) return "";
    std::string out;
    out.reserve(std::strlen(raw_path));
    bool prev_slash = false;
    for (const char *p = raw_path; *p != '\0'; ++p) {
        if (*p == '/') {
            if (!prev_slash) out.push_back('/');
            prev_slash = true;
        } else {
            out.push_back(*p);
            prev_slash = false;
        }
    }
    while (out.size() > 1 && out.back() == '/') {
        out.pop_back();
    }
    return out;
}

inline std::string resolve_dirfd_path_fast(int dirfd, const char *pathname) {
    if (!pathname) return "";
    if (pathname[0] == '/') return normalize_vfs_path(pathname);
    if (dirfd < 0 || dirfd == AT_FDCWD || !is_cpufreq_relative_candidate(pathname)) return "";

    ErrnoRestorer errno_guard;
    char link_target[256] = {0};
    char proc_fd_path[64];
    std::snprintf(proc_fd_path, sizeof(proc_fd_path), "/proc/self/fd/%d", dirfd);
    ssize_t len = orig_readlink
                      ? orig_readlink(proc_fd_path, link_target, sizeof(link_target) - 1)
                      : readlink(proc_fd_path, link_target, sizeof(link_target) - 1);
    if (len <= 0 || !is_vfs_candidate_prefix(link_target)) return "";
    link_target[len] = '\0';

    std::string combined = std::string(link_target) + "/" + pathname;
    return normalize_vfs_path(combined.c_str());
}

} // namespace mirage

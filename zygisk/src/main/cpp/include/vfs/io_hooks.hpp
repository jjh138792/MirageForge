// SPDX-License-Identifier: GPL-3.0-only
// Copyright (c) 2025-2026 jjh138792
// MirageForge: https://github.com/jjh138792/MirageForge

#pragma once

#include <cstdio>
#include <cstring>
#include <string>
#include <algorithm>
#include "vfs/vfs_common.hpp"
#include "vfs/cpu_topology.hpp"
#include "vfs/mem_sysinfo.hpp"
#include "vfs/proc_cloak.hpp"

namespace mirage {

inline bool synthesize_vfs_node_on_demand(const std::string &norm, std::string *out_content, int *out_dyn_core = nullptr) {
    if (norm.empty() || !out_content) return false;
    if (!g_cfg.vfs.any_enabled() && !g_cfg.build.enabled) return false;
    if (out_dyn_core) *out_dyn_core = -1;

    if ((g_cfg.vfs.enable_cpu || g_cfg.vfs.enable_vfs_nodes) &&
        synthesize_cpu_vfs_node(norm, out_content, out_dyn_core)) {
        return true;
    }
    if ((g_cfg.vfs.enable_mem || g_cfg.vfs.enable_vfs_nodes) &&
        synthesize_mem_soc_vfs_node(norm, out_content)) {
        return true;
    }
    if (g_cfg.vfs.any_enabled() && norm.rfind("/proc/", 0) == 0 &&
        sanitize_live_proc_file(norm, out_content)) {
        return true;
    }
    if (g_cfg.build.enabled && synthesize_build_prop_vfs_node(norm, out_content)) {
        return true;
    }
    return false;
}

inline int open_cloaked_vfs_fd_norm(const std::string &norm) {
    if (norm.empty() || (!g_cfg.vfs.any_enabled() && !g_cfg.build.enabled)) return -1;
    if (is_ns_mounted_path(norm)) {
        emit_probe("VFS_MOUNT_NS", norm, "Served by Kernel Bind-Mount");
        return -1;
    }

    ErrnoRestorer errno_guard;
    std::string content;
    int dyn_core_id = -1;
    if (!synthesize_vfs_node_on_demand(norm, &content, &dyn_core_id)) {
        return -1;
    }

    int fd = static_cast<int>(syscall(__NR_memfd_create, "", MFD_CLOEXEC));
    if (fd < 0) return -1;

    if (!content.empty()) {
        write_full(fd, content.data(), content.size());
        if (orig_lseek) orig_lseek(fd, 0, SEEK_SET); else lseek(fd, 0, SEEK_SET);
    }

    g_fd_cloak.register_fd(fd, norm, content.size(), dyn_core_id);
    emit_probe("VFS_OPEN", norm, (dyn_core_id >= 0) ? ("LiveFreq:" + content.substr(0, content.size() - 1)) : "Cloaked");
    return fd;
}

inline int open_cloaked_vfs_fd(const char *raw_path) {
    if (__builtin_expect(!is_vfs_candidate_prefix(raw_path), 1)) return -1;
    return open_cloaked_vfs_fd_norm(normalize_vfs_path(raw_path));
}

inline int hook_open(const char *pathname, int flags, mode_t mode) {
    if (__builtin_expect(is_vfs_candidate_prefix(pathname), 0)) {
        ScopedHookGuard guard;
        if (guard) {
            int fake_fd = open_cloaked_vfs_fd(pathname);
            if (fake_fd >= 0) return fake_fd;
        }
    }
    return orig_open ? orig_open(pathname, flags, mode) : -1;
}

inline int hook_open64(const char *pathname, int flags, mode_t mode) {
    if (__builtin_expect(is_vfs_candidate_prefix(pathname), 0)) {
        ScopedHookGuard guard;
        if (guard) {
            int fake_fd = open_cloaked_vfs_fd(pathname);
            if (fake_fd >= 0) return fake_fd;
        }
    }
    return orig_open64 ? orig_open64(pathname, flags, mode) : (orig_open ? orig_open(pathname, flags, mode) : -1);
}

inline int hook_open_2(const char *pathname, int flags) {
    if (__builtin_expect(is_vfs_candidate_prefix(pathname), 0)) {
        ScopedHookGuard guard;
        if (guard) {
            int fake_fd = open_cloaked_vfs_fd(pathname);
            if (fake_fd >= 0) return fake_fd;
        }
    }
    return orig_open_2 ? orig_open_2(pathname, flags) : (orig_open ? orig_open(pathname, flags, 0) : -1);
}

inline int hook_openat(int dirfd, const char *pathname, int flags, mode_t mode) {
    if (__builtin_expect(is_vfs_candidate_prefix(pathname) || is_cpufreq_relative_candidate(pathname), 0)) {
        ScopedHookGuard guard;
        if (guard) {
            int fake_fd = open_cloaked_vfs_fd_norm(resolve_dirfd_path_fast(dirfd, pathname));
            if (fake_fd >= 0) return fake_fd;
        }
    }
    return orig_openat ? orig_openat(dirfd, pathname, flags, mode) : -1;
}

inline int hook_openat64(int dirfd, const char *pathname, int flags, mode_t mode) {
    if (__builtin_expect(is_vfs_candidate_prefix(pathname) || is_cpufreq_relative_candidate(pathname), 0)) {
        ScopedHookGuard guard;
        if (guard) {
            int fake_fd = open_cloaked_vfs_fd_norm(resolve_dirfd_path_fast(dirfd, pathname));
            if (fake_fd >= 0) return fake_fd;
        }
    }
    return orig_openat64 ? orig_openat64(dirfd, pathname, flags, mode) : (orig_openat ? orig_openat(dirfd, pathname, flags, mode) : -1);
}

inline int hook_openat_2(int dirfd, const char *pathname, int flags) {
    if (__builtin_expect(is_vfs_candidate_prefix(pathname) || is_cpufreq_relative_candidate(pathname), 0)) {
        ScopedHookGuard guard;
        if (guard) {
            int fake_fd = open_cloaked_vfs_fd_norm(resolve_dirfd_path_fast(dirfd, pathname));
            if (fake_fd >= 0) return fake_fd;
        }
    }
    return orig_openat_2 ? orig_openat_2(dirfd, pathname, flags) : (orig_openat ? orig_openat(dirfd, pathname, flags, 0) : -1);
}

inline int hook_openat64_2(int dirfd, const char *pathname, int flags) {
    if (__builtin_expect(is_vfs_candidate_prefix(pathname) || is_cpufreq_relative_candidate(pathname), 0)) {
        ScopedHookGuard guard;
        if (guard) {
            int fake_fd = open_cloaked_vfs_fd_norm(resolve_dirfd_path_fast(dirfd, pathname));
            if (fake_fd >= 0) return fake_fd;
        }
    }
    return orig_openat64_2 ? orig_openat64_2(dirfd, pathname, flags) : (orig_openat_2 ? orig_openat_2(dirfd, pathname, flags) : -1);
}

inline FILE *hook_fopen(const char *pathname, const char *mode) {
    if (__builtin_expect(is_vfs_candidate_prefix(pathname), 0)) {
        ScopedHookGuard guard;
        if (guard) {
            int fake_fd = open_cloaked_vfs_fd(pathname);
            if (fake_fd >= 0) {
                FILE *fp = fdopen(fake_fd, "r");
                if (fp) return fp;
                g_fd_cloak.unregister_fd(fake_fd);
                if (orig_close) orig_close(fake_fd); else close(fake_fd);
            }
        }
    }
    return orig_fopen ? orig_fopen(pathname, mode) : nullptr;
}

inline FILE *hook_fopen64(const char *pathname, const char *mode) {
    return hook_fopen(pathname, mode);
}

inline int hook_fclose(FILE *fp) {
    if (fp) {
        int fd = fileno(fp);
        if (fd >= 0) g_fd_cloak.unregister_fd(fd);
    }
    return orig_fclose ? orig_fclose(fp) : std::fclose(fp);
}

inline int hook_close(int fd) {
    g_fd_cloak.unregister_fd(fd);
    return orig_close ? orig_close(fd) : close(fd);
}

inline int hook_android_fdsan_close_with_tag(int fd, uint64_t tag) {
    g_fd_cloak.unregister_fd(fd);
    return orig_fdsan_close ? orig_fdsan_close(fd, tag) : (orig_close ? orig_close(fd) : close(fd));
}

inline void refresh_dynamic_freq_fd_on_rewind(int fd) {
    int core_id = -1;
    if (__builtin_expect(g_fd_cloak.get_dynamic_core_id(fd, &core_id), 0)) {
        ErrnoRestorer errno_guard;
        std::string next_freq = std::to_string(sample_live_core_freq_khz(core_id)) + "\n";
        if (orig_lseek) orig_lseek(fd, 0, SEEK_SET);
        write_full(fd, next_freq.data(), next_freq.size());
        ftruncate(fd, static_cast<off_t>(next_freq.size()));
    }
}

inline off_t hook_lseek(int fd, off_t offset, int whence) {
    if (offset == 0 && whence == SEEK_SET) {
        refresh_dynamic_freq_fd_on_rewind(fd);
    }
    return orig_lseek ? orig_lseek(fd, offset, whence) : lseek(fd, offset, whence);
}

inline off64_t hook_lseek64(int fd, off64_t offset, int whence) {
    if (offset == 0 && whence == SEEK_SET) {
        refresh_dynamic_freq_fd_on_rewind(fd);
    }
    return orig_lseek64 ? orig_lseek64(fd, offset, whence) : lseek64(fd, offset, whence);
}

inline int hook_access(const char *pathname, int mode) {
    if (__builtin_expect(is_vfs_candidate_prefix(pathname), 0)) {
        ScopedHookGuard guard;
        std::string norm = normalize_vfs_path(pathname);
        if (guard && !is_ns_mounted_path(norm)) {
            std::string content;
            if (synthesize_vfs_node_on_demand(norm, &content)) return 0;
        }
    }
    return orig_access ? orig_access(pathname, mode) : -1;
}

inline int hook_faccessat(int dirfd, const char *pathname, int mode, int flags) {
    if (__builtin_expect(is_vfs_candidate_prefix(pathname) || is_cpufreq_relative_candidate(pathname), 0)) {
        ScopedHookGuard guard;
        std::string norm = resolve_dirfd_path_fast(dirfd, pathname);
        if (guard && !is_ns_mounted_path(norm)) {
            std::string content;
            if (synthesize_vfs_node_on_demand(norm, &content)) return 0;
        }
    }
    return orig_faccessat ? orig_faccessat(dirfd, pathname, mode, flags) : -1;
}

inline int hook_stat(const char *pathname, struct stat *statbuf) {
    if (__builtin_expect(is_vfs_candidate_prefix(pathname), 0)) {
        ScopedHookGuard guard;
        std::string norm = normalize_vfs_path(pathname);
        if (guard && statbuf && !is_ns_mounted_path(norm)) {
            std::string content;
            if (synthesize_vfs_node_on_demand(norm, &content)) {
                populate_virtual_stat(statbuf, norm, content.size());
                return 0;
            }
        }
    }
    return orig_stat ? orig_stat(pathname, statbuf) : -1;
}

inline int hook_stat64(const char *pathname, struct stat64 *statbuf) {
    if (__builtin_expect(is_vfs_candidate_prefix(pathname), 0)) {
        ScopedHookGuard guard;
        std::string norm = normalize_vfs_path(pathname);
        if (guard && statbuf && !is_ns_mounted_path(norm)) {
            std::string content;
            if (synthesize_vfs_node_on_demand(norm, &content)) {
                populate_virtual_stat(statbuf, norm, content.size());
                return 0;
            }
        }
    }
    return orig_stat64 ? orig_stat64(pathname, statbuf) : -1;
}

inline int hook_lstat(const char *pathname, struct stat *statbuf) {
    return hook_stat(pathname, statbuf);
}

inline int hook_lstat64(const char *pathname, struct stat64 *statbuf) {
    return hook_stat64(pathname, statbuf);
}

inline int hook_fstatat(int dirfd, const char *pathname, struct stat *statbuf, int flags) {
    if (__builtin_expect(is_vfs_candidate_prefix(pathname) || is_cpufreq_relative_candidate(pathname), 0)) {
        ScopedHookGuard guard;
        std::string norm = resolve_dirfd_path_fast(dirfd, pathname);
        if (guard && statbuf && !is_ns_mounted_path(norm)) {
            std::string content;
            if (synthesize_vfs_node_on_demand(norm, &content)) {
                populate_virtual_stat(statbuf, norm, content.size());
                return 0;
            }
        }
    }
    return orig_fstatat ? orig_fstatat(dirfd, pathname, statbuf, flags) : -1;
}

inline int hook_fstatat64(int dirfd, const char *pathname, struct stat64 *statbuf, int flags) {
    if (__builtin_expect(is_vfs_candidate_prefix(pathname) || is_cpufreq_relative_candidate(pathname), 0)) {
        ScopedHookGuard guard;
        std::string norm = resolve_dirfd_path_fast(dirfd, pathname);
        if (guard && statbuf && !is_ns_mounted_path(norm)) {
            std::string content;
            if (synthesize_vfs_node_on_demand(norm, &content)) {
                populate_virtual_stat(statbuf, norm, content.size());
                return 0;
            }
        }
    }
    return orig_fstatat64 ? orig_fstatat64(dirfd, pathname, statbuf, flags) : -1;
}

inline int hook_fstat(int fd, struct stat *statbuf) {
    if (__builtin_expect(g_fd_cloak.get_cloaked_stat(fd, statbuf), 0)) {
        return 0;
    }
    return orig_fstat ? orig_fstat(fd, statbuf) : -1;
}

inline int hook_fstat64(int fd, struct stat64 *statbuf) {
    if (__builtin_expect(g_fd_cloak.get_cloaked_stat(fd, statbuf), 0)) {
        return 0;
    }
    return orig_fstat64 ? orig_fstat64(fd, statbuf) : -1;
}

inline ssize_t hook_readlink(const char *pathname, char *buf, size_t bufsiz) {
    std::string cloaked_path;
    if (__builtin_expect(g_fd_cloak.resolve_proc_fd_link(pathname, &cloaked_path), 0) && buf && bufsiz > 0) {
        size_t copy_len = std::min(bufsiz, cloaked_path.size());
        std::memcpy(buf, cloaked_path.data(), copy_len);
        ScopedHookGuard guard;
        if (guard) emit_probe("FD_CLOAK", pathname, cloaked_path);
        return static_cast<ssize_t>(copy_len);
    }
    return orig_readlink ? orig_readlink(pathname, buf, bufsiz) : -1;
}

inline ssize_t hook_readlinkat(int dirfd, const char *pathname, char *buf, size_t bufsiz) {
    std::string cloaked_path;
    if (__builtin_expect(g_fd_cloak.resolve_proc_fd_link(pathname, &cloaked_path), 0) && buf && bufsiz > 0) {
        size_t copy_len = std::min(bufsiz, cloaked_path.size());
        std::memcpy(buf, cloaked_path.data(), copy_len);
        ScopedHookGuard guard;
        if (guard) emit_probe("FD_CLOAK", pathname, cloaked_path);
        return static_cast<ssize_t>(copy_len);
    }
    return orig_readlinkat ? orig_readlinkat(dirfd, pathname, buf, bufsiz) : -1;
}

} // namespace mirage

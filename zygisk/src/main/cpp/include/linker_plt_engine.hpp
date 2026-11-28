// SPDX-License-Identifier: GPL-3.0-only
// Copyright (c) 2025-2026 jjh138792
// MirageForge: https://github.com/jjh138792/MirageForge

#pragma once

#include <cstdio>
#include <cstring>
#include <cinttypes>
#include <set>
#include <tuple>
#include <utility>
#include <mutex>
#include <dlfcn.h>
#include <sys/stat.h>
#include <sys/sysmacros.h>
#include "zygisk.hpp"
#include "mirage_config.hpp"
#include "jvm_art_engine.hpp"
#include "bionic_prop_engine.hpp"
#include "vfs_sysinfo_engine.hpp"
#include "graphics_vulkan_gles.hpp"

namespace mirage {

inline zygisk::Api *g_zygisk_api = nullptr;
inline std::mutex g_maps_mtx;
// 关键修复：按 (dev, inode, file_offset) 三元组去重！
// 解决未解压 Split APK 中多个 .so 共享同一 (dev, inode) 导致后续懒加载 libvulkan_info.so / libaida_vulkan.so 被跳过的问题
inline std::set<std::tuple<dev_t, ino_t, uintptr_t>> g_hooked_elf_segments;
inline void *g_discard_old_slot = nullptr;
inline dev_t g_self_dev = 0;
inline ino_t g_self_ino = 0;

using PFN_loader_dlopen = void *(*)(const char *filename, int flags, const void *caller_addr);
using PFN_loader_android_dlopen_ext = void *(*)(const char *filename, int flags, const void *extinfo, const void *caller_addr);

inline void *(*orig_dlopen)(const char *filename, int flags) = nullptr;
inline void *(*orig_android_dlopen_ext)(const char *filename, int flags, const void *extinfo) = nullptr;
inline PFN_loader_dlopen orig_loader_dlopen = nullptr;
inline PFN_loader_android_dlopen_ext orig_loader_android_dlopen_ext = nullptr;

inline void scan_and_commit_plt_hooks();

inline bool needs_display_lazy_hook() noexcept {
    return (g_cfg.build.enabled || g_cfg.gl.enabled) &&
           (g_cfg.gl.max_refresh_rate >= 90 || g_cfg.gl.enable_hdr_spoof);
}

inline bool needs_vfs_syscall_plt() noexcept {
    if (g_cfg.vfs.enable_mem && g_cfg.vfs.ram_gb > 0) return true;
    if (g_cfg.vfs.enable_cpu && (g_cfg.vfs.cpu_cores > 0 || !g_cfg.vfs.kernel_release.empty())) return true;
    if (g_cfg.vfs.enable_vfs_nodes && !g_cfg.vfs.kernel_release.empty()) return true;
    return false;
}

inline bool needs_legacy_vfs_file_plt() noexcept {
    if (g_cfg.vfs.any_enabled() && !g_vfs_ns_mounted.load(std::memory_order_acquire)) {
        return true;
    }
    if (g_cfg.build.enabled && g_cfg.build.effective_prop_mode == PropSpoofMode::HOOK) {
        return true;
    }
    return false;
}

inline bool needs_any_plt_hooks() noexcept {
    return needs_prop_plt_hooks() ||
           g_cfg.gl.enabled ||
           needs_display_lazy_hook() ||
           needs_vfs_syscall_plt() ||
           needs_legacy_vfs_file_plt() ||
           g_cfg.probe.enabled;
}

inline bool should_use_file_io_plt() noexcept {
    if (needs_legacy_vfs_file_plt()) return true;
    return needs_any_plt_hooks() &&
           (g_vfs_ns_mounted.load(std::memory_order_acquire) ||
            g_prop_ns_mounted.load(std::memory_order_acquire));
}

inline void init_linker_symbols() {
    if (orig_dlopen) return;
    ErrnoRestorer errno_guard;
    orig_dlopen = reinterpret_cast<decltype(orig_dlopen)>(dlsym(RTLD_DEFAULT, "dlopen"));
    orig_android_dlopen_ext = reinterpret_cast<decltype(orig_android_dlopen_ext)>(dlsym(RTLD_DEFAULT, "android_dlopen_ext"));
    orig_loader_dlopen = reinterpret_cast<PFN_loader_dlopen>(dlsym(RTLD_DEFAULT, "__loader_dlopen"));
    orig_loader_android_dlopen_ext = reinterpret_cast<PFN_loader_android_dlopen_ext>(dlsym(RTLD_DEFAULT, "__loader_android_dlopen_ext"));
    if (!orig_loader_dlsym) {
        orig_loader_dlsym = reinterpret_cast<PFN_loader_dlsym>(dlsym(RTLD_DEFAULT, "__loader_dlsym"));
    }
    if (!orig_dlsym) {
        orig_dlsym = reinterpret_cast<PFN_dlsym>(dlsym(RTLD_DEFAULT, "dlsym"));
    }

    Dl_info self_info{};
    if (dladdr(reinterpret_cast<const void *>(&init_linker_symbols), &self_info) && self_info.dli_fname) {
        struct stat st{};
        if (::stat(self_info.dli_fname, &st) == 0) {
            g_self_dev = st.st_dev;
            g_self_ino = st.st_ino;
        }
    }
    g_on_vulkan_prop_hit = scan_and_commit_plt_hooks;
}

inline bool ends_with_ext(const char *path, size_t path_len, const char *ext) noexcept {
    size_t ext_len = std::strlen(ext);
    if (path_len < ext_len) return false;
    return std::memcmp(path + path_len - ext_len, ext, ext_len) == 0;
}

inline bool should_hook_elf_path(const char *path, size_t len) noexcept {
    if (path[0] != '/') return false;

    if (std::strstr(path, "mirage_forge") != nullptr ||
        std::strncmp(path, "/data/adb/", 10) == 0 ||
        std::strstr(path, "libpairipcore") != nullptr) {
        return false;
    }

    bool is_binary = ends_with_ext(path, len, ".so") ||
                     ends_with_ext(path, len, ".apk") ||
                     (std::strstr(path, ".so.") != nullptr);
    if (!is_binary) return false;

    const char *slash = std::strrchr(path, '/');
    const char *fname = slash ? (slash + 1) : path;

    if (std::strcmp(fname, "libc.so") == 0 ||
        std::strcmp(fname, "libdl.so") == 0 ||
        std::strcmp(fname, "libart.so") == 0 ||
        std::strncmp(fname, "linker", 6) == 0) {
        return false;
    }

    if (std::strncmp(path, "/data/", 6) == 0 ||
        std::strncmp(path, "/mnt/", 5) == 0 ||
        ends_with_ext(path, len, ".apk")) {
        return true;
    }

    static const char *const kSystemBridgeLibs[] = {
        "libnativeloader.so",
        "libnativeloader_lazy.so",
        "libandroid_runtime.so",
        "libjavacore.so",
        "libopenjdk.so",
        "libopenjdkjvm.so",
        "libmeminfo.so",
        "libprocinfo.so",
        "libhardware.so",
        "libvndksupport.so",
        "libgraphicsenv.so",
        "libc++.so",
        "libcutils.so",
        "libutils.so",
        "libbase.so",
        "libandroid.so",
        "libEGL.so",
        "libGLESv1_CM.so",
        "libGLESv2.so",
        "libGLESv3.so",
        "libvulkan.so",
        "libmediandk.so",
        "libgui.so",
        "libhwui.so",
        "libandroidio.so"
    };

    for (const char *bridge : kSystemBridgeLibs) {
        if (std::strcmp(fname, bridge) == 0) {
            return true;
        }
    }
    return false;
}

inline void *hook_dlsym_all(void *handle, const char *symbol) {
    const void *caller_addr = __builtin_return_address(0);
    void *real_sym = nullptr;
    if (orig_loader_dlsym) {
        real_sym = orig_loader_dlsym(handle, symbol, caller_addr);
    } else if (orig_dlsym) {
        real_sym = orig_dlsym(handle, symbol);
    }

    if (!symbol || !real_sym || g_tls_in_hook) {
        return real_sym;
    }

    if (g_cfg.gl.enabled) {
        if (std::strcmp(symbol, "HMI") == 0) {
            const auto *wrapped = wrap_hwvulkan_module_if_needed(static_cast<const HwModuleCompat *>(real_sym));
            return const_cast<HwModuleCompat *>(wrapped);
        }
        if (std::strcmp(symbol, "hw_get_module") == 0) {
            auto fn = reinterpret_cast<PFN_hw_get_module>(real_sym);
            if (fn != hook_hw_get_module) orig_hw_get_module = fn;
            return reinterpret_cast<void *>(hook_hw_get_module);
        }
        if (std::strcmp(symbol, "hw_get_module_by_class") == 0) {
            auto fn = reinterpret_cast<PFN_hw_get_module_by_class>(real_sym);
            if (fn != hook_hw_get_module_by_class) orig_hw_get_module_by_class = fn;
            return reinterpret_cast<void *>(hook_hw_get_module_by_class);
        }
        if (std::strcmp(symbol, "vk_icdGetInstanceProcAddr") == 0) {
            auto fn = reinterpret_cast<PFN_vkGetInstanceProcAddr>(real_sym);
            if (fn != hook_icd_GetInstanceProcAddr) orig_icd_GetInstanceProcAddr = fn;
            return reinterpret_cast<void *>(hook_icd_GetInstanceProcAddr);
        }
        if (std::strcmp(symbol, "vkGetInstanceProcAddr") == 0) {
            auto fn = reinterpret_cast<PFN_vkGetInstanceProcAddr>(real_sym);
            if (fn != hook_vkGetInstanceProcAddr) orig_vkGetInstanceProcAddr = fn;
            return reinterpret_cast<void *>(hook_vkGetInstanceProcAddr);
        }
        if (std::strcmp(symbol, "vkGetPhysicalDeviceProperties") == 0) {
            auto fn = reinterpret_cast<PFN_vkGetPhysicalDeviceProperties>(real_sym);
            if (fn != hook_vkGetPhysicalDeviceProperties) orig_vkGetPhysicalDeviceProperties = fn;
            return reinterpret_cast<void *>(hook_vkGetPhysicalDeviceProperties);
        }
        if (std::strcmp(symbol, "vkGetPhysicalDeviceProperties2") == 0 ||
            std::strcmp(symbol, "vkGetPhysicalDeviceProperties2KHR") == 0) {
            auto fn = reinterpret_cast<PFN_vkGetPhysicalDeviceProperties2>(real_sym);
            if (fn != hook_vkGetPhysicalDeviceProperties2) orig_vkGetPhysicalDeviceProperties2 = fn;
            return reinterpret_cast<void *>(hook_vkGetPhysicalDeviceProperties2);
        }
        if (std::strcmp(symbol, "glGetString") == 0) {
            auto fn = reinterpret_cast<decltype(orig_glGetString)>(real_sym);
            if (fn != hook_glGetString) orig_glGetString = fn;
            return reinterpret_cast<void *>(hook_glGetString);
        }
        if (std::strcmp(symbol, "eglGetProcAddress") == 0) {
            auto fn = reinterpret_cast<decltype(orig_eglGetProcAddress)>(real_sym);
            if (fn != hook_eglGetProcAddress) orig_eglGetProcAddress = fn;
            return reinterpret_cast<void *>(hook_eglGetProcAddress);
        }
        if (std::strcmp(symbol, "AMediaDrm_getPropertyString") == 0) {
            auto fn = reinterpret_cast<decltype(orig_AMediaDrm_getPropertyString)>(real_sym);
            if (fn != hook_AMediaDrm_getPropertyString) orig_AMediaDrm_getPropertyString = fn;
            return reinterpret_cast<void *>(hook_AMediaDrm_getPropertyString);
        }
    }

    if (should_use_file_io_plt()) {
        if (std::strcmp(symbol, "open") == 0) return reinterpret_cast<void *>(hook_open);
        if (std::strcmp(symbol, "open64") == 0) return reinterpret_cast<void *>(hook_open64);
        if (std::strcmp(symbol, "openat") == 0) return reinterpret_cast<void *>(hook_openat);
        if (std::strcmp(symbol, "openat64") == 0) return reinterpret_cast<void *>(hook_openat64);
        if (std::strcmp(symbol, "fopen") == 0) return reinterpret_cast<void *>(hook_fopen);
        if (std::strcmp(symbol, "fopen64") == 0) return reinterpret_cast<void *>(hook_fopen64);
        if (std::strcmp(symbol, "statfs") == 0) return reinterpret_cast<void *>(hook_statfs);
        if (std::strcmp(symbol, "statfs64") == 0) return reinterpret_cast<void *>(hook_statfs64);
        if (std::strcmp(symbol, "fstatfs") == 0) return reinterpret_cast<void *>(hook_fstatfs);
        if (std::strcmp(symbol, "fstatfs64") == 0) return reinterpret_cast<void *>(hook_fstatfs64);
    }
    if (g_cfg.vfs.enable_mem && std::strcmp(symbol, "sysinfo") == 0) {
        return reinterpret_cast<void *>(hook_sysinfo);
    }
    if ((g_cfg.vfs.enable_cpu || g_cfg.vfs.enable_mem) && std::strcmp(symbol, "sysconf") == 0) {
        return reinterpret_cast<void *>(hook_sysconf);
    }
    if ((g_cfg.vfs.enable_cpu || g_cfg.vfs.enable_vfs_nodes) && std::strcmp(symbol, "uname") == 0) {
        return reinterpret_cast<void *>(hook_uname);
    }

    return real_sym;
}

inline bool should_rescan_on_dlopen(const char *filename) noexcept {
    if (!filename) return false;
    if (std::strstr(filename, "libpairipcore") != nullptr) return false;
    if (g_cfg.gl.enabled &&
        (std::strstr(filename, "vulkan") != nullptr ||
         std::strstr(filename, "libhardware") != nullptr ||
         std::strstr(filename, "libvndksupport") != nullptr ||
         std::strstr(filename, "libGLES") != nullptr ||
         std::strstr(filename, "libEGL") != nullptr)) {
        return true;
    }
    if (std::strncmp(filename, "/system/", 8) == 0 ||
        std::strncmp(filename, "/apex/", 6) == 0 ||
        std::strncmp(filename, "/vendor/", 8) == 0) {
        return false;
    }
    return true;
}

inline void *hook_dlopen(const char *filename, int flags) {
    const void *caller_addr = __builtin_return_address(0);
    void *handle = orig_loader_dlopen
                       ? orig_loader_dlopen(filename, flags, caller_addr)
                       : (orig_dlopen ? orig_dlopen(filename, flags) : nullptr);
    if (handle && should_rescan_on_dlopen(filename)) {
        ScopedHookGuard guard;
        if (guard) {
            emit_probe("DLOPEN", filename, "Incremental PLT Hook");
            scan_and_commit_plt_hooks();
            if (needs_display_lazy_hook()) {
                try_lazy_spoof_display_modes();
            }
        }
    }
    return handle;
}

inline void *hook_android_dlopen_ext(const char *filename, int flags, const void *extinfo) {
    const void *caller_addr = __builtin_return_address(0);
    void *handle = orig_loader_android_dlopen_ext
                       ? orig_loader_android_dlopen_ext(filename, flags, extinfo, caller_addr)
                       : (orig_android_dlopen_ext ? orig_android_dlopen_ext(filename, flags, extinfo) : nullptr);
    if (handle && should_rescan_on_dlopen(filename)) {
        ScopedHookGuard guard;
        if (guard) {
            emit_probe("DLOPEN_EXT", filename, "Incremental PLT Hook");
            scan_and_commit_plt_hooks();
            if (needs_display_lazy_hook()) {
                try_lazy_spoof_display_modes();
            }
        }
    }
    return handle;
}

inline void scan_and_commit_plt_hooks() {
    if (!g_zygisk_api || !needs_any_plt_hooks()) return;
    ErrnoRestorer errno_guard;
    if (!g_maps_mtx.try_lock()) return;

    FILE *fp = orig_fopen ? orig_fopen("/proc/self/maps", "r") : std::fopen("/proc/self/maps", "r");
    if (!fp) {
        g_maps_mtx.unlock();
        return;
    }

    const bool use_file_plt = should_use_file_io_plt();
    const bool use_prop_plt = needs_prop_plt_hooks();
    const bool use_popen_plt = g_cfg.build.enabled && (g_cfg.build.effective_prop_mode != PropSpoofMode::MOUNT_NS);

    std::set<std::pair<dev_t, ino_t>> batch_to_hook;
    char line[1024];

    while (std::fgets(line, sizeof(line), fp)) {
        const char *sp = std::strchr(line, ' ');
        if (!sp || sp[1] == '\0' || sp[2] == '\0' || sp[3] != 'x') {
            continue;
        }

        uintptr_t start = 0, end = 0, offset = 0;
        unsigned int dev_maj = 0, dev_min = 0;
        unsigned long inode_ul = 0;
        char perms[8] = {0};
        int path_pos = 0;

        if (std::sscanf(line, "%" SCNxPTR "-%" SCNxPTR " %7s %" SCNxPTR " %x:%x %lu %n",
                        &start, &end, perms, &offset, &dev_maj, &dev_min, &inode_ul, &path_pos) < 7) {
            continue;
        }
        if (inode_ul == 0) continue;

        dev_t dev = makedev(dev_maj, dev_min);
        ino_t inode = static_cast<ino_t>(inode_ul);
        if (g_self_ino != 0 && dev == g_self_dev && inode == g_self_ino) {
            continue;
        }

        auto seg_id = std::make_tuple(dev, inode, offset);
        if (g_hooked_elf_segments.find(seg_id) != g_hooked_elf_segments.end()) {
            continue;
        }

        char *path = line + path_pos;
        while (*path == ' ' || *path == '\t') ++path;
        size_t len = std::strlen(path);
        while (len > 0 && (path[len - 1] == '\n' || path[len - 1] == '\r')) {
            path[--len] = '\0';
        }

        if (!should_hook_elf_path(path, len)) {
            continue;
        }

        g_hooked_elf_segments.insert(seg_id);
        batch_to_hook.insert(std::make_pair(dev, inode));
    }
    if (orig_fclose) orig_fclose(fp); else std::fclose(fp);

    for (const auto &elf_id : batch_to_hook) {
        dev_t dev = elf_id.first;
        ino_t inode = elf_id.second;

        auto reg = [&](const char *sym, void *new_fn, void **orig_slot) {
            void **target_old = (orig_slot && *orig_slot == nullptr) ? orig_slot : &g_discard_old_slot;
            g_zygisk_api->pltHookRegister(dev, inode, sym, new_fn, target_old);
        };

        reg("dlopen", reinterpret_cast<void *>(hook_dlopen), reinterpret_cast<void **>(&orig_dlopen));
        reg("android_dlopen_ext", reinterpret_cast<void *>(hook_android_dlopen_ext), reinterpret_cast<void **>(&orig_android_dlopen_ext));
        if (g_cfg.gl.enabled || g_cfg.vfs.any_enabled() || use_file_plt) {
            reg("dlsym", reinterpret_cast<void *>(hook_dlsym_all), reinterpret_cast<void **>(&orig_dlsym));
        }

        if (use_prop_plt) {
            reg("__system_property_find", reinterpret_cast<void *>(hook_system_property_find), reinterpret_cast<void **>(&orig_system_property_find));
            reg("__system_property_read_callback", reinterpret_cast<void *>(hook_system_property_read_callback), reinterpret_cast<void **>(&orig_system_property_read_callback));
            reg("__system_property_read", reinterpret_cast<void *>(hook_system_property_read), reinterpret_cast<void **>(&orig_system_property_read));
            reg("__system_property_get", reinterpret_cast<void *>(hook_system_property_get), reinterpret_cast<void **>(&orig_system_property_get));
            reg("__system_property_foreach", reinterpret_cast<void *>(hook_system_property_foreach), reinterpret_cast<void **>(&orig_system_property_foreach));
            reg("__system_property_find_nth", reinterpret_cast<void *>(hook_system_property_find_nth), reinterpret_cast<void **>(&orig_system_property_find_nth));
        }
        if (use_popen_plt) {
            reg("popen", reinterpret_cast<void *>(hook_popen), reinterpret_cast<void **>(&orig_popen));
        }

        if (use_file_plt) {
            reg("open", reinterpret_cast<void *>(hook_open), reinterpret_cast<void **>(&orig_open));
            reg("open64", reinterpret_cast<void *>(hook_open64), reinterpret_cast<void **>(&orig_open64));
            reg("__open_2", reinterpret_cast<void *>(hook_open_2), reinterpret_cast<void **>(&orig_open_2));
            reg("__open64_2", reinterpret_cast<void *>(hook_open_2), reinterpret_cast<void **>(&orig_open_2));
            reg("openat", reinterpret_cast<void *>(hook_openat), reinterpret_cast<void **>(&orig_openat));
            reg("openat64", reinterpret_cast<void *>(hook_openat64), reinterpret_cast<void **>(&orig_openat64));
            reg("__openat_2", reinterpret_cast<void *>(hook_openat_2), reinterpret_cast<void **>(&orig_openat_2));
            reg("__openat64_2", reinterpret_cast<void *>(hook_openat64_2), reinterpret_cast<void **>(&orig_openat64_2));
            reg("fopen", reinterpret_cast<void *>(hook_fopen), reinterpret_cast<void **>(&orig_fopen));
            reg("fopen64", reinterpret_cast<void *>(hook_fopen64), reinterpret_cast<void **>(&orig_fopen64));
            reg("fclose", reinterpret_cast<void *>(hook_fclose), reinterpret_cast<void **>(&orig_fclose));
            reg("close", reinterpret_cast<void *>(hook_close), reinterpret_cast<void **>(&orig_close));
            reg("android_fdsan_close_with_tag", reinterpret_cast<void *>(hook_android_fdsan_close_with_tag), reinterpret_cast<void **>(&orig_fdsan_close));
            reg("lseek", reinterpret_cast<void *>(hook_lseek), reinterpret_cast<void **>(&orig_lseek));
            reg("lseek64", reinterpret_cast<void *>(hook_lseek64), reinterpret_cast<void **>(&orig_lseek64));
            reg("access", reinterpret_cast<void *>(hook_access), reinterpret_cast<void **>(&orig_access));
            reg("faccessat", reinterpret_cast<void *>(hook_faccessat), reinterpret_cast<void **>(&orig_faccessat));
            reg("stat", reinterpret_cast<void *>(hook_stat), reinterpret_cast<void **>(&orig_stat));
            reg("stat64", reinterpret_cast<void *>(hook_stat64), reinterpret_cast<void **>(&orig_stat64));
            reg("lstat", reinterpret_cast<void *>(hook_lstat), reinterpret_cast<void **>(&orig_lstat));
            reg("lstat64", reinterpret_cast<void *>(hook_lstat64), reinterpret_cast<void **>(&orig_lstat64));
            reg("fstatat", reinterpret_cast<void *>(hook_fstatat), reinterpret_cast<void **>(&orig_fstatat));
            reg("fstatat64", reinterpret_cast<void *>(hook_fstatat64), reinterpret_cast<void **>(&orig_fstatat64));
            reg("fstat", reinterpret_cast<void *>(hook_fstat), reinterpret_cast<void **>(&orig_fstat));
            reg("fstat64", reinterpret_cast<void *>(hook_fstat64), reinterpret_cast<void **>(&orig_fstat64));
            reg("statfs", reinterpret_cast<void *>(hook_statfs), reinterpret_cast<void **>(&orig_statfs));
            reg("statfs64", reinterpret_cast<void *>(hook_statfs64), reinterpret_cast<void **>(&orig_statfs64));
            reg("fstatfs", reinterpret_cast<void *>(hook_fstatfs), reinterpret_cast<void **>(&orig_fstatfs));
            reg("fstatfs64", reinterpret_cast<void *>(hook_fstatfs64), reinterpret_cast<void **>(&orig_fstatfs64));
            reg("readlink", reinterpret_cast<void *>(hook_readlink), reinterpret_cast<void **>(&orig_readlink));
            reg("readlinkat", reinterpret_cast<void *>(hook_readlinkat), reinterpret_cast<void **>(&orig_readlinkat));
        }

        if (g_cfg.vfs.enable_mem) {
            reg("sysinfo", reinterpret_cast<void *>(hook_sysinfo), reinterpret_cast<void **>(&orig_sysinfo));
        }
        if (g_cfg.vfs.enable_cpu || g_cfg.vfs.enable_mem) {
            reg("sysconf", reinterpret_cast<void *>(hook_sysconf), reinterpret_cast<void **>(&orig_sysconf));
        }
        if (g_cfg.vfs.enable_cpu || g_cfg.vfs.enable_vfs_nodes) {
            reg("uname", reinterpret_cast<void *>(hook_uname), reinterpret_cast<void **>(&orig_uname));
        }

        if (g_cfg.gl.enabled) {
            reg("hw_get_module", reinterpret_cast<void *>(hook_hw_get_module), reinterpret_cast<void **>(&orig_hw_get_module));
            reg("hw_get_module_by_class", reinterpret_cast<void *>(hook_hw_get_module_by_class), reinterpret_cast<void **>(&orig_hw_get_module_by_class));
            reg("glGetString", reinterpret_cast<void *>(hook_glGetString), reinterpret_cast<void **>(&orig_glGetString));
            reg("eglGetProcAddress", reinterpret_cast<void *>(hook_eglGetProcAddress), reinterpret_cast<void **>(&orig_eglGetProcAddress));
            reg("vkGetInstanceProcAddr", reinterpret_cast<void *>(hook_vkGetInstanceProcAddr), reinterpret_cast<void **>(&orig_vkGetInstanceProcAddr));
            reg("vkGetPhysicalDeviceProperties", reinterpret_cast<void *>(hook_vkGetPhysicalDeviceProperties), reinterpret_cast<void **>(&orig_vkGetPhysicalDeviceProperties));
            reg("vkGetPhysicalDeviceProperties2", reinterpret_cast<void *>(hook_vkGetPhysicalDeviceProperties2), reinterpret_cast<void **>(&orig_vkGetPhysicalDeviceProperties2));
            reg("vkGetPhysicalDeviceProperties2KHR", reinterpret_cast<void *>(hook_vkGetPhysicalDeviceProperties2), reinterpret_cast<void **>(&orig_vkGetPhysicalDeviceProperties2));
            reg("AMediaDrm_getPropertyString", reinterpret_cast<void *>(hook_AMediaDrm_getPropertyString), reinterpret_cast<void **>(&orig_AMediaDrm_getPropertyString));
        }
    }

    if (!batch_to_hook.empty()) {
        g_zygisk_api->pltHookCommit();
    }
    g_maps_mtx.unlock();
}

} // namespace mirage

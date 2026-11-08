#pragma once

#include <cstdio>
#include <cstring>
#include <cinttypes>
#include <set>
#include <mutex>
#include <dlfcn.h>
#include <sys/sysmacros.h>
#include "zygisk.hpp"
#include "mirage_shared.hpp"
#include "bionic_prop_engine.hpp"
#include "vfs_sysinfo_engine.hpp"
#include "graphics_vulkan_gles.hpp"

namespace mirage {

inline zygisk::Api *g_zygisk_api = nullptr;
inline std::mutex g_maps_mtx;
inline std::set<std::pair<dev_t, ino_t>> g_hooked_elves;

inline void *(*orig_dlopen)(const char *filename, int flags) = nullptr;
inline void *(*orig_android_dlopen_ext)(const char *filename, int flags, const void *extinfo) = nullptr;
inline void *(*orig_dlsym)(void *handle, const char *symbol) = nullptr;

inline void scan_and_commit_plt_hooks();

inline void *hook_dlsym(void *handle, const char *symbol) {
    if (symbol && !g_tls_in_hook) {
        if (g_ctx.enable_build_props || g_ctx.enable_probe) {
            if (std::strcmp(symbol, "__system_property_find") == 0) return reinterpret_cast<void *>(hook_system_property_find);
            if (std::strcmp(symbol, "__system_property_read_callback") == 0) return reinterpret_cast<void *>(hook_system_property_read_callback);
            if (std::strcmp(symbol, "__system_property_read") == 0) return reinterpret_cast<void *>(hook_system_property_read);
            if (std::strcmp(symbol, "__system_property_get") == 0) return reinterpret_cast<void *>(hook_system_property_get);
            if (std::strcmp(symbol, "__system_property_foreach") == 0) return reinterpret_cast<void *>(hook_system_property_foreach);
            if (std::strcmp(symbol, "popen") == 0) return reinterpret_cast<void *>(hook_popen);
            if (std::strcmp(symbol, "AMediaDrm_getPropertyString") == 0) return reinterpret_cast<void *>(hook_AMediaDrm_getPropertyString);
        }
        if (g_ctx.enable_vfs_shield) {
            if (std::strcmp(symbol, "open") == 0) return reinterpret_cast<void *>(hook_open);
            if (std::strcmp(symbol, "open64") == 0) return reinterpret_cast<void *>(hook_open64);
            if (std::strcmp(symbol, "__open_2") == 0) return reinterpret_cast<void *>(hook_open_2);
            if (std::strcmp(symbol, "openat") == 0) return reinterpret_cast<void *>(hook_openat);
            if (std::strcmp(symbol, "openat64") == 0) return reinterpret_cast<void *>(hook_openat64);
            if (std::strcmp(symbol, "__openat_2") == 0) return reinterpret_cast<void *>(hook_openat_2);
            if (std::strcmp(symbol, "__openat64_2") == 0) return reinterpret_cast<void *>(hook_openat64_2);
            if (std::strcmp(symbol, "fopen") == 0) return reinterpret_cast<void *>(hook_fopen);
            if (std::strcmp(symbol, "fopen64") == 0) return reinterpret_cast<void *>(hook_fopen64);
            if (std::strcmp(symbol, "access") == 0) return reinterpret_cast<void *>(hook_access);
            if (std::strcmp(symbol, "faccessat") == 0) return reinterpret_cast<void *>(hook_faccessat);
            if (std::strcmp(symbol, "stat") == 0) return reinterpret_cast<void *>(hook_stat);
            if (std::strcmp(symbol, "stat64") == 0) return reinterpret_cast<void *>(hook_stat64);
            if (std::strcmp(symbol, "lstat") == 0) return reinterpret_cast<void *>(hook_lstat);
            if (std::strcmp(symbol, "lstat64") == 0) return reinterpret_cast<void *>(hook_lstat64);
            if (std::strcmp(symbol, "fstatat") == 0) return reinterpret_cast<void *>(hook_fstatat);
            if (std::strcmp(symbol, "fstatat64") == 0) return reinterpret_cast<void *>(hook_fstatat64);
            if (std::strcmp(symbol, "fstat") == 0) return reinterpret_cast<void *>(hook_fstat);
            if (std::strcmp(symbol, "fstat64") == 0) return reinterpret_cast<void *>(hook_fstat64);
            if (std::strcmp(symbol, "readlink") == 0) return reinterpret_cast<void *>(hook_readlink);
            if (std::strcmp(symbol, "readlinkat") == 0) return reinterpret_cast<void *>(hook_readlinkat);
            if (std::strcmp(symbol, "sysinfo") == 0) return reinterpret_cast<void *>(hook_sysinfo);
            if (std::strcmp(symbol, "sysconf") == 0) return reinterpret_cast<void *>(hook_sysconf);
            if (std::strcmp(symbol, "uname") == 0) return reinterpret_cast<void *>(hook_uname);
        }
        if (g_ctx.enable_gl_vulkan) {
            if (std::strcmp(symbol, "glGetString") == 0) return reinterpret_cast<void *>(hook_glGetString);
            if (std::strcmp(symbol, "eglGetProcAddress") == 0) return reinterpret_cast<void *>(hook_eglGetProcAddress);
            if (std::strcmp(symbol, "vkGetInstanceProcAddr") == 0) return reinterpret_cast<void *>(hook_vkGetInstanceProcAddr);
            if (std::strcmp(symbol, "vkGetPhysicalDeviceProperties") == 0) return reinterpret_cast<void *>(hook_vkGetPhysicalDeviceProperties);
            if (std::strcmp(symbol, "vkGetPhysicalDeviceProperties2") == 0) return reinterpret_cast<void *>(hook_vkGetPhysicalDeviceProperties2);
        }
    }
    return orig_dlsym ? orig_dlsym(handle, symbol) : nullptr;
}

inline bool should_rescan_on_dlopen(const char *filename) {
    if (!filename) return false;
    if (std::strncmp(filename, "/system/", 8) == 0 ||
        std::strncmp(filename, "/apex/", 6) == 0) {
        return false;
    }
    return true;
}

inline void *hook_dlopen(const char *filename, int flags) {
    void *handle = orig_dlopen ? orig_dlopen(filename, flags) : nullptr;
    if (handle && should_rescan_on_dlopen(filename)) {
        ScopedHookGuard guard;
        if (guard) {
            emit_probe("DLOPEN", filename, "Incremental PLT Hook");
            scan_and_commit_plt_hooks();
        }
    }
    return handle;
}

inline void *hook_android_dlopen_ext(const char *filename, int flags, const void *extinfo) {
    void *handle = orig_android_dlopen_ext ? orig_android_dlopen_ext(filename, flags, extinfo) : nullptr;
    if (handle && should_rescan_on_dlopen(filename)) {
        ScopedHookGuard guard;
        if (guard) {
            emit_probe("DLOPEN_EXT", filename, "Incremental PLT Hook");
            scan_and_commit_plt_hooks();
        }
    }
    return handle;
}

inline void scan_and_commit_plt_hooks() {
    if (!g_zygisk_api) return;
    if (!g_maps_mtx.try_lock()) return;

    FILE *fp = orig_fopen ? orig_fopen("/proc/self/maps", "r") : std::fopen("/proc/self/maps", "r");
    if (!fp) {
        g_maps_mtx.unlock();
        return;
    }

    char line[1024];
    bool dirty = false;

    while (std::fgets(line, sizeof(line), fp)) {
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

        char *path = line + path_pos;
        while (*path == ' ' || *path == '\t') ++path;
        size_t len = std::strlen(path);
        while (len > 0 && (path[len - 1] == '\n' || path[len - 1] == '\r')) {
            path[--len] = '\0';
        }

        // 兼容 extractNativeLibs="false" 直接从 base.apk / split_config.arm64_v8a.apk 映射的未解压可执行段
        bool is_exec_or_elf = (std::strchr(perms, 'x') != nullptr) ||
                              (std::strstr(path, ".so") != nullptr) ||
                              (std::strstr(path, ".apk") != nullptr);

        if (path[0] != '/' || !is_exec_or_elf ||
            std::strstr(path, ".oat") != nullptr ||
            std::strstr(path, ".odex") != nullptr ||
            std::strstr(path, ".vdex") != nullptr ||
            std::strstr(path, ".art") != nullptr ||
            std::strstr(path, ".dex") != nullptr ||
            std::strstr(path, "/libc.so") != nullptr ||
            std::strstr(path, "/libdl.so") != nullptr ||
            std::strstr(path, "/libart.so") != nullptr ||
            std::strstr(path, "/linker") != nullptr ||
            std::strstr(path, "mirage_forge") != nullptr) {
            continue;
        }

        dev_t dev = makedev(dev_maj, dev_min);
        ino_t inode = static_cast<ino_t>(inode_ul);
        auto id_pair = std::make_pair(dev, inode);
        if (!g_hooked_elves.insert(id_pair).second) {
            continue;
        }
        dirty = true;

        auto reg = [&](const char *sym, void *new_fn, void **orig_slot) {
            void *dummy_old = nullptr;
            void **target_old = (orig_slot && *orig_slot == nullptr) ? orig_slot : &dummy_old;
            g_zygisk_api->pltHookRegister(dev, inode, sym, new_fn, target_old);
        };

        reg("dlopen", reinterpret_cast<void *>(hook_dlopen), reinterpret_cast<void **>(&orig_dlopen));
        reg("android_dlopen_ext", reinterpret_cast<void *>(hook_android_dlopen_ext), reinterpret_cast<void **>(&orig_android_dlopen_ext));
        reg("dlsym", reinterpret_cast<void *>(hook_dlsym), reinterpret_cast<void **>(&orig_dlsym));

        if (g_ctx.enable_build_props || g_ctx.enable_probe) {
            reg("__system_property_find", reinterpret_cast<void *>(hook_system_property_find), reinterpret_cast<void **>(&orig_system_property_find));
            reg("__system_property_read_callback", reinterpret_cast<void *>(hook_system_property_read_callback), reinterpret_cast<void **>(&orig_system_property_read_callback));
            reg("__system_property_read", reinterpret_cast<void *>(hook_system_property_read), reinterpret_cast<void **>(&orig_system_property_read));
            reg("__system_property_get", reinterpret_cast<void *>(hook_system_property_get), reinterpret_cast<void **>(&orig_system_property_get));
            reg("__system_property_foreach", reinterpret_cast<void *>(hook_system_property_foreach), reinterpret_cast<void **>(&orig_system_property_foreach));
            reg("__system_property_find_nth", reinterpret_cast<void *>(hook_system_property_find_nth), reinterpret_cast<void **>(&orig_system_property_find_nth));
            reg("popen", reinterpret_cast<void *>(hook_popen), reinterpret_cast<void **>(&orig_popen));
            reg("AMediaDrm_getPropertyString", reinterpret_cast<void *>(hook_AMediaDrm_getPropertyString), reinterpret_cast<void **>(&orig_AMediaDrm_getPropertyString));
        }

        if (g_ctx.enable_vfs_shield) {
            reg("open", reinterpret_cast<void *>(hook_open), reinterpret_cast<void **>(&orig_open));
            reg("open64", reinterpret_cast<void *>(hook_open64), reinterpret_cast<void **>(&orig_open64));
            reg("__open_2", reinterpret_cast<void *>(hook_open_2), reinterpret_cast<void **>(&orig_open_2));
            reg("openat", reinterpret_cast<void *>(hook_openat), reinterpret_cast<void **>(&orig_openat));
            reg("openat64", reinterpret_cast<void *>(hook_openat64), reinterpret_cast<void **>(&orig_openat64));
            reg("__openat_2", reinterpret_cast<void *>(hook_openat_2), reinterpret_cast<void **>(&orig_openat_2));
            reg("__openat64_2", reinterpret_cast<void *>(hook_openat64_2), reinterpret_cast<void **>(&orig_openat64_2));
            reg("fopen", reinterpret_cast<void *>(hook_fopen), reinterpret_cast<void **>(&orig_fopen));
            reg("fopen64", reinterpret_cast<void *>(hook_fopen64), reinterpret_cast<void **>(&orig_fopen64));
            reg("close", reinterpret_cast<void *>(hook_close), reinterpret_cast<void **>(&orig_close));
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
            reg("readlink", reinterpret_cast<void *>(hook_readlink), reinterpret_cast<void **>(&orig_readlink));
            reg("readlinkat", reinterpret_cast<void *>(hook_readlinkat), reinterpret_cast<void **>(&orig_readlinkat));
            reg("sysinfo", reinterpret_cast<void *>(hook_sysinfo), reinterpret_cast<void **>(&orig_sysinfo));
            reg("sysconf", reinterpret_cast<void *>(hook_sysconf), reinterpret_cast<void **>(&orig_sysconf));
            reg("uname", reinterpret_cast<void *>(hook_uname), reinterpret_cast<void **>(&orig_uname));
        }

        if (g_ctx.enable_gl_vulkan) {
            reg("glGetString", reinterpret_cast<void *>(hook_glGetString), reinterpret_cast<void **>(&orig_glGetString));
            reg("eglGetProcAddress", reinterpret_cast<void *>(hook_eglGetProcAddress), reinterpret_cast<void **>(&orig_eglGetProcAddress));
            reg("vkGetInstanceProcAddr", reinterpret_cast<void *>(hook_vkGetInstanceProcAddr), reinterpret_cast<void **>(&orig_vkGetInstanceProcAddr));
            reg("vkGetPhysicalDeviceProperties", reinterpret_cast<void *>(hook_vkGetPhysicalDeviceProperties), reinterpret_cast<void **>(&orig_vkGetPhysicalDeviceProperties));
            reg("vkGetPhysicalDeviceProperties2", reinterpret_cast<void *>(hook_vkGetPhysicalDeviceProperties2), reinterpret_cast<void **>(&orig_vkGetPhysicalDeviceProperties2));
        }
    }
    std::fclose(fp);

    if (dirty) {
        g_zygisk_api->pltHookCommit();
    }
    g_maps_mtx.unlock();
}

} // namespace mirage

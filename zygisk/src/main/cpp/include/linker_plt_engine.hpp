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
    if (symbol) {
        if (g_ctx.enable_build_props) {
            if (std::strcmp(symbol, "__system_property_find") == 0) return reinterpret_cast<void *>(hook_system_property_find);
            if (std::strcmp(symbol, "__system_property_read_callback") == 0) return reinterpret_cast<void *>(hook_system_property_read_callback);
            if (std::strcmp(symbol, "__system_property_read") == 0) return reinterpret_cast<void *>(hook_system_property_read);
            if (std::strcmp(symbol, "__system_property_get") == 0) return reinterpret_cast<void *>(hook_system_property_get);
            if (std::strcmp(symbol, "__system_property_foreach") == 0) return reinterpret_cast<void *>(hook_system_property_foreach);
            if (std::strcmp(symbol, "popen") == 0) return reinterpret_cast<void *>(hook_popen);
            if (std::strcmp(symbol, "AMediaDrm_getPropertyString") == 0) return reinterpret_cast<void *>(hook_AMediaDrm_getPropertyString);
        }
        if (g_ctx.enable_vfs_shield) {
            if (std::strcmp(symbol, "openat") == 0) return reinterpret_cast<void *>(hook_openat);
            if (std::strcmp(symbol, "__openat_2") == 0) return reinterpret_cast<void *>(hook_openat_2);
            if (std::strcmp(symbol, "open") == 0) return reinterpret_cast<void *>(hook_open);
            if (std::strcmp(symbol, "fopen") == 0) return reinterpret_cast<void *>(hook_fopen);
            if (std::strcmp(symbol, "readlink") == 0) return reinterpret_cast<void *>(hook_readlink);
            if (std::strcmp(symbol, "readlinkat") == 0) return reinterpret_cast<void *>(hook_readlinkat);
            if (std::strcmp(symbol, "fstat") == 0) return reinterpret_cast<void *>(hook_fstat);
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

inline void *hook_dlopen(const char *filename, int flags) {
    void *handle = orig_dlopen ? orig_dlopen(filename, flags) : nullptr;
    if (handle && filename) {
        emit_probe("DLOPEN", filename, "Incremental PLT Scan");
        scan_and_commit_plt_hooks();
    }
    return handle;
}

inline void *hook_android_dlopen_ext(const char *filename, int flags, const void *extinfo) {
    void *handle = orig_android_dlopen_ext ? orig_android_dlopen_ext(filename, flags, extinfo) : nullptr;
    if (handle && filename) {
        emit_probe("DLOPEN_EXT", filename, "Incremental PLT Scan");
        scan_and_commit_plt_hooks();
    }
    return handle;
}

inline void scan_and_commit_plt_hooks() {
    if (!g_zygisk_api) return;
    std::lock_guard<std::mutex> lock(g_maps_mtx);

    FILE *fp = orig_fopen ? orig_fopen("/proc/self/maps", "r") : std::fopen("/proc/self/maps", "r");
    if (!fp) return;

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

        if (path[0] != '/' ||
            std::strstr(path, "/libc.so") != nullptr ||
            std::strstr(path, "/libdl.so") != nullptr ||
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

        auto reg = [&](const char *sym, void *new_fn, void **old_fn) {
            g_zygisk_api->pltHookRegister(dev, inode, sym, new_fn, old_fn);
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
            reg("openat", reinterpret_cast<void *>(hook_openat), reinterpret_cast<void **>(&orig_openat));
            reg("__openat_2", reinterpret_cast<void *>(hook_openat_2), reinterpret_cast<void **>(&orig_openat_2));
            reg("open", reinterpret_cast<void *>(hook_open), reinterpret_cast<void **>(&orig_open));
            reg("fopen", reinterpret_cast<void *>(hook_fopen), reinterpret_cast<void **>(&orig_fopen));
            reg("fopen64", reinterpret_cast<void *>(hook_fopen64), reinterpret_cast<void **>(&orig_fopen64));
            reg("close", reinterpret_cast<void *>(hook_close), reinterpret_cast<void **>(&orig_close));
            reg("readlink", reinterpret_cast<void *>(hook_readlink), reinterpret_cast<void **>(&orig_readlink));
            reg("readlinkat", reinterpret_cast<void *>(hook_readlinkat), reinterpret_cast<void **>(&orig_readlinkat));
            reg("fstat", reinterpret_cast<void *>(hook_fstat), reinterpret_cast<void **>(&orig_fstat));
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
}

} // namespace mirage

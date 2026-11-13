#pragma once

#include <cstdio>
#include <cstring>
#include <cinttypes>
#include <set>
#include <mutex>
#include <dlfcn.h>
#include <sys/sysmacros.h>
#include "zygisk.hpp"
#include "mirage_config.hpp"
#include "bionic_prop_engine.hpp"
#include "vfs_sysinfo_engine.hpp"
#include "graphics_vulkan_gles.hpp"

namespace mirage {

inline zygisk::Api *g_zygisk_api = nullptr;
inline std::mutex g_maps_mtx;
inline std::set<std::pair<dev_t, ino_t>> g_hooked_elves;
inline void *g_discard_old_slot = nullptr;

inline void *(*orig_dlopen)(const char *filename, int flags) = nullptr;
inline void *(*orig_android_dlopen_ext)(const char *filename, int flags, const void *extinfo) = nullptr;
inline void *(*orig_dlsym)(void *handle, const char *symbol) = nullptr;

inline void scan_and_commit_plt_hooks();

inline void init_linker_symbols() {
    if (orig_dlopen) return;
    orig_dlopen = reinterpret_cast<decltype(orig_dlopen)>(dlsym(RTLD_DEFAULT, "dlopen"));
    orig_android_dlopen_ext = reinterpret_cast<decltype(orig_android_dlopen_ext)>(dlsym(RTLD_DEFAULT, "android_dlopen_ext"));
    orig_dlsym = reinterpret_cast<decltype(orig_dlsym)>(dlsym(RTLD_DEFAULT, "dlsym"));
}

inline bool ends_with_ext(const char *path, size_t path_len, const char *ext) noexcept {
    size_t ext_len = std::strlen(ext);
    if (path_len < ext_len) return false;
    return std::memcmp(path + path_len - ext_len, ext, ext_len) == 0;
}

// 外科手术式 ELF 过滤器：
// 1. 应用私有库 (/data/*, /mnt/*, .apk 内置库) -> 100% Hook
// 2. 系统分区库 (/system/*, /apex/*, /vendor/*) -> 仅 Hook 核心运行时与硬件/图形桥接库，跳过上百个无关 UI/字体/解码库
inline bool should_hook_elf_path(const char *path, size_t len) noexcept {
    if (path[0] != '/') return false;

    bool is_binary = ends_with_ext(path, len, ".so") ||
                     ends_with_ext(path, len, ".apk") ||
                     (std::strstr(path, ".so.") != nullptr);
    if (!is_binary) return false;

    // 排除应用私有库以外的系统库时，先提取文件名
    const char *slash = std::strrchr(path, '/');
    const char *fname = slash ? (slash + 1) : path;

    // 永远不 Hook Bionic 自身与本模块自身
    if (std::strcmp(fname, "libc.so") == 0 ||
        std::strcmp(fname, "libdl.so") == 0 ||
        std::strcmp(fname, "libart.so") == 0 ||
        std::strncmp(fname, "linker", 6) == 0 ||
        std::strstr(fname, "mirage_forge") != nullptr) {
        return false;
    }

    // 应用自身目录 (/data/app, /data/user, /data/data, /mnt/asec 等) 下的所有库 100% 挂载
    if (std::strncmp(path, "/data/", 6) == 0 ||
        std::strncmp(path, "/mnt/", 5) == 0 ||
        ends_with_ext(path, len, ".apk")) {
        return true;
    }

    // 系统与 APEX 目录下，仅精准拦截负责 Java I/O、SystemProperties、OpenGL/Vulkan、DRM 的核心桥接库
    static const char *const kSystemBridgeLibs[] = {
        "libandroid_runtime.so",
        "libjavacore.so",
        "libopenjdk.so",
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
        "libhwui.so"
    };

    for (const char *bridge : kSystemBridgeLibs) {
        if (std::strcmp(fname, bridge) == 0) {
            return true;
        }
    }
    return false;
}

inline void *hook_dlsym(void *handle, const char *symbol) {
    if (symbol && !g_tls_in_hook) {
        if (g_cfg.build.enabled || g_cfg.probe.enabled) {
            if (std::strcmp(symbol, "__system_property_find") == 0) return reinterpret_cast<void *>(hook_system_property_find);
            if (std::strcmp(symbol, "__system_property_read_callback") == 0) return reinterpret_cast<void *>(hook_system_property_read_callback);
            if (std::strcmp(symbol, "__system_property_read") == 0) return reinterpret_cast<void *>(hook_system_property_read);
            if (std::strcmp(symbol, "__system_property_get") == 0) return reinterpret_cast<void *>(hook_system_property_get);
            if (std::strcmp(symbol, "__system_property_foreach") == 0) return reinterpret_cast<void *>(hook_system_property_foreach);
        }
        if (g_cfg.build.enabled) {
            if (std::strcmp(symbol, "popen") == 0) return reinterpret_cast<void *>(hook_popen);
        }
        if (g_cfg.vfs.enabled) {
            if (std::strcmp(symbol, "open") == 0) return reinterpret_cast<void *>(hook_open);
            if (std::strcmp(symbol, "open64") == 0) return reinterpret_cast<void *>(hook_open64);
            if (std::strcmp(symbol, "__open_2") == 0) return reinterpret_cast<void *>(hook_open_2);
            if (std::strcmp(symbol, "openat") == 0) return reinterpret_cast<void *>(hook_openat);
            if (std::strcmp(symbol, "openat64") == 0) return reinterpret_cast<void *>(hook_openat64);
            if (std::strcmp(symbol, "__openat_2") == 0) return reinterpret_cast<void *>(hook_openat_2);
            if (std::strcmp(symbol, "__openat64_2") == 0) return reinterpret_cast<void *>(hook_openat64_2);
            if (std::strcmp(symbol, "fopen") == 0) return reinterpret_cast<void *>(hook_fopen);
            if (std::strcmp(symbol, "fopen64") == 0) return reinterpret_cast<void *>(hook_fopen64);
            if (std::strcmp(symbol, "lseek") == 0) return reinterpret_cast<void *>(hook_lseek);
            if (std::strcmp(symbol, "lseek64") == 0) return reinterpret_cast<void *>(hook_lseek64);
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
        if (g_cfg.gl.enabled) {
            if (std::strcmp(symbol, "glGetString") == 0) return reinterpret_cast<void *>(hook_glGetString);
            if (std::strcmp(symbol, "eglGetProcAddress") == 0) return reinterpret_cast<void *>(hook_eglGetProcAddress);
            if (std::strcmp(symbol, "vkGetInstanceProcAddr") == 0) return reinterpret_cast<void *>(hook_vkGetInstanceProcAddr);
            if (std::strcmp(symbol, "vkGetPhysicalDeviceProperties") == 0) return reinterpret_cast<void *>(hook_vkGetPhysicalDeviceProperties);
            if (std::strcmp(symbol, "vkGetPhysicalDeviceProperties2") == 0) return reinterpret_cast<void *>(hook_vkGetPhysicalDeviceProperties2);
            if (std::strcmp(symbol, "AMediaDrm_getPropertyString") == 0) return reinterpret_cast<void *>(hook_AMediaDrm_getPropertyString);
        }
    }
    return orig_dlsym ? orig_dlsym(handle, symbol) : nullptr;
}

inline bool should_rescan_on_dlopen(const char *filename) noexcept {
    if (!filename) return false;
    if (std::strncmp(filename, "/system/", 8) == 0 ||
        std::strncmp(filename, "/apex/", 6) == 0 ||
        std::strncmp(filename, "/vendor/", 8) == 0) {
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

        if (!should_hook_elf_path(path, len)) {
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
            void **target_old = (orig_slot && *orig_slot == nullptr) ? orig_slot : &g_discard_old_slot;
            g_zygisk_api->pltHookRegister(dev, inode, sym, new_fn, target_old);
        };

        reg("dlopen", reinterpret_cast<void *>(hook_dlopen), reinterpret_cast<void **>(&orig_dlopen));
        reg("android_dlopen_ext", reinterpret_cast<void *>(hook_android_dlopen_ext), reinterpret_cast<void **>(&orig_android_dlopen_ext));
        reg("dlsym", reinterpret_cast<void *>(hook_dlsym), reinterpret_cast<void **>(&orig_dlsym));

        // 模块 1：仅当开启了 Build 或 Probe 时才注册 Bionic 属性 Hook
        if (g_cfg.build.enabled || g_cfg.probe.enabled) {
            reg("__system_property_find", reinterpret_cast<void *>(hook_system_property_find), reinterpret_cast<void **>(&orig_system_property_find));
            reg("__system_property_read_callback", reinterpret_cast<void *>(hook_system_property_read_callback), reinterpret_cast<void **>(&orig_system_property_read_callback));
            reg("__system_property_read", reinterpret_cast<void *>(hook_system_property_read), reinterpret_cast<void **>(&orig_system_property_read));
            reg("__system_property_get", reinterpret_cast<void *>(hook_system_property_get), reinterpret_cast<void **>(&orig_system_property_get));
            reg("__system_property_foreach", reinterpret_cast<void *>(hook_system_property_foreach), reinterpret_cast<void **>(&orig_system_property_foreach));
            reg("__system_property_find_nth", reinterpret_cast<void *>(hook_system_property_find_nth), reinterpret_cast<void **>(&orig_system_property_find_nth));
        }
        if (g_cfg.build.enabled) {
            reg("popen", reinterpret_cast<void *>(hook_popen), reinterpret_cast<void **>(&orig_popen));
        }

        // 模块 2：仅当开启了 VFS 护盾时才注册文件 I/O 与 sysinfo/sysconf/uname Hook
        if (g_cfg.vfs.enabled) {
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
            reg("readlink", reinterpret_cast<void *>(hook_readlink), reinterpret_cast<void **>(&orig_readlink));
            reg("readlinkat", reinterpret_cast<void *>(hook_readlinkat), reinterpret_cast<void **>(&orig_readlinkat));
            reg("sysinfo", reinterpret_cast<void *>(hook_sysinfo), reinterpret_cast<void **>(&orig_sysinfo));
            reg("sysconf", reinterpret_cast<void *>(hook_sysconf), reinterpret_cast<void **>(&orig_sysconf));
            reg("uname", reinterpret_cast<void *>(hook_uname), reinterpret_cast<void **>(&orig_uname));
        }

        // 模块 3：仅当开启了 GL/Vulkan 模块时才注册图形与 DRM Hook
        if (g_cfg.gl.enabled) {
            reg("glGetString", reinterpret_cast<void *>(hook_glGetString), reinterpret_cast<void **>(&orig_glGetString));
            reg("eglGetProcAddress", reinterpret_cast<void *>(hook_eglGetProcAddress), reinterpret_cast<void **>(&orig_eglGetProcAddress));
            reg("vkGetInstanceProcAddr", reinterpret_cast<void *>(hook_vkGetInstanceProcAddr), reinterpret_cast<void **>(&orig_vkGetInstanceProcAddr));
            reg("vkGetPhysicalDeviceProperties", reinterpret_cast<void *>(hook_vkGetPhysicalDeviceProperties), reinterpret_cast<void **>(&orig_vkGetPhysicalDeviceProperties));
            reg("vkGetPhysicalDeviceProperties2", reinterpret_cast<void *>(hook_vkGetPhysicalDeviceProperties2), reinterpret_cast<void **>(&orig_vkGetPhysicalDeviceProperties2));
            reg("AMediaDrm_getPropertyString", reinterpret_cast<void *>(hook_AMediaDrm_getPropertyString), reinterpret_cast<void **>(&orig_AMediaDrm_getPropertyString));
        }
    }
    std::fclose(fp);

    if (dirty) {
        g_zygisk_api->pltHookCommit();
    }
    g_maps_mtx.unlock();
}

} // namespace mirage

#pragma once

#include <cstdio>
#include <cstring>
#include <cinttypes>
#include <set>
#include <tuple>
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
inline std::set<std::tuple<dev_t, ino_t, uintptr_t>> g_hooked_elves;
inline void *g_discard_old_slot = nullptr;
inline int g_plt_commit_pass = 0;
inline dev_t g_self_dev = 0;
inline ino_t g_self_ino = 0;

using PFN_loader_dlopen = void *(*)(const char *filename, int flags, const void *caller_addr);
using PFN_loader_android_dlopen_ext = void *(*)(const char *filename, int flags, const void *extinfo, const void *caller_addr);

inline void *(*orig_dlopen)(const char *filename, int flags) = nullptr;
inline void *(*orig_android_dlopen_ext)(const char *filename, int flags, const void *extinfo) = nullptr;
inline PFN_loader_dlopen orig_loader_dlopen = nullptr;
inline PFN_loader_android_dlopen_ext orig_loader_android_dlopen_ext = nullptr;

inline void scan_and_commit_plt_hooks();

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
}

inline bool ends_with_ext(const char *path, size_t path_len, const char *ext) noexcept {
    size_t ext_len = std::strlen(ext);
    if (path_len < ext_len) return false;
    return std::memcmp(path + path_len - ext_len, ext, ext_len) == 0;
}

inline bool should_hook_elf_path(const char *path, size_t len) noexcept {
    if (path[0] != '/') return false;

    if (std::strstr(path, "mirage_forge") != nullptr ||
        std::strncmp(path, "/data/adb/", 10) == 0) {
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
        if (std::strcmp(symbol, "vk_icdGetInstanceProcAddr") == 0) {
            orig_icd_GetInstanceProcAddr = reinterpret_cast<PFN_vkGetInstanceProcAddr>(real_sym);
            return reinterpret_cast<void *>(hook_icd_GetInstanceProcAddr);
        }
        if (std::strcmp(symbol, "vkGetInstanceProcAddr") == 0) {
            orig_vkGetInstanceProcAddr = reinterpret_cast<PFN_vkGetInstanceProcAddr>(real_sym);
            return reinterpret_cast<void *>(hook_vkGetInstanceProcAddr);
        }
        if (std::strcmp(symbol, "vkGetPhysicalDeviceProperties") == 0) {
            orig_vkGetPhysicalDeviceProperties = reinterpret_cast<PFN_vkGetPhysicalDeviceProperties>(real_sym);
            return reinterpret_cast<void *>(hook_vkGetPhysicalDeviceProperties);
        }
        if (std::strcmp(symbol, "vkGetPhysicalDeviceProperties2") == 0 ||
            std::strcmp(symbol, "vkGetPhysicalDeviceProperties2KHR") == 0) {
            orig_vkGetPhysicalDeviceProperties2 = reinterpret_cast<PFN_vkGetPhysicalDeviceProperties2>(real_sym);
            return reinterpret_cast<void *>(hook_vkGetPhysicalDeviceProperties2);
        }
        if (std::strcmp(symbol, "glGetString") == 0) {
            orig_glGetString = reinterpret_cast<decltype(orig_glGetString)>(real_sym);
            return reinterpret_cast<void *>(hook_glGetString);
        }
        if (std::strcmp(symbol, "eglGetProcAddress") == 0) {
            orig_eglGetProcAddress = reinterpret_cast<decltype(orig_eglGetProcAddress)>(real_sym);
            return reinterpret_cast<void *>(hook_eglGetProcAddress);
        }
        if (std::strcmp(symbol, "AMediaDrm_getPropertyString") == 0) {
            orig_AMediaDrm_getPropertyString = reinterpret_cast<decltype(orig_AMediaDrm_getPropertyString)>(real_sym);
            return reinterpret_cast<void *>(hook_AMediaDrm_getPropertyString);
        }
    }
    return real_sym;
}

inline bool should_rescan_on_dlopen(const char *filename) noexcept {
    if (!filename) return false;
    if (std::strstr(filename, "vulkan") != nullptr) return true;
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
            try_lazy_spoof_display_modes();
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
            try_lazy_spoof_display_modes();
        }
    }
    return handle;
}

inline void scan_and_commit_plt_hooks() {
    if (!g_zygisk_api) return;
    ErrnoRestorer errno_guard;
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
        if (inode_ul == 0 || std::strchr(perms, 'x') == nullptr) continue;

        dev_t dev = makedev(dev_maj, dev_min);
        ino_t inode = static_cast<ino_t>(inode_ul);
        if (g_self_ino != 0 && dev == g_self_dev && inode == g_self_ino) {
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

        auto id_tuple = std::make_tuple(dev, inode, offset);
        if (!g_hooked_elves.insert(id_tuple).second) {
            continue;
        }
        dirty = true;

        auto reg = [&](const char *sym, void *new_fn, void **orig_slot) {
            void **target_old = (orig_slot && *orig_slot == nullptr) ? orig_slot : &g_discard_old_slot;
            g_zygisk_api->pltHookRegister(dev, inode, sym, new_fn, target_old);
        };

        reg("dlopen", reinterpret_cast<void *>(hook_dlopen), reinterpret_cast<void **>(&orig_dlopen));
        reg("android_dlopen_ext", reinterpret_cast<void *>(hook_android_dlopen_ext), reinterpret_cast<void **>(&orig_android_dlopen_ext));
        if (g_cfg.gl.enabled) {
            reg("dlsym", reinterpret_cast<void *>(hook_dlsym_all), reinterpret_cast<void **>(&orig_dlsym));
        }

        if (g_cfg.build.enabled || g_cfg.gl.enabled || g_cfg.probe.enabled) {
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
            reg("readlink", reinterpret_cast<void *>(hook_readlink), reinterpret_cast<void **>(&orig_readlink));
            reg("readlinkat", reinterpret_cast<void *>(hook_readlinkat), reinterpret_cast<void **>(&orig_readlinkat));
            reg("sysinfo", reinterpret_cast<void *>(hook_sysinfo), reinterpret_cast<void **>(&orig_sysinfo));
            reg("sysconf", reinterpret_cast<void *>(hook_sysconf), reinterpret_cast<void **>(&orig_sysconf));
            reg("uname", reinterpret_cast<void *>(hook_uname), reinterpret_cast<void **>(&orig_uname));
        }

        if (g_cfg.gl.enabled) {
            reg("hw_get_module", reinterpret_cast<void *>(hook_hw_get_module), reinterpret_cast<void **>(&orig_hw_get_module));
            reg("glGetString", reinterpret_cast<void *>(hook_glGetString), reinterpret_cast<void **>(&orig_glGetString));
            reg("eglGetProcAddress", reinterpret_cast<void *>(hook_eglGetProcAddress), reinterpret_cast<void **>(&orig_eglGetProcAddress));
            reg("vkGetInstanceProcAddr", reinterpret_cast<void *>(hook_vkGetInstanceProcAddr), reinterpret_cast<void **>(&orig_vkGetInstanceProcAddr));
            reg("vkGetPhysicalDeviceProperties", reinterpret_cast<void *>(hook_vkGetPhysicalDeviceProperties), reinterpret_cast<void **>(&orig_vkGetPhysicalDeviceProperties));
            reg("vkGetPhysicalDeviceProperties2", reinterpret_cast<void *>(hook_vkGetPhysicalDeviceProperties2), reinterpret_cast<void **>(&orig_vkGetPhysicalDeviceProperties2));
            reg("vkGetPhysicalDeviceProperties2KHR", reinterpret_cast<void *>(hook_vkGetPhysicalDeviceProperties2), reinterpret_cast<void **>(&orig_vkGetPhysicalDeviceProperties2));
            reg("AMediaDrm_getPropertyString", reinterpret_cast<void *>(hook_AMediaDrm_getPropertyString), reinterpret_cast<void **>(&orig_AMediaDrm_getPropertyString));
        }
    }
    if (orig_fclose) orig_fclose(fp); else std::fclose(fp);

    if (dirty) {
        g_zygisk_api->pltHookCommit();
    }
    g_maps_mtx.unlock();

    if (++g_plt_commit_pass == 2 && g_cfg.vfs.enabled && g_cfg.vfs.enable_seccomp) {
        install_on_demand_seccomp_bpf();
    }
}

} // namespace mirage

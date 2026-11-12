#pragma once

#include <cstring>
#include <string>
#include <unordered_set>
#include <mutex>
#include <dlfcn.h>
#include <link.h>
#include <sys/stat.h>
#include "zygisk.hpp"
#include "mirage_config.hpp"
#include "bionic_prop_engine.hpp"
#include "vfs_sysinfo_engine.hpp"
#include "graphics_vulkan_gles.hpp"

namespace mirage {

inline zygisk::Api *g_zygisk_api = nullptr;
inline std::mutex g_hook_State_mtx;
inline std::unordered_set<uintptr_t> g_hooked_base_addrs;

inline void *(*orig_dlopen)(const char *filename, int flags) = nullptr;
inline void *(*orig_android_dlopen_ext)(const char *filename, int flags, const void *extinfo) = nullptr;

inline void scan_and_commit_plt_hooks();

inline void *hook_dlopen(const char *filename, int flags) {
    void *handle = orig_dlopen ? orig_dlopen(filename, flags) : nullptr;
    if (handle && g_cfg.active) {
        scan_and_commit_plt_hooks();
    }
    return handle;
}

inline void *hook_android_dlopen_ext(const char *filename, int flags, const void *extinfo) {
    void *handle = orig_android_dlopen_ext ? orig_android_dlopen_ext(filename, flags, extinfo) : nullptr;
    if (handle && g_cfg.active) {
        scan_and_commit_plt_hooks();
    }
    return handle;
}

inline void init_linker_symbols() {
    if (orig_dlopen) return;
    orig_dlopen = reinterpret_cast<decltype(orig_dlopen)>(dlsym(RTLD_DEFAULT, "dlopen"));
    orig_android_dlopen_ext = reinterpret_cast<decltype(orig_android_dlopen_ext)>(dlsym(RTLD_DEFAULT, "android_dlopen_ext"));
}

// 包含 libnativeloader.so (Java System.loadLibrary 入口) 与 libmeminfo.so (系统物理内存解析库)
inline bool is_system_bridge_lib(const char *soname) noexcept {
    static constexpr const char *kBridgeLibs[] = {
        "libc.so",
        "libdl.so",
        "libnativeloader.so",
        "libnativeloader_lazy.so",
        "libart.so",
        "libjavacore.so",
        "libopenjdk.so",
        "libandroid_runtime.so",
        "libandroid.so",
        "libbase.so",
        "libcutils.so",
        "libutils.so",
        "libmeminfo.so",
        "libprocinfo.so",
        "libvintf.so",
        "libEGL.so",
        "libGLESv1_CM.so",
        "libGLESv2.so",
        "libGLESv3.so",
        "libvulkan.so",
        "libgui.so",
        "libhwui.so",
        "libmediandk.so",
        "libwilhelm.so"
    };
    for (const char *target : kBridgeLibs) {
        if (std::strcmp(soname, target) == 0) return true;
    }
    return false;
}

inline bool should_hook_elf(const char *path, std::string *out_soname) {
    if (!path || path[0] != '/') return false;
    if (std::strstr(path, "mirage_forge") != nullptr ||
        std::strstr(path, "zygisk") != nullptr) {
        return false;
    }

    const char *slash = std::strrchr(path, '/');
    const char *base_name = slash ? (slash + 1) : path;
    if (out_soname) *out_soname = base_name;

    // 1. 第三方应用安装目录 /data/app/... 或私有目录 /data/data/... (包含 .apk!/lib/ 内存直映射库)
    if (std::strncmp(path, "/data/", 6) == 0 || std::strncmp(path, "/mnt/expand/", 12) == 0) {
        return std::strstr(base_name, ".so") != nullptr || std::strstr(path, ".apk") != nullptr;
    }

    // 2. 系统桥接白名单库
    return is_system_bridge_lib(base_name);
}

inline void register_hooks_for_library(dev_t dev, ino_t ino) {
    if (!g_zygisk_api) return;

    #define REG_PLT(symbol, replacement, backup_ptr) \
        g_zygisk_api->pltHookRegister(dev, ino, symbol, reinterpret_cast<void *>(replacement), reinterpret_cast<void **>(backup_ptr))

    void *dummy = nullptr;

    // 1. 动态加载追踪器 (确保 libnativeloader.so 加载的 JNI 库实时受控)
    if (orig_dlopen) REG_PLT("dlopen", hook_dlopen, &dummy);
    if (orig_android_dlopen_ext) REG_PLT("android_dlopen_ext", hook_android_dlopen_ext, &dummy);

    // 2. Bionic 属性模块
    if (g_cfg.build.enabled || g_cfg.probe.enabled) {
        if (orig_system_property_find)          REG_PLT("__system_property_find", hook_system_property_find, &dummy);
        if (orig_system_property_read_callback) REG_PLT("__system_property_read_callback", hook_system_property_read_callback, &dummy);
        if (orig_system_property_read)          REG_PLT("__system_property_read", hook_system_property_read, &dummy);
        if (orig_system_property_get)           REG_PLT("__system_property_get", hook_system_property_get, &dummy);
        if (orig_system_property_foreach)       REG_PLT("__system_property_foreach", hook_system_property_foreach, &dummy);
        if (orig_system_property_find_nth)      REG_PLT("__system_property_find_nth", hook_system_property_find_nth, &dummy);
    }

    // 3. VFS 与硬件拓扑模块 (含 fopen/fclose 与 android_fdsan_close_with_tag)
    if (g_cfg.vfs.enabled) {
        if (orig_open)       REG_PLT("open", hook_open, &dummy);
        if (orig_open64)     REG_PLT("open64", hook_open64, &dummy);
        if (orig_open_2)     REG_PLT("__open_2", hook_open_2, &dummy);
        if (orig_openat)     REG_PLT("openat", hook_openat, &dummy);
        if (orig_openat64)   REG_PLT("openat64", hook_openat64, &dummy);
        if (orig_openat_2)   REG_PLT("__openat_2", hook_openat_2, &dummy);
        if (orig_fopen)      REG_PLT("fopen", hook_fopen, &dummy);
        if (orig_fopen64)    REG_PLT("fopen64", hook_fopen64, &dummy);
        if (orig_fclose)     REG_PLT("fclose", hook_fclose, &dummy);
        if (orig_close)      REG_PLT("close", hook_close, &dummy);
        if (orig_android_fdsan_close_with_tag) {
            REG_PLT("android_fdsan_close_with_tag", hook_android_fdsan_close_with_tag, &dummy);
        }

        if (orig_lseek)      REG_PLT("lseek", hook_lseek, &dummy);
        if (orig_lseek64)    REG_PLT("lseek64", hook_lseek64, &dummy);
        if (orig_pread64)    REG_PLT("pread64", hook_pread64, &dummy);
        if (orig_readlink)   REG_PLT("readlink", hook_readlink, &dummy);
        if (orig_readlinkat) REG_PLT("readlinkat", hook_readlinkat, &dummy);
        if (orig_access)     REG_PLT("access", hook_access, &dummy);
        if (orig_faccessat)  REG_PLT("faccessat", hook_faccessat, &dummy);

        if (orig_stat)       REG_PLT("stat", hook_stat, &dummy);
        if (orig_stat64)     REG_PLT("stat64", hook_stat64, &dummy);
        if (orig_lstat)      REG_PLT("lstat", hook_lstat, &dummy);
        if (orig_lstat64)    REG_PLT("lstat64", hook_lstat64, &dummy);
        if (orig_fstat)      REG_PLT("fstat", hook_fstat, &dummy);
        if (orig_fstat64)    REG_PLT("fstat64", hook_fstat64, &dummy);
        if (orig_fstatat)    REG_PLT("fstatat", hook_fstatat, &dummy);
        if (orig_fstatat64)  REG_PLT("fstatat64", hook_fstatat64, &dummy);

        if (orig_uname)      REG_PLT("uname", hook_uname, &dummy);
        if (orig_sysinfo)    REG_PLT("sysinfo", hook_sysinfo, &dummy);
        if (orig_sysconf)    REG_PLT("sysconf", hook_sysconf, &dummy);
    }

    if ((g_cfg.vfs.enabled || g_cfg.build.enabled) && orig_popen) {
        REG_PLT("popen", hook_popen, &dummy);
    }

    // 4. OpenGL ES / Vulkan / MediaDrm 模块
    if (g_cfg.gl.enabled) {
        if (orig_glGetString)         REG_PLT("glGetString", hook_glGetString, &dummy);
        if (orig_eglGetProcAddress)   REG_PLT("eglGetProcAddress", hook_eglGetProcAddress, &dummy);
        if (orig_vkGetInstanceProcAddr) {
            REG_PLT("vkGetInstanceProcAddr", hook_vkGetInstanceProcAddr, &dummy);
        }
        if (orig_vkGetPhysicalDeviceProperties) {
            REG_PLT("vkGetPhysicalDeviceProperties", hook_vkGetPhysicalDeviceProperties, &dummy);
        }
        if (orig_vkGetPhysicalDeviceProperties2) {
            REG_PLT("vkGetPhysicalDeviceProperties2", hook_vkGetPhysicalDeviceProperties2, &dummy);
            REG_PLT("vkGetPhysicalDeviceProperties2KHR", hook_vkGetPhysicalDeviceProperties2, &dummy);
        }
        if (orig_AMediaDrm_getPropertyString) {
            REG_PLT("AMediaDrm_getPropertyString", hook_AMediaDrm_getPropertyString, &dummy);
        }
    }

    #undef REG_PLT
}

inline void scan_and_commit_plt_hooks() {
    if (!g_zygisk_api || !g_cfg.active) return;
    std::lock_guard<std::mutex> lock(g_hook_State_mtx);

    bool added_any = false;
    dl_iterate_phdr([](struct dl_phdr_info *info, size_t, void *data) -> int {
        if (!info->dlpi_name || info->dlpi_name[0] != '/') return 0;

        uintptr_t base = static_cast<uintptr_t>(info->dlpi_addr);
        if (g_hooked_base_addrs.find(base) != g_hooked_base_addrs.end()) {
            return 0;
        }

        std::string soname;
        if (!should_hook_elf(info->dlpi_name, &soname)) {
            g_hooked_base_addrs.insert(base);
            return 0;
        }

        std::string real_file_path(info->dlpi_name);
        size_t bang = real_file_path.find("!/");
        if (bang != std::string::npos) {
            real_file_path = real_file_path.substr(0, bang);
        }

        struct stat st{};
        int st_ret = orig_stat ? orig_stat(real_file_path.c_str(), &st) : stat(real_file_path.c_str(), &st);
        if (st_ret == 0) {
            register_hooks_for_library(st.st_dev, st.st_ino);
            g_hooked_base_addrs.insert(base);
            *static_cast<bool *>(data) = true;
        }
        return 0;
    }, &added_any);

    if (added_any) {
        g_zygisk_api->pltHookCommit();
    }
}

} // namespace mirage

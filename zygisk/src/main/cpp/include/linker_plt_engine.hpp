#pragma once

#include <cstdio>
#include <cstring>
#include <cinttypes>
#include <tuple>
#include <set>
#include <unordered_set>
#include <mutex>
#include <dlfcn.h>
#include <link.h>
#include <elf.h>
#include <unistd.h>
#include <sys/mman.h>
#include <sys/sysmacros.h>
#include "zygisk.hpp"
#include "mirage_config.hpp"
#include "bionic_prop_engine.hpp"
#include "vfs_sysinfo_engine.hpp"
#include "graphics_vulkan_gles.hpp"

#ifndef R_AARCH64_JUMP_SLOT
#define R_AARCH64_JUMP_SLOT 1026
#define R_AARCH64_GLOB_DAT  1025
#endif
#ifndef R_ARM_JUMP_SLOT
#define R_ARM_JUMP_SLOT 22
#define R_ARM_GLOB_DAT  21
#endif
#ifndef R_X86_64_JUMP_SLOT
#define R_X86_64_JUMP_SLOT 7
#define R_X86_64_GLOB_DAT  6
#endif
#ifndef R_386_JMP_SLOT
#define R_386_JMP_SLOT 7
#define R_386_GLOB_DAT 6
#endif

namespace mirage {

inline zygisk::Api *g_zygisk_api = nullptr;
inline std::mutex g_maps_mtx;
inline std::set<std::tuple<dev_t, ino_t, uintptr_t>> g_hooked_segments;
inline std::unordered_set<uintptr_t> g_runtime_jmprel_patched_bases;
inline void *g_discard_old_slot = nullptr;

// 主线程 DPI 延迟同步回调钩子（由 jvm_art_engine.hpp 注入实现，零线程开销）
inline void (*g_main_thread_dpi_tick_fn)() = nullptr;

inline void *(*orig_dlopen)(const char *filename, int flags) = nullptr;
inline void *(*orig_android_dlopen_ext)(const char *filename, int flags, const void *extinfo) = nullptr;
inline void *(*orig_dlsym)(void *handle, const char *symbol) = nullptr;

inline void scan_and_commit_plt_hooks();
inline void patch_runtime_app_lib_jmprel_safe();

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

inline bool should_hook_elf_path(const char *path, size_t len) noexcept {
    if (!path || len == 0 || path[0] != '/') return false;

    bool is_binary = ends_with_ext(path, len, ".so") ||
                     ends_with_ext(path, len, ".apk") ||
                     (std::strstr(path, ".so.") != nullptr);
    if (!is_binary) return false;

    const char *slash = std::strrchr(path, '/');
    const char *fname = slash ? (slash + 1) : path;

    if (std::strcmp(fname, "libc.so") == 0 ||
        std::strcmp(fname, "libdl.so") == 0 ||
        std::strcmp(fname, "libm.so") == 0 ||
        std::strcmp(fname, "libart.so") == 0 ||
        std::strcmp(fname, "libartbase.so") == 0 ||
        std::strncmp(fname, "linker", 6) == 0 ||
        std::strncmp(fname, "libwebviewchromium", 18) == 0 ||
        std::strstr(path, "libpairipcore.so") != nullptr ||
        std::strstr(path, "mirage_forge") != nullptr) {
        return false;
    }

    if (std::strncmp(path, "/data/", 6) == 0 ||
        std::strncmp(path, "/mnt/", 5) == 0 ||
        ends_with_ext(path, len, ".apk")) {
        return true;
    }

    // 包含 libnativeloader.so (拦截 System.loadLibrary) 与 libnio.so (拦截 Java NIO 文件读取)
    static const char *const kSystemBridgeLibs[] = {
        "libnativeloader.so",
        "libnativeloader_lazy.so",
        "libdl_android.so",
        "libandroid_runtime.so",
        "libjavacore.so",
        "libopenjdk.so",
        "libopenjdkjvm.so",
        "libnio.so",
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

inline void *resolve_mirage_hook_symbol(const char *symbol) noexcept {
    if (!symbol) return nullptr;

    if (g_cfg.build.enabled || g_cfg.probe.enabled) {
        if (std::strcmp(symbol, "__system_property_find") == 0) return reinterpret_cast<void *>(hook_system_property_find);
        if (std::strcmp(symbol, "__system_property_read_callback") == 0) return reinterpret_cast<void *>(hook_system_property_read_callback);
        if (std::strcmp(symbol, "__system_property_read") == 0) return reinterpret_cast<void *>(hook_system_property_read);
        if (std::strcmp(symbol, "__system_property_get") == 0) return reinterpret_cast<void *>(hook_system_property_get);
        if (std::strcmp(symbol, "__system_property_foreach") == 0) return reinterpret_cast<void *>(hook_system_property_foreach);
        if (std::strcmp(symbol, "__system_property_find_nth") == 0) return reinterpret_cast<void *>(hook_system_property_find_nth);
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
        if (std::strcmp(symbol, "fclose") == 0) return reinterpret_cast<void *>(hook_fclose);
        if (std::strcmp(symbol, "close") == 0) return reinterpret_cast<void *>(hook_close);
        if (std::strcmp(symbol, "android_fdsan_close_with_tag") == 0) return reinterpret_cast<void *>(hook_android_fdsan_close_with_tag);
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
        if (std::strcmp(symbol, "execve") == 0) return reinterpret_cast<void *>(hook_execve);
    }
    if (g_cfg.gl.enabled) {
        if (std::strcmp(symbol, "glGetString") == 0) return reinterpret_cast<void *>(hook_glGetString);
        if (std::strcmp(symbol, "eglGetProcAddress") == 0) return reinterpret_cast<void *>(hook_eglGetProcAddress);
        if (std::strcmp(symbol, "vkGetInstanceProcAddr") == 0 ||
            std::strcmp(symbol, "vk_icdGetInstanceProcAddr") == 0) {
            return reinterpret_cast<void *>(hook_vkGetInstanceProcAddr);
        }
        if (std::strcmp(symbol, "vkGetDeviceProcAddr") == 0) {
            return reinterpret_cast<void *>(hook_vkGetDeviceProcAddr);
        }
        if (std::strcmp(symbol, "vkGetPhysicalDeviceProperties") == 0) {
            return reinterpret_cast<void *>(hook_vkGetPhysicalDeviceProperties);
        }
        if (std::strcmp(symbol, "vkGetPhysicalDeviceProperties2") == 0 ||
            std::strcmp(symbol, "vkGetPhysicalDeviceProperties2KHR") == 0) {
            return reinterpret_cast<void *>(hook_vkGetPhysicalDeviceProperties2);
        }
        if (std::strcmp(symbol, "AMediaDrm_getPropertyString") == 0) {
            return reinterpret_cast<void *>(hook_AMediaDrm_getPropertyString);
        }
    }
    return nullptr;
}

inline void *hook_dlsym(void *handle, const char *symbol) {
    if (symbol && !g_tls_in_hook) {
        if (void *hooked = resolve_mirage_hook_symbol(symbol)) {
            return hooked;
        }
        // 当 libvulkan.so 通过 dlsym(hal_handle, "HMI") 加载底层 Vulkan HAL 驱动时，直接包裹其根部派发表
        if (g_cfg.gl.enabled && std::strcmp(symbol, "HMI") == 0 && orig_dlsym) {
            void *real_hmi = orig_dlsym(handle, symbol);
            return intercept_vulkan_hal_hmi_if_needed(real_hmi);
        }
    }
    return orig_dlsym ? orig_dlsym(handle, symbol) : nullptr;
}

inline bool is_app_private_library_name(const char *filename) noexcept {
    if (!filename) return false;
    if (std::strstr(filename, "libpairipcore.so") != nullptr ||
        std::strstr(filename, "mirage_forge") != nullptr ||
        std::strstr(filename, "libwebviewchromium") != nullptr) {
        return false;
    }
    if (std::strncmp(filename, "/data/", 6) == 0 ||
        std::strncmp(filename, "/mnt/", 5) == 0 ||
        std::strstr(filename, ".apk!") != nullptr) {
        return true;
    }
    return false;
}

inline void *hook_dlopen(const char *filename, int flags) {
    void *handle = orig_dlopen ? orig_dlopen(filename, flags) : nullptr;
    if (handle && is_app_private_library_name(filename)) {
        ScopedHookGuard guard;
        if (guard) {
            patch_runtime_app_lib_jmprel_safe();
        }
    }
    if (g_main_thread_dpi_tick_fn && !g_tls_in_hook) {
        g_main_thread_dpi_tick_fn();
    }
    return handle;
}

inline void *hook_android_dlopen_ext(const char *filename, int flags, const void *extinfo) {
    void *handle = orig_android_dlopen_ext ? orig_android_dlopen_ext(filename, flags, extinfo) : nullptr;
    if (handle && is_app_private_library_name(filename)) {
        ScopedHookGuard guard;
        if (guard) {
            emit_probe("DLOPEN_EXT", filename, "Runtime JMPREL Hook");
            patch_runtime_app_lib_jmprel_safe();
        }
    }
    if (g_main_thread_dpi_tick_fn && !g_tls_in_hook) {
        g_main_thread_dpi_tick_fn();
    }
    return handle;
}

// ============================================================================
// 运行期纯 DT_JMPREL (PLT 函数跳转槽) 安全热修补器
// 仅针对 postAppSpecialize 之后通过 System.loadLibrary 加载的应用私有库 (如 libvulkan_info.so)
// 严格遵守：1. 绝不触碰 DT_RELA 数据重定位表；2. 绝不触碰代码段；3. 严格校验 DT_STRSZ 边界
// ============================================================================
inline void patch_single_jmprel_slot(uintptr_t base, uintptr_t r_offset, const char *sym_name,
                                     uintptr_t relro_start, uintptr_t relro_end, size_t page_sz) {
    void *replacement = nullptr;
    if (std::strcmp(sym_name, "dlsym") == 0) {
        replacement = reinterpret_cast<void *>(hook_dlsym);
    } else if (std::strcmp(sym_name, "dlopen") == 0) {
        replacement = reinterpret_cast<void *>(hook_dlopen);
    } else if (std::strcmp(sym_name, "android_dlopen_ext") == 0) {
        replacement = reinterpret_cast<void *>(hook_android_dlopen_ext);
    } else {
        replacement = resolve_mirage_hook_symbol(sym_name);
    }
    if (!replacement) return;

    uintptr_t slot_addr = base + r_offset;
    auto *slot = reinterpret_cast<void **>(slot_addr);
    if (*slot == replacement) return;

    uintptr_t page_start = slot_addr & ~(page_sz - 1);
    bool in_relro = (relro_start < relro_end && slot_addr >= relro_start && slot_addr < relro_end);

    if (in_relro) {
        if (mprotect(reinterpret_cast<void *>(page_start), page_sz, PROT_READ | PROT_WRITE) != 0) {
            return;
        }
    }

    *slot = replacement;

    if (in_relro && page_start >= relro_start && (page_start + page_sz) <= relro_end) {
        mprotect(reinterpret_cast<void *>(page_start), page_sz, PROT_READ);
    }
}

inline void patch_runtime_app_lib_jmprel_safe() {
    if (!g_maps_mtx.try_lock()) return;

    dl_iterate_phdr([](struct dl_phdr_info *info, size_t, void *) -> int {
        if (!info || !info->dlpi_name || info->dlpi_addr == 0 || !info->dlpi_phdr) return 0;

        // 仅修补应用私有目录 (/data/ 或 .apk!) 下的库，跳过所有系统库与 PairIP 壳
        if (!is_app_private_library_name(info->dlpi_name)) return 0;

        uintptr_t base = static_cast<uintptr_t>(info->dlpi_addr);
        if (!g_runtime_jmprel_patched_bases.insert(base).second) {
            return 0;
        }

        const ElfW(Dyn) *dyn = nullptr;
        uintptr_t relro_start = 0, relro_end = 0;

        for (int i = 0; i < info->dlpi_phnum; ++i) {
            const ElfW(Phdr) &ph = info->dlpi_phdr[i];
            if (ph.p_type == PT_DYNAMIC) {
                dyn = reinterpret_cast<const ElfW(Dyn) *>(base + ph.p_vaddr);
            } else if (ph.p_type == PT_GNU_RELRO) {
                relro_start = base + ph.p_vaddr;
                relro_end = relro_start + ph.p_memsz;
            }
        }
        if (!dyn) return 0;

        const ElfW(Sym) *symtab = nullptr;
        const char *strtab = nullptr;
        size_t strsz = 0;
        uintptr_t jmprel = 0;
        size_t pltrelsz = 0;
        uintptr_t pltrel_type = 0;

        // Bionic linker 中 PT_DYNAMIC 的 d_ptr 始终为相对于 dlpi_addr (base) 的虚拟偏移量
        auto to_abs_ptr = [base](uintptr_t val) -> uintptr_t {
            return (val < base) ? (base + val) : val;
        };

        for (const ElfW(Dyn) *d = dyn; d->d_tag != DT_NULL; ++d) {
            switch (d->d_tag) {
                case DT_SYMTAB:   symtab = reinterpret_cast<const ElfW(Sym) *>(to_abs_ptr(d->d_un.d_ptr)); break;
                case DT_STRTAB:   strtab = reinterpret_cast<const char *>(to_abs_ptr(d->d_un.d_ptr)); break;
                case DT_STRSZ:    strsz = static_cast<size_t>(d->d_un.d_val); break;
                case DT_JMPREL:   jmprel = to_abs_ptr(d->d_un.d_ptr); break;
                case DT_PLTRELSZ: pltrelsz = static_cast<size_t>(d->d_un.d_val); break;
                case DT_PLTREL:   pltrel_type = static_cast<uintptr_t>(d->d_un.d_val); break;
                default: break;
            }
        }

        if (!symtab || !strtab || !jmprel || pltrelsz == 0) return 0;
        size_t page_sz = static_cast<size_t>(sysconf(_SC_PAGESIZE));
        if (page_sz == 0) page_sz = 4096;

#if defined(__LP64__)
        if (pltrel_type == DT_RELA || pltrel_type == 0) {
            size_t count = pltrelsz / sizeof(Elf64_Rela);
            const auto *entries = reinterpret_cast<const Elf64_Rela *>(jmprel);
            for (size_t i = 0; i < count; ++i) {
                uint32_t r_type = ELF64_R_TYPE(entries[i].r_info);
                uint32_t sym_idx = ELF64_R_SYM(entries[i].r_info);
                if (sym_idx == 0) continue;
                if (r_type != R_AARCH64_JUMP_SLOT && r_type != R_AARCH64_GLOB_DAT &&
                    r_type != R_X86_64_JUMP_SLOT && r_type != R_X86_64_GLOB_DAT) {
                    continue;
                }
                uint32_t st_name = symtab[sym_idx].st_name;
                if (strsz > 0 && st_name >= strsz) continue;
                const char *sym_name = strtab + st_name;
                if (sym_name && sym_name[0] != '\0') {
                    patch_single_jmprel_slot(base, entries[i].r_offset, sym_name, relro_start, relro_end, page_sz);
                }
            }
        }
#else
        if (pltrel_type == DT_REL || pltrel_type == 0) {
            size_t count = pltrelsz / sizeof(Elf32_Rel);
            const auto *entries = reinterpret_cast<const Elf32_Rel *>(jmprel);
            for (size_t i = 0; i < count; ++i) {
                uint32_t r_type = ELF32_R_TYPE(entries[i].r_info);
                uint32_t sym_idx = ELF32_R_SYM(entries[i].r_info);
                if (sym_idx == 0) continue;
                if (r_type != R_ARM_JUMP_SLOT && r_type != R_ARM_GLOB_DAT &&
                    r_type != R_386_JMP_SLOT && r_type != R_386_GLOB_DAT) {
                    continue;
                }
                uint32_t st_name = symtab[sym_idx].st_name;
                if (strsz > 0 && st_name >= strsz) continue;
                const char *sym_name = strtab + st_name;
                if (sym_name && sym_name[0] != '\0') {
                    patch_single_jmprel_slot(base, entries[i].r_offset, sym_name, relro_start, relro_end, page_sz);
                }
            }
        }
#endif
        return 0;
    }, nullptr);

    g_maps_mtx.unlock();
}

inline void scan_and_commit_plt_hooks() {
    if (!g_zygisk_api) {
        patch_runtime_app_lib_jmprel_safe();
        return;
    }
    if (!g_maps_mtx.try_lock()) return;

    FILE *fp = orig_fopen ? orig_fopen("/proc/self/maps", "r") : std::fopen("/proc/self/maps", "r");
    if (!fp) {
        g_maps_mtx.unlock();
        return;
    }

    char line[1024];
    std::set<std::pair<dev_t, ino_t>> batch_to_register;

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
        auto seg_key = std::make_tuple(dev, inode, offset);
        if (g_hooked_segments.insert(seg_key).second) {
            batch_to_register.emplace(dev, inode);
        }
    }
    if (orig_fclose) orig_fclose(fp); else std::fclose(fp);

    for (const auto &target : batch_to_register) {
        dev_t dev = target.first;
        ino_t inode = target.second;

        auto reg = [&](const char *sym, void *new_fn, void **orig_slot) {
            void **target_old = (orig_slot && *orig_slot == nullptr) ? orig_slot : &g_discard_old_slot;
            g_zygisk_api->pltHookRegister(dev, inode, sym, new_fn, target_old);
        };

        reg("dlopen", reinterpret_cast<void *>(hook_dlopen), reinterpret_cast<void **>(&orig_dlopen));
        reg("android_dlopen_ext", reinterpret_cast<void *>(hook_android_dlopen_ext), reinterpret_cast<void **>(&orig_android_dlopen_ext));
        reg("dlsym", reinterpret_cast<void *>(hook_dlsym), reinterpret_cast<void **>(&orig_dlsym));

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
            reg("android_fdsan_close_with_tag", reinterpret_cast<void *>(hook_android_fdsan_close_with_tag), reinterpret_cast<void **>(&orig_android_fdsan_close_with_tag));
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
            reg("execve", reinterpret_cast<void *>(hook_execve), reinterpret_cast<void **>(&orig_execve));
        }

        if (g_cfg.gl.enabled) {
            reg("glGetString", reinterpret_cast<void *>(hook_glGetString), reinterpret_cast<void **>(&orig_glGetString));
            reg("eglGetProcAddress", reinterpret_cast<void *>(hook_eglGetProcAddress), reinterpret_cast<void **>(&orig_eglGetProcAddress));
            reg("vkGetInstanceProcAddr", reinterpret_cast<void *>(hook_vkGetInstanceProcAddr), reinterpret_cast<void **>(&orig_vkGetInstanceProcAddr));
            reg("vkGetDeviceProcAddr", reinterpret_cast<void *>(hook_vkGetDeviceProcAddr), reinterpret_cast<void **>(&orig_vkGetDeviceProcAddr));
            reg("vkGetPhysicalDeviceProperties", reinterpret_cast<void *>(hook_vkGetPhysicalDeviceProperties), reinterpret_cast<void **>(&orig_vkGetPhysicalDeviceProperties));
            reg("vkGetPhysicalDeviceProperties2", reinterpret_cast<void *>(hook_vkGetPhysicalDeviceProperties2), reinterpret_cast<void **>(&orig_vkGetPhysicalDeviceProperties2));
            reg("vkGetPhysicalDeviceProperties2KHR", reinterpret_cast<void *>(hook_vkGetPhysicalDeviceProperties2), &g_discard_old_slot);
            reg("AMediaDrm_getPropertyString", reinterpret_cast<void *>(hook_AMediaDrm_getPropertyString), reinterpret_cast<void **>(&orig_AMediaDrm_getPropertyString));
        }
    }

    if (!batch_to_register.empty()) {
        g_zygisk_api->pltHookCommit();
    }
    g_maps_mtx.unlock();
}

} // namespace mirage

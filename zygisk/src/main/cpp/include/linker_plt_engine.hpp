#pragma once

#include <cstring>
#include <string>
#include <unordered_set>
#include <mutex>
#include <dlfcn.h>
#include <link.h>
#include <elf.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/sysmacros.h>
#include "zygisk.hpp"
#include "mirage_config.hpp"
#include "mirage_logger.hpp"
#include "bionic_prop_engine.hpp"
#include "vfs_sysinfo_engine.hpp"
#include "graphics_vulkan_gles.hpp"

#if defined(__LP64__)
using Elf_Ehdr = Elf64_Ehdr;
using Elf_Phdr = Elf64_Phdr;
using Elf_Dyn  = Elf64_Dyn;
using Elf_Sym  = Elf64_Sym;
using Elf_Rela = Elf64_Rela;
using Elf_Rel  = Elf64_Rel;
using Elf_Addr = Elf64_Addr;
#define ELF_R_SYM_FN(info) ELF64_R_SYM(info)
#else
using Elf_Ehdr = Elf32_Ehdr;
using Elf_Phdr = Elf32_Phdr;
using Elf_Dyn  = Elf32_Dyn;
using Elf_Sym  = Elf32_Sym;
using Elf_Rela = Elf32_Rela;
using Elf_Rel  = Elf32_Rel;
using Elf_Addr = Elf32_Addr;
#define ELF_R_SYM_FN(info) ELF32_R_SYM(info)
#endif

namespace mirage {

inline zygisk::Api *g_zygisk_api = nullptr;
inline std::mutex g_plt_hook_mtx;
inline std::unordered_set<uint64_t> g_hooked_inodes;

inline void *(*orig_dlopen)(const char *filename, int flags) = nullptr;
inline void *(*orig_android_dlopen_ext)(const char *filename, int flags, const void *extinfo) = nullptr;
inline void *(*orig_dlsym)(void *handle, const char *symbol) = nullptr;

inline void *hook_dlopen(const char *filename, int flags);
inline void *hook_android_dlopen_ext(const char *filename, int flags, const void *extinfo);
inline void *hook_dlsym(void *handle, const char *symbol);

inline void init_linker_symbols() {
    if (orig_dlopen) return;
    orig_dlopen = reinterpret_cast<decltype(orig_dlopen)>(dlsym(RTLD_DEFAULT, "dlopen"));
    orig_android_dlopen_ext = reinterpret_cast<decltype(orig_android_dlopen_ext)>(dlsym(RTLD_DEFAULT, "android_dlopen_ext"));
    orig_dlsym = reinterpret_cast<decltype(orig_dlsym)>(dlsym(RTLD_DEFAULT, "dlsym"));
}

inline void *resolve_active_hook_symbol(const char *sym) noexcept {
    if (!sym) return nullptr;

    if (g_cfg.build.enabled || g_cfg.probe.enabled) {
        if (std::strcmp(sym, "__system_property_find") == 0)          return reinterpret_cast<void *>(hook_system_property_find);
        if (std::strcmp(sym, "__system_property_read_callback") == 0) return reinterpret_cast<void *>(hook_system_property_read_callback);
        if (std::strcmp(sym, "__system_property_read") == 0)          return reinterpret_cast<void *>(hook_system_property_read);
        if (std::strcmp(sym, "__system_property_get") == 0)           return reinterpret_cast<void *>(hook_system_property_get);
        if (std::strcmp(sym, "__system_property_foreach") == 0)       return reinterpret_cast<void *>(hook_system_property_foreach);
        if (std::strcmp(sym, "__system_property_find_nth") == 0)      return reinterpret_cast<void *>(hook_system_property_find_nth);
        if (std::strcmp(sym, "popen") == 0)                           return reinterpret_cast<void *>(hook_popen);
    }

    if (g_cfg.gl.enabled) {
        if (std::strcmp(sym, "glGetString") == 0)                        return reinterpret_cast<void *>(hook_glGetString);
        if (std::strcmp(sym, "glGetStringi") == 0)                       return reinterpret_cast<void *>(hook_glGetStringi);
        if (std::strcmp(sym, "eglGetProcAddress") == 0)                  return reinterpret_cast<void *>(hook_eglGetProcAddress);
        if (std::strcmp(sym, "vkGetInstanceProcAddr") == 0)              return reinterpret_cast<void *>(hook_vkGetInstanceProcAddr);
        if (std::strcmp(sym, "vkGetPhysicalDeviceProperties") == 0)      return reinterpret_cast<void *>(hook_vkGetPhysicalDeviceProperties);
        if (std::strcmp(sym, "vkGetPhysicalDeviceProperties2") == 0)     return reinterpret_cast<void *>(hook_vkGetPhysicalDeviceProperties2);
        if (std::strcmp(sym, "vkGetPhysicalDeviceProperties2KHR") == 0)  return reinterpret_cast<void *>(hook_vkGetPhysicalDeviceProperties2KHR);
        if (std::strcmp(sym, "AMediaDrm_getPropertyString") == 0)        return reinterpret_cast<void *>(hook_AMediaDrm_getPropertyString);
    }

    if (g_cfg.vfs.enabled || g_cfg.build.enabled || g_cfg.probe.enabled) {
        if (std::strcmp(sym, "openat") == 0)     return reinterpret_cast<void *>(hook_openat);
        if (std::strcmp(sym, "openat64") == 0)   return reinterpret_cast<void *>(hook_openat64);
        if (std::strcmp(sym, "open") == 0)       return reinterpret_cast<void *>(hook_open);
        if (std::strcmp(sym, "open64") == 0)     return reinterpret_cast<void *>(hook_open64);
        if (std::strcmp(sym, "fopen") == 0)      return reinterpret_cast<void *>(hook_fopen);
        if (std::strcmp(sym, "fopen64") == 0)    return reinterpret_cast<void *>(hook_fopen64);
        if (std::strcmp(sym, "close") == 0)      return reinterpret_cast<void *>(hook_close);
        if (std::strcmp(sym, "readlinkat") == 0) return reinterpret_cast<void *>(hook_readlinkat);
        if (std::strcmp(sym, "readlink") == 0)   return reinterpret_cast<void *>(hook_readlink);
        if (std::strcmp(sym, "fstat") == 0)      return reinterpret_cast<void *>(hook_fstat);
        if (std::strcmp(sym, "fstat64") == 0)    return reinterpret_cast<void *>(hook_fstat64);
        if (std::strcmp(sym, "stat") == 0)       return reinterpret_cast<void *>(hook_stat);
        if (std::strcmp(sym, "stat64") == 0)     return reinterpret_cast<void *>(hook_stat64);
        if (std::strcmp(sym, "lstat") == 0)      return reinterpret_cast<void *>(hook_lstat);
        if (std::strcmp(sym, "lstat64") == 0)    return reinterpret_cast<void *>(hook_lstat64);
        if (std::strcmp(sym, "fstatat") == 0)    return reinterpret_cast<void *>(hook_fstatat);
        if (std::strcmp(sym, "fstatat64") == 0)  return reinterpret_cast<void *>(hook_fstatat64);
        if (std::strcmp(sym, "access") == 0)     return reinterpret_cast<void *>(hook_access);
        if (std::strcmp(sym, "faccessat") == 0)  return reinterpret_cast<void *>(hook_faccessat);
    }

    if (g_cfg.vfs.enabled) {
        if (std::strcmp(sym, "uname") == 0)   return reinterpret_cast<void *>(hook_uname);
        if (std::strcmp(sym, "sysinfo") == 0) return reinterpret_cast<void *>(hook_sysinfo);
        if (std::strcmp(sym, "sysconf") == 0) return reinterpret_cast<void *>(hook_sysconf);
    }

    return nullptr;
}

inline void *hook_dlsym(void *handle, const char *symbol) {
    void *real_sym = orig_dlsym ? orig_dlsym(handle, symbol) : nullptr;
    if (!symbol) return real_sym;

    if (void *hooked = resolve_active_hook_symbol(symbol)) {
        if (real_sym) {
            if (std::strcmp(symbol, "vkGetInstanceProcAddr") == 0 && !orig_vkGetInstanceProcAddr) {
                orig_vkGetInstanceProcAddr = reinterpret_cast<PFN_vkGetInstanceProcAddr>(real_sym);
            } else if (std::strcmp(symbol, "vkGetPhysicalDeviceProperties") == 0 && !orig_vkGetPhysicalDeviceProperties) {
                orig_vkGetPhysicalDeviceProperties = reinterpret_cast<PFN_vkGetPhysicalDeviceProperties>(real_sym);
            } else if (std::strcmp(symbol, "vkGetPhysicalDeviceProperties2") == 0 && !orig_vkGetPhysicalDeviceProperties2) {
                orig_vkGetPhysicalDeviceProperties2 = reinterpret_cast<PFN_vkGetPhysicalDeviceProperties2>(real_sym);
            }
        }
        return hooked;
    }
    return real_sym;
}

inline void register_hooks_for_inode(dev_t dev, ino_t ino) {
    if (!g_zygisk_api) return;
    auto reg = [&](const char *sym, void *new_fn) {
        g_zygisk_api->pltHookRegister(dev, ino, sym, new_fn, nullptr);
    };

    if (g_cfg.build.enabled || g_cfg.probe.enabled) {
        reg("__system_property_find",          reinterpret_cast<void *>(hook_system_property_find));
        reg("__system_property_read_callback", reinterpret_cast<void *>(hook_system_property_read_callback));
        reg("__system_property_read",          reinterpret_cast<void *>(hook_system_property_read));
        reg("__system_property_get",           reinterpret_cast<void *>(hook_system_property_get));
        reg("__system_property_foreach",       reinterpret_cast<void *>(hook_system_property_foreach));
        reg("__system_property_find_nth",      reinterpret_cast<void *>(hook_system_property_find_nth));
        reg("popen",                           reinterpret_cast<void *>(hook_popen));
    }

    if (g_cfg.gl.enabled) {
        reg("glGetString",                       reinterpret_cast<void *>(hook_glGetString));
        reg("glGetStringi",                      reinterpret_cast<void *>(hook_glGetStringi));
        reg("eglGetProcAddress",                 reinterpret_cast<void *>(hook_eglGetProcAddress));
        reg("vkGetInstanceProcAddr",             reinterpret_cast<void *>(hook_vkGetInstanceProcAddr));
        reg("vkGetPhysicalDeviceProperties",     reinterpret_cast<void *>(hook_vkGetPhysicalDeviceProperties));
        reg("vkGetPhysicalDeviceProperties2",    reinterpret_cast<void *>(hook_vkGetPhysicalDeviceProperties2));
        reg("vkGetPhysicalDeviceProperties2KHR", reinterpret_cast<void *>(hook_vkGetPhysicalDeviceProperties2KHR));
        reg("AMediaDrm_getPropertyString",       reinterpret_cast<void *>(hook_AMediaDrm_getPropertyString));
    }

    if (g_cfg.vfs.enabled || g_cfg.build.enabled || g_cfg.probe.enabled) {
        reg("openat",     reinterpret_cast<void *>(hook_openat));
        reg("openat64",   reinterpret_cast<void *>(hook_openat64));
        reg("open",       reinterpret_cast<void *>(hook_open));
        reg("open64",     reinterpret_cast<void *>(hook_open64));
        reg("fopen",      reinterpret_cast<void *>(hook_fopen));
        reg("fopen64",    reinterpret_cast<void *>(hook_fopen64));
        reg("close",      reinterpret_cast<void *>(hook_close));
        reg("readlinkat", reinterpret_cast<void *>(hook_readlinkat));
        reg("readlink",   reinterpret_cast<void *>(hook_readlink));
        reg("fstat",      reinterpret_cast<void *>(hook_fstat));
        reg("fstat64",    reinterpret_cast<void *>(hook_fstat64));
        reg("stat",       reinterpret_cast<void *>(hook_stat));
        reg("stat64",     reinterpret_cast<void *>(hook_stat64));
        reg("lstat",      reinterpret_cast<void *>(hook_lstat));
        reg("lstat64",    reinterpret_cast<void *>(hook_lstat64));
        reg("fstatat",    reinterpret_cast<void *>(hook_fstatat));
        reg("fstatat64",  reinterpret_cast<void *>(hook_fstatat64));
        reg("access",     reinterpret_cast<void *>(hook_access));
        reg("faccessat",  reinterpret_cast<void *>(hook_faccessat));
    }

    if (g_cfg.vfs.enabled) {
        reg("uname",   reinterpret_cast<void *>(hook_uname));
        reg("sysinfo", reinterpret_cast<void *>(hook_sysinfo);
        reg("sysconf", reinterpret_cast<void *>(hook_sysconf));
    }

    reg("dlopen",             reinterpret_cast<void *>(hook_dlopen));
    reg("android_dlopen_ext", reinterpret_cast<void *>(hook_android_dlopen_ext));
    reg("dlsym",              reinterpret_cast<void *>(hook_dlsym));
}

inline bool is_target_elf_library(const char *path) noexcept {
    if (!path || path[0] != '/') return false;

    if (std::strstr(path, "libmirage_forge") ||
        std::strstr(path, "zygisk") ||
        std::strstr(path, "/libc.so") ||
        std::strstr(path, "/libdl.so") ||
        std::strstr(path, "/libm.so") ||
        std::strstr(path, "/ld-android.so") ||
        std::strstr(path, "/linker")) {
        return false;
    }

    if (std::strncmp(path, "/data/", 6) == 0 ||
        std::strncmp(path, "/mnt/asec/", 10) == 0) {
        return true;
    }

    static constexpr const char *kSystemBridgeLibs[] = {
        "/libandroid_runtime.so",
        "/libjavacore.so",
        "/libopenjdk.so",
        "/libopenjdkjvm.so",
        "/libnativeloader.so",
        "/libnativeloader_lazy.so",
        "/libhwui.so",
        "/libEGL.so",
        "/libGLESv1_CM.so",
        "/libGLESv2.so",
        "/libGLESv3.so",
        "/libvulkan.so",
        "/libmediandk.so",
        "/libcutils.so",
        "/libbase.so",
        "/libgui.so",
        "/libui.so",
        "/libandroid.so",
        "/libsensor.so",
    };

    for (const char *bridge : kSystemBridgeLibs) {
        if (std::strstr(path, bridge) != nullptr) {
            return true;
        }
    }
    return false;
}

inline void scan_and_commit_plt_hooks() {
    std::lock_guard<std::mutex> lock(g_plt_hook_mtx);
    if (!g_zygisk_api) return;

    FILE *fp = orig_fopen ? orig_fopen("/proc/self/maps", "r") : std::fopen("/proc/self/maps", "r");
    if (!fp) return;

    char line[1024];
    bool added_any = false;

    while (std::fgets(line, sizeof(line), fp)) {
        if (!std::strstr(line, "r-xp") && !std::strstr(line, "r--p")) continue;

        char *path = std::strchr(line, '/');
        if (!path) continue;
        char *nl = std::strpbrk(path, "\r\n");
        if (nl) *nl = '\0';

        if (!is_target_elf_library(path)) continue;

        unsigned int dev_maj = 0, dev_min = 0;
        unsigned long ino = 0;
        if (std::sscanf(line, "%*x-%*x %*4s %*x %x:%x %lu", &dev_maj, &dev_min, &ino) == 3 && ino > 0) {
            dev_t dev = makedev(dev_maj, dev_min);
            uint64_t key = (static_cast<uint64_t>(dev) << 32) ^ static_cast<uint64_t>(ino);
            if (g_hooked_inodes.insert(key).second) {
                register_hooks_for_inode(dev, static_cast<ino_t>(ino));
                added_any = true;
            }
        }
    }
    std::fclose(fp);

    if (added_any) {
        g_zygisk_api->pltHookCommit();
    }
}

inline void patch_single_got_slot(Elf_Addr *got_slot, void *new_func) {
    if (!got_slot || !new_func) return;
    Elf_Addr target_val = reinterpret_cast<Elf_Addr>(new_func);
    if (*got_slot == target_val) return;

    uintptr_t page_size = static_cast<uintptr_t>(sysconf(_SC_PAGESIZE));
    if (page_size == 0) page_size = 4096;
    uintptr_t page_start = reinterpret_cast<uintptr_t>(got_slot) & ~(page_size - 1);

    if (mprotect(reinterpret_cast<void *>(page_start), page_size, PROT_READ | PROT_WRITE) == 0) {
        __atomic_store_n(got_slot, target_val, __ATOMIC_RELEASE);
        mprotect(reinterpret_cast<void *>(page_start), page_size, PROT_READ);
    }
}

inline int dl_iterate_app_private_cb(struct dl_phdr_info *info, size_t, void *) {
    const char *name = info->dlpi_name;
    if (!name || name[0] != '/') return 0;

    if (std::strncmp(name, "/data/", 6) != 0 && std::strncmp(name, "/mnt/asec/", 10) != 0) {
        return 0;
    }
    if (std::strstr(name, "libmirage_forge") != nullptr) return 0;

    uint64_t base_key = static_cast<uint64_t>(info->dlpi_addr);
    if (base_key == 0 || !g_hooked_inodes.insert(base_key).second) {
        return 0;
    }

    const Elf_Dyn *dyn = nullptr;
    for (int i = 0; i < info->dlpi_phnum; ++i) {
        if (info->dlpi_phdr[i].p_type == PT_DYNAMIC) {
            dyn = reinterpret_cast<const Elf_Dyn *>(info->dlpi_addr + info->dlpi_phdr[i].p_vaddr);
            break;
        }
    }
    if (!dyn) return 0;

    const Elf_Sym *symtab = nullptr;
    const char *strtab = nullptr;
    const void *jmprel = nullptr;
    size_t pltrelsz = 0;
    int pltrel_type = 0;

    const void *rela_dyn = nullptr;
    size_t rela_sz = 0;

    for (; dyn->d_tag != DT_NULL; ++dyn) {
        switch (dyn->d_tag) {
            case DT_SYMTAB:   symtab = reinterpret_cast<const Elf_Sym *>(dyn->d_un.d_ptr); break;
            case DT_STRTAB:   strtab = reinterpret_cast<const char *>(dyn->d_un.d_ptr); break;
            case DT_JMPREL:   jmprel = reinterpret_cast<const void *>(dyn->d_un.d_ptr); break;
            case DT_PLTRELSZ: pltrelsz = static_cast<size_t>(dyn->d_un.d_val); break;
            case DT_PLTREL:   pltrel_type = static_cast<int>(dyn->d_un.d_val); break;
            case DT_RELA:     rela_dyn = reinterpret_cast<const void *>(dyn->d_un.d_ptr); break;
            case DT_RELASZ:   rela_sz = static_cast<size_t>(dyn->d_un.d_val); break;
            default: break;
        }
    }

    auto norm_ptr = [&](const void *p) -> uintptr_t {
        uintptr_t u = reinterpret_cast<uintptr_t>(p);
        if (u != 0 && u < info->dlpi_addr) u += info->dlpi_addr;
        return u;
    };

    symtab = reinterpret_cast<const Elf_Sym *>(norm_ptr(symtab));
    strtab = reinterpret_cast<const char *>(norm_ptr(strtab));
    jmprel = reinterpret_cast<const void *>(norm_ptr(jmprel));
    rela_dyn = reinterpret_cast<const void *>(norm_ptr(rela_dyn));

    if (!symtab || !strtab) return 0;

    auto process_relocs = [&](const void *rel_base, size_t rel_bytes, bool is_rela) {
        if (!rel_base || rel_bytes == 0) return;
        if (is_rela) {
            size_t count = rel_bytes / sizeof(Elf_Rela);
            const auto *rels = static_cast<const Elf_Rela *>(rel_base);
            for (size_t i = 0; i < count; ++i) {
                size_t sym_idx = ELF_R_SYM_FN(rels[i].r_info);
                if (sym_idx == 0) continue;
                const char *sym_name = strtab + symtab[sym_idx].st_name;
                if (void *hook_fn = resolve_active_hook_symbol(sym_name)) {
                    auto *slot = reinterpret_cast<Elf_Addr *>(info->dlpi_addr + rels[i].r_offset);
                    patch_single_got_slot(slot, hook_fn);
                } else if (std::strcmp(sym_name, "dlsym") == 0) {
                    auto *slot = reinterpret_cast<Elf_Addr *>(info->dlpi_addr + rels[i].r_offset);
                    patch_single_got_slot(slot, reinterpret_cast<void *>(hook_dlsym));
                }
            }
        } else {
            size_t count = rel_bytes / sizeof(Elf_Rel);
            const auto *rels = static_cast<const Elf_Rel *>(rel_base);
            for (size_t i = 0; i < count; ++i) {
                size_t sym_idx = ELF_R_SYM_FN(rels[i].r_info);
                if (sym_idx == 0) continue;
                const char *sym_name = strtab + symtab[sym_idx].st_name;
                if (void *hook_fn = resolve_active_hook_symbol(sym_name)) {
                    auto *slot = reinterpret_cast<Elf_Addr *>(info->dlpi_addr + rels[i].r_offset);
                    patch_single_got_slot(slot, hook_fn);
                } else if (std::strcmp(sym_name, "dlsym") == 0) {
                    auto *slot = reinterpret_cast<Elf_Addr *>(info->dlpi_addr + rels[i].r_offset);
                    patch_single_got_slot(slot, reinterpret_cast<void *>(hook_dlsym));
                }
            }
        }
    };

    process_relocs(jmprel, pltrelsz, pltrel_type == DT_RELA);
    process_relocs(rela_dyn, rela_sz, true);
    return 0;
}

inline void patch_newly_loaded_app_libraries() {
    std::lock_guard<std::mutex> lock(g_plt_hook_mtx);
    dl_iterate_phdr(dl_iterate_app_private_cb, nullptr);
}

inline bool is_app_private_dlopen_target(const char *filename) noexcept {
    if (!filename) return false;
    if (std::strncmp(filename, "/system/", 8) == 0 ||
        std::strncmp(filename, "/apex/", 6) == 0 ||
        std::strncmp(filename, "/vendor/", 8) == 0 ||
        std::strcmp(filename, "libandroid.so") == 0 ||
        std::strcmp(filename, "libEGL.so") == 0 ||
        std::strcmp(filename, "libGLESv2.so") == 0 ||
        std::strcmp(filename, "libvulkan.so") == 0) {
        return false;
    }
    return true;
}

inline void *hook_dlopen(const char *filename, int flags) {
    void *handle = orig_dlopen ? orig_dlopen(filename, flags) : nullptr;
    if (handle && g_cfg.active && is_app_private_dlopen_target(filename)) {
        ScopedHookGuard guard;
        if (guard) {
            patch_newly_loaded_app_libraries();
        }
    }
    return handle;
}

inline void *hook_android_dlopen_ext(const char *filename, int flags, const void *extinfo) {
    void *handle = orig_android_dlopen_ext ? orig_android_dlopen_ext(filename, flags, extinfo) : nullptr;
    if (handle && g_cfg.active && is_app_private_dlopen_target(filename)) {
        ScopedHookGuard guard;
        if (guard) {
            patch_newly_loaded_app_libraries();
        }
    }
    return handle;
}

} // namespace mirage

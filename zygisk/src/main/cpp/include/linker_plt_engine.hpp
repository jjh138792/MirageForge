#pragma once

#include <cstdio>
#include <cstring>
#include <cinttypes>
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

namespace mirage {

inline zygisk::Api *g_zygisk_api = nullptr;
inline std::mutex g_maps_mtx;
inline std::set<std::pair<dev_t, ino_t>> g_hooked_elves;
inline std::unordered_set<uintptr_t> g_patched_phdr_bases;
inline void *g_discard_old_slot = nullptr;

inline void *(*orig_dlopen)(const char *filename, int flags) = nullptr;
inline void *(*orig_android_dlopen_ext)(const char *filename, int flags, const void *extinfo) = nullptr;
inline void *(*orig_dlsym)(void *handle, const char *symbol) = nullptr;

inline void scan_and_commit_plt_hooks();
inline void patch_loaded_elves_via_phdr();

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
    if (!path || len == 0) return false;

    bool is_binary = ends_with_ext(path, len, ".so") ||
                     ends_with_ext(path, len, ".apk") ||
                     (std::strstr(path, ".so.") != nullptr);
    if (!is_binary) return false;

    const char *slash = std::strrchr(path, '/');
    const char *fname = slash ? (slash + 1) : path;
    if (const char *bang = std::strrchr(fname, '!')) {
        fname = bang + 1;
        if (const char *s2 = std::strrchr(fname, '/')) fname = s2 + 1;
    }

    // 永远不 Hook Bionic 自身、ART 核心、WebView 与本模块自身
    if (std::strcmp(fname, "libc.so") == 0 ||
        std::strcmp(fname, "libdl.so") == 0 ||
        std::strcmp(fname, "libm.so") == 0 ||
        std::strcmp(fname, "libart.so") == 0 ||
        std::strncmp(fname, "linker", 6) == 0 ||
        std::strncmp(fname, "libwebviewchromium", 18) == 0 ||
        std::strstr(path, "mirage_forge") != nullptr) {
        return false;
    }

    // 应用自身目录 (/data/app, /data/user, /data/data, /mnt/asec, base.apk!... 等) 下的所有库 100% 挂载
    if (std::strncmp(path, "/data/", 6) == 0 ||
        std::strncmp(path, "/mnt/", 5) == 0 ||
        std::strstr(path, ".apk") != nullptr) {
        return true;
    }

    // 系统与 APEX 目录下，精准拦截核心桥接库（含负责 System.loadLibrary 的 libnativeloader.so！）
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

    // 若 dl_iterate_phdr 传入的是不带 '/' 的纯 SONAME 且不在系统黑名单中，允许挂载
    if (path[0] != '/') {
        return true;
    }
    return false;
}

// 统一符号路由表：同时供 hook_dlsym 与 dl_iterate_phdr GOT 热修补器调用
inline void *resolve_mirage_hook_symbol(const char *symbol) noexcept {
    if (!symbol) return nullptr;

    if (std::strcmp(symbol, "dlopen") == 0) return reinterpret_cast<void *>(orig_dlopen ? nullptr : nullptr); // 单独处理

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
    }
    if (g_cfg.gl.enabled) {
        if (std::strcmp(symbol, "glGetString") == 0) return reinterpret_cast<void *>(hook_glGetString);
        if (std::strcmp(symbol, "eglGetProcAddress") == 0) return reinterpret_cast<void *>(hook_eglGetProcAddress);
        if (std::strcmp(symbol, "vkGetInstanceProcAddr") == 0) return reinterpret_cast<void *>(hook_vkGetInstanceProcAddr);
        if (std::strcmp(symbol, "vkGetPhysicalDeviceProperties") == 0) return reinterpret_cast<void *>(hook_vkGetPhysicalDeviceProperties);
        if (std::strcmp(symbol, "vkGetPhysicalDeviceProperties2") == 0 ||
            std::strcmp(symbol, "vkGetPhysicalDeviceProperties2KHR") == 0) {
            return reinterpret_cast<void *>(hook_vkGetPhysicalDeviceProperties2);
        }
        if (std::strcmp(symbol, "AMediaDrm_getPropertyString") == 0) return reinterpret_cast<void *>(hook_AMediaDrm_getPropertyString);
    }
    return nullptr;
}

inline void *hook_dlsym(void *handle, const char *symbol) {
    if (symbol && !g_tls_in_hook) {
        if (void *hooked = resolve_mirage_hook_symbol(symbol)) {
            return hooked;
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
    // 过滤纯系统库名（如 libhwui 动态 dlopen("libandroid.so")），仅对应用层/Vulkan JNI 库触发增量扫描
    if (std::strcmp(filename, "libandroid.so") == 0 ||
        std::strcmp(filename, "libc.so") == 0 ||
        std::strcmp(filename, "libdl.so") == 0 ||
        std::strcmp(filename, "liblog.so") == 0) {
        return false;
    }
    return true;
}

inline void *hook_dlopen(const char *filename, int flags) {
    void *handle = orig_dlopen ? orig_dlopen(filename, flags) : nullptr;
    if (handle && should_rescan_on_dlopen(filename)) {
        ScopedHookGuard guard;
        if (guard) {
            emit_probe("DLOPEN", filename, "Incremental GOT Hook");
            patch_loaded_elves_via_phdr();
        }
    }
    return handle;
}

inline void *hook_android_dlopen_ext(const char *filename, int flags, const void *extinfo) {
    void *handle = orig_android_dlopen_ext ? orig_android_dlopen_ext(filename, flags, extinfo) : nullptr;
    if (handle && should_rescan_on_dlopen(filename)) {
        ScopedHookGuard guard;
        if (guard) {
            emit_probe("DLOPEN_EXT", filename, "Incremental GOT Hook");
            patch_loaded_elves_via_phdr();
        }
    }
    return handle;
}

// ============================================================================
// 基于 dl_iterate_phdr 的运行期安全 ELF GOT/PLT 热修补引擎
// 优势：1. 不依赖运行期已失效的 g_zygisk_api；2. 按 dlpi_addr 唯一基址识别 base.apk 内的每个独立 .so
// ============================================================================
inline void patch_single_got_entry(uintptr_t base, uintptr_t offset, const char *sym_name,
                                   uintptr_t relro_start, uintptr_t relro_end, size_t page_sz) {
    void *replacement = nullptr;
    if (std::strcmp(sym_name, "dlopen") == 0) replacement = reinterpret_cast<void *>(hook_dlopen);
    else if (std::strcmp(sym_name, "android_dlopen_ext") == 0) replacement = reinterpret_cast<void *>(hook_android_dlopen_ext);
    else if (std::strcmp(sym_name, "dlsym") == 0) replacement = reinterpret_cast<void *>(hook_dlsym);
    else replacement = resolve_mirage_hook_symbol(sym_name);

    if (!replacement) return;

    auto *slot = reinterpret_cast<void **>(base + offset);
    if (*slot == replacement) return;

    uintptr_t addr = reinterpret_cast<uintptr_t>(slot);
    uintptr_t page_start = addr & ~(page_sz - 1);

    if (mprotect(reinterpret_cast<void *>(page_start), page_sz, PROT_READ | PROT_WRITE) != 0) {
        return;
    }

    *slot = replacement;

    // 若整页完全处于 PT_GNU_RELRO 区间内，恢复只读保护；若跨 .data 边界则保持读写以防崩溃
    if (relro_start < relro_end && page_start >= relro_start && (page_start + page_sz) <= relro_end) {
        mprotect(reinterpret_cast<void *>(page_start), page_sz, PROT_READ);
    }
}

inline void patch_loaded_elves_via_phdr() {
    if (!g_maps_mtx.try_lock()) return;

    dl_iterate_phdr([](struct dl_phdr_info *info, size_t, void *) -> int {
        if (!info || !info->dlpi_name || info->dlpi_addr == 0 || !info->dlpi_phdr) return 0;

        size_t name_len = std::strlen(info->dlpi_name);
        if (!should_hook_elf_path(info->dlpi_name, name_len)) return 0;

        if (!g_patched_phdr_bases.insert(static_cast<uintptr_t>(info->dlpi_addr)).second) {
            return 0;
        }

        uintptr_t base = static_cast<uintptr_t>(info->dlpi_addr);
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
        uintptr_t jmprel = 0, pltrelsz = 0, pltrel_type = 0;
        uintptr_t rela = 0, relasz = 0;
        uintptr_t rel = 0, relsz = 0;

        for (const ElfW(Dyn) *d = dyn; d->d_tag != DT_NULL; ++d) {
            switch (d->d_tag) {
                case DT_SYMTAB:   symtab = reinterpret_cast<const ElfW(Sym) *>(d->d_un.d_ptr > base ? d->d_un.d_ptr : base + d->d_un.d_ptr); break;
                case DT_STRTAB:   strtab = reinterpret_cast<const char *>(d->d_un.d_ptr > base ? d->d_un.d_ptr : base + d->d_un.d_ptr); break;
                case DT_JMPREL:   jmprel = (d->d_un.d_ptr > base ? d->d_un.d_ptr : base + d->d_un.d_ptr); break;
                case DT_PLTRELSZ: pltrelsz = d->d_un.d_val; break;
                case DT_PLTREL:   pltrel_type = d->d_un.d_val; break;
                case DT_RELA:     rela = (d->d_un.d_ptr > base ? d->d_un.d_ptr : base + d->d_un.d_ptr); break;
                case DT_RELASZ:   relasz = d->d_un.d_val; break;
                case DT_REL:      rel = (d->d_un.d_ptr > base ? d->d_un.d_ptr : base + d->d_un.d_ptr); break;
                case DT_RELSZ:    relsz = d->d_un.d_val; break;
                default: break;
            }
        }

        if (!symtab || !strtab) return 0;
        size_t page_sz = static_cast<size_t>(sysconf(_SC_PAGESIZE));
        if (page_sz == 0) page_sz = 4096;

#if defined(__LP64__)
        auto process_rela = [&](uintptr_t table_addr, size_t total_bytes) {
            if (!table_addr || total_bytes == 0) return;
            size_t count = total_bytes / sizeof(Elf64_Rela);
            const auto *entries = reinterpret_cast<const Elf64_Rela *>(table_addr);
            for (size_t i = 0; i < count; ++i) {
                uint32_t sym_idx = ELF64_R_SYM(entries[i].r_info);
                if (sym_idx == 0) continue;
                const char *sym_name = strtab + symtab[sym_idx].st_name;
                if (sym_name && sym_name[0] != '\0') {
                    patch_single_got_entry(base, entries[i].r_offset, sym_name, relro_start, relro_end, page_sz);
                }
            }
        };
        if (jmprel && pltrelsz) process_rela(jmprel, pltrelsz);
        if (rela && relasz)     process_rela(rela, relasz);
        (void)pltrel_type; (void)rel; (void)relsz;
#else
        auto process_rel = [&](uintptr_t table_addr, size_t total_bytes) {
            if (!table_addr || total_bytes == 0) return;
            size_t count = total_bytes / sizeof(Elf32_Rel);
            const auto *entries = reinterpret_cast<const Elf32_Rel *>(table_addr);
            for (size_t i = 0; i < count; ++i) {
                uint32_t sym_idx = ELF32_R_SYM(entries[i].r_info);
                if (sym_idx == 0) continue;
                const char *sym_name = strtab + symtab[sym_idx].st_name;
                if (sym_name && sym_name[0] != '\0') {
                    patch_single_got_entry(base, entries[i].r_offset, sym_name, relro_start, relro_end, page_sz);
                }
            }
        };
        if (jmprel && pltrelsz && pltrel_type == DT_REL) process_rel(jmprel, pltrelsz);
        if (rel && relsz) process_rel(rel, relsz);
        (void)rela; (void)relasz;
#endif
        return 0;
    }, nullptr);

    g_maps_mtx.unlock();
}

// 仅在 preAppSpecialize / postAppSpecialize 期间使用 Zygisk 原生 API 注册，随后同步执行一次 phdr 扫描
inline void scan_and_commit_plt_hooks() {
    if (!g_zygisk_api) {
        patch_loaded_elves_via_phdr();
        return;
    }
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
        }

        if (g_cfg.gl.enabled) {
            reg("glGetString", reinterpret_cast<void *>(hook_glGetString), reinterpret_cast<void **>(&orig_glGetString));
            reg("eglGetProcAddress", reinterpret_cast<void *>(hook_eglGetProcAddress), reinterpret_cast<void **>(&orig_eglGetProcAddress));
            reg("vkGetInstanceProcAddr", reinterpret_cast<void *>(hook_vkGetInstanceProcAddr), reinterpret_cast<void **>(&orig_vkGetInstanceProcAddr));
            reg("vkGetPhysicalDeviceProperties", reinterpret_cast<void *>(hook_vkGetPhysicalDeviceProperties), reinterpret_cast<void **>(&orig_vkGetPhysicalDeviceProperties));
            reg("vkGetPhysicalDeviceProperties2", reinterpret_cast<void *>(hook_vkGetPhysicalDeviceProperties2), reinterpret_cast<void **>(&orig_vkGetPhysicalDeviceProperties2));
            reg("vkGetPhysicalDeviceProperties2KHR", reinterpret_cast<void *>(hook_vkGetPhysicalDeviceProperties2), &g_discard_old_slot);
            reg("AMediaDrm_getPropertyString", reinterpret_cast<void *>(hook_AMediaDrm_getPropertyString), reinterpret_cast<void **>(&orig_AMediaDrm_getPropertyString));
        }
    }
    if (orig_fclose) orig_fclose(fp); else std::fclose(fp);

    if (dirty) {
        g_zygisk_api->pltHookCommit();
    }
    g_maps_mtx.unlock();

    // 同步执行一次 phdr 扫描以记录当前基址
    patch_loaded_elves_via_phdr();
}

} // namespace mirage

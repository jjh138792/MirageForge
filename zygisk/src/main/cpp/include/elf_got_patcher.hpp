// SPDX-License-Identifier: GPL-3.0-only
// Copyright (c) 2025-2026 jjh138792
// MirageForge: https://github.com/jjh138792/MirageForge

#pragma once

#include <cstdint>
#include <cstddef>
#include <cstdio>
#include <cstring>
#include <cinttypes>
#include <unordered_set>
#include <mutex>
#include <link.h>
#include <elf.h>
#include <unistd.h>
#include <sys/mman.h>
#include "mirage_config.hpp"

namespace mirage {

struct GotPatchSpec {
    const char *symbol;
    void *replacement;
    void **orig_out;
};

inline int query_page_prot(
    uintptr_t addr,
    FILE *(*orig_fopen_fn)(const char *, const char *),
    int (*orig_fclose_fn)(FILE *)) noexcept {
    FILE *fp = orig_fopen_fn ? orig_fopen_fn("/proc/self/maps", "r") : std::fopen("/proc/self/maps", "r");
    if (!fp) return PROT_READ | PROT_WRITE;

    int prot = PROT_READ | PROT_WRITE;
    char line[512];
    while (std::fgets(line, sizeof(line), fp)) {
        uintptr_t s = 0, e = 0;
        char perms[8] = {0};
        if (std::sscanf(line, "%" SCNxPTR "-%" SCNxPTR " %7s", &s, &e, perms) >= 3) {
            if (addr >= s && addr < e) {
                prot = 0;
                if (perms[0] == 'r') prot |= PROT_READ;
                if (perms[1] == 'w') prot |= PROT_WRITE;
                if (perms[2] == 'x') prot |= PROT_EXEC;
                break;
            }
        }
    }
    if (orig_fclose_fn) orig_fclose_fn(fp); else std::fclose(fp);
    return prot;
}

inline bool write_got_slot_safe(
    void **slot,
    void *replacement,
    void **orig_out,
    FILE *(*orig_fopen_fn)(const char *, const char *),
    int (*orig_fclose_fn)(FILE *)) noexcept {
    if (!slot || !replacement) return false;

    void *cur = *slot;
    if (cur == replacement) return false;

    if (orig_out && *orig_out == nullptr && cur != nullptr) {
        *orig_out = cur;
    }

    long page_sz = sysconf(_SC_PAGESIZE);
    if (page_sz <= 0) page_sz = 4096;
    uintptr_t slot_addr = reinterpret_cast<uintptr_t>(slot);
    uintptr_t page_start = slot_addr & ~static_cast<uintptr_t>(page_sz - 1);

    int orig_prot = query_page_prot(slot_addr, orig_fopen_fn, orig_fclose_fn);
    bool need_mprotect = (orig_prot & PROT_WRITE) == 0;

    if (need_mprotect) {
        if (mprotect(reinterpret_cast<void *>(page_start), static_cast<size_t>(page_sz),
                     PROT_READ | PROT_WRITE) != 0) {
            return false;
        }
    }

    __atomic_store_n(slot, replacement, __ATOMIC_RELEASE);

    if (need_mprotect) {
        mprotect(reinterpret_cast<void *>(page_start), static_cast<size_t>(page_sz), orig_prot);
    }
    return true;
}

inline bool is_got_reloc_type(uint32_t r_type) noexcept {
#if defined(__aarch64__)
    return r_type == R_AARCH64_JUMP_SLOT || r_type == R_AARCH64_GLOB_DAT;
#elif defined(__arm__)
    return r_type == R_ARM_JUMP_SLOT || r_type == R_ARM_GLOB_DAT;
#elif defined(__x86_64__)
    return r_type == R_X86_64_JUMP_SLOT || r_type == R_X86_64_GLOB_DAT;
#elif defined(__i386__)
    return r_type == R_386_JMP_SLOT || r_type == R_386_GLOB_DAT;
#else
    (void)r_type;
    return false;
#endif
}

inline bool should_direct_got_patch_phdr(const char *name) noexcept {
    if (!name || name[0] == '\0') return false;
    if (std::strstr(name, "mirage_forge") != nullptr ||
        std::strncmp(name, "/data/adb/", 10) == 0 ||
        std::strstr(name, "libpairipcore") != nullptr) {
        return false;
    }

    const char *slash = std::strrchr(name, '/');
    const char *fname = slash ? (slash + 1) : name;

    // 严格跳过 Bionic / ART / Linker 核心库
    if (std::strcmp(fname, "libc.so") == 0 ||
        std::strcmp(fname, "libdl.so") == 0 ||
        std::strcmp(fname, "libm.so") == 0 ||
        std::strcmp(fname, "libart.so") == 0 ||
        std::strcmp(fname, "libartbase.so") == 0 ||
        std::strcmp(fname, "libdexfile.so") == 0 ||
        std::strncmp(fname, "linker", 6) == 0 ||
        std::strncmp(fname, "libopenjdk", 10) == 0) {
        return false;
    }

    // 1. 应用私有路径或未解压 APK 路径
    if (std::strstr(name, ".apk") != nullptr ||
        std::strncmp(name, "/data/", 6) == 0 ||
        std::strncmp(name, "/mnt/", 5) == 0) {
        return true;
    }

    // 2. 关键修复：Android Bionic 对 APK 内嵌库在 dlpi_name 中常仅保留纯文件名 (无 '/')
    // 例如 "libvulkan_info.so"、"libaida_vulkan.so"、"libdevcheck.so"
    if (slash == nullptr && std::strstr(fname, ".so") != nullptr) {
        if (std::strncmp(fname, "libandroid", 10) != 0 &&
            std::strncmp(fname, "libjavacore", 11) != 0 &&
            std::strncmp(fname, "libbase", 7) != 0 &&
            std::strncmp(fname, "libc++", 6) != 0 &&
            std::strncmp(fname, "libutils", 8) != 0 &&
            std::strncmp(fname, "libbinder", 9) != 0) {
            return true;
        }
    }

    // 3. 图形 / Vulkan / HAL 系统与厂商桥接库
    if (std::strcmp(fname, "libvulkan.so") == 0 ||
        std::strcmp(fname, "libhwui.so") == 0 ||
        std::strcmp(fname, "libvndksupport.so") == 0 ||
        std::strcmp(fname, "libhardware.so") == 0 ||
        std::strcmp(fname, "libEGL.so") == 0 ||
        std::strcmp(fname, "libGLESv1_CM.so") == 0 ||
        std::strcmp(fname, "libGLESv2.so") == 0 ||
        std::strcmp(fname, "libGLESv3.so") == 0 ||
        std::strncmp(fname, "vulkan.", 7) == 0 ||
        std::strstr(fname, "GLES_mali") != nullptr) {
        return true;
    }
    return false;
}

inline std::mutex g_direct_got_mtx;
inline std::unordered_set<uintptr_t> g_direct_got_bases;

struct DirectGotIterateCtx {
    const GotPatchSpec *specs;
    size_t spec_count;
    FILE *(*orig_fopen_fn)(const char *, const char *);
    int (*orig_fclose_fn)(FILE *);
    uint32_t patched_slots;
    uint32_t scanned_libs;
};

inline int direct_got_phdr_cb(struct dl_phdr_info *info, size_t, void *data) {
    auto *ctx = static_cast<DirectGotIterateCtx *>(data);
    if (!info || !ctx || info->dlpi_addr == 0 || !info->dlpi_phdr) return 0;

    const char *name = info->dlpi_name ? info->dlpi_name : "";
    if (!should_direct_got_patch_phdr(name)) return 0;

    uintptr_t base = static_cast<uintptr_t>(info->dlpi_addr);
    if (g_direct_got_bases.find(base) != g_direct_got_bases.end()) {
        return 0;
    }
    g_direct_got_bases.insert(base);
    ++ctx->scanned_libs;

    const ElfW(Dyn) *dyn = nullptr;
    for (ElfW(Half) i = 0; i < info->dlpi_phnum; ++i) {
        if (info->dlpi_phdr[i].p_type == PT_DYNAMIC) {
            dyn = reinterpret_cast<const ElfW(Dyn) *>(base + info->dlpi_phdr[i].p_vaddr);
            break;
        }
    }
    if (!dyn) return 0;

    const ElfW(Sym) *symtab = nullptr;
    const char *strtab = nullptr;
    uintptr_t jmprel = 0;
    size_t pltrelsz = 0;
#if defined(__LP64__)
    int pltrel_type = DT_RELA;
#else
    int pltrel_type = DT_REL;
#endif
    uintptr_t rela_tbl = 0;
    size_t rela_sz = 0;
    uintptr_t rel_tbl = 0;
    size_t rel_sz = 0;

    auto resolve_dyn_ptr = [&](uintptr_t val) -> uintptr_t {
        if (val == 0) return 0;
        return (val >= base) ? val : (base + val);
    };

    for (const ElfW(Dyn) *d = dyn; d->d_tag != DT_NULL; ++d) {
        switch (d->d_tag) {
            case DT_SYMTAB:   symtab = reinterpret_cast<const ElfW(Sym) *>(resolve_dyn_ptr(d->d_un.d_ptr)); break;
            case DT_STRTAB:   strtab = reinterpret_cast<const char *>(resolve_dyn_ptr(d->d_un.d_ptr)); break;
            case DT_JMPREL:   jmprel = resolve_dyn_ptr(d->d_un.d_ptr); break;
            case DT_PLTRELSZ: pltrelsz = static_cast<size_t>(d->d_un.d_val); break;
            case DT_PLTREL:   pltrel_type = static_cast<int>(d->d_un.d_val); break;
            case DT_RELA:     rela_tbl = resolve_dyn_ptr(d->d_un.d_ptr); break;
            case DT_RELASZ:   rela_sz = static_cast<size_t>(d->d_un.d_val); break;
            case DT_REL:      rel_tbl = resolve_dyn_ptr(d->d_un.d_ptr); break;
            case DT_RELSZ:    rel_sz = static_cast<size_t>(d->d_un.d_val); break;
            default: break;
        }
    }
    if (!symtab || !strtab) return 0;

    uint32_t lib_hits = 0;
    auto try_patch_entry = [&](uint32_t sym_idx, uint32_t r_type, uintptr_t r_offset) {
        if (sym_idx == 0 || !is_got_reloc_type(r_type)) return;
        const char *sym_name = strtab + symtab[sym_idx].st_name;
        if (!sym_name || sym_name[0] == '\0') return;

        for (size_t k = 0; k < ctx->spec_count; ++k) {
            if (std::strcmp(sym_name, ctx->specs[k].symbol) == 0) {
                auto **slot = reinterpret_cast<void **>(base + r_offset);
                if (write_got_slot_safe(slot, ctx->specs[k].replacement, ctx->specs[k].orig_out,
                                        ctx->orig_fopen_fn, ctx->orig_fclose_fn)) {
                    ++ctx->patched_slots;
                    ++lib_hits;
                }
                break;
            }
        }
    };

    auto scan_rela = [&](uintptr_t tbl, size_t sz) {
        if (!tbl || sz < sizeof(ElfW(Rela))) return;
        const auto *arr = reinterpret_cast<const ElfW(Rela) *>(tbl);
        size_t cnt = sz / sizeof(ElfW(Rela));
        for (size_t i = 0; i < cnt; ++i) {
            uint32_t sym_idx = static_cast<uint32_t>(
#if defined(__LP64__)
                ELF64_R_SYM(arr[i].r_info)
#else
                ELF32_R_SYM(arr[i].r_info)
#endif
            );
            uint32_t r_type = static_cast<uint32_t>(
#if defined(__LP64__)
                ELF64_R_TYPE(arr[i].r_info)
#else
                ELF32_R_TYPE(arr[i].r_info)
#endif
            );
            try_patch_entry(sym_idx, r_type, static_cast<uintptr_t>(arr[i].r_offset));
        }
    };

    auto scan_rel = [&](uintptr_t tbl, size_t sz) {
        if (!tbl || sz < sizeof(ElfW(Rel))) return;
        const auto *arr = reinterpret_cast<const ElfW(Rel) *>(tbl);
        size_t cnt = sz / sizeof(ElfW(Rel));
        for (size_t i = 0; i < cnt; ++i) {
            uint32_t sym_idx = static_cast<uint32_t>(
#if defined(__LP64__)
                ELF64_R_SYM(arr[i].r_info)
#else
                ELF32_R_SYM(arr[i].r_info)
#endif
            );
            uint32_t r_type = static_cast<uint32_t>(
#if defined(__LP64__)
                ELF64_R_TYPE(arr[i].r_info)
#else
                ELF32_R_TYPE(arr[i].r_info)
#endif
            );
            try_patch_entry(sym_idx, r_type, static_cast<uintptr_t>(arr[i].r_offset));
        }
    };

    if (jmprel && pltrelsz > 0) {
        if (pltrel_type == DT_RELA) scan_rela(jmprel, pltrelsz);
        else if (pltrel_type == DT_REL) scan_rel(jmprel, pltrelsz);
    }
    if (rela_tbl && rela_sz > 0) scan_rela(rela_tbl, rela_sz);
    if (rel_tbl && rel_sz > 0) scan_rel(rel_tbl, rel_sz);

    if (lib_hits > 0) {
        LOGI("[GOT_DIRECT] patched %u slot(s) in %s (base=0x%" PRIxPTR ")", lib_hits, name, base);
    }
    return 0;
}

inline uint32_t apply_direct_got_patches_all(
    const GotPatchSpec *specs,
    size_t spec_count,
    FILE *(*orig_fopen_fn)(const char *, const char *),
    int (*orig_fclose_fn)(FILE *)) {
    if (!specs || spec_count == 0) return 0;
    ErrnoRestorer errno_guard;
    if (!g_direct_got_mtx.try_lock()) return 0;

    DirectGotIterateCtx ctx{specs, spec_count, orig_fopen_fn, orig_fclose_fn, 0, 0};
    dl_iterate_phdr(direct_got_phdr_cb, &ctx);

    g_direct_got_mtx.unlock();
    return ctx.patched_slots;
}

} // namespace mirage

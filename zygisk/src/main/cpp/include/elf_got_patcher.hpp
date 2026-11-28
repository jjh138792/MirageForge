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

// 直接基于内存中已有的 PT_GNU_RELRO / PT_LOAD 推算槽位原始页权限，彻底消除 /proc/self/maps I/O
inline int infer_slot_prot_from_phdr(const struct dl_phdr_info *info, uintptr_t slot_addr) noexcept {
    uintptr_t base = static_cast<uintptr_t>(info->dlpi_addr);
    for (ElfW(Half) i = 0; i < info->dlpi_phnum; ++i) {
        const auto &ph = info->dlpi_phdr[i];
        if (ph.p_type == PT_GNU_RELRO) {
            uintptr_t rs = base + ph.p_vaddr;
            uintptr_t re = rs + ph.p_memsz;
            if (slot_addr >= rs && slot_addr < re) {
                return PROT_READ;
            }
        }
    }
    for (ElfW(Half) i = 0; i < info->dlpi_phnum; ++i) {
        const auto &ph = info->dlpi_phdr[i];
        if (ph.p_type == PT_LOAD) {
            uintptr_t ls = base + ph.p_vaddr;
            uintptr_t le = ls + ph.p_memsz;
            if (slot_addr >= ls && slot_addr < le) {
                int p = 0;
                if (ph.p_flags & PF_R) p |= PROT_READ;
                if (ph.p_flags & PF_W) p |= PROT_WRITE;
                if (ph.p_flags & PF_X) p |= PROT_EXEC;
                return p ? p : (PROT_READ | PROT_WRITE);
            }
        }
    }
    return PROT_READ | PROT_WRITE;
}

inline bool write_got_slot_fast(
    const struct dl_phdr_info *info,
    void **slot,
    void *replacement,
    void **orig_out,
    uintptr_t page_sz) noexcept {
    if (!slot || !replacement) return false;

    void *cur = *slot;
    if (cur == replacement) return false;

    if (orig_out && *orig_out == nullptr && cur != nullptr) {
        *orig_out = cur;
    }

    uintptr_t slot_addr = reinterpret_cast<uintptr_t>(slot);
    uintptr_t page_start = slot_addr & ~(page_sz - 1);
    int orig_prot = infer_slot_prot_from_phdr(info, slot_addr);
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

inline bool should_direct_got_patch_phdr(const char *name, bool gl_enabled) noexcept {
    if (!name || name[0] == '\0') return false;
    if (std::strstr(name, "mirage_forge") != nullptr ||
        std::strncmp(name, "/data/adb/", 10) == 0 ||
        std::strstr(name, "libpairipcore") != nullptr ||
        std::strstr(name, "webviewchromium") != nullptr ||
        std::strstr(name, "GLES_mali") != nullptr ||
        std::strstr(name, "libllvm") != nullptr ||
        std::strstr(name, "libadreno") != nullptr) {
        return false;
    }

    const char *slash = std::strrchr(name, '/');
    const char *fname = slash ? (slash + 1) : name;

    // 跳过所有系统基础与巨型框架库，保持零拖慢
    static const char *const kSkipPrefixes[] = {
        "libc.", "libdl.", "libm.", "libz.", "liblog.",
        "libart", "libdex", "linker", "libopenjdk", "libicu",
        "libandroid", "libjavacore", "libbase", "libc++", "libutils",
        "libbinder", "libframework", "libcrypto", "libssl", "libsqlite",
        "libskia", "libgui", "libui.", "libinput", "libmedia",
        "libstagefright", "libaudio", "libcamera", "libhidl", "libfmq",
        "libnativehelper", "libnativeloader", "mapper.", "libOpenCL",
    };
    for (const char *pfx : kSkipPrefixes) {
        if (std::strncmp(fname, pfx, std::strlen(pfx)) == 0) {
            return false;
        }
    }

    // 1. 仅在开启「伪装 GPU」时才触碰图形/Vulkan 系统与 HAL 桥接库（严格遵守 0 改动）
    if (gl_enabled) {
        if (std::strcmp(fname, "libvulkan.so") == 0 ||
            std::strcmp(fname, "libhwui.so") == 0 ||
            std::strcmp(fname, "libvndksupport.so") == 0 ||
            std::strcmp(fname, "libhardware.so") == 0 ||
            std::strcmp(fname, "libEGL.so") == 0 ||
            std::strcmp(fname, "libGLESv1_CM.so") == 0 ||
            std::strcmp(fname, "libGLESv2.so") == 0 ||
            std::strcmp(fname, "libGLESv3.so") == 0 ||
            std::strncmp(fname, "vulkan.", 7) == 0) {
            return true;
        }
    }

    // 2. 应用自身库及未解压 Split APK 内嵌库（含 Bionic 返回的无路径裸 SONAME）
    if (std::strstr(name, ".apk") != nullptr ||
        std::strncmp(name, "/data/", 6) == 0 ||
        std::strncmp(name, "/mnt/", 5) == 0) {
        return true;
    }
    if (slash == nullptr && std::strstr(fname, ".so") != nullptr) {
        return true;
    }
    return false;
}

inline std::mutex g_direct_got_mtx;
inline std::unordered_set<uintptr_t> g_direct_got_bases;

struct DirectGotIterateCtx {
    const GotPatchSpec *specs;
    size_t spec_count;
    bool gl_enabled;
    uintptr_t page_sz;
    uint64_t char_mask[2]; // 128-bit ASCII 首字符快筛位图
    uint32_t patched_slots;
};

inline bool mask_has_char(const uint64_t mask[2], unsigned char c) noexcept {
    if (c >= 128) return false;
    return ((mask[c >> 6] >> (c & 63)) & 1ULL) != 0;
}

inline int direct_got_phdr_cb(struct dl_phdr_info *info, size_t, void *data) {
    auto *ctx = static_cast<DirectGotIterateCtx *>(data);
    if (!info || !ctx || info->dlpi_addr == 0 || !info->dlpi_phdr) return 0;

    const char *name = info->dlpi_name ? info->dlpi_name : "";
    if (!should_direct_got_patch_phdr(name, ctx->gl_enabled)) return 0;

    uintptr_t base = static_cast<uintptr_t>(info->dlpi_addr);
    if (!g_direct_got_bases.insert(base).second) {
        return 0;
    }

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

    auto try_patch_entry = [&](uint32_t sym_idx, uint32_t r_type, uintptr_t r_offset) {
        if (sym_idx == 0 || !is_got_reloc_type(r_type)) return;
        const char *sym_name = strtab + symtab[sym_idx].st_name;
        unsigned char c0 = static_cast<unsigned char>(sym_name[0]);
        if (__builtin_expect(!mask_has_char(ctx->char_mask, c0), 1)) return;

        for (size_t k = 0; k < ctx->spec_count; ++k) {
            if (std::strcmp(sym_name, ctx->specs[k].symbol) == 0) {
                auto **slot = reinterpret_cast<void **>(base + r_offset);
                if (write_got_slot_fast(info, slot, ctx->specs[k].replacement, ctx->specs[k].orig_out, ctx->page_sz)) {
                    ++ctx->patched_slots;
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

    return 0;
}

inline uint32_t apply_direct_got_patches_all(
    const GotPatchSpec *specs,
    size_t spec_count,
    bool gl_enabled) {
    if (!specs || spec_count == 0) return 0;
    ErrnoRestorer errno_guard;
    if (!g_direct_got_mtx.try_lock()) return 0;

    long psz = sysconf(_SC_PAGESIZE);
    if (psz <= 0) psz = 4096;

    DirectGotIterateCtx ctx{specs, spec_count, gl_enabled, static_cast<uintptr_t>(psz), {0, 0}, 0};
    for (size_t i = 0; i < spec_count; ++i) {
        unsigned char c0 = static_cast<unsigned char>(specs[i].symbol[0]);
        if (c0 < 128) {
            ctx.char_mask[c0 >> 6] |= (1ULL << (c0 & 63));
        }
    }

    dl_iterate_phdr(direct_got_phdr_cb, &ctx);

    g_direct_got_mtx.unlock();
    return ctx.patched_slots;
}

} // namespace mirage

#pragma once

#include <cstring>
#include <dlfcn.h>
#include <sys/mman.h>
#include <sys/prctl.h>
#include <sys/system_properties.h>
#include "mirage_config.hpp"
#include "mirage_logger.hpp"

#ifndef PR_SET_VMA
#define PR_SET_VMA 0x53564d41
#define PR_SET_VMA_ANON_NAME 0
#endif

namespace mirage {

// 供第三部分 jvm_art_engine.hpp 挂载的主线程 DPI 按需刷新钩子（0 后台线程）
inline void (*g_prop_dpi_lazy_sync_fn)() = nullptr;

struct alignas(4) BionicPropInfoCompat {
    uint32_t serial;              // 偏移 0..3
    char value[PROP_VALUE_MAX];   // 偏移 4..95 (PROP_VALUE_MAX == 92)
    char name[1];                 // 偏移 96..N
};

class MmapPropArena {
public:
    static constexpr size_t kArenaSize = 65536;

    void ensure_mapped() {
        if (arena_base_) return;
        void *mem = mmap(nullptr, kArenaSize, PROT_READ | PROT_WRITE,
                         MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
        if (mem != MAP_FAILED) {
            arena_base_ = static_cast<uint8_t *>(mem);
            arena_offset_ = 0;
            prctl(PR_SET_VMA, PR_SET_VMA_ANON_NAME, arena_base_, kArenaSize, "SystemProperties");
        }
    }

    void insert_before_freeze(const std::string &name, const std::string &val) {
        ensure_mapped();
        if (!arena_base_ || by_name_.find(name) != by_name_.end()) return;

        size_t alloc_sz = (offsetof(BionicPropInfoCompat, name) + name.size() + 1 + 7) & ~static_cast<size_t>(7);
        if (arena_offset_ + alloc_sz > kArenaSize) return;

        auto *pi = reinterpret_cast<BionicPropInfoCompat *>(arena_base_ + arena_offset_);
        arena_offset_ += alloc_sz;

        __atomic_store_n(&pi->serial, 0x02000000u, __ATOMIC_RELAXED);
        std::strncpy(pi->value, val.c_str(), PROP_VALUE_MAX - 1);
        pi->value[PROP_VALUE_MAX - 1] = '\0';
        std::memcpy(pi->name, name.data(), name.size());
        pi->name[name.size()] = '\0';

        by_name_.emplace(name, pi);
        by_ptr_.emplace(pi, val);
    }

    inline const prop_info *find_lockfree(const char *name, const std::string **out_val = nullptr) const noexcept {
        if (!name) return nullptr;
        auto it = by_name_.find(name);
        if (it == by_name_.end()) return nullptr;
        if (out_val) {
            auto v_it = by_ptr_.find(it->second);
            if (v_it != by_ptr_.end()) *out_val = &v_it->second;
        }
        return reinterpret_cast<const prop_info *>(it->second);
    }

    inline bool is_synthetic_lockfree(const prop_info *pi, const char **out_name, const std::string **out_val) const noexcept {
        if (!pi || !arena_base_) return false;
        const auto *ptr = reinterpret_cast<const uint8_t *>(pi);
        if (__builtin_expect(ptr < arena_base_ || ptr >= arena_base_ + arena_offset_, 1)) {
            return false;
        }
        auto *compat = reinterpret_cast<BionicPropInfoCompat *>(const_cast<prop_info *>(pi));
        if (out_name) *out_name = compat->name;
        if (out_val) {
            auto it = by_ptr_.find(compat);
            if (it != by_ptr_.end()) *out_val = &it->second;
        }
        return true;
    }

private:
    uint8_t *arena_base_ = nullptr;
    size_t arena_offset_ = 0;
    std::unordered_map<std::string, BionicPropInfoCompat *> by_name_;
    std::unordered_map<BionicPropInfoCompat *, std::string> by_ptr_;
};

inline MmapPropArena g_prop_arena;

using T_PropCallback = void (*)(void *cookie, const char *name, const char *value, uint32_t serial);
using T_ForeachCallback = void (*)(const prop_info *pi, void *cookie);

inline const prop_info *(*orig_system_property_find)(const char *name) = nullptr;
inline void (*orig_system_property_read_callback)(const prop_info *pi, T_PropCallback callback, void *cookie) = nullptr;
inline int (*orig_system_property_read)(const prop_info *pi, char *name, char *value) = nullptr;
inline int (*orig_system_property_get)(const char *name, char *value) = nullptr;
inline int (*orig_system_property_foreach)(T_ForeachCallback propfn, void *cookie) = nullptr;
inline const prop_info *(*orig_system_property_find_nth)(unsigned n) = nullptr;

inline void init_bionic_prop_symbols() {
    if (orig_system_property_find) return;
    orig_system_property_find = reinterpret_cast<decltype(orig_system_property_find)>(dlsym(RTLD_DEFAULT, "__system_property_find"));
    orig_system_property_read_callback = reinterpret_cast<decltype(orig_system_property_read_callback)>(dlsym(RTLD_DEFAULT, "__system_property_read_callback"));
    orig_system_property_read = reinterpret_cast<decltype(orig_system_property_read)>(dlsym(RTLD_DEFAULT, "__system_property_read"));
    orig_system_property_get = reinterpret_cast<decltype(orig_system_property_get)>(dlsym(RTLD_DEFAULT, "__system_property_get"));
    orig_system_property_foreach = reinterpret_cast<decltype(orig_system_property_foreach)>(dlsym(RTLD_DEFAULT, "__system_property_foreach"));
    orig_system_property_find_nth = reinterpret_cast<decltype(orig_system_property_find_nth)>(dlsym(RTLD_DEFAULT, "__system_property_find_nth"));
}

// 检查宿主机真实环境是否存在指定属性
inline bool real_host_prop_exists(const char *name) noexcept {
    return orig_system_property_find && name && (orig_system_property_find(name) != nullptr);
}

// 自适应云机属性伪装：默认不开启！仅当探测到宿主机确实存在云机底层属性时才激活覆盖
inline void inject_adaptive_cloud_props_if_present() {
    bool is_cloud_host = real_host_prop_exists("ro.sys.cloud.magisk") ||
                         real_host_prop_exists("persist.sys.cloud.root.global") ||
                         real_host_prop_exists("ro.sys.cloud.root.global") ||
                         real_host_prop_exists("persist.sys.cloud.gpu.vk_devicename") ||
                         real_host_prop_exists("persist.sys.cloud.gpu.gl_renderer") ||
                         real_host_prop_exists("sys.gmali.version");

    if (!is_cloud_host) return;

    auto set_cloud_kv = [&](const char *k, const std::string &v) {
        if (!v.empty() && g_cfg.build.prop_map.find(k) == g_cfg.build.prop_map.end()) {
            g_cfg.build.prop_map[k] = v;
        }
    };

    // 1. 仅对宿主机已存在的云机 Root 暴露属性做隐藏归零
    if (real_host_prop_exists("ro.sys.cloud.magisk"))          set_cloud_kv("ro.sys.cloud.magisk", "0");
    if (real_host_prop_exists("persist.sys.cloud.root.global")) set_cloud_kv("persist.sys.cloud.root.global", "0");
    if (real_host_prop_exists("ro.sys.cloud.root.global"))     set_cloud_kv("ro.sys.cloud.root.global", "0");
    if (real_host_prop_exists("persist.sys.device.allowroot")) set_cloud_kv("persist.sys.device.allowroot", "0");
    if (real_host_prop_exists("persist.sys.device_allowance")) set_cloud_kv("persist.sys.device_allowance", "0");

    // 2. 云机底层 Vulkan / GLES 驱动重定向属性 (解决云机 libvulkan.so 内部通过 property_get 读取 GPU 的问题)
    if (g_cfg.gl.enabled) {
        std::string renderer = !g_cfg.gl.gl_renderer.empty() ? g_cfg.gl.gl_renderer : "Adreno (TM) 830";
        std::string vendor   = !g_cfg.gl.gl_vendor.empty() ? g_cfg.gl.gl_vendor : "Qualcomm";
        std::string version  = !g_cfg.gl.gl_version.empty() ? g_cfg.gl.gl_version : "OpenGL ES 3.2 V@0800.0";

        bool is_arm = (vendor.find("ARM") != std::string::npos ||
                       renderer.find("Mali") != std::string::npos ||
                       renderer.find("Immortalis") != std::string::npos);

        set_cloud_kv("persist.sys.cloud.gpu.vk_devicename", renderer);
        set_cloud_kv("persist.sys.cloud.gpu.vk_vendorid", is_arm ? "5045" : "20803");     // 0x13B5 / 0x5143
        set_cloud_kv("persist.sys.cloud.gpu.vk_deviceid", is_arm ? "2449604624" : "1141178368");
        set_cloud_kv("persist.sys.cloud.gpu.vk_driverversion", "33554432");
        set_cloud_kv("persist.sys.cloud.gpu.gl_renderer", renderer);
        set_cloud_kv("persist.sys.cloud.gpu.gl_vendor", vendor);
        set_cloud_kv("persist.sys.cloud.gpu.gl_version", version);
        set_cloud_kv("persist.sys.gpu.gl_renderer", renderer);
        set_cloud_kv("persist.sys.gpu.gl_vendor", vendor);
    }
}

inline void freeze_bionic_prop_arena() {
    if (!g_cfg.build.enabled) return;

    if (g_cfg.build.app_dpi >= 120 && g_cfg.build.app_dpi <= 960) {
        std::string dpi_s = std::to_string(g_cfg.build.app_dpi);
        g_cfg.build.prop_map["ro.sf.lcd_density"] = dpi_s;
        g_cfg.build.prop_map["qemu.sf.lcd_density"] = dpi_s;
        g_cfg.build.prop_map["persist.sys.dpi"] = dpi_s;
        g_cfg.build.prop_map["ro.vendor.display.default_dpi"] = dpi_s;
        g_cfg.build.prop_map["ro.config.density_dpi"] = dpi_s;
    }

    auto set_default_restricted = [&](const char *k, const char *v) {
        if (g_cfg.build.prop_map.find(k) == g_cfg.build.prop_map.end()) {
            g_cfg.build.prop_map[k] = v;
        }
    };
    set_default_restricted("ro.boot.vbmeta.device_state", "locked");
    set_default_restricted("ro.boot.vbmeta.avb_version", "1.2");
    set_default_restricted("ro.boot.vbmeta.hash_alg", "sha256");
    set_default_restricted("ro.boot.verifiedbootstate", "green");
    set_default_restricted("ro.boot.veritymode", "enforcing");
    set_default_restricted("ro.boot.flash.locked", "1");
    set_default_restricted("ro.boot.warranty_bit", "0");
    set_default_restricted("ro.warranty_bit", "0");
    set_default_restricted("ro.build.selinux", "1");
    set_default_restricted("ro.debuggable", "0");
    set_default_restricted("ro.secure", "1");
    set_default_restricted("ro.adb.secure", "1");
    set_default_restricted("sys.oem_unlock_allowed", "0");
    set_default_restricted("sys.usb.state", "none");
    set_default_restricted("sys.usb.config", "none");
    set_default_restricted("init.svc.adbd", "stopped");

    // 探测宿主机环境：仅当宿主机存在云机属性时才按需注入云机伪装项
    inject_adaptive_cloud_props_if_present();

    for (const auto &kv : g_cfg.build.prop_map) {
        g_prop_arena.insert_before_freeze(kv.first, kv.second);
    }
}

inline bool is_interesting_unmapped_prop(const char *name) noexcept {
    if (!name) return false;
    if (std::strncmp(name, "persist.device_config.", 22) == 0 ||
        std::strncmp(name, "dalvik.vm.", 10) == 0 ||
        std::strncmp(name, "ro.boottime.", 12) == 0 ||
        std::strncmp(name, "debug.", 6) == 0) {
        return false;
    }
    return std::strncmp(name, "ro.", 3) == 0 ||
           std::strncmp(name, "persist.", 8) == 0 ||
           std::strncmp(name, "sys.", 4) == 0 ||
           std::strncmp(name, "gsm.", 4) == 0 ||
           std::strncmp(name, "ril.", 4) == 0;
}

inline const prop_info *hook_system_property_find(const char *name) {
    if (__builtin_expect(g_prop_dpi_lazy_sync_fn != nullptr && !g_tls_in_hook, 0)) {
        g_prop_dpi_lazy_sync_fn();
    }
    if (name) {
        if (g_cfg.build.enabled) {
            const std::string *syn_val = nullptr;
            if (const prop_info *syn = g_prop_arena.find_lockfree(name, &syn_val)) {
                if (__builtin_expect(g_cfg.probe.enabled, 0)) {
                    ScopedHookGuard guard;
                    if (guard && syn_val) emit_probe("PROP_FIND", name, *syn_val);
                }
                return syn;
            }
        }
        if (__builtin_expect(g_cfg.probe.enabled, 0)) {
            ScopedHookGuard guard;
            if (guard && is_interesting_unmapped_prop(name)) {
                emit_probe("PROP_UNMAPPED", name, "<find>");
            }
        }
    }
    return orig_system_property_find ? orig_system_property_find(name) : nullptr;
}

struct ReadCbCtx {
    void *user_cookie;
    T_PropCallback user_cb;
};

inline void internal_read_cb_bridge(void *cookie, const char *name, const char *value, uint32_t serial) {
    auto *ctx = static_cast<ReadCbCtx *>(cookie);
    if (!ctx || !ctx->user_cb) return;

    if (name) {
        if (g_cfg.build.enabled) {
            auto it = g_cfg.build.prop_map.find(name);
            if (it != g_cfg.build.prop_map.end()) {
                if (__builtin_expect(g_cfg.probe.enabled, 0)) {
                    ScopedHookGuard guard;
                    if (guard) emit_probe("PROP_CB", name, it->second);
                }
                ctx->user_cb(ctx->user_cookie, name, it->second.c_str(), serial & ~1u);
                return;
            }
        }
        if (__builtin_expect(g_cfg.probe.enabled, 0)) {
            ScopedHookGuard guard;
            if (guard && is_interesting_unmapped_prop(name)) {
                emit_probe("PROP_UNMAPPED", name, value ? value : "");
            }
        }
    }
    ctx->user_cb(ctx->user_cookie, name, value, serial);
}

inline void hook_system_property_read_callback(const prop_info *pi, T_PropCallback callback, void *cookie) {
    if (!pi || !callback) return;

    if (g_cfg.build.enabled) {
        const char *syn_name = nullptr;
        const std::string *syn_val = nullptr;
        if (g_prop_arena.is_synthetic_lockfree(pi, &syn_name, &syn_val) && syn_name && syn_val) {
            if (__builtin_expect(g_cfg.probe.enabled, 0)) {
                ScopedHookGuard guard;
                if (guard) emit_probe("PROP_CB", syn_name, *syn_val);
            }
            callback(cookie, syn_name, syn_val->c_str(), 0x02000000u);
            return;
        }
    }

    if (orig_system_property_read_callback) {
        ReadCbCtx bridge{cookie, callback};
        orig_system_property_read_callback(pi, internal_read_cb_bridge, &bridge);
    }
}

inline int hook_system_property_read(const prop_info *pi, char *name, char *value) {
    if (!pi) return 0;

    if (g_cfg.build.enabled) {
        const char *syn_name = nullptr;
        const std::string *syn_val = nullptr;
        if (g_prop_arena.is_synthetic_lockfree(pi, &syn_name, &syn_val) && syn_name && syn_val) {
            if (__builtin_expect(g_cfg.probe.enabled, 0)) {
                ScopedHookGuard guard;
                if (guard) emit_probe("PROP_READ", syn_name, *syn_val);
            }
            if (name) {
                std::strncpy(name, syn_name, PROP_NAME_MAX - 1);
                name[PROP_NAME_MAX - 1] = '\0';
            }
            if (value) {
                std::strncpy(value, syn_val->c_str(), PROP_VALUE_MAX - 1);
                value[PROP_VALUE_MAX - 1] = '\0';
                return static_cast<int>(std::strlen(value));
            }
            return 0;
        }
    }

    if (!orig_system_property_read) return 0;
    int ret = orig_system_property_read(pi, name, value);
    if (name && value && g_cfg.build.enabled) {
        auto it = g_cfg.build.prop_map.find(name);
        if (it != g_cfg.build.prop_map.end()) {
            if (__builtin_expect(g_cfg.probe.enabled, 0)) {
                ScopedHookGuard guard;
                if (guard) emit_probe("PROP_READ", name, it->second);
            }
            std::strncpy(value, it->second.c_str(), PROP_VALUE_MAX - 1);
            value[PROP_VALUE_MAX - 1] = '\0';
            return static_cast<int>(std::strlen(value));
        }
    }
    return ret;
}

inline int hook_system_property_get(const char *name, char *value) {
    if (__builtin_expect(g_prop_dpi_lazy_sync_fn != nullptr && !g_tls_in_hook, 0)) {
        g_prop_dpi_lazy_sync_fn();
    }
    if (name && value) {
        if (g_cfg.build.enabled) {
            auto it = g_cfg.build.prop_map.find(name);
            if (it != g_cfg.build.prop_map.end()) {
                if (__builtin_expect(g_cfg.probe.enabled, 0)) {
                    ScopedHookGuard guard;
                    if (guard) emit_probe("PROP_GET", name, it->second);
                }
                std::strncpy(value, it->second.c_str(), PROP_VALUE_MAX - 1);
                value[PROP_VALUE_MAX - 1] = '\0';
                return static_cast<int>(std::strlen(value));
            }
        }
        int real_len = orig_system_property_get ? orig_system_property_get(name, value) : 0;
        if (__builtin_expect(g_cfg.probe.enabled, 0)) {
            ScopedHookGuard guard;
            if (guard && is_interesting_unmapped_prop(name)) {
                emit_probe("PROP_UNMAPPED", name, real_len > 0 ? value : "<empty>");
            }
        }
        return real_len;
    }
    return orig_system_property_get ? orig_system_property_get(name, value) : 0;
}

struct ForeachBridgeCtx {
    T_ForeachCallback user_fn;
    void *user_cookie;
    std::unordered_set<std::string> seen_keys;
};

inline void internal_foreach_cb(const prop_info *pi, void *cookie) {
    auto *ctx = static_cast<ForeachBridgeCtx *>(cookie);
    if (!ctx || !ctx->user_fn || !pi) return;

    if (g_cfg.build.enabled && orig_system_property_read_callback) {
        std::string key;
        auto key_extractor = [](void *c, const char *name, const char *, uint32_t) {
            if (name) *static_cast<std::string *>(c) = name;
        };
        orig_system_property_read_callback(pi, key_extractor, &key);
        if (!key.empty()) {
            ctx->seen_keys.insert(key);
            if (const prop_info *syn = g_prop_arena.find_lockfree(key.c_str())) {
                ctx->user_fn(syn, ctx->user_cookie);
                return;
            }
        }
    }
    ctx->user_fn(pi, ctx->user_cookie);
}

inline int hook_system_property_foreach(T_ForeachCallback propfn, void *cookie) {
    if (!propfn) return -1;
    if (!g_cfg.build.enabled || !orig_system_property_foreach) {
        return orig_system_property_foreach ? orig_system_property_foreach(propfn, cookie) : -1;
    }

    ForeachBridgeCtx bridge{propfn, cookie, {}};
    int res = orig_system_property_foreach(internal_foreach_cb, &bridge);

    for (const auto &kv : g_cfg.build.prop_map) {
        if (bridge.seen_keys.find(kv.first) == bridge.seen_keys.end()) {
            if (const prop_info *syn = g_prop_arena.find_lockfree(kv.first.c_str())) {
                propfn(syn, cookie);
            }
        }
    }
    if (__builtin_expect(g_cfg.probe.enabled, 0)) {
        ScopedHookGuard guard;
        if (guard) emit_probe("PROP_FOREACH", "all_properties", std::to_string(g_cfg.build.prop_map.size()));
    }
    return res;
}

inline const prop_info *hook_system_property_find_nth(unsigned n) {
    const prop_info *real_pi = orig_system_property_find_nth ? orig_system_property_find_nth(n) : nullptr;
    if (real_pi && g_cfg.build.enabled && orig_system_property_read_callback) {
        std::string key;
        auto key_extractor = [](void *c, const char *name, const char *, uint32_t) {
            if (name) *static_cast<std::string *>(c) = name;
        };
        orig_system_property_read_callback(real_pi, key_extractor, &key);
        if (!key.empty()) {
            if (const prop_info *syn = g_prop_arena.find_lockfree(key.c_str())) {
                return syn;
            }
        }
    }
    return real_pi;
}

} // namespace mirage

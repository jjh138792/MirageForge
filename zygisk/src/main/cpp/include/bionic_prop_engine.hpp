#pragma once

#include <cstring>
#include <sys/system_properties.h>
#include "mirage_shared.hpp"

namespace mirage {

using T_PropCallback = void (*)(void *cookie, const char *name, const char *value, uint32_t serial);
using T_ForeachCallback = void (*)(const prop_info *pi, void *cookie);

inline const prop_info *(*orig_system_property_find)(const char *name) = nullptr;
inline void (*orig_system_property_read_callback)(const prop_info *pi, T_PropCallback callback, void *cookie) = nullptr;
inline int (*orig_system_property_read)(const prop_info *pi, char *name, char *value) = nullptr;
inline int (*orig_system_property_get)(const char *name, char *value) = nullptr;
inline int (*orig_system_property_foreach)(T_ForeachCallback propfn, void *cookie) = nullptr;
inline const prop_info *(*orig_system_property_find_nth)(unsigned n) = nullptr;

inline bool is_interesting_unmapped_prop(const char *name) {
    if (!name) return false;
    return std::strncmp(name, "ro.", 3) == 0 ||
           std::strncmp(name, "persist.", 8) == 0 ||
           std::strncmp(name, "sys.", 4) == 0 ||
           std::strncmp(name, "gsm.", 4) == 0 ||
           std::strncmp(name, "ril.", 4) == 0 ||
           std::strncmp(name, "dalvik.vm.", 10) == 0;
}

inline void prewarm_bionic_prop_arena() {
    int ram = g_ctx.ram_gb > 0 ? g_ctx.ram_gb : 16;
    std::string ram_str = std::to_string(ram) + "GB";
    std::string ram_mb_str = std::to_string(ram * 1024);
    std::string ddr_full = ram_str + "," + g_ctx.ddr_vendor + "," + g_ctx.ddr_type;

    g_ctx.prop_map["ro.boot.hardware.ddr"] = ddr_full;
    g_ctx.prop_map["ro.boot.ddr_size"]     = ram_str;
    g_ctx.prop_map["ro.boot.ddr_type"]     = g_ctx.ddr_type;
    g_ctx.prop_map["ro.boot.ddr_info"]     = ddr_full;
    g_ctx.prop_map["ro.boot.ramsize"]      = ram_str;
    g_ctx.prop_map["ro.boot.memsize"]      = ram_str;
    g_ctx.prop_map["ro.hardware.ddr"]      = ddr_full;
    g_ctx.prop_map["ro.vendor.hw.ram"]     = ram_str;
    g_ctx.prop_map["ro.product.ram"]       = ram_str;
    g_ctx.prop_map["ro.config.ram_size"]   = ram_mb_str;
    g_ctx.prop_map["ro.sys.ram_size"]      = ram_str;
    g_ctx.prop_map["persist.sys.ram_size"] = ram_str;

    for (const auto &kv : g_ctx.prop_map) {
        g_prop_arena.obtain(kv.first, kv.second);
    }
}

inline const prop_info *hook_system_property_find(const char *name) {
    ScopedHookGuard guard;
    if (guard && name && (g_ctx.enable_build_props || g_ctx.enable_probe)) {
        if (g_ctx.enable_build_props) {
            auto it = g_ctx.prop_map.find(name);
            if (it != g_ctx.prop_map.end()) {
                emit_probe("PROP_FIND", name, it->second);
                return g_prop_arena.obtain(it->first, it->second);
            }
        }
        if (g_ctx.enable_probe && is_interesting_unmapped_prop(name)) {
            emit_probe("PROP_UNMAPPED", name, "<find_passthrough>");
        }
    }
    return orig_system_property_find ? orig_system_property_find(name) : nullptr;
}

struct ReadCbCtx {
    void *user_cookie;
    T_PropCallback user_cb;
    bool allow_probe;
};

inline void internal_read_cb_bridge(void *cookie, const char *name, const char *value, uint32_t serial) {
    auto *ctx = static_cast<ReadCbCtx *>(cookie);
    if (!ctx || !ctx->user_cb) return;

    if (name) {
        if (g_ctx.enable_build_props) {
            auto it = g_ctx.prop_map.find(name);
            if (it != g_ctx.prop_map.end()) {
                if (ctx->allow_probe) {
                    emit_probe("PROP_CB", name, it->second);
                }
                ctx->user_cb(ctx->user_cookie, name, it->second.c_str(), serial & ~1u);
                return;
            }
        }
        if (ctx->allow_probe && g_ctx.enable_probe && is_interesting_unmapped_prop(name)) {
            emit_probe("PROP_UNMAPPED", name, value ? value : "");
        }
    }
    ctx->user_cb(ctx->user_cookie, name, value, serial);
}

// 95% 以上的 Android 8~15 属性读取都会走此函数 (包括 Zygote 预缓存的 prop_info 句柄)
inline void hook_system_property_read_callback(const prop_info *pi, T_PropCallback callback, void *cookie) {
    if (!pi || !callback) return;
    ScopedHookGuard guard;

    std::string syn_name, syn_val;
    if (g_prop_arena.is_synthetic(pi, &syn_name, &syn_val)) {
        if (guard) {
            emit_probe("PROP_CB", syn_name, syn_val);
        }
        callback(cookie, syn_name.c_str(), syn_val.c_str(), 0x02000000u);
        return;
    }

    if (orig_system_property_read_callback) {
        ReadCbCtx bridge{cookie, callback, static_cast<bool>(guard)};
        orig_system_property_read_callback(pi, internal_read_cb_bridge, &bridge);
    }
}

inline int hook_system_property_read(const prop_info *pi, char *name, char *value) {
    if (!pi) return 0;
    ScopedHookGuard guard;

    std::string syn_name, syn_val;
    if (g_prop_arena.is_synthetic(pi, &syn_name, &syn_val)) {
        if (guard) {
            emit_probe("PROP_READ", syn_name, syn_val);
        }
        if (name) {
            std::strncpy(name, syn_name.c_str(), PROP_NAME_MAX - 1);
            name[PROP_NAME_MAX - 1] = '\0';
        }
        if (value) {
            std::strncpy(value, syn_val.c_str(), PROP_VALUE_MAX - 1);
            value[PROP_VALUE_MAX - 1] = '\0';
            return static_cast<int>(std::strlen(value));
        }
        return 0;
    }

    if (!orig_system_property_read) return 0;
    int ret = orig_system_property_read(pi, name, value);
    if (name && value) {
        if (g_ctx.enable_build_props) {
            auto it = g_ctx.prop_map.find(name);
            if (it != g_ctx.prop_map.end()) {
                if (guard) emit_probe("PROP_READ", name, it->second);
                std::strncpy(value, it->second.c_str(), PROP_VALUE_MAX - 1);
                value[PROP_VALUE_MAX - 1] = '\0';
                return static_cast<int>(std::strlen(value));
            }
        }
        if (guard && g_ctx.enable_probe && is_interesting_unmapped_prop(name)) {
            emit_probe("PROP_UNMAPPED", name, value);
        }
    }
    return ret;
}

inline int hook_system_property_get(const char *name, char *value) {
    ScopedHookGuard guard;
    if (guard && name && value) {
        if (g_ctx.enable_build_props) {
            auto it = g_ctx.prop_map.find(name);
            if (it != g_ctx.prop_map.end()) {
                emit_probe("PROP_GET", name, it->second);
                std::strncpy(value, it->second.c_str(), PROP_VALUE_MAX - 1);
                value[PROP_VALUE_MAX - 1] = '\0';
                return static_cast<int>(std::strlen(value));
            }
        }
        int real_len = orig_system_property_get ? orig_system_property_get(name, value) : 0;
        if (g_ctx.enable_probe && is_interesting_unmapped_prop(name)) {
            emit_probe("PROP_UNMAPPED", name, real_len > 0 ? value : "<empty>");
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

    if (orig_system_property_read_callback) {
        std::string key;
        auto key_extractor = [](void *c, const char *name, const char *, uint32_t) {
            if (name) *static_cast<std::string *>(c) = name;
        };
        orig_system_property_read_callback(pi, key_extractor, &key);
        if (!key.empty()) {
            ctx->seen_keys.insert(key);
            auto it = g_ctx.prop_map.find(key);
            if (it != g_ctx.prop_map.end()) {
                const prop_info *syn = g_prop_arena.obtain(it->first, it->second);
                ctx->user_fn(syn, ctx->user_cookie);
                return;
            }
        }
    }
    ctx->user_fn(pi, ctx->user_cookie);
}

inline int hook_system_property_foreach(T_ForeachCallback propfn, void *cookie) {
    if (!propfn) return -1;
    if (!g_ctx.enable_build_props || !orig_system_property_foreach) {
        return orig_system_property_foreach ? orig_system_property_foreach(propfn, cookie) : -1;
    }

    ScopedHookGuard guard;
    ForeachBridgeCtx bridge{propfn, cookie, {}};
    int res = orig_system_property_foreach(internal_foreach_cb, &bridge);

    for (const auto &kv : g_ctx.prop_map) {
        if (bridge.seen_keys.find(kv.first) == bridge.seen_keys.end()) {
            const prop_info *syn = g_prop_arena.obtain(kv.first, kv.second);
            propfn(syn, cookie);
        }
    }
    if (guard) {
        emit_probe("PROP_FOREACH", "all_properties", std::to_string(g_ctx.prop_map.size()));
    }
    return res;
}

inline const prop_info *hook_system_property_find_nth(unsigned n) {
    const prop_info *real_pi = orig_system_property_find_nth ? orig_system_property_find_nth(n) : nullptr;
    if (real_pi && g_ctx.enable_build_props && orig_system_property_read_callback) {
        std::string key;
        auto key_extractor = [](void *c, const char *name, const char *, uint32_t) {
            if (name) *static_cast<std::string *>(c) = name;
        };
        orig_system_property_read_callback(real_pi, key_extractor, &key);
        if (!key.empty()) {
            auto it = g_ctx.prop_map.find(key);
            if (it != g_ctx.prop_map.end()) {
                return g_prop_arena.obtain(it->first, it->second);
            }
        }
    }
    return real_pi;
}

} // namespace mirage

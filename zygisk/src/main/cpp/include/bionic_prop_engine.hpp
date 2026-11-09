#pragma once

#include <cstring>
#include <dlfcn.h>
#include <sys/system_properties.h>
#include "mirage_config.hpp"
#include "mirage_logger.hpp"

namespace mirage {

// AOSP Bionic `prop_info` 1:1 真实二进制内存布局结构
struct alignas(4) BionicPropInfoCompat {
    uint32_t serial;              // 偏移 0..3
    char value[PROP_VALUE_MAX];   // 偏移 4..95 (PROP_VALUE_MAX == 92)
    char name[1];                 // 偏移 96..N
};

// 冻结态无锁属性句柄池：在 preAppSpecialize 单线程初始化后，运行期 100% 只读无锁查询
class LockFreePropArena {
public:
    void insert_before_freeze(const std::string &name, const std::string &val) {
        if (by_name_.find(name) != by_name_.end()) return;

        size_t alloc_sz = offsetof(BionicPropInfoCompat, name) + name.size() + 1;
        void *raw = std::calloc(1, alloc_sz);
        if (!raw) return;

        auto *pi = static_cast<BionicPropInfoCompat *>(raw);
        __atomic_store_n(&pi->serial, 0x02000000u, __ATOMIC_RELAXED);
        std::strncpy(pi->value, val.c_str(), PROP_VALUE_MAX - 1);
        pi->value[PROP_VALUE_MAX - 1] = '\0';
        std::memcpy(pi->name, name.data(), name.size());
        pi->name[name.size()] = '\0';

        uintptr_t addr = reinterpret_cast<uintptr_t>(pi);
        if (addr < min_addr_) min_addr_ = addr;
        if (addr > max_addr_) max_addr_ = addr;

        by_name_.emplace(name, pi);
        by_ptr_.emplace(pi, val);
    }

    // 运行期无锁 O(1) 查询属性名
    inline const prop_info *find_lockfree(const char *name) const noexcept {
        if (!name) return nullptr;
        auto it = by_name_.find(name);
        return (it != by_name_.end()) ? reinterpret_cast<const prop_info *>(it->second) : nullptr;
    }

    // 运行期无锁 O(1) 判断指针是否属于虚拟句柄池（先做极速指针边界过滤）
    inline bool is_synthetic_lockfree(const prop_info *pi, const char **out_name, const std::string **out_val) const noexcept {
        if (!pi) return false;
        uintptr_t addr = reinterpret_cast<uintptr_t>(pi);
        if (__builtin_expect(addr < min_addr_ || addr > max_addr_, 1)) {
            return false;
        }
        auto *compat = reinterpret_cast<BionicPropInfoCompat *>(const_cast<prop_info *>(pi));
        auto it = by_ptr_.find(compat);
        if (it == by_ptr_.end()) return false;
        if (out_name) *out_name = compat->name;
        if (out_val) *out_val = &it->second;
        return true;
    }

private:
    uintptr_t min_addr_ = UINTPTR_MAX;
    uintptr_t max_addr_ = 0;
    std::unordered_map<std::string, BionicPropInfoCompat *> by_name_;
    std::unordered_map<BionicPropInfoCompat *, std::string> by_ptr_;
};

inline LockFreePropArena g_prop_arena;

using T_PropCallback = void (*)(void *cookie, const char *name, const char *value, uint32_t serial);
using T_ForeachCallback = void (*)(const prop_info *pi, void *cookie);

inline const prop_info *(*orig_system_property_find)(const char *name) = nullptr;
inline void (*orig_system_property_read_callback)(const prop_info *pi, T_PropCallback callback, void *cookie) = nullptr;
inline int (*orig_system_property_read)(const prop_info *pi, char *name, char *value) = nullptr;
inline int (*orig_system_property_get)(const char *name, char *value) = nullptr;
inline int (*orig_system_property_foreach)(T_ForeachCallback propfn, void *cookie) = nullptr;
inline const prop_info *(*orig_system_property_find_nth)(unsigned n) = nullptr;

// 仅在目标应用启用了 Build 或 Probe 模块时才按需解析符号
inline void init_bionic_prop_symbols() {
    if (orig_system_property_find) return;
    orig_system_property_find = reinterpret_cast<decltype(orig_system_property_find)>(dlsym(RTLD_DEFAULT, "__system_property_find"));
    orig_system_property_read_callback = reinterpret_cast<decltype(orig_system_property_read_callback)>(dlsym(RTLD_DEFAULT, "__system_property_read_callback"));
    orig_system_property_read = reinterpret_cast<decltype(orig_system_property_read)>(dlsym(RTLD_DEFAULT, "__system_property_read"));
    orig_system_property_get = reinterpret_cast<decltype(orig_system_property_get)>(dlsym(RTLD_DEFAULT, "__system_property_get"));
    orig_system_property_foreach = reinterpret_cast<decltype(orig_system_property_foreach)>(dlsym(RTLD_DEFAULT, "__system_property_foreach"));
    orig_system_property_find_nth = reinterpret_cast<decltype(orig_system_property_find_nth)>(dlsym(RTLD_DEFAULT, "__system_property_find_nth"));
}

// 单线程阶段冻结属性池（若未开启 Build 模块，则完全不填充任何虚拟属性）
inline void freeze_bionic_prop_arena() {
    if (!g_cfg.build.enabled) return;
    for (const auto &kv : g_cfg.build.prop_map) {
        g_prop_arena.insert_before_freeze(kv.first, kv.second);
    }
}

inline bool is_interesting_unmapped_prop(const char *name) noexcept {
    if (!name) return false;
    return std::strncmp(name, "ro.", 3) == 0 ||
           std::strncmp(name, "persist.", 8) == 0 ||
           std::strncmp(name, "sys.", 4) == 0 ||
           std::strncmp(name, "gsm.", 4) == 0;
}

inline const prop_info *hook_system_property_find(const char *name) {
    if (name) {
        if (g_cfg.build.enabled) {
            if (const prop_info *syn = g_prop_arena.find_lockfree(name)) {
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
        if (g_prop_arena.is_synthetic_lockfree(pi, &syn_name, &syn_val)) {
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
        if (g_prop_arena.is_synthetic_lockfree(pi, &syn_name, &syn_val)) {
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

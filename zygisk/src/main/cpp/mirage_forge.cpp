#include <cstdlib>
#include <cstdio>
#include <cstring>
#include <cinttypes>
#include <string>
#include <vector>
#include <set>
#include <fstream>
#include <dlfcn.h>
#include <sys/stat.h>
#include <sys/sysmacros.h>

#include "zygisk.hpp"
#include "mirage_protocol.hpp"
#include "vfs_synthesizer.hpp"
#include "gl_spoof.hpp"

using zygisk::Api;
using zygisk::AppSpecializeArgs;
using zygisk::ServerSpecializeArgs;
using namespace mirage;

// ============================================================================
// Bionic System Properties 五维全量拦截 (含 foreach 全表枚举劫持)
// ============================================================================
using T_Callback = void (*)(void *cookie, const char *name, const char *value, uint32_t serial);
using T_ForeachCallback = void (*)(const prop_info *pi, void *cookie);

static const prop_info *(*orig_system_property_find)(const char *name) = nullptr;
static void (*orig_system_property_read_callback)(const prop_info *pi, T_Callback callback, void *cookie) = nullptr;
static int (*orig_system_property_read)(const prop_info *pi, char *name, char *value) = nullptr;
static int (*orig_system_property_get)(const char *name, char *value) = nullptr;
static int (*orig_system_property_foreach)(T_ForeachCallback propfn, void *cookie) = nullptr;

struct PropCallbackContext {
    void *orig_cookie;
    T_Callback orig_callback;
};

static const prop_info *hook_system_property_find(const char *name) {
    if (name && g_profile.enable_build_props) {
        auto it = g_profile.prop_map.find(name);
        if (it != g_profile.prop_map.end()) {
            record_probe_event("PROP", name, it->second);
            return get_or_create_synthetic_prop(it->first, it->second);
        }
        // 记录目标应用探测的敏感属性（即使未配置伪装也记录到探针中，方便用户发现）
        if (g_profile.enable_probe &&
            (strncmp(name, "ro.", 3) == 0 || strncmp(name, "persist.", 8) == 0 || strncmp(name, "gsm.", 4) == 0)) {
            record_probe_event("PROP_MISS", name, "<real>");
        }
    }
    return orig_system_property_find ? orig_system_property_find(name) : nullptr;
}

static void modify_prop_callback(void *cookie, const char *name, const char *value, uint32_t serial) {
    auto *ctx = static_cast<PropCallbackContext *>(cookie);
    if (!ctx || !ctx->orig_callback) return;

    if (name && g_profile.enable_build_props) {
        auto it = g_profile.prop_map.find(name);
        if (it != g_profile.prop_map.end()) {
            record_probe_event("PROP", name, it->second);
            ctx->orig_callback(ctx->orig_cookie, name, it->second.c_str(), serial);
            return;
        }
    }
    ctx->orig_callback(ctx->orig_cookie, name, value, serial);
}

static void hook_system_property_read_callback(const prop_info *pi, T_Callback callback, void *cookie) {
    if (!pi || !callback) return;
    if (const SyntheticProp *sp = as_synthetic_prop(pi)) {
        callback(cookie, sp->name.c_str(), sp->value.c_str(), 1);
        return;
    }
    if (orig_system_property_read_callback) {
        PropCallbackContext ctx{cookie, callback};
        orig_system_property_read_callback(pi, modify_prop_callback, &ctx);
    }
}

static int hook_system_property_read(const prop_info *pi, char *name, char *value) {
    if (const SyntheticProp *sp = as_synthetic_prop(pi)) {
        if (name) {
            strncpy(name, sp->name.c_str(), PROP_NAME_MAX - 1);
            name[PROP_NAME_MAX - 1] = '\0';
        }
        if (value) {
            strncpy(value, sp->value.c_str(), PROP_VALUE_MAX - 1);
            value[PROP_VALUE_MAX - 1] = '\0';
            return static_cast<int>(strlen(value));
        }
        return 0;
    }
    if (!orig_system_property_read) return 0;
    int len = orig_system_property_read(pi, name, value);
    if (name && value && g_profile.enable_build_props) {
        auto it = g_profile.prop_map.find(name);
        if (it != g_profile.prop_map.end()) {
            record_probe_event("PROP", name, it->second);
            strncpy(value, it->second.c_str(), PROP_VALUE_MAX - 1);
            value[PROP_VALUE_MAX - 1] = '\0';
            return static_cast<int>(strlen(value));
        }
    }
    return len;
}

static int hook_system_property_get(const char *name, char *value) {
    if (name && value && g_profile.enable_build_props) {
        auto it = g_profile.prop_map.find(name);
        if (it != g_profile.prop_map.end()) {
            record_probe_event("PROP", name, it->second);
            strncpy(value, it->second.c_str(), PROP_VALUE_MAX - 1);
            value[PROP_VALUE_MAX - 1] = '\0';
            return static_cast<int>(strlen(value));
        }
        if (g_profile.enable_probe && strncmp(name, "ro.", 3) == 0) {
            record_probe_event("PROP_MISS", name, "<real>");
        }
    }
    return orig_system_property_get ? orig_system_property_get(name, value) : 0;
}

// 拦截 __system_property_foreach，确保遍历全表时也能看到所有虚拟跨品牌属性
struct ForeachWrapCtx {
    T_ForeachCallback user_fn;
    void *user_cookie;
    std::unordered_set<std::string> visited;
};

static void foreach_interceptor(const prop_info *pi, void *cookie) {
    auto *ctx = static_cast<ForeachWrapCtx *>(cookie);
    if (!ctx || !ctx->user_fn || !pi) return;

    // 读取真实属性的 key 名称以判断是否已被伪装表覆盖
    if (orig_system_property_read_callback) {
        std::string prop_name;
        auto extract_name = [](void *c, const char *name, const char *, uint32_t) {
            if (name) *static_cast<std::string *>(c) = name;
        };
        orig_system_property_read_callback(pi, extract_name, &prop_name);
        if (!prop_name.empty()) {
            ctx->visited.insert(prop_name);
            auto it = g_profile.prop_map.find(prop_name);
            if (it != g_profile.prop_map.end()) {
                const prop_info *syn = get_or_create_synthetic_prop(it->first, it->second);
                ctx->user_fn(syn, ctx->user_cookie);
                return;
            }
        }
    }
    ctx->user_fn(pi, ctx->user_cookie);
}

static int hook_system_property_foreach(T_ForeachCallback propfn, void *cookie) {
    if (!propfn) return -1;
    if (!g_profile.enable_build_props || !orig_system_property_foreach) {
        return orig_system_property_foreach ? orig_system_property_foreach(propfn, cookie) : -1;
    }

    ForeachWrapCtx ctx{propfn, cookie, {}};
    int res = orig_system_property_foreach(foreach_interceptor, &ctx);

    // 追加遍历宿主机原本不存在但模板中新增的跨厂商属性
    for (const auto &kv : g_profile.prop_map) {
        if (ctx.visited.find(kv.first) == ctx.visited.end()) {
            const prop_info *syn = get_or_create_synthetic_prop(kv.first, kv.second);
            propfn(syn, cookie);
        }
    }
    record_probe_event("PROP", "__system_property_foreach", "Full Table Synthesized");
    return res;
}

// ============================================================================
// JNI RegisterNatives 接管 android.os.SystemProperties
// ============================================================================
static std::string query_property_internal(const char *key, const char *def_val) {
    if (!key) return def_val ? def_val : "";
    if (g_profile.enable_build_props) {
        auto it = g_profile.prop_map.find(key);
        if (it != g_profile.prop_map.end()) {
            record_probe_event("JNI_PROP", key, it->second);
            return it->second;
        }
        if (g_profile.enable_probe && strncmp(key, "ro.", 3) == 0) {
            record_probe_event("PROP_MISS", key, "<real>");
        }
    }
    char buf[PROP_VALUE_MAX] = {0};
    if (orig_system_property_get && orig_system_property_get(key, buf) > 0) {
        return std::string(buf);
    }
    if (orig_system_property_find && orig_system_property_read_callback) {
        const prop_info *pi = orig_system_property_find(key);
        if (pi) {
            std::string out;
            auto cb = [](void *c, const char *, const char *val, uint32_t) {
                if (val) *static_cast<std::string *>(c) = val;
            };
            orig_system_property_read_callback(pi, cb, &out);
            if (!out.empty()) return out;
        }
    }
    return def_val ? def_val : "";
}

static jstring JNICALL jni_native_get(JNIEnv *env, jclass, jstring keyJ, jstring defJ) {
    if (!keyJ) return defJ;
    const char *key = env->GetStringUTFChars(keyJ, nullptr);
    const char *def = defJ ? env->GetStringUTFChars(defJ, nullptr) : nullptr;
    std::string res = query_property_internal(key, def);
    if (key) env->ReleaseStringUTFChars(keyJ, key);
    if (defJ && def) env->ReleaseStringUTFChars(defJ, def);
    return env->NewStringUTF(res.c_str());
}

static jint JNICALL jni_native_get_int(JNIEnv *env, jclass, jstring keyJ, jint defV) {
    if (!keyJ) return defV;
    const char *key = env->GetStringUTFChars(keyJ, nullptr);
    std::string res = query_property_internal(key, "");
    if (key) env->ReleaseStringUTFChars(keyJ, key);
    if (res.empty()) return defV;
    char *end = nullptr;
    long v = strtol(res.c_str(), &end, 0);
    return (end != res.c_str()) ? static_cast<jint>(v) : defV;
}

static jlong JNICALL jni_native_get_long(JNIEnv *env, jclass, jstring keyJ, jlong defV) {
    if (!keyJ) return defV;
    const char *key = env->GetStringUTFChars(keyJ, nullptr);
    std::string res = query_property_internal(key, "");
    if (key) env->ReleaseStringUTFChars(keyJ, key);
    if (res.empty()) return defV;
    char *end = nullptr;
    long long v = strtoll(res.c_str(), &end, 0);
    return (end != res.c_str()) ? static_cast<jlong>(v) : defV;
}

static jboolean JNICALL jni_native_get_boolean(JNIEnv *env, jclass, jstring keyJ, jboolean defV) {
    if (!keyJ) return defV;
    const char *key = env->GetStringUTFChars(keyJ, nullptr);
    std::string res = query_property_internal(key, "");
    if (key) env->ReleaseStringUTFChars(keyJ, key);
    if (res.empty()) return defV;
    if (res == "1" || res == "true" || res == "y" || res == "yes" || res == "on") return JNI_TRUE;
    if (res == "0" || res == "false" || res == "n" || res == "no" || res == "off") return JNI_FALSE;
    return defV;
}

// ============================================================================
// Zygisk 模块主类
// ============================================================================
class MirageForgeModule : public zygisk::ModuleBase {
public:
    void onLoad(Api *api, JNIEnv *env) override {
        this->api = api;
        this->env = env;

        orig_system_property_find = reinterpret_cast<decltype(orig_system_property_find)>(
            dlsym(RTLD_DEFAULT, "__system_property_find"));
        orig_system_property_read_callback = reinterpret_cast<decltype(orig_system_property_read_callback)>(
            dlsym(RTLD_DEFAULT, "__system_property_read_callback"));
        orig_system_property_read = reinterpret_cast<decltype(orig_system_property_read)>(
            dlsym(RTLD_DEFAULT, "__system_property_read"));
        orig_system_property_get = reinterpret_cast<decltype(orig_system_property_get)>(
            dlsym(RTLD_DEFAULT, "__system_property_get"));
        orig_system_property_foreach = reinterpret_cast<decltype(orig_system_property_foreach)>(
            dlsym(RTLD_DEFAULT, "__system_property_foreach"));

        orig_openat = reinterpret_cast<decltype(orig_openat)>(dlsym(RTLD_DEFAULT, "openat"));
        orig_openat_2 = reinterpret_cast<decltype(orig_openat_2)>(dlsym(RTLD_DEFAULT, "__openat_2"));
        orig_open = reinterpret_cast<decltype(orig_open)>(dlsym(RTLD_DEFAULT, "open"));
        orig_fopen = reinterpret_cast<decltype(orig_fopen)>(dlsym(RTLD_DEFAULT, "fopen"));
        orig_fopen64 = reinterpret_cast<decltype(orig_fopen64)>(dlsym(RTLD_DEFAULT, "fopen64"));
        orig_popen = reinterpret_cast<decltype(orig_popen)>(dlsym(RTLD_DEFAULT, "popen"));
        orig_uname = reinterpret_cast<decltype(orig_uname)>(dlsym(RTLD_DEFAULT, "uname"));
        orig_glGetString = reinterpret_cast<decltype(orig_glGetString)>(dlsym(RTLD_DEFAULT, "glGetString"));
    }

    void preAppSpecialize(AppSpecializeArgs *args) override {
        if (!args || !args->nice_name) {
            api->setOption(zygisk::Option::DLCLOSE_MODULE_LIBRARY);
            return;
        }

        const char *raw_pkg = env->GetStringUTFChars(args->nice_name, nullptr);
        if (!raw_pkg) {
            api->setOption(zygisk::Option::DLCLOSE_MODULE_LIBRARY);
            return;
        }
        std::string pkg_name(raw_pkg);
        env->ReleaseStringUTFChars(args->nice_name, raw_pkg);

        size_t colon_pos = pkg_name.find(':');
        if (colon_pos != std::string::npos) {
            pkg_name = pkg_name.substr(0, colon_pos);
        }

        int fd = api->connectCompanion();
        if (fd < 0) {
            api->setOption(zygisk::Option::DLCLOSE_MODULE_LIBRARY);
            return;
        }

        uint8_t action = static_cast<uint8_t>(IpcAction::QUERY_CONFIG);
        write_exact(fd, &action, sizeof(action));
        write_string(fd, pkg_name);

        uint8_t has_config = 0;
        if (!read_exact(fd, &has_config, sizeof(has_config)) || !has_config) {
            close(fd);
            api->setOption(zygisk::Option::DLCLOSE_MODULE_LIBRARY);
            return;
        }

        g_profile.pkg_name = pkg_name;
        uint32_t count = 0;
        read_exact(fd, &count, sizeof(count));

        for (uint32_t i = 0; i < count; ++i) {
            std::string key = read_string(fd);
            std::string val = read_string(fd);
            if (key.empty() || val.empty()) continue;

            if (key == "META:ENABLE_BUILD") {
                g_profile.enable_build_props = (val == "1");
            } else if (key == "META:ENABLE_GL") {
                g_profile.enable_gl_spoof = (val == "1");
            } else if (key == "META:ENABLE_VFS") {
                g_profile.enable_vfs_shield = (val == "1");
            } else if (key == "META:ENABLE_PROBE") {
                g_profile.enable_probe = (val == "1");
            } else if (key == "GLES:VENDOR") {
                g_profile.gl_vendor = val;
            } else if (key == "GLES:RENDERER") {
                g_profile.gl_renderer = val;
            } else if (key == "GLES:VERSION") {
                g_profile.gl_version = val;
            } else if (key == "VFS:CPU_HARDWARE") {
                g_profile.cpu_hardware = val;
            } else if (key == "VFS:CPU_ARCH") {
                g_profile.cpu_arch = val;
            } else if (key == "VFS:CPU_CORES") {
                g_profile.cpu_cores = atoi(val.c_str());
            } else if (key == "VFS:RAM_GB") {
                g_profile.ram_gb = atoi(val.c_str());
            } else if (key == "VFS:KERNEL_VERSION") {
                g_profile.kernel_version = val;
            } else if (key.rfind("BUILD:", 0) == 0) {
                g_profile.build_map[key.substr(6)] = val;
            } else if (key.rfind("VERSION:", 0) == 0) {
                g_profile.version_map[key.substr(8)] = val;
            } else {
                g_profile.prop_map[key] = val;
            }
        }
        close(fd);

        synthesize_vfs_nodes();
        g_profile.active = true;

        LOGI("MirageForge v3.0 Forging Mirage for %s [Props:%zu, GL:%s, VFS:%d, Probe:%d]",
             pkg_name.c_str(), g_profile.prop_map.size(),
             g_profile.gl_renderer.c_str(), g_profile.enable_vfs_shield, g_profile.enable_probe);

        if (g_profile.enable_build_props) {
            applyJavaBuildReflection();
            hijackSystemPropertiesJni();
        }
        registerPltHooksFromMaps();
    }

    void postAppSpecialize(const AppSpecializeArgs *) override {
        if (!g_profile.active) return;
        if (g_profile.enable_build_props) {
            applyJavaBuildReflection();
        }
        registerPltHooksFromMaps();
    }

    void preServerSpecialize(ServerSpecializeArgs *) override {
        api->setOption(zygisk::Option::DLCLOSE_MODULE_LIBRARY);
    }

private:
    Api *api = nullptr;
    JNIEnv *env = nullptr;
    std::set<std::pair<dev_t, ino_t>> hooked_inodes;

    void registerPltHooksFromMaps() {
        FILE *fp = orig_fopen ? orig_fopen("/proc/self/maps", "r") : fopen("/proc/self/maps", "r");
        if (!fp) return;

        char line[1024];
        bool need_commit = false;

        while (fgets(line, sizeof(line), fp)) {
            uintptr_t start = 0, end = 0, offset = 0;
            unsigned int dev_major = 0, dev_minor = 0;
            unsigned long inode_ul = 0;
            char perms[8] = {0};
            int path_off = 0;

            if (sscanf(line, "%" SCNxPTR "-%" SCNxPTR " %7s %" SCNxPTR " %x:%x %lu %n",
                       &start, &end, perms, &offset, &dev_major, &dev_minor, &inode_ul, &path_off) < 7) {
                continue;
            }
            if (inode_ul == 0) continue;

            char *path = line + path_off;
            while (*path == ' ' || *path == '\t') ++path;
            size_t len = strlen(path);
            while (len > 0 && (path[len - 1] == '\n' || path[len - 1] == '\r')) {
                path[--len] = '\0';
            }

            if (path[0] != '/' || strstr(path, "/libc.so") != nullptr || strstr(path, "mirage_forge") != nullptr) {
                continue;
            }

            dev_t dev = makedev(dev_major, dev_minor);
            ino_t inode = static_cast<ino_t>(inode_ul);
            auto key = std::make_pair(dev, inode);
            if (hooked_inodes.find(key) != hooked_inodes.end()) {
                continue;
            }
            hooked_inodes.insert(key);
            need_commit = true;

            if (g_profile.enable_build_props || g_profile.enable_probe) {
                api->pltHookRegister(dev, inode, "__system_property_find",
                                     reinterpret_cast<void *>(hook_system_property_find),
                                     reinterpret_cast<void **>(&orig_system_property_find));
                api->pltHookRegister(dev, inode, "__system_property_read_callback",
                                     reinterpret_cast<void *>(hook_system_property_read_callback),
                                     reinterpret_cast<void **>(&orig_system_property_read_callback));
                api->pltHookRegister(dev, inode, "__system_property_read",
                                     reinterpret_cast<void *>(hook_system_property_read),
                                     reinterpret_cast<void **>(&orig_system_property_read));
                api->pltHookRegister(dev, inode, "__system_property_get",
                                     reinterpret_cast<void *>(hook_system_property_get),
                                     reinterpret_cast<void **>(&orig_system_property_get));
                api->pltHookRegister(dev, inode, "__system_property_foreach",
                                     reinterpret_cast<void *>(hook_system_property_foreach),
                                     reinterpret_cast<void **>(&orig_system_property_foreach));
                api->pltHookRegister(dev, inode, "popen",
                                     reinterpret_cast<void *>(hook_popen),
                                     reinterpret_cast<void **>(&orig_popen));
            }

            if (g_profile.enable_vfs_shield) {
                api->pltHookRegister(dev, inode, "openat",
                                     reinterpret_cast<void *>(hook_openat),
                                     reinterpret_cast<void **>(&orig_openat));
                api->pltHookRegister(dev, inode, "__openat_2",
                                     reinterpret_cast<void *>(hook_openat_2),
                                     reinterpret_cast<void **>(&orig_openat_2));
                api->pltHookRegister(dev, inode, "open",
                                     reinterpret_cast<void *>(hook_open),
                                     reinterpret_cast<void **>(&orig_open));
                api->pltHookRegister(dev, inode, "fopen",
                                     reinterpret_cast<void *>(hook_fopen),
                                     reinterpret_cast<void **>(&orig_fopen));
                api->pltHookRegister(dev, inode, "fopen64",
                                     reinterpret_cast<void *>(hook_fopen64),
                                     reinterpret_cast<void **>(&orig_fopen64));
                api->pltHookRegister(dev, inode, "uname",
                                     reinterpret_cast<void *>(hook_uname),
                                     reinterpret_cast<void **>(&orig_uname));
            }

            if (g_profile.enable_gl_spoof) {
                api->pltHookRegister(dev, inode, "glGetString",
                                     reinterpret_cast<void *>(hook_glGetString),
                                     reinterpret_cast<void **>(&orig_glGetString));
            }
        }
        fclose(fp);

        if (need_commit) {
            api->pltHookCommit();
        }
    }

    void setStaticStringField(jclass cls, const char *field_name, const std::string &val) {
        jfieldID fid = env->GetStaticFieldID(cls, field_name, "Ljava/lang/String;");
        if (env->ExceptionCheck()) {
            env->ExceptionClear();
            return;
        }
        if (fid) {
            jstring jval = env->NewStringUTF(val.c_str());
            env->SetStaticObjectField(cls, fid, jval);
            env->DeleteLocalRef(jval);
        }
    }

    void setStaticIntField(jclass cls, const char *field_name, int val) {
        jfieldID fid = env->GetStaticFieldID(cls, field_name, "I");
        if (env->ExceptionCheck()) {
            env->ExceptionClear();
            return;
        }
        if (fid) {
            env->SetStaticIntField(cls, fid, static_cast<jint>(val));
        }
    }

    void setStaticLongField(jclass cls, const char *field_name, long long val) {
        jfieldID fid = env->GetStaticFieldID(cls, field_name, "J");
        if (env->ExceptionCheck()) {
            env->ExceptionClear();
            return;
        }
        if (fid) {
            env->SetStaticLongField(cls, fid, static_cast<jlong>(val));
        }
    }

    void setStaticStringArrayField(jclass cls, const char *field_name, const std::vector<std::string> &items) {
        if (items.empty()) return;
        jfieldID fid = env->GetStaticFieldID(cls, field_name, "[Ljava/lang/String;");
        if (env->ExceptionCheck()) {
            env->ExceptionClear();
            return;
        }
        jclass str_cls = env->FindClass("java/lang/String");
        if (!str_cls) {
            env->ExceptionClear();
            return;
        }
        jobjectArray arr = env->NewObjectArray(static_cast<jsize>(items.size()), str_cls, nullptr);
        if (arr) {
            for (size_t i = 0; i < items.size(); ++i) {
                jstring s = env->NewStringUTF(items[i].c_str());
                env->SetObjectArrayElement(arr, static_cast<jsize>(i), s);
                env->DeleteLocalRef(s);
            }
            env->SetStaticObjectField(cls, fid, arr);
            env->DeleteLocalRef(arr);
        }
        env->DeleteLocalRef(str_cls);
    }

    void applyJavaBuildReflection() {
        jclass build_cls = env->FindClass("android/os/Build");
        if (build_cls) {
            for (const auto &kv : g_profile.build_map) {
                if (kv.first == "TIME") {
                    long long t = atoll(kv.second.c_str());
                    if (t > 0) setStaticLongField(build_cls, "TIME", t);
                } else if (kv.first == "SUPPORTED_ABIS" ||
                           kv.first == "SUPPORTED_64_BIT_ABIS" ||
                           kv.first == "SUPPORTED_32_BIT_ABIS") {
                    setStaticStringArrayField(build_cls, kv.first.c_str(), split_csv(kv.second, ','));
                } else {
                    setStaticStringField(build_cls, kv.first.c_str(), kv.second);
                }
            }
            env->DeleteLocalRef(build_cls);
        } else {
            env->ExceptionClear();
        }

        jclass ver_cls = env->FindClass("android/os/Build$VERSION");
        if (ver_cls) {
            for (const auto &kv : g_profile.version_map) {
                if (kv.first == "SDK_INT" || kv.first == "DEVICE_INITIAL_SDK_INT") {
                    int sdk = atoi(kv.second.c_str());
                    if (sdk > 0) setStaticIntField(ver_cls, kv.first.c_str(), sdk);
                } else {
                    setStaticStringField(ver_cls, kv.first.c_str(), kv.second);
                }
            }
            env->DeleteLocalRef(ver_cls);
        } else {
            env->ExceptionClear();
        }
    }

    void hijackSystemPropertiesJni() {
        jclass sp_cls = env->FindClass("android/os/SystemProperties");
        if (!sp_cls) {
            env->ExceptionClear();
            return;
        }
        JNINativeMethod methods[] = {
            {"native_get", "(Ljava/lang/String;Ljava/lang/String;)Ljava/lang/String;", reinterpret_cast<void *>(jni_native_get)},
            {"native_get_int", "(Ljava/lang/String;I)I", reinterpret_cast<void *>(jni_native_get_int)},
            {"native_get_long", "(Ljava/lang/String;J)J", reinterpret_cast<void *>(jni_native_get_long)},
            {"native_get_boolean", "(Ljava/lang/String;Z)Z", reinterpret_cast<void *>(jni_native_get_boolean)}
        };
        env->RegisterNatives(sp_cls, methods, sizeof(methods) / sizeof(methods[0]));
        if (env->ExceptionCheck()) {
            env->ExceptionClear();
        }
        env->DeleteLocalRef(sp_cls);
    }
};

// ============================================================================
// Root Companion 守护进程
// ============================================================================
static void companion_handler(int fd) {
    uint8_t action = 0;
    if (!read_exact(fd, &action, sizeof(action))) return;

    if (action == static_cast<uint8_t>(IpcAction::QUERY_CONFIG)) {
        std::string pkg_name = read_string(fd);
        if (pkg_name.empty() || pkg_name.find('/') != std::string::npos) {
            uint8_t zero = 0;
            write_exact(fd, &zero, sizeof(zero));
            return;
        }

        std::string conf_path = "/data/adb/mirage_forge/apps/" + pkg_name + ".conf";
        std::ifstream ifs(conf_path);
        if (!ifs.is_open()) {
            uint8_t zero = 0;
            write_exact(fd, &zero, sizeof(zero));
            return;
        }

        std::unordered_map<std::string, std::string> entries;
        std::string line;
        while (std::getline(ifs, line)) {
            if (line.empty() || line[0] == '#') continue;
            size_t eq = line.find('=');
            if (eq == std::string::npos) continue;
            std::string k = line.substr(0, eq);
            std::string v = line.substr(eq + 1);
            if (!k.empty() && !v.empty()) {
                entries[k] = v;
            }
        }

        if (entries.empty()) {
            uint8_t zero = 0;
            write_exact(fd, &zero, sizeof(zero));
            return;
        }

        uint8_t one = 1;
        write_exact(fd, &one, sizeof(one));
        uint32_t count = static_cast<uint32_t>(entries.size());
        write_exact(fd, &count, sizeof(count));
        for (const auto &kv : entries) {
            write_string(fd, kv.first);
            write_string(fd, kv.second);
        }
    }
}

REGISTER_ZYGISK_MODULE(MirageForgeModule)
REGISTER_ZYGISK_COMPANION(companion_handler)

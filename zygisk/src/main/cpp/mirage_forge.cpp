#include <cstdlib>
#include <cstdio>
#include <cstring>
#include <cinttypes>
#include <string>
#include <vector>
#include <unordered_map>
#include <unordered_set>
#include <set>
#include <fstream>
#include <sstream>
#include <mutex>
#include <unistd.h>
#include <fcntl.h>
#include <dlfcn.h>
#include <sys/stat.h>
#include <sys/sysmacros.h>
#include <sys/utsname.h>
#include <sys/syscall.h>
#include <sys/system_properties.h>
#include <linux/memfd.h>
#include <android/log.h>

#include "zygisk.hpp"

#define LOG_TAG "MirageForge"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, LOG_TAG, __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, __VA_ARGS__)

using zygisk::Api;
using zygisk::AppSpecializeArgs;
using zygisk::ServerSpecializeArgs;

// ============================================================================
// 全局进程内伪装状态池
// ============================================================================
static bool g_active = false;
static bool g_memfd_shield = true;
static std::unordered_map<std::string, std::string> g_prop_map;      // ro.* / persist.* 属性池
static std::unordered_map<std::string, std::string> g_build_map;     // android.os.Build 字段池
static std::unordered_map<std::string, std::string> g_version_map;   // android.os.Build$VERSION 字段池
static std::string g_synthesized_build_prop;                         // 供 memfd 重定向读取的合成 build.prop 内容

// 虚拟 prop_info 句柄结构：解决宿主机不存在对应 key 时 __system_property_find 返回 nullptr 的致命漏洞
struct SyntheticProp {
    std::string name;
    std::string value;
};

static std::mutex g_syn_mutex;
static std::unordered_map<std::string, SyntheticProp *> g_syn_by_name;
static std::unordered_set<const void *> g_syn_ptrs;

static const prop_info *get_or_create_synthetic_prop(const std::string &name, const std::string &val) {
    std::lock_guard<std::mutex> lock(g_syn_mutex);
    auto it = g_syn_by_name.find(name);
    if (it != g_syn_by_name.end()) {
        return reinterpret_cast<const prop_info *>(it->second);
    }
    auto *sp = new SyntheticProp{name, val};
    g_syn_by_name[name] = sp;
    g_syn_ptrs.insert(static_cast<const void *>(sp));
    return reinterpret_cast<const prop_info *>(sp);
}

static const SyntheticProp *as_synthetic_prop(const prop_info *pi) {
    if (!pi) return nullptr;
    std::lock_guard<std::mutex> lock(g_syn_mutex);
    if (g_syn_ptrs.count(reinterpret_cast<const void *>(pi)) > 0) {
        return reinterpret_cast<const SyntheticProp *>(pi);
    }
    return nullptr;
}

// ============================================================================
// 第 1 层：Bionic System Properties 四重底层拦截
// ============================================================================
using T_Callback = void (*)(void *cookie, const char *name, const char *value, uint32_t serial);

static const prop_info *(*orig_system_property_find)(const char *name) = nullptr;
static void (*orig_system_property_read_callback)(const prop_info *pi, T_Callback callback, void *cookie) = nullptr;
static int (*orig_system_property_read)(const prop_info *pi, char *name, char *value) = nullptr;
static int (*orig_system_property_get)(const char *name, char *value) = nullptr;

struct PropCallbackContext {
    void *orig_cookie;
    T_Callback orig_callback;
};

static const prop_info *hook_system_property_find(const char *name) {
    if (name) {
        auto it = g_prop_map.find(name);
        if (it != g_prop_map.end()) {
            return get_or_create_synthetic_prop(it->first, it->second);
        }
    }
    return orig_system_property_find ? orig_system_property_find(name) : nullptr;
}

static void modify_prop_callback(void *cookie, const char *name, const char *value, uint32_t serial) {
    auto *ctx = static_cast<PropCallbackContext *>(cookie);
    if (!ctx || !ctx->orig_callback) return;

    if (name) {
        auto it = g_prop_map.find(name);
        if (it != g_prop_map.end()) {
            ctx->orig_callback(ctx->orig_cookie, name, it->second.c_str(), serial);
            return;
        }
    }
    ctx->orig_callback(ctx->orig_cookie, name, value, serial);
}

static void hook_system_property_read_callback(const prop_info *pi, T_Callback callback, void *cookie) {
    if (!pi || !callback) return;
    // 若是虚拟 prop_info 句柄，直接回调返回伪装值，不进入 Bionic 原生函数解引用
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
    if (name && value) {
        auto it = g_prop_map.find(name);
        if (it != g_prop_map.end()) {
            strncpy(value, it->second.c_str(), PROP_VALUE_MAX - 1);
            value[PROP_VALUE_MAX - 1] = '\0';
            return static_cast<int>(strlen(value));
        }
    }
    return len;
}

static int hook_system_property_get(const char *name, char *value) {
    if (name && value) {
        auto it = g_prop_map.find(name);
        if (it != g_prop_map.end()) {
            strncpy(value, it->second.c_str(), PROP_VALUE_MAX - 1);
            value[PROP_VALUE_MAX - 1] = '\0';
            return static_cast<int>(strlen(value));
        }
    }
    return orig_system_property_get ? orig_system_property_get(name, value) : 0;
}

// ============================================================================
// 第 2 层：VFS 内存匿名文件重定向 (memfd_create) 与 uname 伪装
// ============================================================================
static int (*orig_openat)(int dirfd, const char *pathname, int flags, mode_t mode) = nullptr;
static int (*orig_open)(const char *pathname, int flags, mode_t mode) = nullptr;
static FILE *(*orig_fopen)(const char *pathname, const char *mode) = nullptr;
static int (*orig_uname)(struct utsname *buf) = nullptr;

static bool is_target_build_prop_path(const char *path) {
    if (!path || !g_memfd_shield) return false;
    return strcmp(path, "/system/build.prop") == 0 ||
           strcmp(path, "/system/etc/build.prop") == 0 ||
           strcmp(path, "/vendor/build.prop") == 0 ||
           strcmp(path, "/product/build.prop") == 0 ||
           strcmp(path, "/product/etc/build.prop") == 0 ||
           strcmp(path, "/odm/etc/build.prop") == 0;
}

static int create_synthetic_build_prop_fd() {
    int memfd = static_cast<int>(syscall(__NR_memfd_create, "build.prop", MFD_CLOEXEC));
    if (memfd < 0) return -1;
    if (!g_synthesized_build_prop.empty()) {
        write(memfd, g_synthesized_build_prop.data(), g_synthesized_build_prop.size());
        lseek(memfd, 0, SEEK_SET);
    }
    return memfd;
}

static int hook_openat(int dirfd, const char *pathname, int flags, mode_t mode) {
    if (is_target_build_prop_path(pathname)) {
        int fake_fd = create_synthetic_build_prop_fd();
        if (fake_fd >= 0) return fake_fd;
    }
    return orig_openat ? orig_openat(dirfd, pathname, flags, mode) : -1;
}

static int hook_open(const char *pathname, int flags, mode_t mode) {
    if (is_target_build_prop_path(pathname)) {
        int fake_fd = create_synthetic_build_prop_fd();
        if (fake_fd >= 0) return fake_fd;
    }
    return orig_open ? orig_open(pathname, flags, mode) : -1;
}

static FILE *hook_fopen(const char *pathname, const char *mode) {
    if (is_target_build_prop_path(pathname)) {
        int fake_fd = create_synthetic_build_prop_fd();
        if (fake_fd >= 0) {
            FILE *fp = fdopen(fake_fd, "r");
            if (fp) return fp;
            close(fake_fd);
        }
    }
    return orig_fopen ? orig_fopen(pathname, mode) : nullptr;
}

static int hook_uname(struct utsname *buf) {
    int ret = orig_uname ? orig_uname(buf) : uname(buf);
    if (ret == 0 && buf) {
        auto it = g_prop_map.find("ro.product.cpu.abi");
        if (it != g_prop_map.end() && it->second.find("arm64") != std::string::npos) {
            strncpy(buf->machine, "aarch64", sizeof(buf->machine) - 1);
            buf->machine[sizeof(buf->machine) - 1] = '\0';
        }
    }
    return ret;
}

// ============================================================================
// 第 3 层：JNI RegisterNatives 直接接管 android.os.SystemProperties
// ============================================================================
static std::string query_property_internal(const char *key, const char *def_val) {
    if (!key) return def_val ? def_val : "";
    auto it = g_prop_map.find(key);
    if (it != g_prop_map.end()) {
        return it->second;
    }
    // 未命中伪装池时回退至底层原厂属性读取
    char buf[PROP_VALUE_MAX] = {0};
    if (orig_system_property_get && orig_system_property_get(key, buf) > 0) {
        return std::string(buf);
    }
    if (orig_system_property_find && orig_system_property_read_callback) {
        const prop_info *pi = orig_system_property_find(key);
        if (pi) {
            std::string out;
            auto cb = [](void *cookie, const char *, const char *val, uint32_t) {
                if (val) *static_cast<std::string *>(cookie) = val;
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
// Socket IPC 辅助函数
// ============================================================================
static void write_string(int fd, const std::string &str) {
    uint32_t len = static_cast<uint32_t>(str.size());
    write(fd, &len, sizeof(len));
    if (len > 0) write(fd, str.data(), len);
}

static std::string read_string(int fd) {
    uint32_t len = 0;
    if (read(fd, &len, sizeof(len)) != sizeof(len) || len == 0 || len > 131072) return "";
    std::string str(len, '\0');
    if (read(fd, &str[0], len) != static_cast<ssize_t>(len)) return "";
    return str;
}

// ============================================================================
// Zygisk 模块主类 (适配 Zygisk API v4)
// ============================================================================
class MirageForgeModule : public zygisk::ModuleBase {
public:
    void onLoad(Api *api, JNIEnv *env) override {
        this->api = api;
        this->env = env;

        // 预先通过 dlsym 兜底获取 Bionic / Libc 原始函数指针
        orig_system_property_find = reinterpret_cast<decltype(orig_system_property_find)>(
            dlsym(RTLD_DEFAULT, "__system_property_find"));
        orig_system_property_read_callback = reinterpret_cast<decltype(orig_system_property_read_callback)>(
            dlsym(RTLD_DEFAULT, "__system_property_read_callback"));
        orig_system_property_read = reinterpret_cast<decltype(orig_system_property_read)>(
            dlsym(RTLD_DEFAULT, "__system_property_read"));
        orig_system_property_get = reinterpret_cast<decltype(orig_system_property_get)>(
            dlsym(RTLD_DEFAULT, "__system_property_get"));
        orig_openat = reinterpret_cast<decltype(orig_openat)>(dlsym(RTLD_DEFAULT, "openat"));
        orig_open = reinterpret_cast<decltype(orig_open)>(dlsym(RTLD_DEFAULT, "open"));
        orig_fopen = reinterpret_cast<decltype(orig_fopen)>(dlsym(RTLD_DEFAULT, "fopen"));
        orig_uname = reinterpret_cast<decltype(orig_uname)>(dlsym(RTLD_DEFAULT, "uname"));
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

        // 剥离多进程后缀 (如 com.tencent.mobileqq:MSF -> com.tencent.mobileqq)
        size_t colon_pos = pkg_name.find(':');
        if (colon_pos != std::string::npos) {
            pkg_name = pkg_name.substr(0, colon_pos);
        }

        int fd = api->connectCompanion();
        if (fd < 0) {
            api->setOption(zygisk::Option::DLCLOSE_MODULE_LIBRARY);
            return;
        }

        write_string(fd, pkg_name);
        uint8_t has_config = 0;
        if (read(fd, &has_config, sizeof(has_config)) != sizeof(has_config) || !has_config) {
            close(fd);
            api->setOption(zygisk::Option::DLCLOSE_MODULE_LIBRARY);
            return;
        }

        uint32_t count = 0;
        read(fd, &count, sizeof(count));
        std::ostringstream prop_stream;
        prop_stream << "# Generated by MirageForge Virtual VFS Engine\n";

        for (uint32_t i = 0; i < count; ++i) {
            std::string key = read_string(fd);
            std::string val = read_string(fd);
            if (key.empty() || val.empty()) continue;

            if (key == "META:MEMFD_SHIELD") {
                g_memfd_shield = (val == "1" || val == "true");
            } else if (key.rfind("BUILD:", 0) == 0) {
                g_build_map[key.substr(6)] = val;
            } else if (key.rfind("VERSION:", 0) == 0) {
                g_version_map[key.substr(8)] = val;
            } else {
                g_prop_map[key] = val;
                prop_stream << key << "=" << val << "\n";
            }
        }
        close(fd);

        g_synthesized_build_prop = prop_stream.str();
        g_active = true;

        LOGI("MirageForge v2.0 Activated -> %s [Build:%zu, Ver:%zu, Props:%zu, MemfdShield:%d]",
             pkg_name.c_str(), g_build_map.size(), g_version_map.size(), g_prop_map.size(), g_memfd_shield);

        // 执行第一阶段全栈劫持
        applyJavaBuildReflection();
        hijackSystemPropertiesJni();
        registerPltHooksFromMaps();
    }

    void postAppSpecialize(const AppSpecializeArgs *) override {
        if (!g_active) return;
        // 第二阶段加固：防止特种框架在 Specialize 期间重置静态字段或加载新库
        applyJavaBuildReflection();
        registerPltHooksFromMaps();
    }

    void preServerSpecialize(ServerSpecializeArgs *) override {
        api->setOption(zygisk::Option::DLCLOSE_MODULE_LIBRARY);
    }

private:
    Api *api = nullptr;
    JNIEnv *env = nullptr;
    std::set<std::pair<dev_t, ino_t>> hooked_inodes;

    // 扫描 /proc/self/maps 获取 (dev_t, ino_t) 并调用 Zygisk API v4 5 参数 pltHookRegister
    void registerPltHooksFromMaps() {
        FILE *fp = orig_fopen ? orig_fopen("/proc/self/maps", "r") : fopen("/proc/self/maps", "r");
        if (!fp) return;

        char line[1024];
        bool committed_needed = false;

        while (fgets(line, sizeof(line), fp)) {
            uintptr_t start, end, offset;
            unsigned int dev_major = 0, dev_minor = 0;
            ino_t inode = 0;
            char perms[8] = {0};
            int path_off = 0;

            if (sscanf(line, "%" SCNxPTR "-%" SCNxPTR " %7s %" SCNxPTR " %x:%x %lu %n",
                       &start, &end, perms, &offset, &dev_major, &dev_minor, &inode, &path_off) < 7) {
                continue;
            }
            if (inode == 0) continue;

            char *path = line + path_off;
            while (*path == ' ' || *path == '\t') ++path;
            size_t len = strlen(path);
            while (len > 0 && (path[len - 1] == '\n' || path[len - 1] == '\r')) {
                path[--len] = '\0';
            }

            // 跳过匿名映射、libc 自身以及本模块自身
            if (path[0] != '/' || strstr(path, "/libc.so") != nullptr || strstr(path, "mirage_forge") != nullptr) {
                continue;
            }

            dev_t dev = makedev(dev_major, dev_minor);
            auto key = std::make_pair(dev, inode);
            if (hooked_inodes.find(key) != hooked_inodes.end()) {
                continue;
            }
            hooked_inodes.insert(key);
            committed_needed = true;

            // Zygisk API v4 严格 5 参数签名：(dev_t, ino_t, symbol, newFunc, oldFunc)
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

            if (g_memfd_shield) {
                api->pltHookRegister(dev, inode, "openat",
                                     reinterpret_cast<void *>(hook_openat),
                                     reinterpret_cast<void **>(&orig_openat));
                api->pltHookRegister(dev, inode, "open",
                                     reinterpret_cast<void *>(hook_open),
                                     reinterpret_cast<void **>(&orig_open));
                api->pltHookRegister(dev, inode, "fopen",
                                     reinterpret_cast<void *>(hook_fopen),
                                     reinterpret_cast<void **>(&orig_fopen));
            }

            api->pltHookRegister(dev, inode, "uname",
                                 reinterpret_cast<void *>(hook_uname),
                                 reinterpret_cast<void **>(&orig_uname));
        }
        fclose(fp);

        if (committed_needed) {
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

    void applyJavaBuildReflection() {
        jclass build_cls = env->FindClass("android/os/Build");
        if (build_cls) {
            for (const auto &kv : g_build_map) {
                setStaticStringField(build_cls, kv.first.c_str(), kv.second);
            }
            env->DeleteLocalRef(build_cls);
        } else {
            env->ExceptionClear();
        }

        jclass ver_cls = env->FindClass("android/os/Build$VERSION");
        if (ver_cls) {
            for (const auto &kv : g_version_map) {
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
// Root Companion 守护进程：读取 /data/adb/mirage_forge/apps/<pkg>.conf
// ============================================================================
static void companion_handler(int fd) {
    std::string pkg_name = read_string(fd);
    if (pkg_name.empty() || pkg_name.find('/') != std::string::npos) {
        uint8_t zero = 0;
        write(fd, &zero, sizeof(zero));
        return;
    }

    std::string conf_path = "/data/adb/mirage_forge/apps/" + pkg_name + ".conf";
    std::ifstream ifs(conf_path);
    if (!ifs.is_open()) {
        uint8_t zero = 0;
        write(fd, &zero, sizeof(zero));
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
        write(fd, &zero, sizeof(zero));
        return;
    }

    uint8_t one = 1;
    write(fd, &one, sizeof(one));
    uint32_t count = static_cast<uint32_t>(entries.size());
    write(fd, &count, sizeof(count));
    for (const auto &kv : entries) {
        write_string(fd, kv.first);
        write_string(fd, kv.second);
    }
}

REGISTER_ZYGISK_MODULE(MirageForgeModule)
REGISTER_ZYGISK_COMPANION(companion_handler)

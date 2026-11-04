#include <cstdlib>
#include <cstring>
#include <string>
#include <unordered_map>
#include <fstream>
#include <unistd.h>
#include <fcntl.h>
#include <sys/system_properties.h>
#include <android/log.h>

#include "zygisk.hpp"

#define LOG_TAG "MirageForge"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, LOG_TAG, __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, __VA_ARGS__)

using zygisk::Api;
using zygisk::AppSpecializeArgs;
using zygisk::ServerSpecializeArgs;

// 进程内缓存：存储当前应用需要伪装的属性表与 Build 字段表
static std::unordered_map<std::string, std::string> g_prop_map;
static std::unordered_map<std::string, std::string> g_build_map;

// 原生函数指针保存
using T_Callback = void (*)(void *cookie, const char *name, const char *value, uint32_t serial);
static void (*orig_system_property_read_callback)(const prop_info *pi, T_Callback callback, void *cookie) = nullptr;
static int (*orig_system_property_get)(const char *name, char *value) = nullptr;

struct PropCallbackContext {
    void *orig_cookie;
    T_Callback orig_callback;
};

// 拦截 Android 8.0+ 的 __system_property_read_callback
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
    PropCallbackContext ctx{cookie, callback};
    orig_system_property_read_callback(pi, modify_prop_callback, &ctx);
}

// 拦截旧版与部分 Native SDK 直接调用的 __system_property_get
static int hook_system_property_get(const char *name, char *value) {
    if (name && value) {
        auto it = g_prop_map.find(name);
        if (it != g_prop_map.end()) {
            strncpy(value, it->second.c_str(), PROP_VALUE_MAX - 1);
            value[PROP_VALUE_MAX - 1] = '\0';
            return static_cast<int>(strlen(value));
        }
    }
    return orig_system_property_get(name, value);
}

// Socket 读写辅助函数
static void write_string(int fd, const std::string &str) {
    uint32_t len = str.size();
    write(fd, &len, sizeof(len));
    if (len > 0) write(fd, str.data(), len);
}

static std::string read_string(int fd) {
    uint32_t len = 0;
    if (read(fd, &len, sizeof(len)) != sizeof(len) || len == 0 || len > 65536) return "";
    std::string str(len, '\0');
    if (read(fd, &str[0], len) != static_cast<ssize_t>(len)) return "";
    return str;
}

class MirageForgeModule : public zygisk::ModuleBase {
public:
    void onLoad(Api *api, JNIEnv *env) override {
        this->api = api;
        this->env = env;
    }

    void preAppSpecialize(AppSpecializeArgs *args) override {
        const char *raw_pkg = env->GetStringUTFChars(args->nice_name, nullptr);
        if (!raw_pkg) {
            api->setOption(zygisk::Option::DLCLOSE_MODULE_LIBRARY);
            return;
        }
        std::string pkg_name(raw_pkg);
        env->ReleaseStringUTFChars(args->nice_name, raw_pkg);

        // 处理多进程包名 (如 com.tencent.mm:push -> com.tencent.mm)
        size_t colon_pos = pkg_name.find(':');
        if (colon_pos != std::string::npos) {
            pkg_name = pkg_name.substr(0, colon_pos);
        }

        // 连接 Root 权限的 Companion 进程查询该应用是否配置了机型伪装
        int fd = api->connectCompanion();
        if (fd < 0) {
            api->setOption(zygisk::Option::DLCLOSE_MODULE_LIBRARY);
            return;
        }

        write_string(fd, pkg_name);
        uint8_t has_config = 0;
        read(fd, &has_config, sizeof(has_config));

        if (!has_config) {
            close(fd);
            // 未配置的应用立即卸载模块，不留任何痕迹
            api->setOption(zygisk::Option::DLCLOSE_MODULE_LIBRARY);
            return;
        }

        uint32_t count = 0;
        read(fd, &count, sizeof(count));
        for (uint32_t i = 0; i < count; ++i) {
            std::string key = read_string(fd);
            std::string val = read_string(fd);
            if (!key.empty() && !val.empty()) {
                if (key.rfind("BUILD:", 0) == 0) {
                    g_build_map[key.substr(6)] = val;
                } else {
                    g_prop_map[key] = val;
                }
            }
        }
        close(fd);

        LOGI("Activating MirageForge for package: %s (Build fields: %zu, Props: %zu)",
             pkg_name.c_str(), g_build_map.size(), g_prop_map.size());

        // 1. 修改 Java 层 android.os.Build 静态常量
        spoofJavaBuildFields();

        // 2. 注册 PLT Hook 拦截 Native 层属性读取
        api->pltHookRegister(".*", "__system_property_read_callback",
                             reinterpret_cast<void *>(hook_system_property_read_callback),
                             reinterpret_cast<void **>(&orig_system_property_read_callback));
        api->pltHookRegister(".*", "__system_property_get",
                             reinterpret_cast<void *>(hook_system_property_get),
                             reinterpret_cast<void **>(&orig_system_property_get));
        api->pltHookCommit();
    }

    void preServerSpecialize(ServerSpecializeArgs *args) override {
        // 不注入 system_server
        api->setOption(zygisk::Option::DLCLOSE_MODULE_LIBRARY);
    }

private:
    Api *api = nullptr;
    JNIEnv *env = nullptr;

    void spoofJavaBuildFields() {
        jclass build_cls = env->FindClass("android/os/Build");
        if (!build_cls) {
            env->ExceptionClear();
            return;
        }

        for (const auto &kv : g_build_map) {
            jfieldID fid = env->GetStaticFieldID(build_cls, kv.first.c_str(), "Ljava/lang/String;");
            if (env->ExceptionCheck()) {
                env->ExceptionClear();
                continue;
            }
            if (fid) {
                jstring jval = env->NewStringUTF(kv.second.c_str());
                env->SetStaticObjectField(build_cls, fid, jval);
                env->DeleteLocalRef(jval);
            }
        }
        env->DeleteLocalRef(build_cls);
    }
};

// Root 守护进程：读取 /data/adb/mirage_forge/apps/<pkg>.conf 并下发给应用进程
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
    uint32_t count = entries.size();
    write(fd, &count, sizeof(count));
    for (const auto &kv : entries) {
        write_string(fd, kv.first);
        write_string(fd, kv.second);
    }
}

REGISTER_ZYGISK_MODULE(MirageForgeModule)
REGISTER_ZYGISK_COMPANION(companion_handler)

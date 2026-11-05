#pragma once

#include <cstdlib>
#include <jni.h>
#include "mirage_shared.hpp"
#include "bionic_prop_engine.hpp"

namespace mirage {

inline std::string query_prop_for_jni(const char *key, const char *def_val) {
    if (!key) return def_val ? def_val : "";
    if (g_ctx.enable_build_props) {
        auto it = g_ctx.prop_map.find(key);
        if (it != g_ctx.prop_map.end()) {
            emit_probe("JNI_PROP", key, it->second);
            return it->second;
        }
        if (g_ctx.enable_probe && std::strncmp(key, "ro.", 3) == 0) {
            emit_probe("JNI_PROP_UNMAPPED", key, "<passthrough>");
        }
    }
    char buf[PROP_VALUE_MAX] = {0};
    if (orig_system_property_get && orig_system_property_get(key, buf) > 0) {
        return std::string(buf);
    }
    return def_val ? def_val : "";
}

inline jstring JNICALL jni_native_get(JNIEnv *env, jclass, jstring keyJ, jstring defJ) {
    if (!keyJ) return defJ;
    const char *key = env->GetStringUTFChars(keyJ, nullptr);
    const char *def = defJ ? env->GetStringUTFChars(defJ, nullptr) : nullptr;
    std::string res = query_prop_for_jni(key, def);
    if (key) env->ReleaseStringUTFChars(keyJ, key);
    if (defJ && def) env->ReleaseStringUTFChars(defJ, def);
    return env->NewStringUTF(res.c_str());
}

inline jint JNICALL jni_native_get_int(JNIEnv *env, jclass, jstring keyJ, jint defV) {
    if (!keyJ) return defV;
    const char *key = env->GetStringUTFChars(keyJ, nullptr);
    std::string res = query_prop_for_jni(key, "");
    if (key) env->ReleaseStringUTFChars(keyJ, key);
    if (res.empty()) return defV;
    char *end = nullptr;
    long v = std::strtol(res.c_str(), &end, 0);
    return (end != res.c_str()) ? static_cast<jint>(v) : defV;
}

inline jlong JNICALL jni_native_get_long(JNIEnv *env, jclass, jstring keyJ, jlong defV) {
    if (!keyJ) return defV;
    const char *key = env->GetStringUTFChars(keyJ, nullptr);
    std::string res = query_prop_for_jni(key, "");
    if (key) env->ReleaseStringUTFChars(keyJ, key);
    if (res.empty()) return defV;
    char *end = nullptr;
    long long v = std::strtoll(res.c_str(), &end, 0);
    return (end != res.c_str()) ? static_cast<jlong>(v) : defV;
}

inline jboolean JNICALL jni_native_get_boolean(JNIEnv *env, jclass, jstring keyJ, jboolean defV) {
    if (!keyJ) return defV;
    const char *key = env->GetStringUTFChars(keyJ, nullptr);
    std::string res = query_prop_for_jni(key, "");
    if (key) env->ReleaseStringUTFChars(keyJ, key);
    if (res.empty()) return defV;
    if (res == "1" || res == "true" || res == "y" || res == "yes" || res == "on") return JNI_TRUE;
    if (res == "0" || res == "false" || res == "n" || res == "no" || res == "off") return JNI_FALSE;
    return defV;
}

inline void register_system_properties_natives(JNIEnv *env) {
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
    if (env->ExceptionCheck()) env->ExceptionClear();
    env->DeleteLocalRef(sp_cls);
}

inline void reflect_java_build_fields(JNIEnv *env) {
    auto set_str = [&](jclass cls, const char *name, const std::string &val) {
        jfieldID fid = env->GetStaticFieldID(cls, name, "Ljava/lang/String;");
        if (env->ExceptionCheck()) { env->ExceptionClear(); return; }
        if (fid) {
            jstring jstr = env->NewStringUTF(val.c_str());
            env->SetStaticObjectField(cls, fid, jstr);
            env->DeleteLocalRef(jstr);
        }
    };

    auto set_int = [&](jclass cls, const char *name, int val) {
        jfieldID fid = env->GetStaticFieldID(cls, name, "I");
        if (env->ExceptionCheck()) { env->ExceptionClear(); return; }
        if (fid) env->SetStaticIntField(cls, fid, static_cast<jint>(val));
    };

    auto set_long = [&](jclass cls, const char *name, long long val) {
        jfieldID fid = env->GetStaticFieldID(cls, name, "J");
        if (env->ExceptionCheck()) { env->ExceptionClear(); return; }
        if (fid) env->SetStaticLongField(cls, fid, static_cast<jlong>(val));
    };

    auto set_str_arr = [&](jclass cls, const char *name, const std::vector<std::string> &items) {
        if (items.empty()) return;
        jfieldID fid = env->GetStaticFieldID(cls, name, "[Ljava/lang/String;");
        if (env->ExceptionCheck()) { env->ExceptionClear(); return; }
        jclass str_cls = env->FindClass("java/lang/String");
        if (!str_cls) { env->ExceptionClear(); return; }
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
    };

    jclass build_cls = env->FindClass("android/os/Build");
    if (build_cls) {
        for (const auto &kv : g_ctx.build_map) {
            if (kv.first == "TIME") {
                long long t = std::atoll(kv.second.c_str());
                if (t > 0) set_long(build_cls, "TIME", t);
            } else if (kv.first == "SUPPORTED_ABIS" ||
                       kv.first == "SUPPORTED_64_BIT_ABIS" ||
                       kv.first == "SUPPORTED_32_BIT_ABIS") {
                set_str_arr(build_cls, kv.first.c_str(), split_str(kv.second, ','));
            } else {
                set_str(build_cls, kv.first.c_str(), kv.second);
            }
        }
        env->DeleteLocalRef(build_cls);
    } else {
        env->ExceptionClear();
    }

    jclass ver_cls = env->FindClass("android/os/Build$VERSION");
    if (ver_cls) {
        for (const auto &kv : g_ctx.version_map) {
            if (kv.first == "SDK_INT" || kv.first == "DEVICE_INITIAL_SDK_INT" || kv.first == "PREVIEW_SDK_INT") {
                int sdk = std::atoi(kv.second.c_str());
                if (sdk >= 0) set_int(ver_cls, kv.first.c_str(), sdk);
            } else {
                set_str(ver_cls, kv.first.c_str(), kv.second);
            }
        }
        env->DeleteLocalRef(ver_cls);
    } else {
        env->ExceptionClear();
    }
}

} // namespace mirage

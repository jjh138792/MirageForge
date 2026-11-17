#pragma once

#include <cstdlib>
#include <jni.h>
#include "mirage_config.hpp"
#include "mirage_logger.hpp"

namespace mirage {

inline void reflect_java_build_fields(JNIEnv *env) {
    if (!g_cfg.build.enabled || !env) return;

    auto set_str = [&](jclass cls, const char *name, const std::string &val) {
        if (val.empty()) return;
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

    // 1. 反射 android.os.Build (含 BOARD, SOC_MODEL, SOC_MANUFACTURER, DISPLAY, HOST, USER, RADIO 等)
    jclass build_cls = env->FindClass("android/os/Build");
    if (build_cls) {
        for (const auto &kv : g_cfg.build.build_map) {
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

    // 2. 反射 android.os.Build$VERSION (含 INCREMENTAL, SECURITY_PATCH, CODENAME, BASE_OS 等)
    jclass ver_cls = env->FindClass("android/os/Build$VERSION");
    if (ver_cls) {
        for (const auto &kv : g_cfg.build.version_map) {
            if (kv.first == "SDK_INT" || kv.first == "DEVICE_INITIAL_SDK_INT" || kv.first == "PREVIEW_SDK_INT") {
                if (g_cfg.build.force_spoof_sdk) {
                    int sdk = std::atoi(kv.second.c_str());
                    if (sdk > 0) set_int(ver_cls, kv.first.c_str(), sdk);
                }
            } else if (kv.first == "SECURITY_PATCH" ||
                       kv.first == "INCREMENTAL" ||
                       kv.first == "CODENAME" ||
                       kv.first == "BASE_OS") {
                set_str(ver_cls, kv.first.c_str(), kv.second);
            } else if (kv.first == "RELEASE" && g_cfg.build.force_spoof_sdk) {
                set_str(ver_cls, "RELEASE", kv.second);
                set_str(ver_cls, "RELEASE_OR_CODENAME", kv.second);
            }
        }
        env->DeleteLocalRef(ver_cls);
    } else {
        env->ExceptionClear();
    }
}

// 按应用独立 DPI 虚拟化引擎：直接修改当前进程 Resources.getSystem() 的 DisplayMetrics 与 Configuration
inline void apply_app_dpi_virtualization(JNIEnv *env) {
    int dpi = g_cfg.build.app_dpi;
    if (!env || !g_cfg.build.enabled || dpi < 120 || dpi > 960) return;

    jclass res_cls = env->FindClass("android/content/res/Resources");
    if (!res_cls) { env->ExceptionClear(); return; }

    jmethodID get_sys_mid = env->GetStaticMethodID(res_cls, "getSystem", "()Landroid/content/res/Resources;");
    if (!get_sys_mid) {
        env->ExceptionClear();
        env->DeleteLocalRef(res_cls);
        return;
    }

    jobject sys_res = env->CallStaticObjectMethod(res_cls, get_sys_mid);
    if (env->ExceptionCheck() || !sys_res) {
        env->ExceptionClear();
        env->DeleteLocalRef(res_cls);
        return;
    }

    float scale = static_cast<float>(dpi) / 160.0f;

    // 1. 修改 DisplayMetrics (densityDpi, density, scaledDensity, xdpi, ydpi)
    jmethodID get_dm_mid = env->GetMethodID(res_cls, "getDisplayMetrics", "()Landroid/util/DisplayMetrics;");
    if (get_dm_mid) {
        jobject dm = env->CallObjectMethod(sys_res, get_dm_mid);
        if (!env->ExceptionCheck() && dm) {
            jclass dm_cls = env->GetObjectClass(dm);
            if (dm_cls) {
                auto set_dm_int = [&](const char *name, int v) {
                    jfieldID f = env->GetFieldID(dm_cls, name, "I");
                    if (!env->ExceptionCheck() && f) env->SetIntField(dm, f, v);
                    else env->ExceptionClear();
                };
                auto set_dm_float = [&](const char *name, float v) {
                    jfieldID f = env->GetFieldID(dm_cls, name, "F");
                    if (!env->ExceptionCheck() && f) env->SetFloatField(dm, f, v);
                    else env->ExceptionClear();
                };
                set_dm_int("densityDpi", dpi);
                set_dm_float("density", scale);
                set_dm_float("scaledDensity", scale);
                set_dm_float("xdpi", static_cast<float>(dpi));
                set_dm_float("ydpi", static_cast<float>(dpi));
                env->DeleteLocalRef(dm_cls);
            }
            env->DeleteLocalRef(dm);
        } else {
            env->ExceptionClear();
        }
    } else {
        env->ExceptionClear();
    }

    // 2. 修改 Configuration.densityDpi
    jmethodID get_cfg_mid = env->GetMethodID(res_cls, "getConfiguration", "()Landroid/content/res/Configuration;");
    if (get_cfg_mid) {
        jobject cfg = env->CallObjectMethod(sys_res, get_cfg_mid);
        if (!env->ExceptionCheck() && cfg) {
            jclass cfg_cls = env->GetObjectClass(cfg);
            if (cfg_cls) {
                jfieldID dpi_fid = env->GetFieldID(cfg_cls, "densityDpi", "I");
                if (!env->ExceptionCheck() && dpi_fid) {
                    env->SetIntField(cfg, dpi_fid, dpi);
                } else {
                    env->ExceptionClear();
                }
                env->DeleteLocalRef(cfg_cls);
            }
            env->DeleteLocalRef(cfg);
        } else {
            env->ExceptionClear();
        }
    } else {
        env->ExceptionClear();
    }

    env->DeleteLocalRef(sys_res);
    env->DeleteLocalRef(res_cls);
    emit_probe("DPI_SPOOF", "DisplayMetrics.densityDpi", std::to_string(dpi));
}

} // namespace mirage

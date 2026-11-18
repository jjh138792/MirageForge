#pragma once

#include <cstdlib>
#include <jni.h>
#include "mirage_config.hpp"
#include "mirage_logger.hpp"

namespace mirage {

// 屏幕最高刷新率欺骗引擎：仅当 max_refresh_rate >= 90 时按需修改 DisplayInfo & Display$Mode
inline void apply_display_refresh_rate_spoof(JNIEnv *env) {
    int target_hz = g_cfg.gl.max_refresh_rate;
    if (!env || !g_cfg.gl.enabled || target_hz < 90 || target_hz > 240) return;

    float fps = static_cast<float>(target_hz);

    jclass dmg_cls = env->FindClass("android/hardware/display/DisplayManagerGlobal");
    if (!dmg_cls) { env->ExceptionClear(); return; }

    jmethodID get_inst = env->GetStaticMethodID(dmg_cls, "getInstance", "()Landroid/hardware/display/DisplayManagerGlobal;");
    if (!get_inst) {
        env->ExceptionClear();
        env->DeleteLocalRef(dmg_cls);
        return;
    }

    jobject dmg = env->CallStaticObjectMethod(dmg_cls, get_inst);
    if (env->ExceptionCheck() || !dmg) {
        env->ExceptionClear();
        env->DeleteLocalRef(dmg_cls);
        return;
    }

    jmethodID get_di = env->GetMethodID(dmg_cls, "getDisplayInfo", "(I)Landroid/view/DisplayInfo;");
    if (get_di) {
        jobject di = env->CallObjectMethod(dmg, get_di, 0);
        if (!env->ExceptionCheck() && di) {
            jclass di_cls = env->GetObjectClass(di);
            if (di_cls) {
                jfieldID ovr_fid = env->GetFieldID(di_cls, "refreshRateOverride", "F");
                if (!env->ExceptionCheck() && ovr_fid) {
                    env->SetFloatField(di, ovr_fid, fps);
                } else {
                    env->ExceptionClear();
                }

                jfieldID rfr_fid = env->GetFieldID(di_cls, "renderFrameRate", "F");
                if (!env->ExceptionCheck() && rfr_fid) {
                    env->SetFloatField(di, rfr_fid, fps);
                } else {
                    env->ExceptionClear();
                }

                // 同步提升 supportedModes 与 appsSupportedModes 中的 Display$Mode.mRefreshRate
                jclass mode_cls = env->FindClass("android/view/Display$Mode");
                jfieldID mode_rr_fid = mode_cls ? env->GetFieldID(mode_cls, "mRefreshRate", "F") : nullptr;
                if (env->ExceptionCheck()) { env->ExceptionClear(); mode_rr_fid = nullptr; }

                auto patch_mode_array = [&](const char *arr_field_name) {
                    if (!mode_rr_fid) return;
                    jfieldID arr_fid = env->GetFieldID(di_cls, arr_field_name, "[Landroid/view/Display$Mode;");
                    if (env->ExceptionCheck() || !arr_fid) { env->ExceptionClear(); return; }
                    auto arr = static_cast<jobjectArray>(env->GetObjectField(di, arr_fid));
                    if (!env->ExceptionCheck() && arr) {
                        jsize len = env->GetArrayLength(arr);
                        for (jsize i = 0; i < len; ++i) {
                            jobject m_obj = env->GetObjectArrayElement(arr, i);
                            if (m_obj) {
                                env->SetFloatField(m_obj, mode_rr_fid, fps);
                                env->DeleteLocalRef(m_obj);
                            }
                        }
                        env->DeleteLocalRef(arr);
                    } else {
                        env->ExceptionClear();
                    }
                };

                patch_mode_array("supportedModes");
                patch_mode_array("appsSupportedModes");

                if (mode_cls) env->DeleteLocalRef(mode_cls);
                env->DeleteLocalRef(di_cls);
            }
            env->DeleteLocalRef(di);
            emit_probe("DISPLAY_FPS", "Display.getRefreshRate", std::to_string(target_hz) + "Hz");
        } else {
            env->ExceptionClear();
        }
    } else {
        env->ExceptionClear();
    }

    env->DeleteLocalRef(dmg);
    env->DeleteLocalRef(dmg_cls);
}

inline void reflect_java_build_fields(JNIEnv *env) {
    if (!env) return;

    if (g_cfg.gl.enabled && g_cfg.gl.max_refresh_rate >= 90) {
        apply_display_refresh_rate_spoof(env);
    }

    if (!g_cfg.build.enabled) return;

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

    // 1. 反射 android.os.Build
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

    // 2. 反射 android.os.Build$VERSION
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

} // namespace mirage

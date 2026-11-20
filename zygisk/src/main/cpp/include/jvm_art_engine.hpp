#pragma once

#include <cstdlib>
#include <atomic>
#include <jni.h>
#include "mirage_config.hpp"
#include "mirage_logger.hpp"

namespace mirage {

inline JavaVM *g_jvm = nullptr;
inline std::atomic<bool> g_post_specialized{false};
inline std::atomic<bool> g_fps_spoof_applied{false};

inline uint32_t g_am_get_memory_info_code = 0;

// 动态读取当前 Android 版本的 IActivityManager$Stub.TRANSACTION_getMemoryInfo 交易号
inline void resolve_am_get_memory_info_code(JNIEnv *env) {
    if (!env || g_am_get_memory_info_code != 0) return;
    jclass stub_cls = env->FindClass("android/app/IActivityManager$Stub");
    if (!stub_cls) {
        env->ExceptionClear();
        return;
    }
    jfieldID fid = env->GetStaticFieldID(stub_cls, "TRANSACTION_getMemoryInfo", "I");
    if (!env->ExceptionCheck() && fid) {
        jint code = env->GetStaticIntField(stub_cls, fid);
        if (code > 0) {
            g_am_get_memory_info_code = static_cast<uint32_t>(code);
        }
    } else {
        env->ExceptionClear();
    }
    env->DeleteLocalRef(stub_cls);
}
// 仅在 postAppSpecialize 之后、且当前已在合法 Java 线程且 DisplayManagerGlobal.sInstance 已存在时修补缓存，
// 绝不在 Zygote 阶段主动触发 Binder IPC
inline void try_lazy_spoof_display_modes() {
    int target_hz = g_cfg.gl.max_refresh_rate;
    if (!g_cfg.gl.enabled || target_hz < 90 || target_hz > 240 || !g_jvm) return;
    if (!g_post_specialized.load(std::memory_order_acquire)) return;
    if (g_fps_spoof_applied.load(std::memory_order_relaxed)) return;

    JNIEnv *env = nullptr;
    if (g_jvm->GetEnv(reinterpret_cast<void **>(&env), JNI_VERSION_1_6) != JNI_OK || !env) {
        return;
    }
    if (env->ExceptionCheck()) return;

    jclass dmg_cls = env->FindClass("android/hardware/display/DisplayManagerGlobal");
    if (!dmg_cls) { env->ExceptionClear(); return; }

    jfieldID sinst_fid = env->GetStaticFieldID(dmg_cls, "sInstance", "Landroid/hardware/display/DisplayManagerGlobal;");
    if (env->ExceptionCheck() || !sinst_fid) {
        env->ExceptionClear();
        env->DeleteLocalRef(dmg_cls);
        return;
    }

    jobject dmg = env->GetStaticObjectField(dmg_cls, sinst_fid);
    if (env->ExceptionCheck() || !dmg) {
        // 应用尚未初始化 DisplayManagerGlobal，等待下次调用再试
        env->ExceptionClear();
        env->DeleteLocalRef(dmg_cls);
        return;
    }

    float fps = static_cast<float>(target_hz);
    bool patched = false;

    jmethodID get_di = env->GetMethodID(dmg_cls, "getDisplayInfo", "(I)Landroid/view/DisplayInfo;");
    if (get_di) {
        jobject di = env->CallObjectMethod(dmg, get_di, 0);
        if (!env->ExceptionCheck() && di) {
            jclass di_cls = env->GetObjectClass(di);
            jclass mode_cls = env->FindClass("android/view/Display$Mode");

            jfieldID mode_rr_fid = mode_cls ? env->GetFieldID(mode_cls, "mRefreshRate", "F") : nullptr;
            if (env->ExceptionCheck()) { env->ExceptionClear(); mode_rr_fid = nullptr; }

            // Android 15+ 新增 mVsyncRate 字段
            jfieldID mode_vsync_fid = mode_cls ? env->GetFieldID(mode_cls, "mVsyncRate", "F") : nullptr;
            if (env->ExceptionCheck()) { env->ExceptionClear(); mode_vsync_fid = nullptr; }

            if (di_cls) {
                jfieldID ovr_fid = env->GetFieldID(di_cls, "refreshRateOverride", "F");
                if (!env->ExceptionCheck() && ovr_fid) {
                    env->SetFloatField(di, ovr_fid, fps);
                    patched = true;
                } else {
                    env->ExceptionClear();
                }

                auto patch_mode_array = [&](const char *arr_name) {
                    if (!mode_rr_fid) return;
                    jfieldID arr_fid = env->GetFieldID(di_cls, arr_name, "[Landroid/view/Display$Mode;");
                    if (env->ExceptionCheck() || !arr_fid) { env->ExceptionClear(); return; }
                    auto arr = static_cast<jobjectArray>(env->GetObjectField(di, arr_fid));
                    if (!env->ExceptionCheck() && arr) {
                        jsize len = env->GetArrayLength(arr);
                        for (jsize i = 0; i < len; ++i) {
                            jobject m_obj = env->GetObjectArrayElement(arr, i);
                            if (m_obj) {
                                env->SetFloatField(m_obj, mode_rr_fid, fps);
                                if (mode_vsync_fid) env->SetFloatField(m_obj, mode_vsync_fid, fps);
                                env->DeleteLocalRef(m_obj);
                                patched = true;
                            }
                        }
                        env->DeleteLocalRef(arr);
                    } else {
                        env->ExceptionClear();
                    }
                };

                patch_mode_array("supportedModes");
                patch_mode_array("appsSupportedModes");
                env->DeleteLocalRef(di_cls);
            }
            if (mode_cls) env->DeleteLocalRef(mode_cls);
            env->DeleteLocalRef(di);
        } else {
            env->ExceptionClear();
        }
    } else {
        env->ExceptionClear();
    }

    env->DeleteLocalRef(dmg);
    env->DeleteLocalRef(dmg_cls);

    if (patched) {
        g_fps_spoof_applied.store(true, std::memory_order_release);
        emit_probe("DISPLAY_FPS", "DisplayInfo.supportedModes", std::to_string(target_hz) + "Hz");
    }
}

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

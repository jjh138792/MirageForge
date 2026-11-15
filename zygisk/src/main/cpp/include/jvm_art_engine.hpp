#pragma once

#include <cstdlib>
#include <atomic>
#include <unistd.h>
#include <jni.h>
#include "mirage_config.hpp"
#include "mirage_logger.hpp"

namespace mirage {

inline JavaVM *g_jvm = nullptr;
inline std::atomic<int> g_dpi_sync_remaining{0};
inline pid_t g_main_tid = 0;

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

// 对任意 Resources 实例修改其 DisplayMetrics 与 Configuration（含 sw/w/h dp 重算），并调用 updateConfiguration
inline void patch_single_resources_dpi(JNIEnv *env, jclass res_cls, jobject res_obj, int dpi) {
    if (!env || !res_cls || !res_obj) return;
    float scale = static_cast<float>(dpi) / 160.0f;

    jmethodID get_dm_mid = env->GetMethodID(res_cls, "getDisplayMetrics", "()Landroid/util/DisplayMetrics;");
    jmethodID get_cfg_mid = env->GetMethodID(res_cls, "getConfiguration", "()Landroid/content/res/Configuration;");
    jmethodID upd_cfg_mid = env->GetMethodID(
        res_cls, "updateConfiguration",
        "(Landroid/content/res/Configuration;Landroid/util/DisplayMetrics;)V");
    if (env->ExceptionCheck()) { env->ExceptionClear(); return; }

    jobject dm = get_dm_mid ? env->CallObjectMethod(res_obj, get_dm_mid) : nullptr;
    if (env->ExceptionCheck()) { env->ExceptionClear(); dm = nullptr; }

    jobject cfg = get_cfg_mid ? env->CallObjectMethod(res_obj, get_cfg_mid) : nullptr;
    if (env->ExceptionCheck()) { env->ExceptionClear(); cfg = nullptr; }

    if (dm) {
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

            if (cfg) {
                jclass cfg_cls = env->GetObjectClass(cfg);
                if (cfg_cls) {
                    jfieldID w_px_fid = env->GetFieldID(dm_cls, "widthPixels", "I");
                    jfieldID h_px_fid = env->GetFieldID(dm_cls, "heightPixels", "I");
                    int w_px = (!env->ExceptionCheck() && w_px_fid) ? env->GetIntField(dm, w_px_fid) : 0;
                    int h_px = (!env->ExceptionCheck() && h_px_fid) ? env->GetIntField(dm, h_px_fid) : 0;
                    env->ExceptionClear();

                    jfieldID dpi_fid = env->GetFieldID(cfg_cls, "densityDpi", "I");
                    if (!env->ExceptionCheck() && dpi_fid) env->SetIntField(cfg, dpi_fid, dpi);
                    else env->ExceptionClear();

                    if (w_px > 0 && h_px > 0 && scale > 0.1f) {
                        int new_w_dp = static_cast<int>(static_cast<float>(w_px) / scale + 0.5f);
                        int new_h_dp = static_cast<int>(static_cast<float>(h_px) / scale + 0.5f);
                        int new_sw_dp = (new_w_dp < new_h_dp) ? new_w_dp : new_h_dp;

                        jfieldID w_dp_fid = env->GetFieldID(cfg_cls, "screenWidthDp", "I");
                        if (!env->ExceptionCheck() && w_dp_fid) env->SetIntField(cfg, w_dp_fid, new_w_dp);
                        else env->ExceptionClear();

                        jfieldID h_dp_fid = env->GetFieldID(cfg_cls, "screenHeightDp", "I");
                        if (!env->ExceptionCheck() && h_dp_fid) env->SetIntField(cfg, h_dp_fid, new_h_dp);
                        else env->ExceptionClear();

                        jfieldID sw_dp_fid = env->GetFieldID(cfg_cls, "smallestScreenWidthDp", "I");
                        if (!env->ExceptionCheck() && sw_dp_fid) env->SetIntField(cfg, sw_dp_fid, new_sw_dp);
                        else env->ExceptionClear();
                    }
                    env->DeleteLocalRef(cfg_cls);
                }
            }
            env->DeleteLocalRef(dm_cls);
        }
    }

    if (upd_cfg_mid && cfg && dm) {
        env->CallVoidMethod(res_obj, upd_cfg_mid, cfg, dm);
        if (env->ExceptionCheck()) env->ExceptionClear();
    }

    if (cfg) env->DeleteLocalRef(cfg);
    if (dm) env->DeleteLocalRef(dm);
}

inline bool apply_app_dpi_virtualization(JNIEnv *env) {
    int dpi = g_cfg.build.app_dpi;
    if (!env || !g_cfg.build.enabled || dpi < 120 || dpi > 960) return false;

    // 1. 覆写 DisplayMetrics 静态基准常量 DENSITY_DEVICE 与 DENSITY_DEVICE_STABLE
    jclass dm_static_cls = env->FindClass("android/util/DisplayMetrics");
    if (dm_static_cls) {
        jfieldID dev_fid = env->GetStaticFieldID(dm_static_cls, "DENSITY_DEVICE", "I");
        if (!env->ExceptionCheck() && dev_fid) env->SetStaticIntField(dm_static_cls, dev_fid, dpi);
        else env->ExceptionClear();

        jfieldID stb_fid = env->GetStaticFieldID(dm_static_cls, "DENSITY_DEVICE_STABLE", "I");
        if (!env->ExceptionCheck() && stb_fid) env->SetStaticIntField(dm_static_cls, stb_fid, dpi);
        else env->ExceptionClear();

        env->DeleteLocalRef(dm_static_cls);
    } else {
        env->ExceptionClear();
    }

    jclass res_cls = env->FindClass("android/content/res/Resources");
    if (!res_cls) { env->ExceptionClear(); return false; }

    // 2. 更新 Resources.getSystem()
    jmethodID get_sys_mid = env->GetStaticMethodID(res_cls, "getSystem", "()Landroid/content/res/Resources;");
    if (!env->ExceptionCheck() && get_sys_mid) {
        jobject sys_res = env->CallStaticObjectMethod(res_cls, get_sys_mid);
        if (!env->ExceptionCheck() && sys_res) {
            patch_single_resources_dpi(env, res_cls, sys_res, dpi);
            env->DeleteLocalRef(sys_res);
        } else {
            env->ExceptionClear();
        }
    } else {
        env->ExceptionClear();
    }

    // 3. 检查 ActivityThread.currentApplication() 是否已在 handleBindApplication 后创建
    bool app_patched = false;
    jclass at_cls = env->FindClass("android/app/ActivityThread");
    if (at_cls) {
        jmethodID cur_app_mid = env->GetStaticMethodID(at_cls, "currentApplication", "()Landroid/app/Application;");
        if (!env->ExceptionCheck() && cur_app_mid) {
            jobject app_obj = env->CallStaticObjectMethod(at_cls, cur_app_mid);
            if (!env->ExceptionCheck() && app_obj) {
                jclass ctx_cls = env->GetObjectClass(app_obj);
                if (ctx_cls) {
                    jmethodID get_res_mid = env->GetMethodID(ctx_cls, "getResources", "()Landroid/content/res/Resources;");
                    if (!env->ExceptionCheck() && get_res_mid) {
                        jobject app_res = env->CallObjectMethod(app_obj, get_res_mid);
                        if (!env->ExceptionCheck() && app_res) {
                            patch_single_resources_dpi(env, res_cls, app_res, dpi);
                            app_patched = true;
                            env->DeleteLocalRef(app_res);
                        } else {
                            env->ExceptionClear();
                        }
                    } else {
                        env->ExceptionClear();
                    }
                    env->DeleteLocalRef(ctx_cls);
                }
                env->DeleteLocalRef(app_obj);
            } else {
                env->ExceptionClear();
            }
        } else {
            env->ExceptionClear();
        }
        env->DeleteLocalRef(at_cls);
    } else {
        env->ExceptionClear();
    }

    env->DeleteLocalRef(res_cls);
    if (app_patched) {
        emit_probe("DPI_SPOOF", "Application.Resources.densityDpi", std::to_string(dpi));
    }
    return app_patched;
}

// 在主线程 handleBindApplication 完成后自动执行二次 DPI 锁定，防止被 AMS 初始配置覆盖
inline void maybe_sync_dpi_post_bind() noexcept {
    int rem = g_dpi_sync_remaining.load(std::memory_order_relaxed);
    if (__builtin_expect(rem <= 0, 1)) return;
    if (!g_jvm || g_cfg.build.app_dpi < 120 || gettid() != g_main_tid) return;

    JNIEnv *env = nullptr;
    if (g_jvm->GetEnv(reinterpret_cast<void **>(&env), JNI_VERSION_1_6) != JNI_OK || !env) {
        return;
    }
    if (env->ExceptionCheck()) return;

    if (apply_app_dpi_virtualization(env)) {
        g_dpi_sync_remaining.fetch_sub(1, std::memory_order_relaxed);
    }
}

} // namespace mirage

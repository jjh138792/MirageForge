#pragma once

#include <cstdlib>
#include <atomic>
#include <jni.h>
#include "mirage_config.hpp"
#include "mirage_logger.hpp"

namespace mirage {

inline JavaVM *g_jvm = nullptr;
inline std::atomic<bool> g_app_dpi_locked_on_app{false};

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

// 对任意 android.content.res.Resources 对象安全应用目标 DPI
inline void apply_dpi_to_resources_obj(JNIEnv *env, jclass res_cls, jobject res_obj, int dpi) {
    if (!env || !res_cls || !res_obj) return;
    float scale = static_cast<float>(dpi) / 160.0f;

    jmethodID get_dm_mid = env->GetMethodID(res_cls, "getDisplayMetrics", "()Landroid/util/DisplayMetrics;");
    jmethodID get_cfg_mid = env->GetMethodID(res_cls, "getConfiguration", "()Landroid/content/res/Configuration;");
    jmethodID upd_cfg_mid = env->GetMethodID(res_cls, "updateConfiguration", "(Landroid/content/res/Configuration;Landroid/util/DisplayMetrics;)V");
    if (env->ExceptionCheck()) { env->ExceptionClear(); return; }

    jobject dm = get_dm_mid ? env->CallObjectMethod(res_obj, get_dm_mid) : nullptr;
    if (env->ExceptionCheck()) { env->ExceptionClear(); dm = nullptr; }

    jobject cfg = get_cfg_mid ? env->CallObjectMethod(res_obj, get_cfg_mid) : nullptr;
    if (env->ExceptionCheck()) { env->ExceptionClear(); cfg = nullptr; }

    if (dm) {
        jclass dm_cls = env->GetObjectClass(dm);
        if (dm_cls) {
            auto set_i = [&](const char *n, int v) {
                jfieldID f = env->GetFieldID(dm_cls, n, "I");
                if (!env->ExceptionCheck() && f) env->SetIntField(dm, f, v);
                else env->ExceptionClear();
            };
            auto set_f = [&](const char *n, float v) {
                jfieldID f = env->GetFieldID(dm_cls, n, "F");
                if (!env->ExceptionCheck() && f) env->SetFloatField(dm, f, v);
                else env->ExceptionClear();
            };
            set_i("densityDpi", dpi);
            set_f("density", scale);
            set_f("scaledDensity", scale);
            set_f("xdpi", static_cast<float>(dpi));
            set_f("ydpi", static_cast<float>(dpi));
            env->DeleteLocalRef(dm_cls);
        }
    }

    if (cfg) {
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
    }

    if (upd_cfg_mid && cfg && dm) {
        env->CallVoidMethod(res_obj, upd_cfg_mid, cfg, dm);
        if (env->ExceptionCheck()) env->ExceptionClear();
    }

    if (dm) env->DeleteLocalRef(dm);
    if (cfg) env->DeleteLocalRef(cfg);
}

// 阶段 1 (postAppSpecialize 调用)：修改 DisplayMetrics.DENSITY_DEVICE 静态基准常量与系统 Resources
inline void apply_app_dpi_virtualization(JNIEnv *env) {
    int dpi = g_cfg.build.app_dpi;
    if (!env || !g_cfg.build.enabled || dpi < 120 || dpi > 960) return;

    if (!g_jvm) {
        env->GetJavaVM(&g_jvm);
    }

    // 1. 篡改 DisplayMetrics.DENSITY_DEVICE 与 DENSITY_DEVICE_STABLE 静态字段
    jclass dm_cls = env->FindClass("android/util/DisplayMetrics");
    if (dm_cls) {
        jfieldID dev_fid = env->GetStaticFieldID(dm_cls, "DENSITY_DEVICE", "I");
        if (!env->ExceptionCheck() && dev_fid) env->SetStaticIntField(dm_cls, dev_fid, dpi);
        else env->ExceptionClear();

        jfieldID stb_fid = env->GetStaticFieldID(dm_cls, "DENSITY_DEVICE_STABLE", "I");
        if (!env->ExceptionCheck() && stb_fid) env->SetStaticIntField(dm_cls, stb_fid, dpi);
        else env->ExceptionClear();

        env->DeleteLocalRef(dm_cls);
    } else {
        env->ExceptionClear();
    }

    // 2. 更新 Resources.getSystem()
    jclass res_cls = env->FindClass("android/content/res/Resources");
    if (res_cls) {
        jmethodID get_sys_mid = env->GetStaticMethodID(res_cls, "getSystem", "()Landroid/content/res/Resources;");
        if (!env->ExceptionCheck() && get_sys_mid) {
            jobject sys_res = env->CallStaticObjectMethod(res_cls, get_sys_mid);
            if (!env->ExceptionCheck() && sys_res) {
                apply_dpi_to_resources_obj(env, res_cls, sys_res, dpi);
                env->DeleteLocalRef(sys_res);
            } else {
                env->ExceptionClear();
            }
        } else {
            env->ExceptionClear();
        }
        env->DeleteLocalRef(res_cls);
    } else {
        env->ExceptionClear();
    }

    emit_probe("DPI_SPOOF", "DisplayMetrics.DENSITY_DEVICE", std::to_string(dpi));
}

// 阶段 2 (运行期当 Application 绑定完成后自动触发一次)：锁定 ActivityThread.currentApplication().getResources()
inline void try_lock_application_dpi_if_needed() {
    int dpi = g_cfg.build.app_dpi;
    if (!g_cfg.build.enabled || dpi < 120 || dpi > 960 || !g_jvm) return;
    if (__builtin_expect(g_app_dpi_locked_on_app.load(std::memory_order_relaxed), 1)) return;

    JNIEnv *env = nullptr;
    if (g_jvm->GetEnv(reinterpret_cast<void **>(&env), JNI_VERSION_1_6) != JNI_OK || !env) {
        return;
    }
    if (env->ExceptionCheck()) return;

    jclass at_cls = env->FindClass("android/app/ActivityThread");
    if (!at_cls) { env->ExceptionClear(); return; }

    jmethodID cur_app_mid = env->GetStaticMethodID(at_cls, "currentApplication", "()Landroid/app/Application;");
    if (!cur_app_mid) {
        env->ExceptionClear();
        env->DeleteLocalRef(at_cls);
        return;
    }

    jobject app_obj = env->CallStaticObjectMethod(at_cls, cur_app_mid);
    if (env->ExceptionCheck() || !app_obj) {
        env->ExceptionClear();
        env->DeleteLocalRef(at_cls);
        return;
    }

    // 此时 ActivityThread.handleBindApplication 已完成，锁定 Application 的 Resources 与系统 Resources！
    g_app_dpi_locked_on_app.store(true, std::memory_order_release);

    jclass app_cls = env->GetObjectClass(app_obj);
    jclass res_cls = env->FindClass("android/content/res/Resources");
    if (app_cls && res_cls) {
        jmethodID get_res_mid = env->GetMethodID(app_cls, "getResources", "()Landroid/content/res/Resources;");
        if (!env->ExceptionCheck() && get_res_mid) {
            jobject app_res = env->CallObjectMethod(app_obj, get_res_mid);
            if (!env->ExceptionCheck() && app_res) {
                apply_dpi_to_resources_obj(env, res_cls, app_res, dpi);
                env->DeleteLocalRef(app_res);
            } else {
                env->ExceptionClear();
            }
        } else {
            env->ExceptionClear();
        }

        jmethodID get_sys_mid = env->GetStaticMethodID(res_cls, "getSystem", "()Landroid/content/res/Resources;");
        if (!env->ExceptionCheck() && get_sys_mid) {
            jobject sys_res = env->CallStaticObjectMethod(res_cls, get_sys_mid);
            if (!env->ExceptionCheck() && sys_res) {
                apply_dpi_to_resources_obj(env, res_cls, sys_res, dpi);
                env->DeleteLocalRef(sys_res);
            } else {
                env->ExceptionClear();
            }
        } else {
            env->ExceptionClear();
        }
    }

    if (app_cls) env->DeleteLocalRef(app_cls);
    if (res_cls) env->DeleteLocalRef(res_cls);
    env->DeleteLocalRef(app_obj);
    env->DeleteLocalRef(at_cls);
}

} // namespace mirage

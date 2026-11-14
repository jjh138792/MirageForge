#pragma once

#include <cstdlib>
#include <atomic>
#include <unistd.h>
#include <pthread.h>
#include <jni.h>
#include "mirage_config.hpp"
#include "mirage_logger.hpp"

namespace mirage {

inline std::atomic<bool> g_dpi_watcher_started{false};

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

inline void patch_display_metrics_fields_only(JNIEnv *env, jobject dm, int dpi) {
    if (!env || !dm) return;
    jclass dm_cls = env->GetObjectClass(dm);
    if (!dm_cls) { env->ExceptionClear(); return; }

    float scale = static_cast<float>(dpi) / 160.0f;

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
    set_dm_int("nonCompatDensityDpi", dpi);
    set_dm_float("density", scale);
    set_dm_float("nonCompatDensity", scale);
    set_dm_float("scaledDensity", scale);
    set_dm_float("nonCompatScaledDensity", scale);
    set_dm_float("xdpi", static_cast<float>(dpi));
    set_dm_float("ydpi", static_cast<float>(dpi));
    set_dm_float("nonCompatXdpi", static_cast<float>(dpi));
    set_dm_float("nonCompatYdpi", static_cast<float>(dpi));

    env->DeleteLocalRef(dm_cls);
}

inline void patch_configuration_fields_only(JNIEnv *env, jobject cfg, int dpi) {
    if (!env || !cfg) return;
    jclass cfg_cls = env->GetObjectClass(cfg);
    if (!cfg_cls) { env->ExceptionClear(); return; }

    jfieldID dpi_fid = env->GetFieldID(cfg_cls, "densityDpi", "I");
    if (!env->ExceptionCheck() && dpi_fid) {
        env->SetIntField(cfg, dpi_fid, dpi);
    } else {
        env->ExceptionClear();
    }
    env->DeleteLocalRef(cfg_cls);
}

inline void patch_resources_fields_only(JNIEnv *env, jobject res_obj, int dpi) {
    if (!env || !res_obj) return;
    jclass res_cls = env->GetObjectClass(res_obj);
    if (!res_cls) { env->ExceptionClear(); return; }

    jmethodID get_dm_mid = env->GetMethodID(res_cls, "getDisplayMetrics", "()Landroid/util/DisplayMetrics;");
    if (!env->ExceptionCheck() && get_dm_mid) {
        jobject dm = env->CallObjectMethod(res_obj, get_dm_mid);
        if (!env->ExceptionCheck() && dm) {
            patch_display_metrics_fields_only(env, dm, dpi);
            env->DeleteLocalRef(dm);
        } else {
            env->ExceptionClear();
        }
    } else {
        env->ExceptionClear();
    }

    jmethodID get_cfg_mid = env->GetMethodID(res_cls, "getConfiguration", "()Landroid/content/res/Configuration;");
    if (!env->ExceptionCheck() && get_cfg_mid) {
        jobject cfg = env->CallObjectMethod(res_obj, get_cfg_mid);
        if (!env->ExceptionCheck() && cfg) {
            patch_configuration_fields_only(env, cfg, dpi);
            env->DeleteLocalRef(cfg);
        } else {
            env->ExceptionClear();
        }
    } else {
        env->ExceptionClear();
    }

    env->DeleteLocalRef(res_cls);
}

// 同步阶段仅修改静态常量与 Resources.getSystem()，绝不发起任何 Binder 调用
inline void apply_early_dpi_fields_safe(JNIEnv *env, int dpi) {
    if (!env || dpi < 120 || dpi > 960) return;

    jclass dm_static_cls = env->FindClass("android/util/DisplayMetrics");
    if (dm_static_cls) {
        jfieldID f_dev = env->GetStaticFieldID(dm_static_cls, "DENSITY_DEVICE", "I");
        if (!env->ExceptionCheck() && f_dev) env->SetStaticIntField(dm_static_cls, f_dev, dpi);
        else env->ExceptionClear();

        jfieldID f_stable = env->GetStaticFieldID(dm_static_cls, "DENSITY_DEVICE_STABLE", "I");
        if (!env->ExceptionCheck() && f_stable) env->SetStaticIntField(dm_static_cls, f_stable, dpi);
        else env->ExceptionClear();

        env->DeleteLocalRef(dm_static_cls);
    } else {
        env->ExceptionClear();
    }

    jclass res_cls = env->FindClass("android/content/res/Resources");
    if (res_cls) {
        jmethodID get_sys_mid = env->GetStaticMethodID(res_cls, "getSystem", "()Landroid/content/res/Resources;");
        if (!env->ExceptionCheck() && get_sys_mid) {
            jobject sys_res = env->CallStaticObjectMethod(res_cls, get_sys_mid);
            if (!env->ExceptionCheck() && sys_res) {
                patch_resources_fields_only(env, sys_res, dpi);
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
}

inline void apply_post_bind_dpi_fields(JNIEnv *env, int dpi) {
    apply_early_dpi_fields_safe(env, dpi);

    // 1. 修改 currentApplication().getResources()
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
                            patch_resources_fields_only(env, app_res, dpi);
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

    // 2. 修改 DisplayManagerGlobal 内部已缓存的 mDisplayInfoCache (不调用 getDisplayInfo Binder IPC)
    jclass dmg_cls = env->FindClass("android/hardware/display/DisplayManagerGlobal");
    if (dmg_cls) {
        jmethodID get_inst_mid = env->GetStaticMethodID(
            dmg_cls, "getInstance", "()Landroid/hardware/display/DisplayManagerGlobal;");
        if (!env->ExceptionCheck() && get_inst_mid) {
            jobject dmg_obj = env->CallStaticObjectMethod(dmg_cls, get_inst_mid);
            if (!env->ExceptionCheck() && dmg_obj) {
                jmethodID get_di_mid = env->GetMethodID(dmg_cls, "getDisplayInfo", "(I)Landroid/view/DisplayInfo;");
                if (!env->ExceptionCheck() && get_di_mid) {
                    jobject di_obj = env->CallObjectMethod(dmg_obj, get_di_mid, 0);
                    if (!env->ExceptionCheck() && di_obj) {
                        jclass di_cls = env->GetObjectClass(di_obj);
                        if (di_cls) {
                            jfieldID log_dpi_fid = env->GetFieldID(di_cls, "logicalDensityDpi", "I");
                            if (!env->ExceptionCheck() && log_dpi_fid) env->SetIntField(di_obj, log_dpi_fid, dpi);
                            else env->ExceptionClear();

                            jfieldID xdpi_fid = env->GetFieldID(di_cls, "physicalXDpi", "F");
                            if (!env->ExceptionCheck() && xdpi_fid) env->SetFloatField(di_obj, xdpi_fid, static_cast<float>(dpi));
                            else env->ExceptionClear();

                            jfieldID ydpi_fid = env->GetFieldID(di_cls, "physicalYDpi", "F");
                            if (!env->ExceptionCheck() && ydpi_fid) env->SetFloatField(di_obj, ydpi_fid, static_cast<float>(dpi));
                            else env->ExceptionClear();

                            env->DeleteLocalRef(di_cls);
                        }
                        env->DeleteLocalRef(di_obj);
                    } else {
                        env->ExceptionClear();
                    }
                } else {
                    env->ExceptionClear();
                }
                env->DeleteLocalRef(dmg_obj);
            } else {
                env->ExceptionClear();
            }
        } else {
            env->ExceptionClear();
        }
        env->DeleteLocalRef(dmg_cls);
    } else {
        env->ExceptionClear();
    }
}

struct DpiWatcherArgs {
    JavaVM *vm;
    int target_dpi;
};

inline void *dpi_post_bind_watcher_thread(void *raw_arg) {
    auto *args = static_cast<DpiWatcherArgs *>(raw_arg);
    if (!args || !args->vm) {
        delete args;
        return nullptr;
    }

    JavaVM *vm = args->vm;
    int dpi = args->target_dpi;
    delete args;

    // 等待 120ms 确保进程已完成 Zygote 派生与 handleBindApplication 初始化
    usleep(120000);

    JNIEnv *env = nullptr;
    JavaVMAttachArgs attach_args{JNI_VERSION_1_6, "MirageDpiSync", nullptr};
    if (vm->AttachCurrentThreadAsDaemon(&env, &attach_args) != JNI_OK || !env) {
        return nullptr;
    }

    constexpr useconds_t kDelaysUs[] = {10000, 150000, 400000, 900000, 1600000};
    for (useconds_t d : kDelaysUs) {
        usleep(d);
        apply_post_bind_dpi_fields(env, dpi);
    }

    vm->DetachCurrentThread();
    return nullptr;
}

inline void apply_app_dpi_virtualization(JNIEnv *env, bool allow_background_sync = false) {
    int dpi = g_cfg.build.app_dpi;
    if (!env || !g_cfg.build.enabled || dpi < 120 || dpi > 960) return;

    apply_early_dpi_fields_safe(env, dpi);

    if (allow_background_sync) {
        bool expected = false;
        if (g_dpi_watcher_started.compare_exchange_strong(expected, true)) {
            JavaVM *vm = nullptr;
            if (env->GetJavaVM(&vm) == JNI_OK && vm) {
                auto *args = new DpiWatcherArgs{vm, dpi};
                pthread_t tid;
                if (pthread_create(&tid, nullptr, dpi_post_bind_watcher_thread, args) == 0) {
                    pthread_detach(tid);
                } else {
                    delete args;
                }
            }
            emit_probe("DPI_SPOOF", "DisplayMetrics.densityDpi", std::to_string(dpi));
        }
    }
}

} // namespace mirage

#pragma once

#include <cstdlib>
#include <unistd.h>
#include <pthread.h>
#include <jni.h>
#include "mirage_config.hpp"
#include "mirage_logger.hpp"

namespace mirage {

inline JavaVM *g_java_vm = nullptr;

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

inline void patch_resources_object_dpi(JNIEnv *env, jobject res_obj, int dpi) {
    if (!env || !res_obj) return;
    jclass res_cls = env->GetObjectClass(res_obj);
    if (!res_cls) { env->ExceptionClear(); return; }

    float scale = static_cast<float>(dpi) / 160.0f;

    jmethodID get_dm_mid = env->GetMethodID(res_cls, "getDisplayMetrics", "()Landroid/util/DisplayMetrics;");
    jmethodID get_cfg_mid = env->GetMethodID(res_cls, "getConfiguration", "()Landroid/content/res/Configuration;");
    jmethodID upd_mid = env->GetMethodID(res_cls, "updateConfiguration",
                                         "(Landroid/content/res/Configuration;Landroid/util/DisplayMetrics;)V");
    env->ExceptionClear();

    jobject dm = get_dm_mid ? env->CallObjectMethod(res_obj, get_dm_mid) : nullptr;
    if (env->ExceptionCheck()) { env->ExceptionClear(); dm = nullptr; }

    jobject cfg = get_cfg_mid ? env->CallObjectMethod(res_obj, get_cfg_mid) : nullptr;
    if (env->ExceptionCheck()) { env->ExceptionClear(); cfg = nullptr; }

    if (dm) {
        jclass dm_cls = env->GetObjectClass(dm);
        if (dm_cls) {
            auto set_dm_i = [&](const char *n, int v) {
                jfieldID f = env->GetFieldID(dm_cls, n, "I");
                if (!env->ExceptionCheck() && f) env->SetIntField(dm, f, v);
                else env->ExceptionClear();
            };
            auto set_dm_f = [&](const char *n, float v) {
                jfieldID f = env->GetFieldID(dm_cls, n, "F");
                if (!env->ExceptionCheck() && f) env->SetFloatField(dm, f, v);
                else env->ExceptionClear();
            };
            set_dm_i("densityDpi", dpi);
            set_dm_i("nonCompatDensityDpi", dpi);
            set_dm_f("density", scale);
            set_dm_f("scaledDensity", scale);
            set_dm_f("nonCompatDensity", scale);
            set_dm_f("nonCompatScaledDensity", scale);
            set_dm_f("xdpi", static_cast<float>(dpi));
            set_dm_f("ydpi", static_cast<float>(dpi));
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

    if (upd_mid && cfg && dm) {
        env->CallVoidMethod(res_obj, upd_mid, cfg, dm);
        if (env->ExceptionCheck()) env->ExceptionClear();
    }

    if (dm) env->DeleteLocalRef(dm);
    if (cfg) env->DeleteLocalRef(cfg);
    env->DeleteLocalRef(res_cls);
}

inline void apply_app_dpi_virtualization(JNIEnv *env) {
    int dpi = g_cfg.build.app_dpi;
    if (!env || !g_cfg.build.enabled || dpi < 120 || dpi > 960) return;

    // 1. 覆写 DisplayMetrics.DENSITY_DEVICE 与 DENSITY_DEVICE_STABLE 静态基准值
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

    // 2. 覆写 Bitmap.sDefaultDensity
    jclass bmp_cls = env->FindClass("android/graphics/Bitmap");
    if (bmp_cls) {
        jfieldID def_fid = env->GetStaticFieldID(bmp_cls, "sDefaultDensity", "I");
        if (!env->ExceptionCheck() && def_fid) env->SetStaticIntField(bmp_cls, def_fid, dpi);
        else env->ExceptionClear();
        env->DeleteLocalRef(bmp_cls);
    } else {
        env->ExceptionClear();
    }

    // 3. 更新 Resources.getSystem()
    jclass res_cls = env->FindClass("android/content/res/Resources");
    if (res_cls) {
        jmethodID get_sys_mid = env->GetStaticMethodID(res_cls, "getSystem", "()Landroid/content/res/Resources;");
        if (!env->ExceptionCheck() && get_sys_mid) {
            jobject sys_res = env->CallStaticObjectMethod(res_cls, get_sys_mid);
            if (!env->ExceptionCheck() && sys_res) {
                patch_resources_object_dpi(env, sys_res, dpi);
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

    // 4. 更新 ActivityThread.currentActivityThread() 及已绑定的 Application Resources
    jclass at_cls = env->FindClass("android/app/ActivityThread");
    if (at_cls) {
        jmethodID cur_at_mid = env->GetStaticMethodID(at_cls, "currentActivityThread", "()Landroid/app/ActivityThread;");
        if (!env->ExceptionCheck() && cur_at_mid) {
            jobject at_obj = env->CallStaticObjectMethod(at_cls, cur_at_mid);
            if (!env->ExceptionCheck() && at_obj) {
                jfieldID cur_dpi_fid = env->GetFieldID(at_cls, "mCurDefaultDisplayDpi", "I");
                if (!env->ExceptionCheck() && cur_dpi_fid) env->SetIntField(at_obj, cur_dpi_fid, dpi);
                else env->ExceptionClear();

                jmethodID get_app_mid = env->GetMethodID(at_cls, "getApplication", "()Landroid/app/Application;");
                if (!env->ExceptionCheck() && get_app_mid) {
                    jobject app_obj = env->CallObjectMethod(at_obj, get_app_mid);
                    if (!env->ExceptionCheck() && app_obj) {
                        jclass ctx_cls = env->GetObjectClass(app_obj);
                        if (ctx_cls) {
                            jmethodID get_res_mid = env->GetMethodID(ctx_cls, "getResources", "()Landroid/content/res/Resources;");
                            if (!env->ExceptionCheck() && get_res_mid) {
                                jobject app_res = env->CallObjectMethod(app_obj, get_res_mid);
                                if (!env->ExceptionCheck() && app_res) {
                                    patch_resources_object_dpi(env, app_res, dpi);
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
                env->DeleteLocalRef(at_obj);
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

    // 5. 更新 ResourcesManager.getInstance().getConfiguration()
    jclass rm_cls = env->FindClass("android/app/ResourcesManager");
    if (rm_cls) {
        jmethodID get_inst_mid = env->GetStaticMethodID(rm_cls, "getInstance", "()Landroid/app/ResourcesManager;");
        if (!env->ExceptionCheck() && get_inst_mid) {
            jobject rm_obj = env->CallStaticObjectMethod(rm_cls, get_inst_mid);
            if (!env->ExceptionCheck() && rm_obj) {
                jmethodID get_cfg_mid = env->GetMethodID(rm_cls, "getConfiguration", "()Landroid/content/res/Configuration;");
                if (!env->ExceptionCheck() && get_cfg_mid) {
                    jobject rm_cfg = env->CallObjectMethod(rm_obj, get_cfg_mid);
                    if (!env->ExceptionCheck() && rm_cfg) {
                        jclass cfg_cls = env->GetObjectClass(rm_cfg);
                        if (cfg_cls) {
                            jfieldID dpi_fid = env->GetFieldID(cfg_cls, "densityDpi", "I");
                            if (!env->ExceptionCheck() && dpi_fid) env->SetIntField(rm_cfg, dpi_fid, dpi);
                            else env->ExceptionClear();
                            env->DeleteLocalRef(cfg_cls);
                        }
                        env->DeleteLocalRef(rm_cfg);
                    } else {
                        env->ExceptionClear();
                    }
                } else {
                    env->ExceptionClear();
                }
                env->DeleteLocalRef(rm_obj);
            } else {
                env->ExceptionClear();
            }
        } else {
            env->ExceptionClear();
        }
        env->DeleteLocalRef(rm_cls);
    } else {
        env->ExceptionClear();
    }
}

// 后台分阶段 DPI 守护线程：在 handleBindApplication 与首个 Activity 创建后自动锁定目标 DPI
inline void *dpi_enforcer_thread(void *) {
    if (!g_java_vm) return nullptr;
    constexpr useconds_t kDelaysUs[] = {60000, 180000, 450000, 1000000, 2200000};
    for (useconds_t delay : kDelaysUs) {
        usleep(delay);
        JNIEnv *env = nullptr;
        if (g_java_vm->AttachCurrentThread(&env, nullptr) == JNI_OK && env) {
            apply_app_dpi_virtualization(env);
            g_java_vm->DetachCurrentThread();
        }
    }
    return nullptr;
}

inline void start_app_dpi_enforcer(JNIEnv *env) {
    int dpi = g_cfg.build.app_dpi;
    if (!env || !g_cfg.build.enabled || dpi < 120 || dpi > 960) return;

    if (!g_java_vm) {
        env->GetJavaVM(&g_java_vm);
    }
    apply_app_dpi_virtualization(env);
    emit_probe("DPI_SPOOF", "DisplayMetrics.densityDpi", std::to_string(dpi));

    pthread_t tid;
    if (pthread_create(&tid, nullptr, dpi_enforcer_thread, nullptr) == 0) {
        pthread_detach(tid);
    }
}

} // namespace mirage

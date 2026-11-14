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

inline void patch_display_metrics_obj(JNIEnv *env, jobject dm, int dpi) {
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

inline void patch_configuration_obj(JNIEnv *env, jobject cfg, int dpi) {
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

inline void patch_resources_instance_dpi(JNIEnv *env, jobject res_obj, int dpi) {
    if (!env || !res_obj) return;
    jclass res_cls = env->GetObjectClass(res_obj);
    if (!res_cls) { env->ExceptionClear(); return; }

    jmethodID get_dm_mid = env->GetMethodID(res_cls, "getDisplayMetrics", "()Landroid/util/DisplayMetrics;");
    if (!env->ExceptionCheck() && get_dm_mid) {
        jobject dm = env->CallObjectMethod(res_obj, get_dm_mid);
        if (!env->ExceptionCheck() && dm) {
            patch_display_metrics_obj(env, dm, dpi);
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
            patch_configuration_obj(env, cfg, dpi);
            env->DeleteLocalRef(cfg);
        } else {
            env->ExceptionClear();
        }
    } else {
        env->ExceptionClear();
    }

    env->DeleteLocalRef(res_cls);
}

// 执行一次全量 DPI 注入：覆盖 DisplayMetrics 静态常量、Resources.getSystem()、ResourcesManager 弱引用池与 currentApplication
inline void apply_dpi_to_all_active_resources(JNIEnv *env, int dpi) {
    if (!env || dpi < 120 || dpi > 960) return;

    // 1. 覆写 DisplayMetrics.DENSITY_DEVICE 与 DENSITY_DEVICE_STABLE 静态常量
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

    // 2. 覆写 Resources.getSystem()
    jclass res_cls = env->FindClass("android/content/res/Resources");
    if (res_cls) {
        jmethodID get_sys_mid = env->GetStaticMethodID(res_cls, "getSystem", "()Landroid/content/res/Resources;");
        if (!env->ExceptionCheck() && get_sys_mid) {
            jobject sys_res = env->CallStaticObjectMethod(res_cls, get_sys_mid);
            if (!env->ExceptionCheck() && sys_res) {
                patch_resources_instance_dpi(env, sys_res, dpi);
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

    // 3. 覆写 ActivityThread.currentApplication().getResources()
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
                            patch_resources_instance_dpi(env, app_res, dpi);
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

    // 4. 遍历 ResourcesManager.getInstance().mResourceReferences (涵盖所有 Activity 与 Context 的 Resources)
    jclass rm_cls = env->FindClass("android/app/ResourcesManager");
    if (rm_cls) {
        jmethodID get_inst_mid = env->GetStaticMethodID(rm_cls, "getInstance", "()Landroid/app/ResourcesManager;");
        if (!env->ExceptionCheck() && get_inst_mid) {
            jobject rm_obj = env->CallStaticObjectMethod(rm_cls, get_inst_mid);
            if (!env->ExceptionCheck() && rm_obj) {
                // 4.1 修改 ResourcesManager.mResConfiguration
                jfieldID res_cfg_fid = env->GetFieldID(rm_cls, "mResConfiguration", "Landroid/content/res/Configuration;");
                if (!env->ExceptionCheck() && res_cfg_fid) {
                    jobject rm_cfg = env->GetObjectField(rm_obj, res_cfg_fid);
                    if (!env->ExceptionCheck() && rm_cfg) {
                        patch_configuration_obj(env, rm_cfg, dpi);
                        env->DeleteLocalRef(rm_cfg);
                    } else {
                        env->ExceptionClear();
                    }
                } else {
                    env->ExceptionClear();
                }

                // 4.2 遍历 mResourceReferences (ArrayList<WeakReference<Resources>>)
                jfieldID refs_fid = env->GetFieldID(rm_cls, "mResourceReferences", "Ljava/util/ArrayList;");
                if (!env->ExceptionCheck() && refs_fid) {
                    jobject list_obj = env->GetObjectField(rm_obj, refs_fid);
                    if (!env->ExceptionCheck() && list_obj) {
                        jclass list_cls = env->GetObjectClass(list_obj);
                        jclass wref_cls = env->FindClass("java/lang/ref/WeakReference");
                        if (list_cls && wref_cls) {
                            jmethodID size_mid = env->GetMethodID(list_cls, "size", "()I");
                            jmethodID get_mid = env->GetMethodID(list_cls, "get", "(I)Ljava/lang/Object;");
                            jmethodID ref_get_mid = env->GetMethodID(wref_cls, "get", "()Ljava/lang/Object;");
                            if (!env->ExceptionCheck() && size_mid && get_mid && ref_get_mid) {
                                jint count = env->CallIntMethod(list_obj, size_mid);
                                if (!env->ExceptionCheck() && count > 0 && count < 512) {
                                    for (jint i = 0; i < count; ++i) {
                                        jobject wref = env->CallObjectMethod(list_obj, get_mid, i);
                                        if (!env->ExceptionCheck() && wref) {
                                            jobject target_res = env->CallObjectMethod(wref, ref_get_mid);
                                            if (!env->ExceptionCheck() && target_res) {
                                                patch_resources_instance_dpi(env, target_res, dpi);
                                                env->DeleteLocalRef(target_res);
                                            } else {
                                                env->ExceptionClear();
                                            }
                                            env->DeleteLocalRef(wref);
                                        } else {
                                            env->ExceptionClear();
                                        }
                                    }
                                } else {
                                    env->ExceptionClear();
                                }
                            } else {
                                env->ExceptionClear();
                            }
                        }
                        if (list_cls) env->DeleteLocalRef(list_cls);
                        if (wref_cls) env->DeleteLocalRef(wref_cls);
                        env->DeleteLocalRef(list_obj);
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

struct DpiWatcherArgs {
    JavaVM *vm;
    int target_dpi;
};

// 后台短生命周期守护线程：在 handleBindApplication 与首屏 Activity 创建后持续锁定 DPI，2.2秒后自动销毁退出
inline void *dpi_lifecycle_watcher_thread(void *raw_arg) {
    auto *args = static_cast<DpiWatcherArgs *>(raw_arg);
    if (!args || !args->vm) {
        delete args;
        return nullptr;
    }

    JavaVM *vm = args->vm;
    int dpi = args->target_dpi;
    delete args;

    JNIEnv *env = nullptr;
    JavaVMAttachArgs attach_args{JNI_VERSION_1_6, "MirageDpiSync", nullptr};
    if (vm->AttachCurrentThreadAsDaemon(&env, &attach_args) != JNI_OK || !env) {
        return nullptr;
    }

    constexpr useconds_t kDelaysUs[] = {25000, 65000, 150000, 350000, 700000, 1200000};
    for (useconds_t d : kDelaysUs) {
        usleep(d);
        apply_dpi_to_all_active_resources(env, dpi);
    }

    vm->DetachCurrentThread();
    return nullptr;
}

inline void apply_app_dpi_virtualization(JNIEnv *env) {
    int dpi = g_cfg.build.app_dpi;
    if (!env || !g_cfg.build.enabled || dpi < 120 || dpi > 960) return;

    apply_dpi_to_all_active_resources(env, dpi);

    // 仅在进程启动时启动一次短生命周期 DPI 同步守护线程，确保覆盖 handleBindApplication 之后创建的 Activity
    bool expected = false;
    if (g_dpi_watcher_started.compare_exchange_strong(expected, true)) {
        JavaVM *vm = nullptr;
        if (env->GetJavaVM(&vm) == JNI_OK && vm) {
            auto *args = new DpiWatcherArgs{vm, dpi};
            pthread_t tid;
            if (pthread_create(&tid, nullptr, dpi_lifecycle_watcher_thread, args) == 0) {
                pthread_detach(tid);
            } else {
                delete args;
            }
        }
        emit_probe("DPI_SPOOF", "DisplayMetrics.densityDpi", std::to_string(dpi));
    }
}

} // namespace mirage

#pragma once

#include <cstdlib>
#include <atomic>
#include <unistd.h>
#include <pthread.h>
#include <jni.h>
#include "mirage_config.hpp"
#include "mirage_logger.hpp"

namespace mirage {

inline JavaVM *g_jvm = nullptr;
inline std::atomic<int> g_dpi_sync_remaining{0};
inline pid_t g_main_tid = 0;

// 保留空内联定义以兼容其他头文件的调用点，彻底杜绝在 PLT Hook 中触发 JNI 递归调用
inline void maybe_sync_dpi_post_bind() noexcept {}

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

// 纯内存字段覆写：绝不调用 updateConfiguration 等会触发锁或系统属性重入的 Java 方法
inline void patch_dm_and_cfg_fields(JNIEnv *env, jobject dm, jobject cfg, int dpi) {
    if (!env) return;
    float scale = static_cast<float>(dpi) / 160.0f;
    int w_px = 0;
    int h_px = 0;

    if (dm) {
        jclass dm_cls = env->GetObjectClass(dm);
        if (dm_cls) {
            auto set_i = [&](const char *name, int v) {
                jfieldID f = env->GetFieldID(dm_cls, name, "I");
                if (!env->ExceptionCheck() && f) env->SetIntField(dm, f, v);
                else env->ExceptionClear();
            };
            auto set_f = [&](const char *name, float v) {
                jfieldID f = env->GetFieldID(dm_cls, name, "F");
                if (!env->ExceptionCheck() && f) env->SetFloatField(dm, f, v);
                else env->ExceptionClear();
            };

            jfieldID wf = env->GetFieldID(dm_cls, "widthPixels", "I");
            if (!env->ExceptionCheck() && wf) w_px = env->GetIntField(dm, wf);
            else env->ExceptionClear();

            jfieldID hf = env->GetFieldID(dm_cls, "heightPixels", "I");
            if (!env->ExceptionCheck() && hf) h_px = env->GetIntField(dm, hf);
            else env->ExceptionClear();

            set_i("densityDpi", dpi);
            set_i("noncompatDensityDpi", dpi);
            set_f("density", scale);
            set_f("noncompatDensity", scale);
            set_f("scaledDensity", scale);
            set_f("noncompatScaledDensity", scale);
            set_f("xdpi", static_cast<float>(dpi));
            set_f("noncompatXdpi", static_cast<float>(dpi));
            set_f("ydpi", static_cast<float>(dpi));
            set_f("noncompatYdpi", static_cast<float>(dpi));

            env->DeleteLocalRef(dm_cls);
        } else {
            env->ExceptionClear();
        }
    }

    if (cfg) {
        jclass cfg_cls = env->GetObjectClass(cfg);
        if (cfg_cls) {
            auto set_cfg_i = [&](const char *name, int v) {
                jfieldID f = env->GetFieldID(cfg_cls, name, "I");
                if (!env->ExceptionCheck() && f) env->SetIntField(cfg, f, v);
                else env->ExceptionClear();
            };

            set_cfg_i("densityDpi", dpi);
            if (w_px > 0 && h_px > 0 && scale > 0.1f) {
                int new_w_dp = static_cast<int>(static_cast<float>(w_px) / scale + 0.5f);
                int new_h_dp = static_cast<int>(static_cast<float>(h_px) / scale + 0.5f);
                int new_sw_dp = (new_w_dp < new_h_dp) ? new_w_dp : new_h_dp;
                set_cfg_i("screenWidthDp", new_w_dp);
                set_cfg_i("screenHeightDp", new_h_dp);
                set_cfg_i("smallestScreenWidthDp", new_sw_dp);
            }
            env->DeleteLocalRef(cfg_cls);
        } else {
            env->ExceptionClear();
        }
    }
}

// 直接从 Resources 或 ResourcesImpl 对象提取 mMetrics 与 mConfiguration 字段并覆写
inline void patch_resources_or_impl_obj(JNIEnv *env, jobject obj, int dpi) {
    if (!env || !obj) return;
    jclass cls = env->GetObjectClass(obj);
    if (!cls) { env->ExceptionClear(); return; }

    // 如果传入的是 Resources 包装类，先尝试取出内部的 mResourcesImpl
    jfieldID impl_fid = env->GetFieldID(cls, "mResourcesImpl", "Landroid/content/res/ResourcesImpl;");
    if (env->ExceptionCheck()) env->ExceptionClear();
    if (impl_fid) {
        jobject impl_obj = env->GetObjectField(obj, impl_fid);
        if (!env->ExceptionCheck() && impl_obj) {
            patch_resources_or_impl_obj(env, impl_obj, dpi);
            env->DeleteLocalRef(impl_obj);
        } else {
            env->ExceptionClear();
        }
    }

    jobject dm = nullptr;
    jfieldID dm_fid = env->GetFieldID(cls, "mMetrics", "Landroid/util/DisplayMetrics;");
    if (!env->ExceptionCheck() && dm_fid) {
        dm = env->GetObjectField(obj, dm_fid);
    } else {
        env->ExceptionClear();
        jmethodID get_dm = env->GetMethodID(cls, "getDisplayMetrics", "()Landroid/util/DisplayMetrics;");
        if (!env->ExceptionCheck() && get_dm) dm = env->CallObjectMethod(obj, get_dm);
        else env->ExceptionClear();
    }
    if (env->ExceptionCheck()) { env->ExceptionClear(); dm = nullptr; }

    jobject cfg = nullptr;
    jfieldID cfg_fid = env->GetFieldID(cls, "mConfiguration", "Landroid/content/res/Configuration;");
    if (!env->ExceptionCheck() && cfg_fid) {
        cfg = env->GetObjectField(obj, cfg_fid);
    } else {
        env->ExceptionClear();
        jmethodID get_cfg = env->GetMethodID(cls, "getConfiguration", "()Landroid/content/res/Configuration;");
        if (!env->ExceptionCheck() && get_cfg) cfg = env->CallObjectMethod(obj, get_cfg);
        else env->ExceptionClear();
    }
    if (env->ExceptionCheck()) { env->ExceptionClear(); cfg = nullptr; }

    patch_dm_and_cfg_fields(env, dm, cfg, dpi);

    if (dm) env->DeleteLocalRef(dm);
    if (cfg) env->DeleteLocalRef(cfg);
    env->DeleteLocalRef(cls);
}

inline bool apply_app_dpi_virtualization(JNIEnv *env) {
    int dpi = g_cfg.build.app_dpi;
    if (!env || !g_cfg.build.enabled || dpi < 120 || dpi > 960) return false;
    if (env->ExceptionCheck()) return false;

    if (env->PushLocalFrame(128) < 0) {
        env->ExceptionClear();
        return false;
    }

    // 1. 覆写 DisplayMetrics.DENSITY_DEVICE 与 DENSITY_DEVICE_STABLE 静态基准值
    jclass dm_static_cls = env->FindClass("android/util/DisplayMetrics");
    if (!env->ExceptionCheck() && dm_static_cls) {
        jfieldID dev_fid = env->GetStaticFieldID(dm_static_cls, "DENSITY_DEVICE", "I");
        if (!env->ExceptionCheck() && dev_fid) env->SetStaticIntField(dm_static_cls, dev_fid, dpi);
        else env->ExceptionClear();

        jfieldID stb_fid = env->GetStaticFieldID(dm_static_cls, "DENSITY_DEVICE_STABLE", "I");
        if (!env->ExceptionCheck() && stb_fid) env->SetStaticIntField(dm_static_cls, stb_fid, dpi);
        else env->ExceptionClear();
    } else {
        env->ExceptionClear();
    }

    // 2. 覆写 Resources.mSystem (静态字段直取，避免触发不必要的方法调用)
    jclass res_cls = env->FindClass("android/content/res/Resources");
    if (!env->ExceptionCheck() && res_cls) {
        jfieldID sys_fid = env->GetStaticFieldID(res_cls, "mSystem", "Landroid/content/res/Resources;");
        if (!env->ExceptionCheck() && sys_fid) {
            jobject sys_res = env->GetStaticObjectField(res_cls, sys_fid);
            if (!env->ExceptionCheck() && sys_res) {
                patch_resources_or_impl_obj(env, sys_res, dpi);
            } else {
                env->ExceptionClear();
            }
        } else {
            env->ExceptionClear();
        }
    } else {
        env->ExceptionClear();
    }

    // 3. 遍历 ResourcesManager.sResourcesManager 中的 mResConfiguration 与全部 mResourceImpls
    int patched_impl_count = 0;
    jclass rm_cls = env->FindClass("android/app/ResourcesManager");
    if (!env->ExceptionCheck() && rm_cls) {
        jfieldID srm_fid = env->GetStaticFieldID(rm_cls, "sResourcesManager", "Landroid/app/ResourcesManager;");
        if (!env->ExceptionCheck() && srm_fid) {
            jobject rm_obj = env->GetStaticObjectField(rm_cls, srm_fid);
            if (!env->ExceptionCheck() && rm_obj) {
                jfieldID rcfg_fid = env->GetFieldID(rm_cls, "mResConfiguration", "Landroid/content/res/Configuration;");
                if (!env->ExceptionCheck() && rcfg_fid) {
                    jobject rcfg = env->GetObjectField(rm_obj, rcfg_fid);
                    if (!env->ExceptionCheck() && rcfg) {
                        patch_dm_and_cfg_fields(env, nullptr, rcfg, dpi);
                    } else {
                        env->ExceptionClear();
                    }
                } else {
                    env->ExceptionClear();
                }

                jfieldID impls_fid = env->GetFieldID(rm_cls, "mResourceImpls", "Landroid/util/ArrayMap;");
                if (!env->ExceptionCheck() && impls_fid) {
                    jobject map_obj = env->GetObjectField(rm_obj, impls_fid);
                    if (!env->ExceptionCheck() && map_obj) {
                        jclass map_cls = env->GetObjectClass(map_obj);
                        jclass wref_cls = env->FindClass("java/lang/ref/WeakReference");
                        if (!env->ExceptionCheck() && map_cls && wref_cls) {
                            jmethodID sz_mid = env->GetMethodID(map_cls, "size", "()I");
                            jmethodID val_mid = env->GetMethodID(map_cls, "valueAt", "(I)Ljava/lang/Object;");
                            jmethodID get_mid = env->GetMethodID(wref_cls, "get", "()Ljava/lang/Object;");
                            if (!env->ExceptionCheck() && sz_mid && val_mid && get_mid) {
                                env->MonitorEnter(rm_obj);
                                jint count = env->CallIntMethod(map_obj, sz_mid);
                                if (!env->ExceptionCheck() && count > 0 && count < 128) {
                                    for (jint i = 0; i < count; ++i) {
                                        jobject wref = env->CallObjectMethod(map_obj, val_mid, i);
                                        if (!env->ExceptionCheck() && wref) {
                                            jobject impl = env->CallObjectMethod(wref, get_mid);
                                            if (!env->ExceptionCheck() && impl) {
                                                patch_resources_or_impl_obj(env, impl, dpi);
                                                ++patched_impl_count;
                                                env->DeleteLocalRef(impl);
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
                                env->MonitorExit(rm_obj);
                            } else {
                                env->ExceptionClear();
                            }
                        } else {
                            env->ExceptionClear();
                        }
                    } else {
                        env->ExceptionClear();
                    }
                } else {
                    env->ExceptionClear();
                }
            } else {
                env->ExceptionClear();
            }
        } else {
            env->ExceptionClear();
        }
    } else {
        env->ExceptionClear();
    }

    // 4. 覆写 ActivityThread.sCurrentActivityThread 的 mInitialApplication 与 mConfiguration
    bool app_ready = false;
    jclass at_cls = env->FindClass("android/app/ActivityThread");
    if (!env->ExceptionCheck() && at_cls) {
        jfieldID sat_fid = env->GetStaticFieldID(at_cls, "sCurrentActivityThread", "Landroid/app/ActivityThread;");
        if (!env->ExceptionCheck() && sat_fid) {
            jobject at_obj = env->GetStaticObjectField(at_cls, sat_fid);
            if (!env->ExceptionCheck() && at_obj) {
                jfieldID acfg_fid = env->GetFieldID(at_cls, "mConfiguration", "Landroid/content/res/Configuration;");
                if (!env->ExceptionCheck() && acfg_fid) {
                    jobject acfg = env->GetObjectField(at_obj, acfg_fid);
                    if (!env->ExceptionCheck() && acfg) patch_dm_and_cfg_fields(env, nullptr, acfg, dpi);
                    else env->ExceptionClear();
                } else {
                    env->ExceptionClear();
                }

                jfieldID app_fid = env->GetFieldID(at_cls, "mInitialApplication", "Landroid/app/Application;");
                if (!env->ExceptionCheck() && app_fid) {
                    jobject app_obj = env->GetObjectField(at_obj, app_fid);
                    if (!env->ExceptionCheck() && app_obj) {
                        app_ready = true;
                        jclass app_cls = env->GetObjectClass(app_obj);
                        if (!env->ExceptionCheck() && app_cls) {
                            jmethodID get_res = env->GetMethodID(app_cls, "getResources", "()Landroid/content/res/Resources;");
                            if (!env->ExceptionCheck() && get_res) {
                                jobject app_res = env->CallObjectMethod(app_obj, get_res);
                                if (!env->ExceptionCheck() && app_res) {
                                    patch_resources_or_impl_obj(env, app_res, dpi);
                                } else {
                                    env->ExceptionClear();
                                }
                            } else {
                                env->ExceptionClear();
                            }
                        } else {
                            env->ExceptionClear();
                        }
                    } else {
                        env->ExceptionClear();
                    }
                } else {
                    env->ExceptionClear();
                }
            } else {
                env->ExceptionClear();
            }
        } else {
            env->ExceptionClear();
        }
    } else {
        env->ExceptionClear();
    }

    // 5. 覆写 DisplayManagerGlobal.sInstance.mDisplayInfoCache 中的 DisplayInfo (供 Display.getMetrics / AIDA64 读取)
    jclass dmg_cls = env->FindClass("android/hardware/display/DisplayManagerGlobal");
    if (!env->ExceptionCheck() && dmg_cls) {
        jfieldID sdmg_fid = env->GetStaticFieldID(dmg_cls, "sInstance", "Landroid/hardware/display/DisplayManagerGlobal;");
        if (!env->ExceptionCheck() && sdmg_fid) {
            jobject dmg_obj = env->GetStaticObjectField(dmg_cls, sdmg_fid);
            if (!env->ExceptionCheck() && dmg_obj) {
                jfieldID cache_fid = env->GetFieldID(dmg_cls, "mDisplayInfoCache", "Landroid/util/SparseArray;");
                if (!env->ExceptionCheck() && cache_fid) {
                    jobject sa_obj = env->GetObjectField(dmg_obj, cache_fid);
                    if (!env->ExceptionCheck() && sa_obj) {
                        jclass sa_cls = env->GetObjectClass(sa_obj);
                        if (!env->ExceptionCheck() && sa_cls) {
                            jmethodID sz_mid = env->GetMethodID(sa_cls, "size", "()I");
                            jmethodID val_mid = env->GetMethodID(sa_cls, "valueAt", "(I)Ljava/lang/Object;");
                            if (!env->ExceptionCheck() && sz_mid && val_mid) {
                                jint count = env->CallIntMethod(sa_obj, sz_mid);
                                if (!env->ExceptionCheck() && count > 0 && count < 32) {
                                    for (jint i = 0; i < count; ++i) {
                                        jobject dinfo = env->CallObjectMethod(sa_obj, val_mid, i);
                                        if (!env->ExceptionCheck() && dinfo) {
                                            jclass di_cls = env->GetObjectClass(dinfo);
                                            if (!env->ExceptionCheck() && di_cls) {
                                                jfieldID ldpi = env->GetFieldID(di_cls, "logicalDensityDpi", "I");
                                                if (!env->ExceptionCheck() && ldpi) env->SetIntField(dinfo, ldpi, dpi);
                                                else env->ExceptionClear();

                                                jfieldID xdpi = env->GetFieldID(di_cls, "physicalXDpi", "F");
                                                if (!env->ExceptionCheck() && xdpi) env->SetFloatField(dinfo, xdpi, static_cast<float>(dpi));
                                                else env->ExceptionClear();

                                                jfieldID ydpi = env->GetFieldID(di_cls, "physicalYDpi", "F");
                                                if (!env->ExceptionCheck() && ydpi) env->SetFloatField(dinfo, ydpi, static_cast<float>(dpi));
                                                else env->ExceptionClear();

                                                env->DeleteLocalRef(di_cls);
                                            } else {
                                                env->ExceptionClear();
                                            }
                                            env->DeleteLocalRef(dinfo);
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
                        } else {
                            env->ExceptionClear();
                        }
                    } else {
                        env->ExceptionClear();
                    }
                } else {
                    env->ExceptionClear();
                }
            } else {
                env->ExceptionClear();
            }
        } else {
            env->ExceptionClear();
        }
    } else {
        env->ExceptionClear();
    }

    env->PopLocalFrame(nullptr);
    return app_ready && (patched_impl_count > 0);
}

// 独立守护线程：在 postAppSpecialize 之后分段同步 ResourcesManager 中的全部 ResourcesImpl，绝不阻塞或污染主线程调用栈
inline void *dpi_guardian_worker(void *) {
    if (!g_jvm || g_cfg.build.app_dpi < 120 || g_cfg.build.app_dpi > 960) return nullptr;

    JNIEnv *env = nullptr;
    JavaVMAttachArgs args{JNI_VERSION_1_6, "MirageDpiSync", nullptr};
    if (g_jvm->AttachCurrentThreadAsDaemon(&env, &args) != JNI_OK || !env) {
        return nullptr;
    }

    // 覆盖 handleBindApplication (15~50ms)、Activity.onCreate (120~400ms) 及首屏渲染后 (800~2000ms)
    const useconds_t delays_us[] = {
        15000, 35000, 70000, 150000, 300000, 500000, 800000, 1200000
    };
    bool logged = false;

    for (useconds_t d : delays_us) {
        usleep(d);
        if (apply_app_dpi_virtualization(env) && !logged) {
            logged = true;
            emit_probe("DPI_SPOOF", "ResourcesManager.densityDpi", std::to_string(g_cfg.build.app_dpi));
        }
    }

    g_jvm->DetachCurrentThread();
    return nullptr;
}

inline void start_dpi_guardian_after_specialize(JNIEnv *env) {
    int dpi = g_cfg.build.app_dpi;
    if (!g_cfg.build.enabled || dpi < 120 || dpi > 960) return;

    apply_app_dpi_virtualization(env);

    pthread_t tid;
    if (pthread_create(&tid, nullptr, dpi_guardian_worker, nullptr) == 0) {
        pthread_detach(tid);
    }
}

} // namespace mirage

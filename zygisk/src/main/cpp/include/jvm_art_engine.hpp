#pragma once

#include <cstdlib>
#include <ctime>
#include <atomic>
#include <unistd.h>
#include <jni.h>
#include "mirage_config.hpp"
#include "mirage_logger.hpp"
#include "bionic_prop_engine.hpp"
#include "linker_plt_engine.hpp"

namespace mirage {

inline JavaVM *g_jvm = nullptr;
inline pid_t g_main_tid = 0;
inline uint64_t g_dpi_start_ms = 0;
inline uint64_t g_dpi_last_tick_ms = 0;
inline int g_dpi_sync_passes = 0;
inline std::atomic<bool> g_dpi_sync_completed{false};
inline thread_local bool g_tls_in_dpi_sync = false;

inline uint64_t monotonic_now_ms() noexcept {
    struct timespec ts{};
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return static_cast<uint64_t>(ts.tv_sec) * 1000ULL + static_cast<uint64_t>(ts.tv_nsec / 1000000L);
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

    // 根据新的 density 等比换算屏幕宽高 dp 对应的像素感，保持物理像素宽高但更新全部密度系数
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

inline void patch_configuration_obj(JNIEnv *env, jobject cfg, int dpi, jobject optional_dm = nullptr) {
    if (!env || !cfg) return;
    jclass cfg_cls = env->GetObjectClass(cfg);
    if (!cfg_cls) { env->ExceptionClear(); return; }

    jfieldID dpi_fid = env->GetFieldID(cfg_cls, "densityDpi", "I");
    if (!env->ExceptionCheck() && dpi_fid) {
        env->SetIntField(cfg, dpi_fid, dpi);
    } else {
        env->ExceptionClear();
    }

    // 同步按新 DPI 重算 screenWidthDp / screenHeightDp / smallestScreenWidthDp，使响应式布局生效
    if (optional_dm) {
        jclass dm_cls = env->GetObjectClass(optional_dm);
        if (dm_cls) {
            jfieldID w_fid = env->GetFieldID(dm_cls, "widthPixels", "I");
            jfieldID h_fid = env->GetFieldID(dm_cls, "heightPixels", "I");
            if (!env->ExceptionCheck() && w_fid && h_fid) {
                int w_px = env->GetIntField(optional_dm, w_fid);
                int h_px = env->GetIntField(optional_dm, h_fid);
                if (w_px > 0 && h_px > 0) {
                    int w_dp = (w_px * 160) / dpi;
                    int h_dp = (h_px * 160) / dpi;
                    int sw_dp = (w_dp < h_dp) ? w_dp : h_dp;

                    auto set_cfg_int = [&](const char *name, int val) {
                        jfieldID f = env->GetFieldID(cfg_cls, name, "I");
                        if (!env->ExceptionCheck() && f) env->SetIntField(cfg, f, val);
                        else env->ExceptionClear();
                    };
                    set_cfg_int("screenWidthDp", w_dp);
                    set_cfg_int("screenHeightDp", h_dp);
                    set_cfg_int("smallestScreenWidthDp", sw_dp);
                }
            } else {
                env->ExceptionClear();
            }
            env->DeleteLocalRef(dm_cls);
        }
    }

    env->DeleteLocalRef(cfg_cls);
}

// 核心：修改单个 Resources 对象并调用 updateConfiguration 通知底层 C++ AssetManager 重算布局尺寸
inline void patch_resources_and_asset_manager(JNIEnv *env, jobject res_obj, int dpi) {
    if (!env || !res_obj) return;
    jclass res_cls = env->GetObjectClass(res_obj);
    if (!res_cls) { env->ExceptionClear(); return; }

    jobject dm = nullptr;
    jobject cfg = nullptr;

    jmethodID get_dm_mid = env->GetMethodID(res_cls, "getDisplayMetrics", "()Landroid/util/DisplayMetrics;");
    if (!env->ExceptionCheck() && get_dm_mid) {
        dm = env->CallObjectMethod(res_obj, get_dm_mid);
        if (env->ExceptionCheck()) { env->ExceptionClear(); dm = nullptr; }
    } else {
        env->ExceptionClear();
    }

    jmethodID get_cfg_mid = env->GetMethodID(res_cls, "getConfiguration", "()Landroid/content/res/Configuration;");
    if (!env->ExceptionCheck() && get_cfg_mid) {
        cfg = env->CallObjectMethod(res_obj, get_cfg_mid);
        if (env->ExceptionCheck()) { env->ExceptionClear(); cfg = nullptr; }
    } else {
        env->ExceptionClear();
    }

    if (dm) patch_display_metrics_obj(env, dm, dpi);
    if (cfg) patch_configuration_obj(env, cfg, dpi, dm);

    // 调用 Resources.updateConfiguration(cfg, dm) 触发 ResourcesImpl 与底层 C++ AssetManager 同步刷新
    if (cfg && dm) {
        jmethodID upd_mid = env->GetMethodID(
            res_cls, "updateConfiguration",
            "(Landroid/content/res/Configuration;Landroid/util/DisplayMetrics;)V");
        if (!env->ExceptionCheck() && upd_mid) {
            env->CallVoidMethod(res_obj, upd_mid, cfg, dm);
            if (env->ExceptionCheck()) env->ExceptionClear();
        } else {
            env->ExceptionClear();
        }
        // updateConfiguration 内部可能会根据旧 DisplayInfo 覆盖一次 dm，再次锁定确保数值 100% 准确
        patch_display_metrics_obj(env, dm, dpi);
        patch_configuration_obj(env, cfg, dpi, dm);
    }

    if (dm) env->DeleteLocalRef(dm);
    if (cfg) env->DeleteLocalRef(cfg);
    env->DeleteLocalRef(res_cls);
}

// 修改 DisplayManagerGlobal 缓存的 DisplayInfo (解决 WindowManager.getDefaultDisplay().getMetrics() 读出真实 DPI 的问题)
inline void patch_display_manager_global_dpi(JNIEnv *env, int dpi) {
    jclass dmg_cls = env->FindClass("android/hardware/display/DisplayManagerGlobal");
    if (!dmg_cls) { env->ExceptionClear(); return; }

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
                        if (!env->ExceptionCheck() && log_dpi_fid) {
                            env->SetIntField(di_obj, log_dpi_fid, dpi);
                        } else {
                            env->ExceptionClear();
                        }
                        jfieldID xdpi_fid = env->GetFieldID(di_cls, "physicalXDpi", "F");
                        if (!env->ExceptionCheck() && xdpi_fid) {
                            env->SetFloatField(di_obj, xdpi_fid, static_cast<float>(dpi));
                        } else {
                            env->ExceptionClear();
                        }
                        jfieldID ydpi_fid = env->GetFieldID(di_cls, "physicalYDpi", "F");
                        if (!env->ExceptionCheck() && ydpi_fid) {
                            env->SetFloatField(di_obj, ydpi_fid, static_cast<float>(dpi));
                        } else {
                            env->ExceptionClear();
                        }
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
}

// 在主线程一次性同步所有层级的 DPI（静态常量 + DisplayManagerGlobal + System Resources + Application + ResourcesManager 全实例）
inline bool sync_all_dpi_layers_on_main_thread(JNIEnv *env, int dpi) {
    if (!env || dpi < 120 || dpi > 960) return false;
    bool app_bound = false;

    // 1. DisplayMetrics.DENSITY_DEVICE / DENSITY_DEVICE_STABLE
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

    // 2. DisplayManagerGlobal -> DisplayInfo
    patch_display_manager_global_dpi(env, dpi);

    // 3. Resources.getSystem()
    jclass res_cls = env->FindClass("android/content/res/Resources");
    if (res_cls) {
        jmethodID get_sys_mid = env->GetStaticMethodID(res_cls, "getSystem", "()Landroid/content/res/Resources;");
        if (!env->ExceptionCheck() && get_sys_mid) {
            jobject sys_res = env->CallStaticObjectMethod(res_cls, get_sys_mid);
            if (!env->ExceptionCheck() && sys_res) {
                patch_resources_and_asset_manager(env, sys_res, dpi);
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

    // 4. ActivityThread.currentApplication().getResources()
    jclass at_cls = env->FindClass("android/app/ActivityThread");
    if (at_cls) {
        jmethodID cur_app_mid = env->GetStaticMethodID(at_cls, "currentApplication", "()Landroid/app/Application;");
        if (!env->ExceptionCheck() && cur_app_mid) {
            jobject app_obj = env->CallStaticObjectMethod(at_cls, cur_app_mid);
            if (!env->ExceptionCheck() && app_obj) {
                app_bound = true;
                jclass ctx_cls = env->GetObjectClass(app_obj);
                if (ctx_cls) {
                    jmethodID get_res_mid = env->GetMethodID(ctx_cls, "getResources", "()Landroid/content/res/Resources;");
                    if (!env->ExceptionCheck() && get_res_mid) {
                        jobject app_res = env->CallObjectMethod(app_obj, get_res_mid);
                        if (!env->ExceptionCheck() && app_res) {
                            patch_resources_and_asset_manager(env, app_res, dpi);
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

    // 5. ResourcesManager.getInstance() (覆盖所有已创建的 Activity 与 ContextWrapper 的 Resources)
    jclass rm_cls = env->FindClass("android/app/ResourcesManager");
    if (rm_cls) {
        jmethodID get_inst_mid = env->GetStaticMethodID(rm_cls, "getInstance", "()Landroid/app/ResourcesManager;");
        if (!env->ExceptionCheck() && get_inst_mid) {
            jobject rm_obj = env->CallStaticObjectMethod(rm_cls, get_inst_mid);
            if (!env->ExceptionCheck() && rm_obj) {
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
                                if (!env->ExceptionCheck() && count > 0 && count < 256) {
                                    for (jint i = 0; i < count; ++i) {
                                        jobject wref = env->CallObjectMethod(list_obj, get_mid, i);
                                        if (!env->ExceptionCheck() && wref) {
                                            jobject target_res = env->CallObjectMethod(wref, ref_get_mid);
                                            if (!env->ExceptionCheck() && target_res) {
                                                patch_resources_and_asset_manager(env, target_res, dpi);
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

    return app_bound;
}

// 主线程按需回调：当主线程在 handleBindApplication / Activity.onCreate 期间自然调用系统属性或 dlopen 时触发
inline void on_main_thread_lazy_dpi_tick() {
    if (__builtin_expect(g_dpi_sync_completed.load(std::memory_order_relaxed), 1)) return;
    if (g_tls_in_dpi_sync || !g_jvm || gettid() != g_main_tid) return;

    uint64_t now = monotonic_now_ms();
    // 节流控制：两次同步至少间隔 45ms，避免高频属性读取造成性能损耗
    if (now - g_dpi_last_tick_ms < 45ULL) return;
    g_dpi_last_tick_ms = now;

    JNIEnv *env = nullptr;
    if (g_jvm->GetEnv(reinterpret_cast<void **>(&env), JNI_VERSION_1_6) != JNI_OK || !env) {
        return;
    }

    g_tls_in_dpi_sync = true;
    bool app_bound = sync_all_dpi_layers_on_main_thread(env, g_cfg.build.app_dpi);
    g_tls_in_dpi_sync = false;

    if (app_bound) {
        ++g_dpi_sync_passes;
    }

    // 当 Application 已绑定且完成了 8 次主线程阶段同步（覆盖首屏 Activity 创建），或启动超过 5 秒后自动卸载钩子
    if (g_dpi_sync_passes >= 8 || (now - g_dpi_start_ms > 5000ULL && g_dpi_sync_passes >= 2)) {
        g_dpi_sync_completed.store(true, std::memory_order_relaxed);
        g_prop_dpi_lazy_sync_fn = nullptr;
        g_main_thread_dpi_tick_fn = nullptr;
    }
}

inline void apply_app_dpi_virtualization(JNIEnv *env) {
    int dpi = g_cfg.build.app_dpi;
    if (!env || !g_cfg.build.enabled || dpi < 120 || dpi > 960) return;

    if (!g_jvm) {
        env->GetJavaVM(&g_jvm);
    }
    g_main_tid = gettid();
    if (g_dpi_start_ms == 0) {
        g_dpi_start_ms = monotonic_now_ms();
    }

    sync_all_dpi_layers_on_main_thread(env, dpi);

    // 挂载主线程按需同步钩子（在 handleBindApplication 与 Activity 启动时由主线程自身触发，0 后台线程）
    g_prop_dpi_lazy_sync_fn = on_main_thread_lazy_dpi_tick;
    g_main_thread_dpi_tick_fn = on_main_thread_lazy_dpi_tick;

    emit_probe("DPI_SPOOF", "DisplayMetrics.densityDpi", std::to_string(dpi));
}

} // namespace mirage

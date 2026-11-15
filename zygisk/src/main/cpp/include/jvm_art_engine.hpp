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
inline std::atomic<bool> g_hidden_api_unlocked{false};
inline pid_t g_main_tid = 0;

inline void scan_and_commit_plt_hooks();
inline void maybe_sync_dpi_post_bind() noexcept {}

inline void unlock_hidden_api_for_dpi(JNIEnv *env) {
    if (!env || g_hidden_api_unlocked.load(std::memory_order_acquire)) return;

    jclass vm_cls = env->FindClass("dalvik/system/VMRuntime");
    if (env->ExceptionCheck() || !vm_cls) { env->ExceptionClear(); return; }

    jmethodID get_rt = env->GetStaticMethodID(vm_cls, "getRuntime", "()Ldalvik/system/VMRuntime;");
    jmethodID set_ex = env->GetMethodID(vm_cls, "setHiddenApiExemptions", "([Ljava/lang/String;)V");
    if (env->ExceptionCheck() || !get_rt || !set_ex) {
        env->ExceptionClear();
        env->DeleteLocalRef(vm_cls);
        return;
    }

    jobject rt = env->CallStaticObjectMethod(vm_cls, get_rt);
    if (!env->ExceptionCheck() && rt) {
        jclass str_cls = env->FindClass("java/lang/String");
        if (!env->ExceptionCheck() && str_cls) {
            jobjectArray arr = env->NewObjectArray(1, str_cls, nullptr);
            if (!env->ExceptionCheck() && arr) {
                jstring l_str = env->NewStringUTF("L");
                env->SetObjectArrayElement(arr, 0, l_str);
                env->CallVoidMethod(rt, set_ex, arr);
                if (!env->ExceptionCheck()) {
                    g_hidden_api_unlocked.store(true, std::memory_order_release);
                } else {
                    env->ExceptionClear();
                }
                env->DeleteLocalRef(l_str);
                env->DeleteLocalRef(arr);
            } else {
                env->ExceptionClear();
            }
            env->DeleteLocalRef(str_cls);
        } else {
            env->ExceptionClear();
        }
        env->DeleteLocalRef(rt);
    } else {
        env->ExceptionClear();
    }
    env->DeleteLocalRef(vm_cls);
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

            float font_scale = 1.0f;
            if (cfg) {
                jclass c_cls = env->GetObjectClass(cfg);
                if (c_cls) {
                    jfieldID fs_fid = env->GetFieldID(c_cls, "fontScale", "F");
                    if (!env->ExceptionCheck() && fs_fid) {
                        float fs = env->GetFloatField(cfg, fs_fid);
                        if (fs > 0.1f) font_scale = fs;
                    } else {
                        env->ExceptionClear();
                    }
                    env->DeleteLocalRef(c_cls);
                }
            }

            set_i("densityDpi", dpi);
            set_i("noncompatDensityDpi", dpi);
            set_f("density", scale);
            set_f("noncompatDensity", scale);
            set_f("scaledDensity", scale * font_scale);
            set_f("noncompatScaledDensity", scale * font_scale);
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

inline void patch_display_info_obj(JNIEnv *env, jobject dinfo, int dpi) {
    if (!env || !dinfo) return;
    jclass di_cls = env->GetObjectClass(dinfo);
    if (!di_cls) { env->ExceptionClear(); return; }

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
}

inline void patch_display_adjustments_obj(JNIEnv *env, jobject da_obj, int dpi) {
    if (!env || !da_obj) return;
    jclass da_cls = env->GetObjectClass(da_obj);
    if (!da_cls) { env->ExceptionClear(); return; }

    jfieldID cfg_fid = env->GetFieldID(da_cls, "mConfiguration", "Landroid/content/res/Configuration;");
    if (!env->ExceptionCheck() && cfg_fid) {
        jobject cfg = env->GetObjectField(da_obj, cfg_fid);
        if (!env->ExceptionCheck() && cfg) {
            patch_dm_and_cfg_fields(env, nullptr, cfg, dpi);
            env->DeleteLocalRef(cfg);
        } else {
            env->ExceptionClear();
        }
    } else {
        env->ExceptionClear();
    }
    env->DeleteLocalRef(da_cls);
}

inline void patch_display_obj(JNIEnv *env, jobject disp_obj, int dpi) {
    if (!env || !disp_obj) return;
    jclass disp_cls = env->GetObjectClass(disp_obj);
    if (!disp_cls) { env->ExceptionClear(); return; }

    jfieldID di_fid = env->GetFieldID(disp_cls, "mDisplayInfo", "Landroid/view/DisplayInfo;");
    if (!env->ExceptionCheck() && di_fid) {
        jobject dinfo = env->GetObjectField(disp_obj, di_fid);
        if (!env->ExceptionCheck() && dinfo) {
            patch_display_info_obj(env, dinfo, dpi);
            env->DeleteLocalRef(dinfo);
        } else {
            env->ExceptionClear();
        }
    } else {
        env->ExceptionClear();
    }

    jfieldID da_fid = env->GetFieldID(disp_cls, "mDisplayAdjustments", "Landroid/view/DisplayAdjustments;");
    if (!env->ExceptionCheck() && da_fid) {
        jobject da = env->GetObjectField(disp_obj, da_fid);
        if (!env->ExceptionCheck() && da) {
            patch_display_adjustments_obj(env, da, dpi);
            env->DeleteLocalRef(da);
        } else {
            env->ExceptionClear();
        }
    } else {
        env->ExceptionClear();
    }
    env->DeleteLocalRef(disp_cls);
}

// 核心修复：构造全新的 Configuration(origCfg) 副本并将原 densityDpi 置为 1，
// 确保 ResourcesImpl.updateConfiguration 100% 识别到 CONFIG_DENSITY 变化并刷新底层 C++ AssetManager2！
inline void patch_resources_or_impl_obj(JNIEnv *env, jobject obj, int dpi) {
    if (!env || !obj) return;
    jclass cls = env->GetObjectClass(obj);
    if (!cls) { env->ExceptionClear(); return; }

    jfieldID impl_fid = env->GetFieldID(cls, "mResourcesImpl", "Landroid/content/res/ResourcesImpl;");
    if (env->ExceptionCheck()) env->ExceptionClear();

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

    jobject orig_cfg = nullptr;
    jfieldID cfg_fid = env->GetFieldID(cls, "mConfiguration", "Landroid/content/res/Configuration;");
    if (!env->ExceptionCheck() && cfg_fid) {
        orig_cfg = env->GetObjectField(obj, cfg_fid);
    } else {
        env->ExceptionClear();
        jmethodID get_cfg = env->GetMethodID(cls, "getConfiguration", "()Landroid/content/res/Configuration;");
        if (!env->ExceptionCheck() && get_cfg) orig_cfg = env->CallObjectMethod(obj, get_cfg);
        else env->ExceptionClear();
    }
    if (env->ExceptionCheck()) { env->ExceptionClear(); orig_cfg = nullptr; }

    if (orig_cfg && dm) {
        jclass cfg_cls = env->GetObjectClass(orig_cfg);
        jmethodID copy_ctor = cfg_cls
            ? env->GetMethodID(cfg_cls, "<init>", "(Landroid/content/res/Configuration;)V")
            : nullptr;
        if (!env->ExceptionCheck() && cfg_cls && copy_ctor) {
            // 1. 先克隆出一份独立的 new_cfg，设置目标 DPI 与重算后的 dp 宽高
            jobject new_cfg = env->NewObject(cfg_cls, copy_ctor, orig_cfg);
            if (!env->ExceptionCheck() && new_cfg) {
                patch_dm_and_cfg_fields(env, dm, new_cfg, dpi);

                // 2. 将原 mConfiguration.densityDpi 故意改为 1，迫使 updateFrom(new_cfg) 判定 CONFIG_DENSITY 发生变化！
                jfieldID dpi_fid = env->GetFieldID(cfg_cls, "densityDpi", "I");
                if (!env->ExceptionCheck() && dpi_fid) {
                    int cur_dpi = env->GetIntField(orig_cfg, dpi_fid);
                    if (cur_dpi != dpi) {
                        env->SetIntField(orig_cfg, dpi_fid, 1);
                    }
                } else {
                    env->ExceptionClear();
                }

                // 3. 调用 updateConfiguration(new_cfg, dm) 驱动底层 C++ AssetManager2.setConfiguration 更新密度
                if (impl_fid) {
                    jmethodID upd_mid = env->GetMethodID(
                        cls, "updateConfiguration",
                        "(Landroid/content/res/Configuration;Landroid/util/DisplayMetrics;)V");
                    if (!env->ExceptionCheck() && upd_mid) {
                        ScopedHookGuard guard;
                        env->CallVoidMethod(obj, upd_mid, new_cfg, dm);
                        if (env->ExceptionCheck()) env->ExceptionClear();
                    } else {
                        env->ExceptionClear();
                    }
                } else {
                    jmethodID upd_impl_mid = env->GetMethodID(
                        cls, "updateConfiguration",
                        "(Landroid/content/res/Configuration;Landroid/util/DisplayMetrics;Landroid/content/res/CompatibilityInfo;)V");
                    if (!env->ExceptionCheck() && upd_impl_mid) {
                        ScopedHookGuard guard;
                        env->CallVoidMethod(obj, upd_impl_mid, new_cfg, dm, nullptr);
                        if (env->ExceptionCheck()) env->ExceptionClear();
                    } else {
                        env->ExceptionClear();
                    }
                }

                // 4. 再次写入确保 mMetrics 与 mConfiguration 最终值精确等于目标 DPI
                patch_dm_and_cfg_fields(env, dm, orig_cfg, dpi);
                env->DeleteLocalRef(new_cfg);
            } else {
                env->ExceptionClear();
            }
        } else {
            env->ExceptionClear();
            patch_dm_and_cfg_fields(env, dm, orig_cfg, dpi);
        }
        if (cfg_cls) env->DeleteLocalRef(cfg_cls);
    }

    if (impl_fid) {
        jobject impl_obj = env->GetObjectField(obj, impl_fid);
        if (!env->ExceptionCheck() && impl_obj) {
            jclass impl_cls = env->GetObjectClass(impl_obj);
            if (impl_cls) {
                jfieldID da_fid = env->GetFieldID(impl_cls, "mDisplayAdjustments", "Landroid/view/DisplayAdjustments;");
                if (!env->ExceptionCheck() && da_fid) {
                    jobject da_obj = env->GetObjectField(impl_obj, da_fid);
                    if (!env->ExceptionCheck() && da_obj) {
                        patch_display_adjustments_obj(env, da_obj, dpi);
                        env->DeleteLocalRef(da_obj);
                    } else {
                        env->ExceptionClear();
                    }
                } else {
                    env->ExceptionClear();
                }
                env->DeleteLocalRef(impl_cls);
            }
            env->DeleteLocalRef(impl_obj);
        } else {
            env->ExceptionClear();
        }
    }

    if (dm) env->DeleteLocalRef(dm);
    if (orig_cfg) env->DeleteLocalRef(orig_cfg);
    env->DeleteLocalRef(cls);
}

inline bool apply_app_dpi_virtualization(JNIEnv *env) {
    int dpi = g_cfg.build.app_dpi;
    if (!env || !g_cfg.build.enabled || dpi < 120 || dpi > 960) return false;
    if (env->ExceptionCheck()) return false;

    unlock_hidden_api_for_dpi(env);

    if (env->PushLocalFrame(256) < 0) {
        env->ExceptionClear();
        return false;
    }

    // 1. 覆写 Bitmap.sDefaultDensity 与 DisplayMetrics.DENSITY_DEVICE*
    jclass bmp_cls = env->FindClass("android/graphics/Bitmap");
    if (!env->ExceptionCheck() && bmp_cls) {
        jmethodID set_def = env->GetStaticMethodID(bmp_cls, "setDefaultDensity", "(I)V");
        if (!env->ExceptionCheck() && set_def) {
            env->CallStaticVoidMethod(bmp_cls, set_def, dpi);
            if (env->ExceptionCheck()) env->ExceptionClear();
        } else {
            env->ExceptionClear();
        }
    } else {
        env->ExceptionClear();
    }

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

    // 2. 穿透 DisplayManagerGlobal 内部缓存
    jclass dmg_cls = env->FindClass("android/hardware/display/DisplayManagerGlobal");
    if (!env->ExceptionCheck() && dmg_cls) {
        jmethodID get_inst = env->GetStaticMethodID(
            dmg_cls, "getInstance", "()Landroid/hardware/display/DisplayManagerGlobal;");
        if (!env->ExceptionCheck() && get_inst) {
            jobject dmg_obj = env->CallStaticObjectMethod(dmg_cls, get_inst);
            if (!env->ExceptionCheck() && dmg_obj) {
                jmethodID get_di = env->GetMethodID(dmg_cls, "getDisplayInfo", "(I)Landroid/view/DisplayInfo;");
                if (!env->ExceptionCheck() && get_di) {
                    jobject tmp = env->CallObjectMethod(dmg_obj, get_di, 0);
                    if (env->ExceptionCheck()) env->ExceptionClear();
                    if (tmp) env->DeleteLocalRef(tmp);
                } else {
                    env->ExceptionClear();
                }

                jmethodID get_locked = env->GetMethodID(
                    dmg_cls, "getDisplayInfoLocked", "(I)Landroid/view/DisplayInfo;");
                if (!env->ExceptionCheck() && get_locked) {
                    jobject raw_di = env->CallObjectMethod(dmg_obj, get_locked, 0);
                    if (!env->ExceptionCheck() && raw_di) {
                        patch_display_info_obj(env, raw_di, dpi);
                    } else {
                        env->ExceptionClear();
                    }
                } else {
                    env->ExceptionClear();
                }

                jfieldID pcache_fid = env->GetFieldID(
                    dmg_cls, "mDisplayCache", "Landroid/app/PropertyInvalidatedCache;");
                if (!env->ExceptionCheck() && pcache_fid) {
                    jobject pcache = env->GetObjectField(dmg_obj, pcache_fid);
                    if (!env->ExceptionCheck() && pcache) {
                        jclass pcache_cls = env->GetObjectClass(pcache);
                        jclass int_cls = env->FindClass("java/lang/Integer");
                        if (!env->ExceptionCheck() && pcache_cls && int_cls) {
                            jmethodID val_of = env->GetStaticMethodID(int_cls, "valueOf", "(I)Ljava/lang/Integer;");
                            jmethodID query_mid = env->GetMethodID(
                                pcache_cls, "query", "(Ljava/lang/Object;)Ljava/lang/Object;");
                            if (!env->ExceptionCheck() && val_of && query_mid) {
                                jobject zero_int = env->CallStaticObjectMethod(int_cls, val_of, 0);
                                if (!env->ExceptionCheck() && zero_int) {
                                    jobject cached_di = env->CallObjectMethod(pcache, query_mid, zero_int);
                                    if (!env->ExceptionCheck() && cached_di) {
                                        patch_display_info_obj(env, cached_di, dpi);
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
    } else {
        env->ExceptionClear();
    }

    // 3. 覆写 Resources.getSystem()
    jclass res_cls = env->FindClass("android/content/res/Resources");
    if (!env->ExceptionCheck() && res_cls) {
        jmethodID get_sys = env->GetStaticMethodID(res_cls, "getSystem", "()Landroid/content/res/Resources;");
        if (!env->ExceptionCheck() && get_sys) {
            jobject sys_res = env->CallStaticObjectMethod(res_cls, get_sys);
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

    // 4. 遍历 ResourcesManager.getInstance() 中的全部 mResourceReferences 与 mResourceImpls
    int patched_impl_count = 0;
    jclass rm_cls = env->FindClass("android/app/ResourcesManager");
    if (!env->ExceptionCheck() && rm_cls) {
        jmethodID get_rm = env->GetStaticMethodID(rm_cls, "getInstance", "()Landroid/app/ResourcesManager;");
        if (!env->ExceptionCheck() && get_rm) {
            jobject rm_obj = env->CallStaticObjectMethod(rm_cls, get_rm);
            if (!env->ExceptionCheck() && rm_obj) {
                jfieldID rcfg_fid = env->GetFieldID(rm_cls, "mResConfiguration", "Landroid/content/res/Configuration;");
                if (!env->ExceptionCheck() && rcfg_fid) {
                    jobject rcfg = env->GetObjectField(rm_obj, rcfg_fid);
                    if (!env->ExceptionCheck() && rcfg) patch_dm_and_cfg_fields(env, nullptr, rcfg, dpi);
                    else env->ExceptionClear();
                } else {
                    env->ExceptionClear();
                }

                jclass wref_cls = env->FindClass("java/lang/ref/WeakReference");
                jmethodID wref_get = (!env->ExceptionCheck() && wref_cls)
                    ? env->GetMethodID(wref_cls, "get", "()Ljava/lang/Object;")
                    : nullptr;
                env->ExceptionClear();

                jfieldID refs_fid = env->GetFieldID(rm_cls, "mResourceReferences", "Ljava/util/ArrayList;");
                if (!env->ExceptionCheck() && refs_fid && wref_get) {
                    jobject list_obj = env->GetObjectField(rm_obj, refs_fid);
                    if (!env->ExceptionCheck() && list_obj) {
                        jclass list_cls = env->GetObjectClass(list_obj);
                        if (!env->ExceptionCheck() && list_cls) {
                            jmethodID sz_mid = env->GetMethodID(list_cls, "size", "()I");
                            jmethodID get_mid = env->GetMethodID(list_cls, "get", "(I)Ljava/lang/Object;");
                            if (!env->ExceptionCheck() && sz_mid && get_mid) {
                                env->MonitorEnter(rm_obj);
                                jint count = env->CallIntMethod(list_obj, sz_mid);
                                if (!env->ExceptionCheck() && count > 0 && count < 128) {
                                    for (jint i = 0; i < count; ++i) {
                                        jobject wref = env->CallObjectMethod(list_obj, get_mid, i);
                                        if (!env->ExceptionCheck() && wref) {
                                            jobject res_item = env->CallObjectMethod(wref, wref_get);
                                            if (!env->ExceptionCheck() && res_item) {
                                                patch_resources_or_impl_obj(env, res_item, dpi);
                                                ++patched_impl_count;
                                                env->DeleteLocalRef(res_item);
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

                jfieldID impls_fid = env->GetFieldID(rm_cls, "mResourceImpls", "Landroid/util/ArrayMap;");
                if (!env->ExceptionCheck() && impls_fid && wref_get) {
                    jobject map_obj = env->GetObjectField(rm_obj, impls_fid);
                    if (!env->ExceptionCheck() && map_obj) {
                        jclass map_cls = env->GetObjectClass(map_obj);
                        if (!env->ExceptionCheck() && map_cls) {
                            jmethodID sz_mid = env->GetMethodID(map_cls, "size", "()I");
                            jmethodID val_mid = env->GetMethodID(map_cls, "valueAt", "(I)Ljava/lang/Object;");
                            if (!env->ExceptionCheck() && sz_mid && val_mid) {
                                env->MonitorEnter(rm_obj);
                                jint count = env->CallIntMethod(map_obj, sz_mid);
                                if (!env->ExceptionCheck() && count > 0 && count < 128) {
                                    for (jint i = 0; i < count; ++i) {
                                        jobject wref = env->CallObjectMethod(map_obj, val_mid, i);
                                        if (!env->ExceptionCheck() && wref) {
                                            jobject impl = env->CallObjectMethod(wref, wref_get);
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

    // 5. 覆写 ActivityThread 的 Application 与所有活跃 Activity
    bool app_ready = false;
    jclass at_cls = env->FindClass("android/app/ActivityThread");
    if (!env->ExceptionCheck() && at_cls) {
        jmethodID cur_at = env->GetStaticMethodID(at_cls, "currentActivityThread", "()Landroid/app/ActivityThread;");
        if (!env->ExceptionCheck() && cur_at) {
            jobject at_obj = env->CallStaticObjectMethod(at_cls, cur_at);
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
                                if (!env->ExceptionCheck() && app_res) patch_resources_or_impl_obj(env, app_res, dpi);
                                else env->ExceptionClear();
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

                jfieldID acts_fid = env->GetFieldID(at_cls, "mActivities", "Landroid/util/ArrayMap;");
                if (!env->ExceptionCheck() && acts_fid) {
                    jobject acts_map = env->GetObjectField(at_obj, acts_fid);
                    if (!env->ExceptionCheck() && acts_map) {
                        jclass map_cls = env->GetObjectClass(acts_map);
                        if (!env->ExceptionCheck() && map_cls) {
                            jmethodID sz_mid = env->GetMethodID(map_cls, "size", "()I");
                            jmethodID val_mid = env->GetMethodID(map_cls, "valueAt", "(I)Ljava/lang/Object;");
                            if (!env->ExceptionCheck() && sz_mid && val_mid) {
                                jint act_count = env->CallIntMethod(acts_map, sz_mid);
                                if (!env->ExceptionCheck() && act_count > 0 && act_count < 32) {
                                    for (jint i = 0; i < act_count; ++i) {
                                        jobject acr = env->CallObjectMethod(acts_map, val_mid, i);
                                        if (!env->ExceptionCheck() && acr) {
                                            jclass acr_cls = env->GetObjectClass(acr);
                                            if (!env->ExceptionCheck() && acr_cls) {
                                                jfieldID act_fid = env->GetFieldID(acr_cls, "activity", "Landroid/app/Activity;");
                                                if (!env->ExceptionCheck() && act_fid) {
                                                    jobject act = env->GetObjectField(acr, act_fid);
                                                    if (!env->ExceptionCheck() && act) {
                                                        jclass act_cls = env->GetObjectClass(act);
                                                        if (!env->ExceptionCheck() && act_cls) {
                                                            jmethodID get_res = env->GetMethodID(act_cls, "getResources", "()Landroid/content/res/Resources;");
                                                            if (!env->ExceptionCheck() && get_res) {
                                                                jobject a_res = env->CallObjectMethod(act, get_res);
                                                                if (!env->ExceptionCheck() && a_res) {
                                                                    patch_resources_or_impl_obj(env, a_res, dpi);
                                                                    env->DeleteLocalRef(a_res);
                                                                } else {
                                                                    env->ExceptionClear();
                                                                }
                                                            } else {
                                                                env->ExceptionClear();
                                                            }

                                                            jmethodID get_wm = env->GetMethodID(act_cls, "getWindowManager", "()Landroid/view/WindowManager;");
                                                            if (!env->ExceptionCheck() && get_wm) {
                                                                jobject wm = env->CallObjectMethod(act, get_wm);
                                                                if (!env->ExceptionCheck() && wm) {
                                                                    jclass wm_cls = env->GetObjectClass(wm);
                                                                    jmethodID get_disp = wm_cls
                                                                        ? env->GetMethodID(wm_cls, "getDefaultDisplay", "()Landroid/view/Display;")
                                                                        : nullptr;
                                                                    if (!env->ExceptionCheck() && get_disp) {
                                                                        jobject disp = env->CallObjectMethod(wm, get_disp);
                                                                        if (!env->ExceptionCheck() && disp) {
                                                                            patch_display_obj(env, disp, dpi);
                                                                            env->DeleteLocalRef(disp);
                                                                        } else {
                                                                            env->ExceptionClear();
                                                                        }
                                                                    } else {
                                                                        env->ExceptionClear();
                                                                    }
                                                                    if (wm_cls) env->DeleteLocalRef(wm_cls);
                                                                    env->DeleteLocalRef(wm);
                                                                } else {
                                                                    env->ExceptionClear();
                                                                }
                                                            } else {
                                                                env->ExceptionClear();
                                                            }
                                                            env->DeleteLocalRef(act_cls);
                                                        } else {
                                                            env->ExceptionClear();
                                                        }
                                                        env->DeleteLocalRef(act);
                                                    } else {
                                                        env->ExceptionClear();
                                                    }
                                                } else {
                                                    env->ExceptionClear();
                                                }
                                                env->DeleteLocalRef(acr_cls);
                                            } else {
                                                env->ExceptionClear();
                                            }
                                            env->DeleteLocalRef(acr);
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

inline void *runtime_sync_guardian_worker(void *) {
    JNIEnv *env = nullptr;
    bool need_dpi = (g_cfg.build.enabled && g_cfg.build.app_dpi >= 120 && g_cfg.build.app_dpi <= 960);
    if (!need_dpi || !g_jvm) return nullptr;

    JavaVMAttachArgs args{JNI_VERSION_1_6, "MirageDpiSync", nullptr};
    if (g_jvm->AttachCurrentThreadAsDaemon(&env, &args) != JNI_OK || !env) {
        return nullptr;
    }

    const useconds_t delays_us[] = {
        2000, 6000, 14000, 30000, 60000, 120000, 250000, 500000, 1000000, 2000000
    };
    bool logged = false;

    for (useconds_t d : delays_us) {
        usleep(d);
        if (apply_app_dpi_virtualization(env) && !logged) {
            logged = true;
            emit_probe("DPI_SPOOF", "AssetManager2+DisplayCache.densityDpi", std::to_string(g_cfg.build.app_dpi));
        }
    }

    g_jvm->DetachCurrentThread();
    return nullptr;
}

inline void start_runtime_guardian_after_specialize(JNIEnv *env) {
    if (!g_cfg.build.enabled || g_cfg.build.app_dpi < 120 || g_cfg.build.app_dpi > 960) return;

    apply_app_dpi_virtualization(env);

    pthread_t tid;
    if (pthread_create(&tid, nullptr, runtime_sync_guardian_worker, nullptr) == 0) {
        pthread_detach(tid);
    }
}

} // namespace mirage

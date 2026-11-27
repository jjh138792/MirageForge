// SPDX-License-Identifier: GPL-3.0-only
// Copyright (c) 2025-2026 jjh138792
// MirageForge: https://github.com/jjh138792/MirageForge

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

// 仅在 postAppSpecialize 之后、且开启刷新率 (>=90Hz) 或 HDR/杜比视界声明时懒修补 DisplayInfo 缓存，
// 绝不在 Zygote 阶段主动触发 Binder IPC，未开启时严格 0 改动
inline void try_lazy_spoof_display_modes() {
    int target_hz = g_cfg.gl.max_refresh_rate;
    bool want_fps = (target_hz >= 90 && target_hz <= 240);
    bool want_hdr = g_cfg.gl.enable_hdr_spoof;

    if ((!g_cfg.build.enabled && !g_cfg.gl.enabled) || (!want_fps && !want_hdr) || !g_jvm) return;
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
        env->ExceptionClear();
        env->DeleteLocalRef(dmg_cls);
        return;
    }

    bool fps_patched = false;
    bool hdr_patched = false;

    jmethodID get_di = env->GetMethodID(dmg_cls, "getDisplayInfo", "(I)Landroid/view/DisplayInfo;");
    if (get_di) {
        jobject di = env->CallObjectMethod(dmg, get_di, 0);
        if (!env->ExceptionCheck() && di) {
            jclass di_cls = env->GetObjectClass(di);
            if (di_cls) {
                // 1. 屏幕最高刷新率声明修补
                if (want_fps) {
                    float fps = static_cast<float>(target_hz);
                    jclass mode_cls = env->FindClass("android/view/Display$Mode");
                    if (env->ExceptionCheck()) { env->ExceptionClear(); mode_cls = nullptr; }

                    jfieldID mode_rr_fid = mode_cls ? env->GetFieldID(mode_cls, "mRefreshRate", "F") : nullptr;
                    if (env->ExceptionCheck()) { env->ExceptionClear(); mode_rr_fid = nullptr; }

                    jfieldID mode_vsync_fid = mode_cls ? env->GetFieldID(mode_cls, "mVsyncRate", "F") : nullptr;
                    if (env->ExceptionCheck()) { env->ExceptionClear(); mode_vsync_fid = nullptr; }

                    jfieldID ovr_fid = env->GetFieldID(di_cls, "refreshRateOverride", "F");
                    if (!env->ExceptionCheck() && ovr_fid) {
                        env->SetFloatField(di, ovr_fid, fps);
                        fps_patched = true;
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
                                    fps_patched = true;
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
                }

                // 2. HDR10 / HLG / HDR10+ / 杜比视界能力声明修补
                if (want_hdr) {
                    jfieldID hdr_fid = env->GetFieldID(di_cls, "hdrCapabilities", "Landroid/view/Display$HdrCapabilities;");
                    if (!env->ExceptionCheck() && hdr_fid) {
                        jclass hdr_cls = env->FindClass("android/view/Display$HdrCapabilities");
                        if (!env->ExceptionCheck() && hdr_cls) {
                            // 1=Dolby Vision, 2=HDR10, 3=HLG, 4=HDR10+
                            static const jint kHdrTypes[4] = {1, 2, 3, 4};
                            jintArray types_arr = env->NewIntArray(4);
                            if (types_arr) {
                                env->SetIntArrayRegion(types_arr, 0, 4, kHdrTypes);

                                jmethodID ctor = env->GetMethodID(hdr_cls, "<init>", "([IFFF)V");
                                if (!env->ExceptionCheck() && ctor) {
                                    jobject new_hdr = env->NewObject(hdr_cls, ctor, types_arr, 1600.0f, 800.0f, 0.0005f);
                                    if (!env->ExceptionCheck() && new_hdr) {
                                        env->SetObjectField(di, hdr_fid, new_hdr);
                                        env->DeleteLocalRef(new_hdr);
                                        hdr_patched = true;
                                    } else {
                                        env->ExceptionClear();
                                    }
                                } else {
                                    env->ExceptionClear();
                                }

                                // 降级路径：若构造函数签名差异，直接原地改写现有 HdrCapabilities 实例字段
                                if (!hdr_patched) {
                                    jobject existing_hdr = env->GetObjectField(di, hdr_fid);
                                    if (!env->ExceptionCheck() && existing_hdr) {
                                        jfieldID types_fid = env->GetFieldID(hdr_cls, "mSupportedHdrTypes", "[I");
                                        if (!env->ExceptionCheck() && types_fid) {
                                            env->SetObjectField(existing_hdr, types_fid, types_arr);
                                            hdr_patched = true;
                                        } else {
                                            env->ExceptionClear();
                                        }
                                        env->DeleteLocalRef(existing_hdr);
                                    } else {
                                        env->ExceptionClear();
                                    }
                                }
                                env->DeleteLocalRef(types_arr);
                            }
                            env->DeleteLocalRef(hdr_cls);
                        } else {
                            env->ExceptionClear();
                        }
                    } else {
                        env->ExceptionClear();
                    }
                }

                env->DeleteLocalRef(di_cls);
            }
            env->DeleteLocalRef(di);
        } else {
            env->ExceptionClear();
        }
    } else {
        env->ExceptionClear();
    }

    env->DeleteLocalRef(dmg);
    env->DeleteLocalRef(dmg_cls);

    if (fps_patched || hdr_patched) {
        g_fps_spoof_applied.store(true, std::memory_order_release);
        if (fps_patched) {
            emit_probe("DISPLAY_FPS", "DisplayInfo.supportedModes", std::to_string(target_hz) + "Hz");
        }
        if (hdr_patched) {
            emit_probe("DISPLAY_HDR", "DisplayInfo.hdrCapabilities", "DV/HDR10/HLG/HDR10+");
        }
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
                    if (sdk >= 0) set_int(ver_cls, kv.first.c_str(), sdk);
                }
            } else if (kv.first == "SECURITY_PATCH" ||
                       kv.first == "INCREMENTAL" ||
                       kv.first == "BASE_OS") {
                set_str(ver_cls, kv.first.c_str(), kv.second);
            } else if (kv.first == "CODENAME" && g_cfg.build.force_spoof_sdk) {
                set_str(ver_cls, "CODENAME", kv.second);
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

// SPDX-License-Identifier: GPL-3.0-only
// Copyright (c) 2025-2026 jjh138792
// MirageForge: https://github.com/jjh138792/MirageForge

#pragma once

#include <cstdlib>
#include <cstdint>
#include <cstring>
#include <atomic>
#include <jni.h>
#include "zygisk.hpp"
#include "mirage_config.hpp"
#include "mirage_logger.hpp"

namespace mirage {

inline JavaVM *g_jvm = nullptr;
inline std::atomic<bool> g_post_specialized{false};
inline std::atomic<bool> g_fps_spoof_applied{false};
inline std::atomic<bool> g_binder_mem_hooked{false};

// ============================================================================
// Java 层 ActivityManager.getMemoryInfo 精准拦截（基于 Zygisk hookJniNativeMethods）
// - 拦截 BinderProxy.transactNative，非目标事务仅做 1 次整数判断，零性能损耗
// - 复用设备自身的 MemoryInfo.readFromParcel / writeToParcel，100% 兼容 Android 8 ~ 17
// ============================================================================
using PFN_BinderProxy_transactNative = jboolean (*)(
    JNIEnv *env, jobject thiz, jint code, jobject dataObj, jobject replyObj, jint flags);

inline PFN_BinderProxy_transactNative orig_BinderProxy_transactNative = nullptr;
inline jint g_tx_get_memory_info = -1;

inline jclass g_cls_memory_info = nullptr;
inline jmethodID g_mid_meminfo_ctor = nullptr;
inline jmethodID g_mid_meminfo_read = nullptr;
inline jmethodID g_mid_meminfo_write = nullptr;
inline jfieldID g_fid_meminfo_avail = nullptr;
inline jfieldID g_fid_meminfo_total = nullptr;
inline jfieldID g_fid_meminfo_thresh = nullptr;
inline jfieldID g_fid_meminfo_low = nullptr;
inline jfieldID g_fid_meminfo_advertised = nullptr;

inline jclass g_cls_parcel = nullptr;
inline jmethodID g_mid_parcel_data_pos = nullptr;
inline jmethodID g_mid_parcel_set_pos = nullptr;
inline jmethodID g_mid_parcel_set_size = nullptr;
inline jmethodID g_mid_parcel_read_int = nullptr;
inline jmethodID g_mid_parcel_write_no_ex = nullptr;
inline jmethodID g_mid_parcel_write_int = nullptr;

inline jclass g_cls_binder_proxy = nullptr;
inline jmethodID g_mid_bp_get_desc = nullptr;

inline jboolean hook_BinderProxy_transactNative(
    JNIEnv *env, jobject thiz, jint code, jobject dataObj, jobject replyObj, jint flags) {
    if (!orig_BinderProxy_transactNative) return JNI_FALSE;
    jboolean ok = orig_BinderProxy_transactNative(env, thiz, code, dataObj, replyObj, flags);

    if (__builtin_expect(
            !ok || !env || !thiz || !replyObj ||
            code != g_tx_get_memory_info ||
            !g_cfg.vfs.enable_mem || g_cfg.vfs.ram_gb <= 0,
            1)) {
        return ok;
    }

    ScopedHookGuard guard;
    if (!guard || env->ExceptionCheck()) return ok;

    // 校验目标接口确为 android.app.IActivityManager（防止与其他 Binder 接口事务号碰撞）
    bool is_am = false;
    auto desc_obj = static_cast<jstring>(env->CallObjectMethod(thiz, g_mid_bp_get_desc));
    if (env->ExceptionCheck()) {
        env->ExceptionClear();
    } else if (desc_obj) {
        const char *desc_c = env->GetStringUTFChars(desc_obj, nullptr);
        if (desc_c) {
            is_am = (std::strcmp(desc_c, "android.app.IActivityManager") == 0);
            env->ReleaseStringUTFChars(desc_obj, desc_c);
        }
        env->DeleteLocalRef(desc_obj);
    }
    if (!is_am) return ok;

    jint orig_pos = env->CallIntMethod(replyObj, g_mid_parcel_data_pos);
    if (env->ExceptionCheck()) {
        env->ExceptionClear();
        return ok;
    }

    auto restore_pos = [&]() {
        if (env->ExceptionCheck()) env->ExceptionClear();
        env->CallVoidMethod(replyObj, g_mid_parcel_set_pos, orig_pos);
        if (env->ExceptionCheck()) env->ExceptionClear();
    };

    env->CallVoidMethod(replyObj, g_mid_parcel_set_pos, static_cast<jint>(0));
    jint ex_code = env->CallIntMethod(replyObj, g_mid_parcel_read_int);
    if (env->ExceptionCheck() || ex_code != 0) {
        restore_pos();
        return ok;
    }

    jint has_info = env->CallIntMethod(replyObj, g_mid_parcel_read_int);
    if (env->ExceptionCheck() || has_info == 0) {
        restore_pos();
        return ok;
    }

    jobject mem_info = env->NewObject(g_cls_memory_info, g_mid_meminfo_ctor);
    if (env->ExceptionCheck() || !mem_info) {
        restore_pos();
        return ok;
    }

    env->CallVoidMethod(mem_info, g_mid_meminfo_read, replyObj);
    if (env->ExceptionCheck()) {
        env->DeleteLocalRef(mem_info);
        restore_pos();
        return ok;
    }

    jlong real_total  = env->GetLongField(mem_info, g_fid_meminfo_total);
    jlong real_avail  = env->GetLongField(mem_info, g_fid_meminfo_avail);
    jlong real_thresh = env->GetLongField(mem_info, g_fid_meminfo_thresh);

    uint64_t phys_bytes = static_cast<uint64_t>(g_cfg.vfs.ram_gb) * 1024ULL * 1024ULL * 1024ULL;
    uint64_t target_total = (phys_bytes * 935ULL) / 1000ULL;

    double avail_ratio = 0.57;
    if (real_total > 0 && real_avail > 0 && real_avail <= real_total) {
        avail_ratio = static_cast<double>(real_avail) / static_cast<double>(real_total);
        if (avail_ratio < 0.15) avail_ratio = 0.15;
        if (avail_ratio > 0.85) avail_ratio = 0.85;
    }
    uint64_t target_avail = static_cast<uint64_t>(static_cast<double>(target_total) * avail_ratio);

    uint64_t target_thresh = target_total / 16ULL;
    if (real_total > 0 && real_thresh > 0 && real_thresh < real_total) {
        double t_ratio = static_cast<double>(real_thresh) / static_cast<double>(real_total);
        if (t_ratio < 0.02) t_ratio = 0.02;
        if (t_ratio > 0.25) t_ratio = 0.25;
        target_thresh = static_cast<uint64_t>(static_cast<double>(target_total) * t_ratio);
    }

    env->SetLongField(mem_info, g_fid_meminfo_total, static_cast<jlong>(target_total));
    env->SetLongField(mem_info, g_fid_meminfo_avail, static_cast<jlong>(target_avail));
    env->SetLongField(mem_info, g_fid_meminfo_thresh, static_cast<jlong>(target_thresh));
    if (g_fid_meminfo_low) {
        env->SetBooleanField(mem_info, g_fid_meminfo_low,
                             target_avail <= target_thresh ? JNI_TRUE : JNI_FALSE);
    }
    if (g_fid_meminfo_advertised) {
        env->SetLongField(mem_info, g_fid_meminfo_advertised, static_cast<jlong>(phys_bytes));
    }

    // 原地重置 reply Parcel 并重新序列化（PARCELABLE_WRITE_RETURN_VALUE = 1）
    env->CallVoidMethod(replyObj, g_mid_parcel_set_size, static_cast<jint>(0));
    env->CallVoidMethod(replyObj, g_mid_parcel_set_pos, static_cast<jint>(0));
    env->CallVoidMethod(replyObj, g_mid_parcel_write_no_ex);
    env->CallVoidMethod(replyObj, g_mid_parcel_write_int, static_cast<jint>(1));
    env->CallVoidMethod(mem_info, g_mid_meminfo_write, replyObj, static_cast<jint>(1));
    env->CallVoidMethod(replyObj, g_mid_parcel_set_pos, static_cast<jint>(0));

    if (env->ExceptionCheck()) {
        env->ExceptionClear();
    } else if (__builtin_expect(g_cfg.probe.enabled, 0)) {
        emit_probe("AM_MEMINFO", "ActivityManager.getMemoryInfo",
                   std::to_string(g_cfg.vfs.ram_gb) + "GB (avail=" +
                       std::to_string(target_avail / (1024ULL * 1024ULL)) + "MB)");
    }

    env->DeleteLocalRef(mem_info);
    return ok;
}

inline bool install_java_memory_info_hook(zygisk::Api *api, JNIEnv *env) {
    if (!api || !env || !g_cfg.vfs.enable_mem || g_cfg.vfs.ram_gb <= 0) return false;
    if (g_binder_mem_hooked.load(std::memory_order_acquire)) return true;

    jclass stub_cls = env->FindClass("android/app/IActivityManager$Stub");
    if (env->ExceptionCheck() || !stub_cls) {
        env->ExceptionClear();
        return false;
    }
    jfieldID tx_fid = env->GetStaticFieldID(stub_cls, "TRANSACTION_getMemoryInfo", "I");
    if (env->ExceptionCheck() || !tx_fid) {
        env->ExceptionClear();
        env->DeleteLocalRef(stub_cls);
        emit_probe("AM_MEMINFO", "TRANSACTION_getMemoryInfo", "field_not_found");
        return false;
    }
    jint tx_code = env->GetStaticIntField(stub_cls, tx_fid);
    env->DeleteLocalRef(stub_cls);
    if (env->ExceptionCheck() || tx_code <= 0) {
        env->ExceptionClear();
        return false;
    }

    jclass mi_local = env->FindClass("android/app/ActivityManager$MemoryInfo");
    jclass p_local  = env->FindClass("android/os/Parcel");
    jclass bp_local = env->FindClass("android/os/BinderProxy");
    if (env->ExceptionCheck() || !mi_local || !p_local || !bp_local) {
        env->ExceptionClear();
        if (mi_local) env->DeleteLocalRef(mi_local);
        if (p_local) env->DeleteLocalRef(p_local);
        if (bp_local) env->DeleteLocalRef(bp_local);
        return false;
    }

    g_cls_memory_info  = static_cast<jclass>(env->NewGlobalRef(mi_local));
    g_cls_parcel       = static_cast<jclass>(env->NewGlobalRef(p_local));
    g_cls_binder_proxy = static_cast<jclass>(env->NewGlobalRef(bp_local));
    env->DeleteLocalRef(mi_local);
    env->DeleteLocalRef(p_local);
    env->DeleteLocalRef(bp_local);

    if (!g_cls_memory_info || !g_cls_parcel || !g_cls_binder_proxy) {
        return false;
    }

    g_mid_meminfo_ctor   = env->GetMethodID(g_cls_memory_info, "<init>", "()V");
    g_mid_meminfo_read   = env->GetMethodID(g_cls_memory_info, "readFromParcel", "(Landroid/os/Parcel;)V");
    g_mid_meminfo_write  = env->GetMethodID(g_cls_memory_info, "writeToParcel", "(Landroid/os/Parcel;I)V");
    g_fid_meminfo_avail  = env->GetFieldID(g_cls_memory_info, "availMem", "J");
    g_fid_meminfo_total  = env->GetFieldID(g_cls_memory_info, "totalMem", "J");
    g_fid_meminfo_thresh = env->GetFieldID(g_cls_memory_info, "threshold", "J");
    g_fid_meminfo_low    = env->GetFieldID(g_cls_memory_info, "lowMemory", "Z");
    if (env->ExceptionCheck()) {
        env->ExceptionClear();
        return false;
    }

    // Android 14+ (API 34+) 新增 advertisedMem 字段，低版本安全忽略
    g_fid_meminfo_advertised = env->GetFieldID(g_cls_memory_info, "advertisedMem", "J");
    if (env->ExceptionCheck()) {
        env->ExceptionClear();
        g_fid_meminfo_advertised = nullptr;
    }

    g_mid_parcel_data_pos    = env->GetMethodID(g_cls_parcel, "dataPosition", "()I");
    g_mid_parcel_set_pos     = env->GetMethodID(g_cls_parcel, "setDataPosition", "(I)V");
    g_mid_parcel_set_size    = env->GetMethodID(g_cls_parcel, "setDataSize", "(I)V");
    g_mid_parcel_read_int    = env->GetMethodID(g_cls_parcel, "readInt", "()I");
    g_mid_parcel_write_no_ex = env->GetMethodID(g_cls_parcel, "writeNoException", "()V");
    g_mid_parcel_write_int   = env->GetMethodID(g_cls_parcel, "writeInt", "(I)V");
    g_mid_bp_get_desc        = env->GetMethodID(g_cls_binder_proxy, "getInterfaceDescriptor", "()Ljava/lang/String;");

    if (env->ExceptionCheck() ||
        !g_mid_meminfo_ctor || !g_mid_meminfo_read || !g_mid_meminfo_write ||
        !g_fid_meminfo_avail || !g_fid_meminfo_total || !g_fid_meminfo_thresh ||
        !g_mid_parcel_data_pos || !g_mid_parcel_set_pos || !g_mid_parcel_set_size ||
        !g_mid_parcel_read_int || !g_mid_parcel_write_no_ex || !g_mid_parcel_write_int ||
        !g_mid_bp_get_desc) {
        env->ExceptionClear();
        return false;
    }

    g_tx_get_memory_info = tx_code;

    JNINativeMethod method = {
        "transactNative",
        "(ILandroid/os/Parcel;Landroid/os/Parcel;I)Z",
        reinterpret_cast<void *>(&hook_BinderProxy_transactNative),
    };
    api->hookJniNativeMethods(env, "android/os/BinderProxy", &method, 1);
    if (env->ExceptionCheck()) {
        env->ExceptionClear();
        return false;
    }

    if (method.fnPtr != nullptr &&
        method.fnPtr != reinterpret_cast<void *>(&hook_BinderProxy_transactNative)) {
        orig_BinderProxy_transactNative = reinterpret_cast<PFN_BinderProxy_transactNative>(method.fnPtr);
        g_binder_mem_hooked.store(true, std::memory_order_release);
        emit_probe("AM_MEMINFO", "BinderProxy.transactNative",
                   "Hooked (tx=" + std::to_string(tx_code) + ", target=" + std::to_string(g_cfg.vfs.ram_gb) + "GB)");
        return true;
    }
    return false;
}

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

                if (want_hdr) {
                    jfieldID hdr_fid = env->GetFieldID(di_cls, "hdrCapabilities", "Landroid/view/Display$HdrCapabilities;");
                    if (!env->ExceptionCheck() && hdr_fid) {
                        jclass hdr_cls = env->FindClass("android/view/Display$HdrCapabilities");
                        if (!env->ExceptionCheck() && hdr_cls) {
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

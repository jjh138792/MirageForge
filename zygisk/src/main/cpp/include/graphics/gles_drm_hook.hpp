#pragma once

#include <cstdint>
#include <cstring>
#include <dlfcn.h>
#include "mirage_config.hpp"
#include "mirage_logger.hpp"
#include "jvm_art_engine.hpp"

#ifndef GL_VENDOR
#define GL_VENDOR                   0x1F00
#define GL_RENDERER                 0x1F01
#define GL_VERSION                  0x1F02
#define GL_SHADING_LANGUAGE_VERSION 0x8B8C
#endif

namespace mirage {

using GLenum = unsigned int;
using GLubyte = unsigned char;
using GLuint = unsigned int;
using __eglMustCastToProperFunctionPointerType = void (*)(void);

using media_status_t = int32_t;
using PFN_AMediaDrm_getPropertyString = media_status_t (*)(void *drm, const char *propertyName, const char **propertyValue);
using PFN_loader_dlsym = void *(*)(void *handle, const char *symbol, const void *caller_addr);
using PFN_dlsym = void *(*)(void *handle, const char *symbol);

inline const GLubyte *(*orig_glGetString)(GLenum name) = nullptr;
inline const GLubyte *(*orig_glGetStringi)(GLenum name, GLuint index) = nullptr;
inline __eglMustCastToProperFunctionPointerType (*orig_eglGetProcAddress)(const char *procname) = nullptr;
inline PFN_AMediaDrm_getPropertyString orig_AMediaDrm_getPropertyString = nullptr;
inline PFN_loader_dlsym orig_loader_dlsym = nullptr;
inline PFN_dlsym orig_dlsym = nullptr;

inline void init_gles_drm_symbols() {
    if (orig_glGetString) return;
    ErrnoRestorer errno_guard;
    orig_loader_dlsym = reinterpret_cast<PFN_loader_dlsym>(dlsym(RTLD_DEFAULT, "__loader_dlsym"));
    orig_dlsym = reinterpret_cast<PFN_dlsym>(dlsym(RTLD_DEFAULT, "dlsym"));
    orig_glGetString = reinterpret_cast<decltype(orig_glGetString)>(dlsym(RTLD_DEFAULT, "glGetString"));
    orig_glGetStringi = reinterpret_cast<decltype(orig_glGetStringi)>(dlsym(RTLD_DEFAULT, "glGetStringi"));
    orig_eglGetProcAddress = reinterpret_cast<decltype(orig_eglGetProcAddress)>(dlsym(RTLD_DEFAULT, "eglGetProcAddress"));
    orig_AMediaDrm_getPropertyString = reinterpret_cast<decltype(orig_AMediaDrm_getPropertyString)>(dlsym(RTLD_DEFAULT, "AMediaDrm_getPropertyString"));
}

inline const GLubyte *hook_glGetString(GLenum name) {
    if (g_cfg.gl.enabled) {
        try_lazy_spoof_display_modes();
        switch (name) {
            case GL_VENDOR:
                if (!g_cfg.gl.gl_vendor.empty()) {
                    ScopedHookGuard guard;
                    if (guard) emit_probe("GLES", "GL_VENDOR", g_cfg.gl.gl_vendor);
                    return reinterpret_cast<const GLubyte *>(g_cfg.gl.gl_vendor.c_str());
                }
                break;
            case GL_RENDERER:
                if (!g_cfg.gl.gl_renderer.empty()) {
                    ScopedHookGuard guard;
                    if (guard) emit_probe("GLES", "GL_RENDERER", g_cfg.gl.gl_renderer);
                    return reinterpret_cast<const GLubyte *>(g_cfg.gl.gl_renderer.c_str());
                }
                break;
            case GL_VERSION:
                if (!g_cfg.gl.gl_version.empty()) {
                    ScopedHookGuard guard;
                    if (guard) emit_probe("GLES", "GL_VERSION", g_cfg.gl.gl_version);
                    return reinterpret_cast<const GLubyte *>(g_cfg.gl.gl_version.c_str());
                }
                break;
            case GL_SHADING_LANGUAGE_VERSION:
                if (!g_cfg.gl.gl_glsl_version.empty()) {
                    return reinterpret_cast<const GLubyte *>(g_cfg.gl.gl_glsl_version.c_str());
                }
                break;
            default:
                break;
        }
    }
    return orig_glGetString ? orig_glGetString(name) : nullptr;
}

inline __eglMustCastToProperFunctionPointerType hook_eglGetProcAddress(const char *procname) {
    if (g_cfg.gl.enabled) {
        try_lazy_spoof_display_modes();
    }
    __eglMustCastToProperFunctionPointerType real_fn = orig_eglGetProcAddress ? orig_eglGetProcAddress(procname) : nullptr;
    if (procname && g_cfg.gl.enabled) {
        if (std::strcmp(procname, "glGetString") == 0) {
            if (real_fn && !orig_glGetString) orig_glGetString = reinterpret_cast<decltype(orig_glGetString)>(real_fn);
            return reinterpret_cast<__eglMustCastToProperFunctionPointerType>(hook_glGetString);
        }
    }
    return real_fn;
}

inline media_status_t hook_AMediaDrm_getPropertyString(void *drm, const char *propertyName, const char **propertyValue) {
    media_status_t st = orig_AMediaDrm_getPropertyString
                            ? orig_AMediaDrm_getPropertyString(drm, propertyName, propertyValue)
                            : -1;
    if (st == 0 && propertyName && propertyValue && g_cfg.gl.enabled) {
        if (std::strcmp(propertyName, "vendor") == 0 && !g_cfg.gl.gl_vendor.empty()) {
            *propertyValue = g_cfg.gl.gl_vendor.c_str();
            ScopedHookGuard guard;
            if (guard) emit_probe("MEDIADRM", "vendor", g_cfg.gl.gl_vendor);
        } else if (std::strcmp(propertyName, "description") == 0 && !g_cfg.gl.gl_renderer.empty()) {
            *propertyValue = g_cfg.gl.gl_renderer.c_str();
            ScopedHookGuard guard;
            if (guard) emit_probe("MEDIADRM", "description", g_cfg.gl.gl_renderer);
        }
    }
    return st;
}

} // namespace mirage

#pragma once

#include <cstdint>
#include "mirage_protocol.hpp"

// 标准 OpenGL ES 常量定义（避免强依赖链接 libGLESv2.so）
#ifndef GL_VENDOR
#define GL_VENDOR     0x1F00
#define GL_RENDERER   0x1F01
#define GL_VERSION    0x1F02
#define GL_EXTENSIONS 0x1F03
#endif

namespace mirage {

using GLenum = unsigned int;
using GLubyte = unsigned char;

inline const GLubyte *(*orig_glGetString)(GLenum name) = nullptr;

inline const GLubyte *hook_glGetString(GLenum name) {
    if (g_profile.enable_gl_spoof) {
        if (name == GL_VENDOR && !g_profile.gl_vendor.empty()) {
            record_probe_event("GLES", "GL_VENDOR", g_profile.gl_vendor);
            return reinterpret_cast<const GLubyte *>(g_profile.gl_vendor.c_str());
        }
        if (name == GL_RENDERER && !g_profile.gl_renderer.empty()) {
            record_probe_event("GLES", "GL_RENDERER", g_profile.gl_renderer);
            return reinterpret_cast<const GLubyte *>(g_profile.gl_renderer.c_str());
        }
        if (name == GL_VERSION && !g_profile.gl_version.empty()) {
            record_probe_event("GLES", "GL_VERSION", g_profile.gl_version);
            return reinterpret_cast<const GLubyte *>(g_profile.gl_version.c_str());
        }
    }
    return orig_glGetString ? orig_glGetString(name) : nullptr;
}

} // namespace mirage

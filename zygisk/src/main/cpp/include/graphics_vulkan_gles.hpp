#pragma once

#include <cstdint>
#include <cstring>
#include "mirage_shared.hpp"

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

// Khronos Vulkan 标准 ABI 结构内联定义（零外部头文件依赖）
constexpr uint32_t VK_MAX_PHYSICAL_DEVICE_NAME_SIZE = 256U;
constexpr uint32_t VK_UUID_SIZE = 16U;

struct VkPhysicalDeviceLimitsLite {
    uint8_t opaque[504]; // 占位对齐，我们只修改头部字段
};

struct VkPhysicalDeviceSparsePropertiesLite {
    uint32_t residencyStandard2DBlockShape;
    uint32_t residencyStandard2DMultisampleBlockShape;
    uint32_t residencyStandard3DBlockShape;
    uint32_t residencyAlignedMipSize;
    uint32_t residencyNonResidentStrict;
};

struct VkPhysicalDevicePropertiesCompat {
    uint32_t apiVersion;
    uint32_t driverVersion;
    uint32_t vendorID;
    uint32_t deviceID;
    uint32_t deviceType;
    char deviceName[VK_MAX_PHYSICAL_DEVICE_NAME_SIZE];
    uint8_t pipelineCacheUUID[VK_UUID_SIZE];
};

struct VkPhysicalDeviceProperties2Compat {
    uint32_t sType;
    void *pNext;
    VkPhysicalDevicePropertiesCompat properties;
};

using PFN_vkVoidFunction = void (*)(void);
using PFN_vkGetInstanceProcAddr = PFN_vkVoidFunction (*)(void *instance, const char *pName);
using PFN_vkGetPhysicalDeviceProperties = void (*)(void *physicalDevice, VkPhysicalDevicePropertiesCompat *pProperties);
using PFN_vkGetPhysicalDeviceProperties2 = void (*)(void *physicalDevice, VkPhysicalDeviceProperties2Compat *pProperties);

inline const GLubyte *(*orig_glGetString)(GLenum name) = nullptr;
inline const GLubyte *(*orig_glGetStringi)(GLenum name, GLuint index) = nullptr;
inline __eglMustCastToProperFunctionPointerType (*orig_eglGetProcAddress)(const char *procname) = nullptr;

inline PFN_vkGetInstanceProcAddr orig_vkGetInstanceProcAddr = nullptr;
inline PFN_vkGetPhysicalDeviceProperties orig_vkGetPhysicalDeviceProperties = nullptr;
inline PFN_vkGetPhysicalDeviceProperties2 orig_vkGetPhysicalDeviceProperties2 = nullptr;

inline const GLubyte *hook_glGetString(GLenum name) {
    if (g_ctx.enable_gl_vulkan) {
        switch (name) {
            case GL_VENDOR:
                if (!g_ctx.gl_vendor.empty()) {
                    emit_probe("GLES", "GL_VENDOR", g_ctx.gl_vendor);
                    return reinterpret_cast<const GLubyte *>(g_ctx.gl_vendor.c_str());
                }
                break;
            case GL_RENDERER:
                if (!g_ctx.gl_renderer.empty()) {
                    emit_probe("GLES", "GL_RENDERER", g_ctx.gl_renderer);
                    return reinterpret_cast<const GLubyte *>(g_ctx.gl_renderer.c_str());
                }
                break;
            case GL_VERSION:
                if (!g_ctx.gl_version.empty()) {
                    emit_probe("GLES", "GL_VERSION", g_ctx.gl_version);
                    return reinterpret_cast<const GLubyte *>(g_ctx.gl_version.c_str());
                }
                break;
            case GL_SHADING_LANGUAGE_VERSION:
                if (!g_ctx.gl_glsl_version.empty()) {
                    return reinterpret_cast<const GLubyte *>(g_ctx.gl_glsl_version.c_str());
                }
                break;
            default:
                break;
        }
    }
    return orig_glGetString ? orig_glGetString(name) : nullptr;
}

// 拦截 eglGetProcAddress，防止引擎通过动态寻址绕过 libGLESv2 PLT
inline __eglMustCastToProperFunctionPointerType hook_eglGetProcAddress(const char *procname) {
    if (procname && g_ctx.enable_gl_vulkan) {
        if (std::strcmp(procname, "glGetString") == 0) {
            return reinterpret_cast<__eglMustCastToProperFunctionPointerType>(hook_glGetString);
        }
    }
    return orig_eglGetProcAddress ? orig_eglGetProcAddress(procname) : nullptr;
}

// Vulkan 物理设备属性篡改
inline void spoof_vulkan_props_struct(VkPhysicalDevicePropertiesCompat *props) {
    if (!props || !g_ctx.enable_gl_vulkan) return;
    if (!g_ctx.gl_renderer.empty()) {
        std::strncpy(props->deviceName, g_ctx.gl_renderer.c_str(), VK_MAX_PHYSICAL_DEVICE_NAME_SIZE - 1);
        props->deviceName[VK_MAX_PHYSICAL_DEVICE_NAME_SIZE - 1] = '\0';
    }
    if (g_ctx.vulkan_vendor_id != 0) {
        props->vendorID = g_ctx.vulkan_vendor_id;
    }
    if (g_ctx.vulkan_device_id != 0) {
        props->deviceID = g_ctx.vulkan_device_id;
    }
    emit_probe("VULKAN", "VkPhysicalDeviceProperties.deviceName", props->deviceName);
}

inline void hook_vkGetPhysicalDeviceProperties(void *physicalDevice, VkPhysicalDevicePropertiesCompat *pProperties) {
    if (orig_vkGetPhysicalDeviceProperties) {
        orig_vkGetPhysicalDeviceProperties(physicalDevice, pProperties);
        spoof_vulkan_props_struct(pProperties);
    }
}

inline void hook_vkGetPhysicalDeviceProperties2(void *physicalDevice, VkPhysicalDeviceProperties2Compat *pProperties) {
    if (orig_vkGetPhysicalDeviceProperties2) {
        orig_vkGetPhysicalDeviceProperties2(physicalDevice, pProperties);
        if (pProperties) {
            spoof_vulkan_props_struct(&pProperties->properties);
        }
    }
}

inline PFN_vkVoidFunction hook_vkGetInstanceProcAddr(void *instance, const char *pName) {
    PFN_vkVoidFunction real_fn = orig_vkGetInstanceProcAddr ? orig_vkGetInstanceProcAddr(instance, pName) : nullptr;
    if (!real_fn || !pName || !g_ctx.enable_gl_vulkan) return real_fn;

    if (std::strcmp(pName, "vkGetPhysicalDeviceProperties") == 0) {
        orig_vkGetPhysicalDeviceProperties = reinterpret_cast<PFN_vkGetPhysicalDeviceProperties>(real_fn);
        return reinterpret_cast<PFN_vkVoidFunction>(hook_vkGetPhysicalDeviceProperties);
    }
    if (std::strcmp(pName, "vkGetPhysicalDeviceProperties2") == 0 ||
        std::strcmp(pName, "vkGetPhysicalDeviceProperties2KHR") == 0) {
        orig_vkGetPhysicalDeviceProperties2 = reinterpret_cast<PFN_vkGetPhysicalDeviceProperties2>(real_fn);
        return reinterpret_cast<PFN_vkVoidFunction>(hook_vkGetPhysicalDeviceProperties2);
    }
    return real_fn;
}

} // namespace mirage

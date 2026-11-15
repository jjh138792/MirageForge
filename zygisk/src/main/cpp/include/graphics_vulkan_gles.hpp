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

constexpr uint32_t VK_MAX_PHYSICAL_DEVICE_NAME_SIZE = 256U;
constexpr uint32_t VK_UUID_SIZE = 16U;
constexpr uint32_t VK_MAX_DRIVER_NAME_SIZE = 256U;
constexpr uint32_t VK_MAX_DRIVER_INFO_SIZE = 256U;
constexpr uint32_t VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DRIVER_PROPERTIES = 1000196000U;

struct VkBaseOutStructureCompat {
    uint32_t sType;
    VkBaseOutStructureCompat *pNext;
};

struct VkPhysicalDeviceDriverPropertiesCompat {
    uint32_t sType;
    void *pNext;
    uint32_t driverID;
    char driverName[VK_MAX_DRIVER_NAME_SIZE];
    char driverInfo[VK_MAX_DRIVER_INFO_SIZE];
    uint32_t conformanceVersion;
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

using media_status_t = int32_t;
using PFN_AMediaDrm_getPropertyString = media_status_t (*)(void *drm, const char *propertyName, const char **propertyValue);

inline const GLubyte *(*orig_glGetString)(GLenum name) = nullptr;
inline __eglMustCastToProperFunctionPointerType (*orig_eglGetProcAddress)(const char *procname) = nullptr;

inline PFN_vkGetInstanceProcAddr orig_vkGetInstanceProcAddr = nullptr;
inline PFN_vkGetPhysicalDeviceProperties orig_vkGetPhysicalDeviceProperties = nullptr;
inline PFN_vkGetPhysicalDeviceProperties2 orig_vkGetPhysicalDeviceProperties2 = nullptr;
inline PFN_AMediaDrm_getPropertyString orig_AMediaDrm_getPropertyString = nullptr;

// 主动加载并解析 OpenGL ES、Vulkan 与 MediaDrm 全部底层符号，确保直连 PLT 调用时 orig_vk* 绝不为空
inline void init_graphics_symbols() {
    if (!orig_glGetString) {
        orig_glGetString = reinterpret_cast<decltype(orig_glGetString)>(dlsym(RTLD_DEFAULT, "glGetString"));
    }
    if (!orig_eglGetProcAddress) {
        orig_eglGetProcAddress = reinterpret_cast<decltype(orig_eglGetProcAddress)>(dlsym(RTLD_DEFAULT, "eglGetProcAddress"));
    }
    if (!orig_AMediaDrm_getPropertyString) {
        orig_AMediaDrm_getPropertyString = reinterpret_cast<decltype(orig_AMediaDrm_getPropertyString)>(
            dlsym(RTLD_DEFAULT, "AMediaDrm_getPropertyString"));
    }

    void *vk_handle = dlopen("libvulkan.so", RTLD_NOW | RTLD_LOCAL);
    auto resolve_vk = [&](const char *sym) -> void * {
        void *p = vk_handle ? dlsym(vk_handle, sym) : nullptr;
        return p ? p : dlsym(RTLD_DEFAULT, sym);
    };

    if (!orig_vkGetInstanceProcAddr) {
        orig_vkGetInstanceProcAddr = reinterpret_cast<PFN_vkGetInstanceProcAddr>(resolve_vk("vkGetInstanceProcAddr"));
    }
    if (!orig_vkGetPhysicalDeviceProperties) {
        orig_vkGetPhysicalDeviceProperties = reinterpret_cast<PFN_vkGetPhysicalDeviceProperties>(
            resolve_vk("vkGetPhysicalDeviceProperties"));
    }
    if (!orig_vkGetPhysicalDeviceProperties2) {
        orig_vkGetPhysicalDeviceProperties2 = reinterpret_cast<PFN_vkGetPhysicalDeviceProperties2>(
            resolve_vk("vkGetPhysicalDeviceProperties2"));
        if (!orig_vkGetPhysicalDeviceProperties2) {
            orig_vkGetPhysicalDeviceProperties2 = reinterpret_cast<PFN_vkGetPhysicalDeviceProperties2>(
                resolve_vk("vkGetPhysicalDeviceProperties2KHR"));
        }
    }
}

inline const GLubyte *hook_glGetString(GLenum name) {
    maybe_sync_dpi_post_bind();
    if (g_cfg.gl.enabled) {
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
    if (procname && g_cfg.gl.enabled) {
        if (std::strcmp(procname, "glGetString") == 0) {
            return reinterpret_cast<__eglMustCastToProperFunctionPointerType>(hook_glGetString);
        }
    }
    return orig_eglGetProcAddress ? orig_eglGetProcAddress(procname) : nullptr;
}

inline void spoof_vulkan_props_struct(VkPhysicalDevicePropertiesCompat *props, void *pNext = nullptr) {
    if (!props || !g_cfg.gl.enabled) return;
    if (!g_cfg.gl.gl_renderer.empty()) {
        std::strncpy(props->deviceName, g_cfg.gl.gl_renderer.c_str(), VK_MAX_PHYSICAL_DEVICE_NAME_SIZE - 1);
        props->deviceName[VK_MAX_PHYSICAL_DEVICE_NAME_SIZE - 1] = '\0';
    }
    if (g_cfg.gl.vulkan_vendor_id != 0) {
        props->vendorID = g_cfg.gl.vulkan_vendor_id;
    }
    if (g_cfg.gl.vulkan_device_id != 0) {
        props->deviceID = g_cfg.gl.vulkan_device_id;
    }

    // 同步清洗 pNext 扩展链中的 VkPhysicalDeviceDriverProperties (防止暴露 Mali / PowerVR 原始驱动名)
    auto *curr = static_cast<VkBaseOutStructureCompat *>(pNext);
    int depth = 0;
    while (curr && depth++ < 16) {
        if (curr->sType == VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DRIVER_PROPERTIES && !g_cfg.gl.gl_vendor.empty()) {
            auto *drv = reinterpret_cast<VkPhysicalDeviceDriverPropertiesCompat *>(curr);
            bool is_arm = (g_cfg.gl.vulkan_vendor_id == 0x13B5);
            drv->driverID = is_arm ? 10U : 2U; // 10: ARM Proprietary, 2: Qualcomm Proprietary
            const char *drv_name = is_arm ? "Arm Proprietary" : "Qualcomm Technologies Inc. Adreno Vulkan Driver";
            std::strncpy(drv->driverName, drv_name, VK_MAX_DRIVER_NAME_SIZE - 1);
            drv->driverName[VK_MAX_DRIVER_NAME_SIZE - 1] = '\0';
        }
        curr = curr->pNext;
    }

    ScopedHookGuard guard;
    if (guard) emit_probe("VULKAN", "VkPhysicalDeviceProperties.deviceName", props->deviceName);
}

inline void hook_vkGetPhysicalDeviceProperties(void *physicalDevice, VkPhysicalDevicePropertiesCompat *pProperties) {
    if (!orig_vkGetPhysicalDeviceProperties) {
        init_graphics_symbols();
    }
    if (orig_vkGetPhysicalDeviceProperties) {
        orig_vkGetPhysicalDeviceProperties(physicalDevice, pProperties);
        spoof_vulkan_props_struct(pProperties, nullptr);
    }
}

inline void hook_vkGetPhysicalDeviceProperties2(void *physicalDevice, VkPhysicalDeviceProperties2Compat *pProperties) {
    if (!orig_vkGetPhysicalDeviceProperties2) {
        init_graphics_symbols();
    }
    if (orig_vkGetPhysicalDeviceProperties2) {
        orig_vkGetPhysicalDeviceProperties2(physicalDevice, pProperties);
        if (pProperties) {
            spoof_vulkan_props_struct(&pProperties->properties, pProperties->pNext);
        }
    }
}

inline PFN_vkVoidFunction hook_vkGetInstanceProcAddr(void *instance, const char *pName) {
    if (!orig_vkGetInstanceProcAddr) {
        init_graphics_symbols();
    }
    PFN_vkVoidFunction real_fn = orig_vkGetInstanceProcAddr ? orig_vkGetInstanceProcAddr(instance, pName) : nullptr;
    if (!real_fn || !pName || !g_cfg.gl.enabled) return real_fn;

    if (std::strcmp(pName, "vkGetPhysicalDeviceProperties") == 0) {
        orig_vkGetPhysicalDeviceProperties = reinterpret_cast<PFN_vkGetPhysicalDeviceProperties>(real_fn);
        return reinterpret_cast<PFN_vkVoidFunction>(hook_vkGetPhysicalDeviceProperties);
    }
    if (std::strcmp(pName, "vkGetPhysicalDeviceProperties2") == 0 ||
        std::strcmp(pName, "vkGetPhysicalDeviceProperties2KHR") == 0) {
        orig_vkGetPhysicalDeviceProperties2 = reinterpret_cast<PFN_vkGetPhysicalDeviceProperties2>(real_fn);
        return reinterpret_cast<PFN_vkVoidFunction>(hook_vkGetPhysicalDeviceProperties2);
    }
    if (std::strcmp(pName, "vkGetInstanceProcAddr") == 0) {
        orig_vkGetInstanceProcAddr = reinterpret_cast<PFN_vkGetInstanceProcAddr>(real_fn);
        return reinterpret_cast<PFN_vkVoidFunction>(hook_vkGetInstanceProcAddr);
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

#pragma once

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <dlfcn.h>
#include "mirage_config.hpp"
#include "mirage_logger.hpp"

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

// Vulkan 扩展结构体的 sType 常量
constexpr uint32_t VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_PROPERTIES = 49U;
constexpr uint32_t VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DRIVER_PROPERTIES     = 1000196000U;

struct VkBaseOutStructureCompat {
    uint32_t sType;
    VkBaseOutStructureCompat *pNext;
};

struct VkConformanceVersionCompat {
    uint8_t major;
    uint8_t minor;
    uint8_t subminor;
    uint8_t patch;
};

// VK_KHR_driver_properties (sType = 1000196000)
struct VkPhysicalDeviceDriverPropertiesCompat {
    uint32_t sType;
    void *pNext;
    uint32_t driverID;
    char driverName[VK_MAX_DRIVER_NAME_SIZE];
    char driverInfo[VK_MAX_DRIVER_INFO_SIZE];
    VkConformanceVersionCompat conformanceVersion;
};

// VkPhysicalDeviceVulkan12Properties (sType = 49) 头部与 DriverProperties 完全对齐
struct VkPhysicalDeviceVulkan12PropertiesHeaderCompat {
    uint32_t sType;
    void *pNext;
    uint32_t driverID;
    char driverName[VK_MAX_DRIVER_NAME_SIZE];
    char driverInfo[VK_MAX_DRIVER_INFO_SIZE];
    VkConformanceVersionCompat conformanceVersion;
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

// 按需懒加载解析 libvulkan.so 符号：解决 preAppSpecialize 阶段 libvulkan.so 尚未加载或处于 RTLD_LOCAL 命名空间的问题
inline void ensure_vulkan_symbols_loaded() {
    if (orig_vkGetInstanceProcAddr && orig_vkGetPhysicalDeviceProperties && orig_vkGetPhysicalDeviceProperties2) {
        return;
    }

    ScopedHookGuard guard;
    if (!orig_vkGetInstanceProcAddr) {
        orig_vkGetInstanceProcAddr = reinterpret_cast<PFN_vkGetInstanceProcAddr>(
            dlsym(RTLD_DEFAULT, "vkGetInstanceProcAddr"));
    }
    if (!orig_vkGetPhysicalDeviceProperties) {
        orig_vkGetPhysicalDeviceProperties = reinterpret_cast<PFN_vkGetPhysicalDeviceProperties>(
            dlsym(RTLD_DEFAULT, "vkGetPhysicalDeviceProperties"));
    }
    if (!orig_vkGetPhysicalDeviceProperties2) {
        orig_vkGetPhysicalDeviceProperties2 = reinterpret_cast<PFN_vkGetPhysicalDeviceProperties2>(
            dlsym(RTLD_DEFAULT, "vkGetPhysicalDeviceProperties2"));
    }

    if (!orig_vkGetInstanceProcAddr || !orig_vkGetPhysicalDeviceProperties || !orig_vkGetPhysicalDeviceProperties2) {
        void *vk_handle = dlopen("libvulkan.so", RTLD_NOW | RTLD_NOLOAD);
        if (!vk_handle) {
            vk_handle = dlopen("libvulkan.so", RTLD_LAZY);
        }
        if (vk_handle) {
            if (!orig_vkGetInstanceProcAddr) {
                orig_vkGetInstanceProcAddr = reinterpret_cast<PFN_vkGetInstanceProcAddr>(
                    dlsym(vk_handle, "vkGetInstanceProcAddr"));
            }
            if (!orig_vkGetPhysicalDeviceProperties) {
                orig_vkGetPhysicalDeviceProperties = reinterpret_cast<PFN_vkGetPhysicalDeviceProperties>(
                    dlsym(vk_handle, "vkGetPhysicalDeviceProperties"));
            }
            if (!orig_vkGetPhysicalDeviceProperties2) {
                orig_vkGetPhysicalDeviceProperties2 = reinterpret_cast<PFN_vkGetPhysicalDeviceProperties2>(
                    dlsym(vk_handle, "vkGetPhysicalDeviceProperties2"));
            }
        }
    }
}

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
    ensure_vulkan_symbols_loaded();
}

inline const GLubyte *hook_glGetString(GLenum name) {
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
    if (!orig_glGetString) {
        ScopedHookGuard guard;
        orig_glGetString = reinterpret_cast<decltype(orig_glGetString)>(dlsym(RTLD_DEFAULT, "glGetString"));
    }
    return orig_glGetString ? orig_glGetString(name) : nullptr;
}

inline void spoof_vulkan_props_struct(VkPhysicalDevicePropertiesCompat *props) {
    if (!props || !g_cfg.gl.enabled) return;

    if (!g_cfg.gl.gl_renderer.empty()) {
        std::strncpy(props->deviceName, g_cfg.gl.gl_renderer.c_str(), VK_MAX_PHYSICAL_DEVICE_NAME_SIZE - 1);
        props->deviceName[VK_MAX_PHYSICAL_DEVICE_NAME_SIZE - 1] = '\0';
    }

    bool is_arm_mali = (g_cfg.gl.gl_vendor.find("ARM") != std::string::npos ||
                        g_cfg.gl.gl_renderer.find("Mali") != std::string::npos ||
                        g_cfg.gl.gl_renderer.find("Immortalis") != std::string::npos);

    props->vendorID = (g_cfg.gl.vulkan_vendor_id != 0)
                          ? g_cfg.gl.vulkan_vendor_id
                          : (is_arm_mali ? 0x13B5U : 0x5143U);

    if (g_cfg.gl.vulkan_device_id != 0) {
        props->deviceID = g_cfg.gl.vulkan_device_id;
    } else if (!is_arm_mali) {
        // 根据 Adreno 型号生成合理的真实 PCI deviceID
        if (g_cfg.gl.gl_renderer.find("830") != std::string::npos) {
            props->deviceID = 0x44050000U;
        } else if (g_cfg.gl.gl_renderer.find("750") != std::string::npos) {
            props->deviceID = 0x43050A01U;
        } else if (g_cfg.gl.gl_renderer.find("740") != std::string::npos) {
            props->deviceID = 0x43050000U;
        }
    }

    // 設为独立/集成物理 GPU 类型 (VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU = 1)
    if (props->deviceType == 0 || props->deviceType == 4) {
        props->deviceType = 1U;
    }

    ScopedHookGuard guard;
    if (guard) emit_probe("VULKAN", "VkPhysicalDeviceProperties.deviceName", props->deviceName);
}

// 遍历并伪装 VkPhysicalDeviceProperties2 的 pNext 扩展链（防止通过 DriverProperties 读出真实驱动名）
inline void spoof_vulkan_pnext_chain(void *pNext) {
    if (!pNext || !g_cfg.gl.enabled) return;

    bool is_arm_mali = (g_cfg.gl.gl_vendor.find("ARM") != std::string::npos ||
                        g_cfg.gl.gl_renderer.find("Mali") != std::string::npos ||
                        g_cfg.gl.gl_renderer.find("Immortalis") != std::string::npos);

    const char *fake_drv_name = is_arm_mali
                                    ? "Arm Mali Vulkan Driver"
                                    : "Qualcomm Technologies Inc. Adreno Vulkan Driver";
    const char *fake_drv_info = is_arm_mali
                                    ? "v1.r49p0-01eac0"
                                    : "Prometheus V@0800.0 (GIT@I94b8f12ab4)";
    uint32_t fake_drv_id = is_arm_mali ? 9U : 8U; // 8 = VK_DRIVER_ID_QUALCOMM_PROPRIETARY, 9 = VK_DRIVER_ID_ARM_PROPRIETARY

    auto *cur = static_cast<VkBaseOutStructureCompat *>(pNext);
    int depth = 0;
    while (cur && depth < 16) {
        if (cur->sType == VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DRIVER_PROPERTIES ||
            cur->sType == VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_PROPERTIES) {
            auto *drv = reinterpret_cast<VkPhysicalDeviceDriverPropertiesCompat *>(cur);
            drv->driverID = fake_drv_id;
            std::strncpy(drv->driverName, fake_drv_name, VK_MAX_DRIVER_NAME_SIZE - 1);
            drv->driverName[VK_MAX_DRIVER_NAME_SIZE - 1] = '\0';
            std::strncpy(drv->driverInfo, fake_drv_info, VK_MAX_DRIVER_INFO_SIZE - 1);
            drv->driverInfo[VK_MAX_DRIVER_INFO_SIZE - 1] = '\0';
        }
        cur = cur->pNext;
        ++depth;
    }
}

inline void hook_vkGetPhysicalDeviceProperties(void *physicalDevice, VkPhysicalDevicePropertiesCompat *pProperties) {
    ensure_vulkan_symbols_loaded();
    if (orig_vkGetPhysicalDeviceProperties) {
        orig_vkGetPhysicalDeviceProperties(physicalDevice, pProperties);
        spoof_vulkan_props_struct(pProperties);
    }
}

inline void hook_vkGetPhysicalDeviceProperties2(void *physicalDevice, VkPhysicalDeviceProperties2Compat *pProperties) {
    ensure_vulkan_symbols_loaded();
    if (orig_vkGetPhysicalDeviceProperties2) {
        orig_vkGetPhysicalDeviceProperties2(physicalDevice, pProperties);
        if (pProperties) {
            spoof_vulkan_props_struct(&pProperties->properties);
            spoof_vulkan_pnext_chain(pProperties->pNext);
        }
    } else if (orig_vkGetPhysicalDeviceProperties && pProperties) {
        orig_vkGetPhysicalDeviceProperties(physicalDevice, &pProperties->properties);
        spoof_vulkan_props_struct(&pProperties->properties);
        spoof_vulkan_pnext_chain(pProperties->pNext);
    }
}

inline PFN_vkVoidFunction hook_vkGetInstanceProcAddr(void *instance, const char *pName) {
    ensure_vulkan_symbols_loaded();
    if (pName && g_cfg.gl.enabled) {
        if (std::strcmp(pName, "vkGetInstanceProcAddr") == 0) {
            return reinterpret_cast<PFN_vkVoidFunction>(hook_vkGetInstanceProcAddr);
        }
    }

    PFN_vkVoidFunction real_fn = orig_vkGetInstanceProcAddr ? orig_vkGetInstanceProcAddr(instance, pName) : nullptr;
    if (!pName || !g_cfg.gl.enabled) return real_fn;

    if (std::strcmp(pName, "vkGetPhysicalDeviceProperties") == 0) {
        if (real_fn) {
            orig_vkGetPhysicalDeviceProperties = reinterpret_cast<PFN_vkGetPhysicalDeviceProperties>(real_fn);
        }
        return reinterpret_cast<PFN_vkVoidFunction>(hook_vkGetPhysicalDeviceProperties);
    }
    if (std::strcmp(pName, "vkGetPhysicalDeviceProperties2") == 0 ||
        std::strcmp(pName, "vkGetPhysicalDeviceProperties2KHR") == 0) {
        if (real_fn) {
            orig_vkGetPhysicalDeviceProperties2 = reinterpret_cast<PFN_vkGetPhysicalDeviceProperties2>(real_fn);
        }
        return reinterpret_cast<PFN_vkVoidFunction>(hook_vkGetPhysicalDeviceProperties2);
    }
    return real_fn;
}

inline __eglMustCastToProperFunctionPointerType hook_eglGetProcAddress(const char *procname) {
    if (procname && g_cfg.gl.enabled) {
        if (std::strcmp(procname, "glGetString") == 0) {
            return reinterpret_cast<__eglMustCastToProperFunctionPointerType>(hook_glGetString);
        }
        if (std::strcmp(procname, "vkGetInstanceProcAddr") == 0) {
            return reinterpret_cast<__eglMustCastToProperFunctionPointerType>(hook_vkGetInstanceProcAddr);
        }
        if (std::strcmp(procname, "vkGetPhysicalDeviceProperties") == 0) {
            return reinterpret_cast<__eglMustCastToProperFunctionPointerType>(hook_vkGetPhysicalDeviceProperties);
        }
        if (std::strcmp(procname, "vkGetPhysicalDeviceProperties2") == 0 ||
            std::strcmp(procname, "vkGetPhysicalDeviceProperties2KHR") == 0) {
            return reinterpret_cast<__eglMustCastToProperFunctionPointerType>(hook_vkGetPhysicalDeviceProperties2);
        }
    }
    return orig_eglGetProcAddress ? orig_eglGetProcAddress(procname) : nullptr;
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

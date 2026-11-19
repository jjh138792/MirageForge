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
constexpr uint32_t VK_MAX_DRIVER_NAME_SIZE          = 256U;
constexpr uint32_t VK_MAX_DRIVER_INFO_SIZE          = 256U;
constexpr uint32_t VK_UUID_SIZE                     = 16U;
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
using PFN_vkGetDeviceProcAddr = PFN_vkVoidFunction (*)(void *device, const char *pName);
using PFN_vkGetPhysicalDeviceProperties = void (*)(void *physicalDevice, VkPhysicalDevicePropertiesCompat *pProperties);
using PFN_vkGetPhysicalDeviceProperties2 = void (*)(void *physicalDevice, VkPhysicalDeviceProperties2Compat *pProperties);

// AOSP hardware/hardware.h & hardware/hwvulkan.h 1:1 结构体定义 (兼容 32/64 位)
struct HwModuleCompat;
struct HwDeviceCompat;

struct HwModuleMethodsCompat {
    int (*open)(const HwModuleCompat *module, const char *id, HwDeviceCompat **device);
};

struct HwModuleCompat {
    uint32_t tag;
    uint16_t module_api_version;
    uint16_t hal_api_version;
    const char *id;
    const char *name;
    const char *author;
    HwModuleMethodsCompat *methods;
    void *dso;
#ifdef __LP64__
    uint64_t reserved[32 - 7];
#else
    uint32_t reserved[32 - 7];
#endif
};

struct HwDeviceCompat {
    uint32_t tag;
    uint32_t version;
    HwModuleCompat *module;
#ifdef __LP64__
    uint64_t reserved[12];
#else
    uint32_t reserved[12];
#endif
    int (*close)(HwDeviceCompat *device);
};

struct HwVulkanDeviceCompat {
    HwDeviceCompat common;
    PFN_vkVoidFunction EnumerateInstanceExtensionProperties;
    PFN_vkVoidFunction CreateInstance;
    PFN_vkGetInstanceProcAddr GetInstanceProcAddr;
};

using PFN_hw_get_module = int (*)(const char *id, const HwModuleCompat **module);
using media_status_t = int32_t;
using PFN_AMediaDrm_getPropertyString = media_status_t (*)(void *drm, const char *propertyName, const char **propertyValue);
using PFN_loader_dlsym = void *(*)(void *handle, const char *symbol, const void *caller_addr);
using PFN_dlsym = void *(*)(void *handle, const char *symbol);

inline const GLubyte *(*orig_glGetString)(GLenum name) = nullptr;
inline const GLubyte *(*orig_glGetStringi)(GLenum name, GLuint index) = nullptr;
inline __eglMustCastToProperFunctionPointerType (*orig_eglGetProcAddress)(const char *procname) = nullptr;

inline PFN_vkGetInstanceProcAddr orig_vkGetInstanceProcAddr = nullptr;
inline PFN_vkGetDeviceProcAddr orig_vkGetDeviceProcAddr = nullptr;
inline PFN_vkGetPhysicalDeviceProperties orig_vkGetPhysicalDeviceProperties = nullptr;
inline PFN_vkGetPhysicalDeviceProperties2 orig_vkGetPhysicalDeviceProperties2 = nullptr;

// Vulkan HAL / ICD 底层源头拦截指针
inline PFN_hw_get_module orig_hw_get_module = nullptr;
inline const HwModuleCompat *orig_hwvulkan_module = nullptr;
inline int (*orig_hwvulkan_open)(const HwModuleCompat *, const char *, HwDeviceCompat **) = nullptr;
inline HwModuleCompat g_wrapped_hwvulkan_module{};
inline HwModuleMethodsCompat g_wrapped_hwvulkan_methods{};

inline PFN_vkGetInstanceProcAddr orig_icd_GetInstanceProcAddr = nullptr;
inline PFN_vkGetPhysicalDeviceProperties orig_icd_vkGetPhysicalDeviceProperties = nullptr;
inline PFN_vkGetPhysicalDeviceProperties2 orig_icd_vkGetPhysicalDeviceProperties2 = nullptr;

inline PFN_AMediaDrm_getPropertyString orig_AMediaDrm_getPropertyString = nullptr;
inline PFN_loader_dlsym orig_loader_dlsym = nullptr;
inline PFN_dlsym orig_dlsym = nullptr;

inline void init_graphics_symbols();

inline void ensure_vulkan_symbols_loaded() {
    if (orig_vkGetInstanceProcAddr && orig_vkGetPhysicalDeviceProperties && orig_vkGetPhysicalDeviceProperties2) {
        return;
    }
    ErrnoRestorer errno_guard;
    void *vk_handle = dlopen("libvulkan.so", RTLD_NOW | RTLD_NOLOAD);
    if (!vk_handle) {
        vk_handle = RTLD_DEFAULT;
    }
    auto raw_lookup = [&](const char *sym) -> void * {
        if (orig_loader_dlsym) {
            return orig_loader_dlsym(vk_handle, sym, reinterpret_cast<const void *>(&ensure_vulkan_symbols_loaded));
        }
        return orig_dlsym ? orig_dlsym(vk_handle, sym) : dlsym(vk_handle, sym);
    };

    if (!orig_vkGetInstanceProcAddr) {
        orig_vkGetInstanceProcAddr = reinterpret_cast<PFN_vkGetInstanceProcAddr>(raw_lookup("vkGetInstanceProcAddr"));
    }
    if (!orig_vkGetDeviceProcAddr) {
        orig_vkGetDeviceProcAddr = reinterpret_cast<PFN_vkGetDeviceProcAddr>(raw_lookup("vkGetDeviceProcAddr"));
    }
    if (!orig_vkGetPhysicalDeviceProperties) {
        orig_vkGetPhysicalDeviceProperties = reinterpret_cast<PFN_vkGetPhysicalDeviceProperties>(raw_lookup("vkGetPhysicalDeviceProperties"));
    }
    if (!orig_vkGetPhysicalDeviceProperties2) {
        orig_vkGetPhysicalDeviceProperties2 = reinterpret_cast<PFN_vkGetPhysicalDeviceProperties2>(raw_lookup("vkGetPhysicalDeviceProperties2"));
        if (!orig_vkGetPhysicalDeviceProperties2) {
            orig_vkGetPhysicalDeviceProperties2 = reinterpret_cast<PFN_vkGetPhysicalDeviceProperties2>(raw_lookup("vkGetPhysicalDeviceProperties2KHR"));
        }
    }
}

inline void init_graphics_symbols() {
    if (orig_glGetString) return;
    ErrnoRestorer errno_guard;
    orig_loader_dlsym = reinterpret_cast<PFN_loader_dlsym>(dlsym(RTLD_DEFAULT, "__loader_dlsym"));
    orig_dlsym = reinterpret_cast<PFN_dlsym>(dlsym(RTLD_DEFAULT, "dlsym"));
    orig_glGetString = reinterpret_cast<decltype(orig_glGetString)>(dlsym(RTLD_DEFAULT, "glGetString"));
    orig_glGetStringi = reinterpret_cast<decltype(orig_glGetStringi)>(dlsym(RTLD_DEFAULT, "glGetStringi"));
    orig_eglGetProcAddress = reinterpret_cast<decltype(orig_eglGetProcAddress)>(dlsym(RTLD_DEFAULT, "eglGetProcAddress"));
    orig_hw_get_module = reinterpret_cast<PFN_hw_get_module>(dlsym(RTLD_DEFAULT, "hw_get_module"));
    orig_AMediaDrm_getPropertyString = reinterpret_cast<decltype(orig_AMediaDrm_getPropertyString)>(dlsym(RTLD_DEFAULT, "AMediaDrm_getPropertyString"));
    ensure_vulkan_symbols_loaded();
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

inline void spoof_vulkan_props_struct(VkPhysicalDevicePropertiesCompat *props) {
    if (!props || !g_cfg.gl.enabled) return;
    if (!g_cfg.gl.gl_renderer.empty()) {
        std::strncpy(props->deviceName, g_cfg.gl.gl_renderer.c_str(), VK_MAX_PHYSICAL_DEVICE_NAME_SIZE - 1);
        props->deviceName[VK_MAX_PHYSICAL_DEVICE_NAME_SIZE - 1] = '\0';
    }
    if (g_cfg.gl.vulkan_vendor_id != 0) {
        props->vendorID = g_cfg.gl.vulkan_vendor_id;
    } else {
        props->vendorID = 0x5143U;
    }
    if (g_cfg.gl.vulkan_device_id != 0) {
        props->deviceID = g_cfg.gl.vulkan_device_id;
    }
    ScopedHookGuard guard;
    if (guard) emit_probe("VULKAN", "VkPhysicalDeviceProperties.deviceName", props->deviceName);
}

inline void spoof_vulkan_pnext_chain(void *pNext) {
    if (!pNext || !g_cfg.gl.enabled) return;
    auto *curr = static_cast<VkBaseOutStructureCompat *>(pNext);
    int depth = 0;
    while (curr && depth++ < 16) {
        if (curr->sType == VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DRIVER_PROPERTIES) {
            auto *drv = reinterpret_cast<VkPhysicalDeviceDriverPropertiesCompat *>(curr);
            bool is_arm = (g_cfg.gl.vulkan_vendor_id == 0x13B5);
            drv->driverID = is_arm ? 9U : 8U;
            const char *d_name = is_arm ? "Arm Proprietary" : "Qualcomm Technologies Inc. Adreno Vulkan Driver";
            std::strncpy(drv->driverName, d_name, VK_MAX_DRIVER_NAME_SIZE - 1);
            drv->driverName[VK_MAX_DRIVER_NAME_SIZE - 1] = '\0';
            if (!g_cfg.gl.gl_version.empty()) {
                std::strncpy(drv->driverInfo, g_cfg.gl.gl_version.c_str(), VK_MAX_DRIVER_INFO_SIZE - 1);
                drv->driverInfo[VK_MAX_DRIVER_INFO_SIZE - 1] = '\0';
            }
        }
        curr = curr->pNext;
    }
}

// ============================================================================
// 1. Vulkan HAL / ICD 底层源头拦截 (libvulkan.so -> hw_get_module / vk_icdGetInstanceProcAddr)
//    无论应用如何加载 libvulkan.so 或通过何种 JNI 库调用，都会在此处被 100% 拦截！
// ============================================================================
inline void hook_icd_vkGetPhysicalDeviceProperties(void *physicalDevice, VkPhysicalDevicePropertiesCompat *pProperties) {
    if (orig_icd_vkGetPhysicalDeviceProperties) {
        orig_icd_vkGetPhysicalDeviceProperties(physicalDevice, pProperties);
        spoof_vulkan_props_struct(pProperties);
    }
}

inline void hook_icd_vkGetPhysicalDeviceProperties2(void *physicalDevice, VkPhysicalDeviceProperties2Compat *pProperties) {
    if (orig_icd_vkGetPhysicalDeviceProperties2) {
        orig_icd_vkGetPhysicalDeviceProperties2(physicalDevice, pProperties);
        if (pProperties) {
            spoof_vulkan_props_struct(&pProperties->properties);
            spoof_vulkan_pnext_chain(pProperties->pNext);
        }
    }
}

inline PFN_vkVoidFunction hook_icd_GetInstanceProcAddr(void *instance, const char *pName) {
    PFN_vkVoidFunction real_fn = orig_icd_GetInstanceProcAddr ? orig_icd_GetInstanceProcAddr(instance, pName) : nullptr;
    if (!real_fn || !pName || !g_cfg.gl.enabled) return real_fn;

    if (std::strcmp(pName, "vkGetPhysicalDeviceProperties") == 0) {
        orig_icd_vkGetPhysicalDeviceProperties = reinterpret_cast<PFN_vkGetPhysicalDeviceProperties>(real_fn);
        return reinterpret_cast<PFN_vkVoidFunction>(hook_icd_vkGetPhysicalDeviceProperties);
    }
    if (std::strcmp(pName, "vkGetPhysicalDeviceProperties2") == 0 ||
        std::strcmp(pName, "vkGetPhysicalDeviceProperties2KHR") == 0) {
        orig_icd_vkGetPhysicalDeviceProperties2 = reinterpret_cast<PFN_vkGetPhysicalDeviceProperties2>(real_fn);
        return reinterpret_cast<PFN_vkVoidFunction>(hook_icd_vkGetPhysicalDeviceProperties2);
    }
    return real_fn;
}

inline int hook_hwvulkan_open(const HwModuleCompat *module, const char *id, HwDeviceCompat **device) {
    const HwModuleCompat *real_mod = orig_hwvulkan_module ? orig_hwvulkan_module : module;
    int ret = orig_hwvulkan_open ? orig_hwvulkan_open(real_mod, id, device) : -1;
    if (ret == 0 && device && *device && g_cfg.gl.enabled) {
        auto *vk_dev = reinterpret_cast<HwVulkanDeviceCompat *>(*device);
        if (vk_dev->GetInstanceProcAddr && vk_dev->GetInstanceProcAddr != hook_icd_GetInstanceProcAddr) {
            orig_icd_GetInstanceProcAddr = vk_dev->GetInstanceProcAddr;
            vk_dev->GetInstanceProcAddr = hook_icd_GetInstanceProcAddr;
            ScopedHookGuard guard;
            if (guard) emit_probe("VULKAN_HAL", "hwvulkan_device_t.GetInstanceProcAddr", "Hooked ICD");
        }
    }
    return ret;
}

inline int hook_hw_get_module(const char *id, const HwModuleCompat **module) {
    int ret = orig_hw_get_module ? orig_hw_get_module(id, module) : -1;
    if (ret == 0 && g_cfg.gl.enabled && id && std::strcmp(id, "vulkan") == 0 &&
        module && *module && (*module)->methods && (*module)->methods->open) {
        orig_hwvulkan_module = *module;
        orig_hwvulkan_open = (*module)->methods->open;
        g_wrapped_hwvulkan_module = **module;
        g_wrapped_hwvulkan_methods = *((*module)->methods);
        g_wrapped_hwvulkan_methods.open = hook_hwvulkan_open;
        g_wrapped_hwvulkan_module.methods = &g_wrapped_hwvulkan_methods;
        *module = &g_wrapped_hwvulkan_module;
    }
    return ret;
}

// ============================================================================
// 2. 应用侧 Vulkan / EGL / MediaDrm 拦截层
// ============================================================================
inline void hook_vkGetPhysicalDeviceProperties(void *physicalDevice, VkPhysicalDevicePropertiesCompat *pProperties) {
    ensure_vulkan_symbols_loaded();
    if (orig_vkGetPhysicalDeviceProperties) {
        orig_vkGetPhysicalDeviceProperties(physicalDevice, pProperties);
        spoof_vulkan_props_struct(pProperties);
    }
}

inline void hook_vkGetPhysicalDeviceProperties2(void *physicalDevice, VkPhysicalDeviceProperties2Compat *pProperties) {
    ensure_vulkan_symbols_loaded();
    if (!orig_vkGetPhysicalDeviceProperties2 && orig_vkGetInstanceProcAddr) {
        orig_vkGetPhysicalDeviceProperties2 = reinterpret_cast<PFN_vkGetPhysicalDeviceProperties2>(
            orig_vkGetInstanceProcAddr(nullptr, "vkGetPhysicalDeviceProperties2"));
    }
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
    PFN_vkVoidFunction real_fn = orig_vkGetInstanceProcAddr ? orig_vkGetInstanceProcAddr(instance, pName) : nullptr;
    if (!pName || !g_cfg.gl.enabled) return real_fn;

    if (std::strcmp(pName, "vkGetInstanceProcAddr") == 0) {
        return reinterpret_cast<PFN_vkVoidFunction>(hook_vkGetInstanceProcAddr);
    }
    if (std::strcmp(pName, "vkGetPhysicalDeviceProperties") == 0) {
        if (real_fn) orig_vkGetPhysicalDeviceProperties = reinterpret_cast<PFN_vkGetPhysicalDeviceProperties>(real_fn);
        return reinterpret_cast<PFN_vkVoidFunction>(hook_vkGetPhysicalDeviceProperties);
    }
    if (std::strcmp(pName, "vkGetPhysicalDeviceProperties2") == 0 ||
        std::strcmp(pName, "vkGetPhysicalDeviceProperties2KHR") == 0) {
        if (real_fn) orig_vkGetPhysicalDeviceProperties2 = reinterpret_cast<PFN_vkGetPhysicalDeviceProperties2>(real_fn);
        return reinterpret_cast<PFN_vkVoidFunction>(hook_vkGetPhysicalDeviceProperties2);
    }
    return real_fn;
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

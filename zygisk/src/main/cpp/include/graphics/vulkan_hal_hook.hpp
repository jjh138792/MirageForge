// SPDX-License-Identifier: GPL-3.0-only
// Copyright (c) 2025-2026 jjh138792
// MirageForge: https://github.com/jjh138792/MirageForge

#pragma once

#include <cstdint>
#include <cstring>
#include <dlfcn.h>
#include "mirage_config.hpp"
#include "mirage_logger.hpp"
#include "graphics/gles_drm_hook.hpp"

namespace mirage {

constexpr uint32_t VK_MAX_PHYSICAL_DEVICE_NAME_SIZE = 256U;
constexpr uint32_t VK_MAX_DRIVER_NAME_SIZE          = 256U;
constexpr uint32_t VK_MAX_DRIVER_INFO_SIZE          = 256U;
constexpr uint32_t VK_UUID_SIZE                     = 16U;
constexpr uint32_t VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DRIVER_PROPERTIES = 1000196000U;
constexpr uint32_t kHardwareModuleTag               = 0x48574D54U; // 'HWMT'

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

inline PFN_vkGetInstanceProcAddr orig_vkGetInstanceProcAddr = nullptr;
inline PFN_vkGetDeviceProcAddr orig_vkGetDeviceProcAddr = nullptr;
inline PFN_vkGetPhysicalDeviceProperties orig_vkGetPhysicalDeviceProperties = nullptr;
inline PFN_vkGetPhysicalDeviceProperties2 orig_vkGetPhysicalDeviceProperties2 = nullptr;

inline PFN_hw_get_module orig_hw_get_module = nullptr;
inline const HwModuleCompat *orig_hwvulkan_module = nullptr;
inline int (*orig_hwvulkan_open)(const HwModuleCompat *, const char *, HwDeviceCompat **) = nullptr;
inline HwModuleCompat g_wrapped_hwvulkan_module{};
inline HwModuleMethodsCompat g_wrapped_hwvulkan_methods{};

inline PFN_vkGetInstanceProcAddr orig_icd_GetInstanceProcAddr = nullptr;
inline PFN_vkGetPhysicalDeviceProperties orig_icd_vkGetPhysicalDeviceProperties = nullptr;
inline PFN_vkGetPhysicalDeviceProperties2 orig_icd_vkGetPhysicalDeviceProperties2 = nullptr;

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

inline void init_vulkan_hal_symbols() {
    ErrnoRestorer errno_guard;
    if (!orig_hw_get_module) {
        orig_hw_get_module = reinterpret_cast<PFN_hw_get_module>(dlsym(RTLD_DEFAULT, "hw_get_module"));
    }
    ensure_vulkan_symbols_loaded();
}

inline uint32_t resolve_vk_driver_id(uint32_t vendor_id) noexcept {
    switch (vendor_id) {
        case 0x5143U: return 8U;  // VK_DRIVER_ID_QUALCOMM_PROPRIETARY
        case 0x13B5U: return 9U;  // VK_DRIVER_ID_ARM_PROPRIETARY
        case 0x1010U: return 7U;  // VK_DRIVER_ID_IMAGINATION_PROPRIETARY
        case 0x144DU: return 19U; // VK_DRIVER_ID_SAMSUNG_PROPRIETARY
        default:      return 8U;
    }
}

inline void spoof_vulkan_props_struct(VkPhysicalDevicePropertiesCompat *props) {
    if (!props || !g_cfg.gl.enabled) return;

    const std::string &dev_name = g_cfg.gl.effective_vk_device_name();
    if (!dev_name.empty()) {
        std::strncpy(props->deviceName, dev_name.c_str(), VK_MAX_PHYSICAL_DEVICE_NAME_SIZE - 1);
        props->deviceName[VK_MAX_PHYSICAL_DEVICE_NAME_SIZE - 1] = '\0';
    }
    if (g_cfg.gl.vk_vendor_id != 0) {
        props->vendorID = g_cfg.gl.vk_vendor_id;
    }
    if (g_cfg.gl.vk_device_id != 0) {
        props->deviceID = g_cfg.gl.vk_device_id;
    }
    if (g_cfg.gl.vk_api_version_packed != 0) {
        props->apiVersion = g_cfg.gl.vk_api_version_packed;
    }
    if (g_cfg.gl.vk_driver_version_packed != 0) {
        props->driverVersion = g_cfg.gl.vk_driver_version_packed;
    }

    ScopedHookGuard guard;
    if (guard) emit_probe("VULKAN", "VkPhysicalDeviceProperties.deviceName", props->deviceName);
}

// 遍历 Vulkan 1.1+ pNext 扩展链：使用独立的 vk_driver_name / vk_driver_info，彻底杜绝 OpenGL ES 字符串串台
inline void spoof_vulkan_pnext_chain(void *pNext) {
    if (!pNext || !g_cfg.gl.enabled) return;
    auto *curr = static_cast<VkBaseOutStructureCompat *>(pNext);
    int depth = 0;
    while (curr && depth++ < 16) {
        if (curr->sType == VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DRIVER_PROPERTIES) {
            auto *drv = reinterpret_cast<VkPhysicalDeviceDriverPropertiesCompat *>(curr);
            drv->driverID = resolve_vk_driver_id(g_cfg.gl.vk_vendor_id);

            if (!g_cfg.gl.vk_driver_name.empty()) {
                std::strncpy(drv->driverName, g_cfg.gl.vk_driver_name.c_str(), VK_MAX_DRIVER_NAME_SIZE - 1);
                drv->driverName[VK_MAX_DRIVER_NAME_SIZE - 1] = '\0';
            }
            if (!g_cfg.gl.vk_driver_info.empty()) {
                std::strncpy(drv->driverInfo, g_cfg.gl.vk_driver_info.c_str(), VK_MAX_DRIVER_INFO_SIZE - 1);
                drv->driverInfo[VK_MAX_DRIVER_INFO_SIZE - 1] = '\0';
            }
        }
        curr = curr->pNext;
    }
}

// ============================================================================
// 1. Vulkan HAL / ICD 底层源头拦截 (libvulkan.so -> hw_get_module / HMI / vk_icdGetInstanceProcAddr)
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

inline const HwModuleCompat *wrap_hwvulkan_module_if_needed(const HwModuleCompat *mod) {
    if (!mod || !g_cfg.gl.enabled) return mod;
    if (mod->tag != kHardwareModuleTag || !mod->id || std::strcmp(mod->id, "vulkan") != 0) {
        return mod;
    }
    if (!mod->methods || !mod->methods->open || mod->methods->open == hook_hwvulkan_open) {
        return mod;
    }
    orig_hwvulkan_module = mod;
    orig_hwvulkan_open = mod->methods->open;
    g_wrapped_hwvulkan_module = *mod;
    g_wrapped_hwvulkan_methods = *(mod->methods);
    g_wrapped_hwvulkan_methods.open = hook_hwvulkan_open;
    g_wrapped_hwvulkan_module.methods = &g_wrapped_hwvulkan_methods;
    return &g_wrapped_hwvulkan_module;
}

inline int hook_hw_get_module(const char *id, const HwModuleCompat **module) {
    int ret = orig_hw_get_module ? orig_hw_get_module(id, module) : -1;
    if (ret == 0 && g_cfg.gl.enabled && id && std::strcmp(id, "vulkan") == 0 && module && *module) {
        *module = wrap_hwvulkan_module_if_needed(*module);
    }
    return ret;
}

// ============================================================================
// 2. 应用侧 Vulkan API 拦截层
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

} // namespace mirage

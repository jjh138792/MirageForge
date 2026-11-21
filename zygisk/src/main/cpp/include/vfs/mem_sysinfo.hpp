#pragma once

#include <cstdint>
#include <cstring>
#include <sstream>
#include <string>
#include "vfs/vfs_common.hpp"

namespace mirage {

inline uint64_t calc_realistic_memtotal_kb(int ram_gb) noexcept {
    uint64_t phys_kb = static_cast<uint64_t>(ram_gb) * 1024ULL * 1024ULL;
    return (phys_kb * 935ULL) / 1000ULL;
}

inline bool synthesize_mem_soc_vfs_node(const std::string &norm, std::string *out_content) {
    if (norm == "/proc/meminfo") {
        if (g_cfg.vfs.ram_gb <= 0) return false;
        uint64_t total_kb = calc_realistic_memtotal_kb(g_cfg.vfs.ram_gb);
        uint64_t free_kb  = (total_kb * 24ULL) / 100ULL;
        uint64_t avail_kb = (total_kb * 58ULL) / 100ULL;
        uint64_t cached_kb = (total_kb * 28ULL) / 100ULL;
        uint64_t active_kb = (total_kb * 36ULL) / 100ULL;
        uint64_t inactive_kb = (total_kb * 22ULL) / 100ULL;
        uint64_t sreclaim_kb = (total_kb * 4ULL) / 100ULL;
        uint64_t sunreclaim_kb = (total_kb * 3ULL) / 100ULL;
        uint64_t mapped_kb = (total_kb * 8ULL) / 100ULL;
        uint64_t shmem_kb = (total_kb * 2ULL) / 100ULL;

        std::ostringstream ss;
        ss << "MemTotal:       " << total_kb << " kB\n"
           << "MemFree:        " << free_kb << " kB\n"
           << "MemAvailable:   " << avail_kb << " kB\n"
           << "Buffers:          184320 kB\n"
           << "Cached:         " << cached_kb << " kB\n"
           << "SwapCached:            0 kB\n"
           << "Active:         " << active_kb << " kB\n"
           << "Inactive:       " << inactive_kb << " kB\n"
           << "Shmem:          " << shmem_kb << " kB\n"
           << "KReclaimable:   " << sreclaim_kb << " kB\n"
           << "Slab:           " << (sreclaim_kb + sunreclaim_kb) << " kB\n"
           << "SReclaimable:   " << sreclaim_kb << " kB\n"
           << "SUnreclaim:     " << sunreclaim_kb << " kB\n"
           << "Mapped:         " << mapped_kb << " kB\n"
           << "SwapTotal:       8388608 kB\n"
           << "SwapFree:        8126464 kB\n";
        *out_content = ss.str();
        return true;
    }

    if (norm == "/proc/ddr_info" || norm == "/sys/devices/system/chip-info/ddr_info") {
        if (g_cfg.vfs.ram_gb <= 0) return false;
        *out_content = std::to_string(g_cfg.vfs.ram_gb) + "GB " + g_cfg.vfs.ddr_type + " (" + g_cfg.vfs.ddr_vendor + ")\n";
        return true;
    }
    if (norm == "/proc/version") {
        if (g_cfg.vfs.kernel_release.empty()) return false;
        *out_content = "Linux version " + g_cfg.vfs.kernel_release + " (kleaf@build-host) " + g_cfg.vfs.kernel_version + "\n";
        return true;
    }
    if (norm == "/proc/sys/kernel/osrelease") {
        if (g_cfg.vfs.kernel_release.empty()) return false;
        *out_content = g_cfg.vfs.kernel_release + "\n";
        return true;
    }
    // 严格 0 改动：关闭 GL 开关时绝不触碰 GPU sysfs 节点
    if (norm == "/sys/class/kgsl/kgsl-3d0/gpu_model") {
        if (!g_cfg.gl.enabled || g_cfg.gl.effective_vk_device_name().empty()) return false;
        *out_content = g_cfg.gl.effective_vk_device_name() + "\n";
        return true;
    }
    if (norm == "/sys/devices/soc0/soc_id") {
        if (g_cfg.vfs.soc_id.empty()) return false;
        *out_content = g_cfg.vfs.soc_id + "\n";
        return true;
    }
    if (norm == "/sys/devices/soc0/family") {
        if (g_cfg.vfs.soc_family.empty()) return false;
        *out_content = g_cfg.vfs.soc_family + "\n";
        return true;
    }
    if (norm == "/sys/devices/soc0/machine") {
        if (g_cfg.vfs.soc_machine.empty()) return false;
        *out_content = g_cfg.vfs.soc_machine + "\n";
        return true;
    }
    if (norm == "/sys/devices/system/chip-info/ddr_type" ||
        norm == "/sys/firmware/devicetree/base/memory/ddr_device_type") {
        if (g_cfg.vfs.ddr_type.empty()) return false;
        *out_content = g_cfg.vfs.ddr_type + "\n";
        return true;
    }
    if (norm == "/sys/devices/system/chip-info/ddr_size") {
        if (g_cfg.vfs.ram_gb <= 0) return false;
        *out_content = std::to_string(g_cfg.vfs.ram_gb) + "GB\n";
        return true;
    }
    return false;
}

inline int hook_sysinfo(struct sysinfo *info) {
    int ret = orig_sysinfo ? orig_sysinfo(info) : sysinfo(info);
    if (ret == 0 && info && g_cfg.vfs.enabled && g_cfg.vfs.ram_gb > 0) {
        uint64_t total_bytes = calc_realistic_memtotal_kb(g_cfg.vfs.ram_gb) * 1024ULL;
        uint32_t unit = info->mem_unit > 0 ? info->mem_unit : 1;
        info->totalram = static_cast<unsigned long>(total_bytes / unit);
        info->freeram  = (info->totalram * 58UL) / 100UL;
        info->bufferram = (info->totalram * 4UL) / 100UL;
        ScopedHookGuard guard;
        if (guard) emit_probe("SYSINFO", "totalram", std::to_string(g_cfg.vfs.ram_gb) + "GB");
    }
    return ret;
}

inline long hook_sysconf(int name) {
    if (g_cfg.vfs.enabled) {
        if ((name == _SC_NPROCESSORS_CONF || name == _SC_NPROCESSORS_ONLN) && g_cfg.vfs.cpu_cores > 0) {
            ScopedHookGuard guard;
            if (guard) emit_probe("SYSCONF", "_SC_NPROCESSORS", std::to_string(g_cfg.vfs.cpu_cores));
            return g_cfg.vfs.cpu_cores;
        }
        if ((name == _SC_PHYS_PAGES || name == _SC_AVPHYS_PAGES) && g_cfg.vfs.ram_gb > 0) {
            long page_sz = orig_sysconf ? orig_sysconf(_SC_PAGESIZE) : 4096;
            if (page_sz <= 0) page_sz = 4096;
            uint64_t total_bytes = calc_realistic_memtotal_kb(g_cfg.vfs.ram_gb) * 1024ULL;
            uint64_t target_bytes = (name == _SC_AVPHYS_PAGES) ? ((total_bytes * 58ULL) / 100ULL) : total_bytes;
            ScopedHookGuard guard;
            if (guard) {
                emit_probe("SYSCONF",
                           name == _SC_AVPHYS_PAGES ? "_SC_AVPHYS_PAGES" : "_SC_PHYS_PAGES",
                           std::to_string(g_cfg.vfs.ram_gb) + "GB");
            }
            return static_cast<long>(target_bytes / static_cast<uint64_t>(page_sz));
        }
    }
    return orig_sysconf ? orig_sysconf(name) : sysconf(name);
}

inline int hook_uname(struct utsname *buf) {
    int ret = orig_uname ? orig_uname(buf) : uname(buf);
    if (ret == 0 && buf && g_cfg.vfs.enabled) {
        if (!g_cfg.vfs.cpu_arch.empty()) {
            std::strncpy(buf->machine, g_cfg.vfs.cpu_arch.c_str(), sizeof(buf->machine) - 1);
            buf->machine[sizeof(buf->machine) - 1] = '\0';
        }
        if (!g_cfg.vfs.kernel_release.empty()) {
            std::strncpy(buf->release, g_cfg.vfs.kernel_release.c_str(), sizeof(buf->release) - 1);
            buf->release[sizeof(buf->release) - 1] = '\0';
        }
        if (!g_cfg.vfs.kernel_version.empty()) {
            std::strncpy(buf->version, g_cfg.vfs.kernel_version.c_str(), sizeof(buf->version) - 1);
            buf->version[sizeof(buf->version) - 1] = '\0';
        }
        ScopedHookGuard guard;
        if (guard) emit_probe("UNAME", "release/machine", std::string(buf->release) + " / " + buf->machine);
    }
    return ret;
}

} // namespace mirage

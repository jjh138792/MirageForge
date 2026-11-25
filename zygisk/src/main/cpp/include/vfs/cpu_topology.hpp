// SPDX-License-Identifier: GPL-3.0-only
// Copyright (c) 2025-2026 jjh138792
// MirageForge: https://github.com/jjh138792/MirageForge

#pragma once

#include <ctime>
#include <cstdlib>
#include <cstring>
#include <sstream>
#include <string>
#include <vector>
#include "vfs/vfs_common.hpp"

namespace mirage {

inline std::vector<int> build_opp_steps(int min_khz, int max_khz) {
    std::vector<int> steps;
    if (max_khz <= min_khz) {
        steps.push_back(max_khz);
        return steps;
    }
    constexpr int kNumSteps = 8;
    for (int i = 0; i < kNumSteps; ++i) {
        long raw = min_khz + ((long)(max_khz - min_khz) * i) / (kNumSteps - 1);
        if (i > 0 && i < kNumSteps - 1) {
            raw = (raw / 19200L) * 19200L;
        }
        steps.push_back(static_cast<int>(raw));
    }
    return steps;
}

inline void init_vfs_clusters_lightweight() {
    if (!g_cfg.vfs.enable_cpu && !g_cfg.vfs.enable_vfs_nodes) return;
    g_cfg.vfs.clusters.clear();

    if (!g_cfg.vfs.cpu_clusters_spec.empty()) {
        auto segs = split_str(g_cfg.vfs.cpu_clusters_spec, ';');
        for (const auto &seg : segs) {
            auto parts = split_str(seg, ':');
            if (parts.size() >= 3) {
                auto range = split_str(parts[0], '-');
                int c_start = std::atoi(range[0].c_str());
                int c_end = (range.size() >= 2) ? std::atoi(range[1].c_str()) : c_start;
                int f_min = std::atoi(parts[1].c_str());
                int f_max = std::atoi(parts[2].c_str());
                if (c_start >= 0 && c_end >= c_start && f_max > 0) {
                    CpuClusterSpec cl;
                    cl.first_core = c_start;
                    cl.last_core = c_end;
                    cl.min_freq_khz = f_min > 0 ? f_min : 614400;
                    cl.max_freq_khz = f_max;
                    cl.cpu_part = (c_end >= 6) ? 0xd87 : 0xd85;
                    cl.capacity = (c_end >= 6) ? 1024 : 768;
                    cl.opp_freqs_khz = build_opp_steps(cl.min_freq_khz, cl.max_freq_khz);
                    g_cfg.vfs.clusters.push_back(cl);
                }
            }
        }
    }

    if (!g_cfg.vfs.clusters.empty()) return;

    std::string token = g_cfg.vfs.soc_machine + " " + g_cfg.vfs.cpu_hardware;
    for (char &c : token) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));

    if (token.find("mt6991") != std::string::npos || token.find("6991") != std::string::npos) {
        g_cfg.vfs.clusters.push_back({0, 3, 800000,  2400000, 0xd81, 612,  build_opp_steps(800000,  2400000)});
        g_cfg.vfs.clusters.push_back({4, 6, 1000000, 3300000, 0xd82, 896,  build_opp_steps(1000000, 3300000)});
        g_cfg.vfs.clusters.push_back({7, 7, 1200000, 3620000, 0xd85, 1024, build_opp_steps(1200000, 3620000)});
    } else if (token.find("sm8650") != std::string::npos || token.find("pineapple") != std::string::npos) {
        g_cfg.vfs.clusters.push_back({0, 1, 556800, 2265600, 0xd80, 384,  build_opp_steps(556800, 2265600)});
        g_cfg.vfs.clusters.push_back({2, 6, 691200, 3148800, 0xd81, 845,  build_opp_steps(691200, 3148800)});
        g_cfg.vfs.clusters.push_back({7, 7, 806400, 3302400, 0xd82, 1024, build_opp_steps(806400, 3302400)});
    } else {
        int top_freq = g_cfg.vfs.cpu_max_freq_khz > 2000000 ? g_cfg.vfs.cpu_max_freq_khz : 4320000;
        int perf_freq = (top_freq == 4320000) ? 3532800 : (top_freq * 82 / 100);
        g_cfg.vfs.clusters.push_back({0, 5, 614400, perf_freq, 0xd85, 788,  build_opp_steps(614400, perf_freq)});
        g_cfg.vfs.clusters.push_back({6, 7, 806400, top_freq,  0xd87, 1024, build_opp_steps(806400, top_freq)});
    }
}

inline const CpuClusterSpec *find_cluster_for_core(int core_idx) noexcept {
    for (const auto &cl : g_cfg.vfs.clusters) {
        if (core_idx >= cl.first_core && core_idx <= cl.last_core) {
            return &cl;
        }
    }
    return g_cfg.vfs.clusters.empty() ? nullptr : &g_cfg.vfs.clusters.back();
}

inline int sample_live_core_freq_khz(int core_idx) noexcept {
    const CpuClusterSpec *cl = find_cluster_for_core(core_idx);
    if (!cl || cl->opp_freqs_khz.empty()) return 2400000;

    struct timespec ts{};
    clock_gettime(CLOCK_MONOTONIC, &ts);
    uint64_t tick = static_cast<uint64_t>(ts.tv_sec) * 8ULL + static_cast<uint64_t>(ts.tv_nsec / 120000000L);
    uint64_t mix = (tick + static_cast<uint64_t>(core_idx) * 3ULL) ^ (tick >> 2);

    size_t n = cl->opp_freqs_khz.size();
    size_t idx = static_cast<size_t>((mix + static_cast<uint64_t>(core_idx)) % n);
    return cl->opp_freqs_khz[idx];
}

inline bool synthesize_cpufreq_leaf(int core_id, const char *leaf, std::string *out_content, int *out_dyn_core) {
    const CpuClusterSpec *cl = find_cluster_for_core(core_id);
    if (!cl || !leaf || !out_content) return false;

    if (std::strcmp(leaf, "scaling_cur_freq") == 0 || std::strcmp(leaf, "cpuinfo_cur_freq") == 0) {
        *out_content = std::to_string(sample_live_core_freq_khz(core_id)) + "\n";
        if (out_dyn_core) *out_dyn_core = core_id;
        return true;
    }
    if (std::strcmp(leaf, "cpuinfo_max_freq") == 0 || std::strcmp(leaf, "scaling_max_freq") == 0 ||
        std::strcmp(leaf, "scaling_boost_frequencies") == 0) {
        *out_content = std::to_string(cl->max_freq_khz) + "\n";
        return true;
    }
    if (std::strcmp(leaf, "cpuinfo_min_freq") == 0 || std::strcmp(leaf, "scaling_min_freq") == 0) {
        *out_content = std::to_string(cl->min_freq_khz) + "\n";
        return true;
    }
    if (std::strcmp(leaf, "scaling_available_frequencies") == 0) {
        std::ostringstream ss;
        for (size_t i = 0; i < cl->opp_freqs_khz.size(); ++i) {
            if (i > 0) ss << " ";
            ss << cl->opp_freqs_khz[i];
        }
        ss << "\n";
        *out_content = ss.str();
        return true;
    }
    if (std::strcmp(leaf, "related_cpus") == 0 || std::strcmp(leaf, "affected_cpus") == 0) {
        std::ostringstream ss;
        for (int c = cl->first_core; c <= cl->last_core; ++c) {
            if (c > cl->first_core) ss << " ";
            ss << c;
        }
        ss << "\n";
        *out_content = ss.str();
        return true;
    }
    if (std::strcmp(leaf, "scaling_governor") == 0) {
        *out_content = "walt\n";
        return true;
    }
    if (std::strcmp(leaf, "scaling_available_governors") == 0) {
        *out_content = "walt schedutil performance powersave\n";
        return true;
    }
    if (std::strcmp(leaf, "scaling_driver") == 0) {
        *out_content = "qcom-cpufreq-hw\n";
        return true;
    }
    if (std::strcmp(leaf, "cpuinfo_transition_latency") == 0) {
        *out_content = "1000\n";
        return true;
    }
    if (std::strcmp(leaf, "stats/time_in_state") == 0) {
        std::ostringstream ss;
        for (size_t i = 0; i < cl->opp_freqs_khz.size(); ++i) {
            ss << cl->opp_freqs_khz[i] << " " << ((i + 1) * 14200) << "\n";
        }
        *out_content = ss.str();
        return true;
    }
    return false;
}

inline bool synthesize_cpu_vfs_node(const std::string &norm, std::string *out_content, int *out_dyn_core) {
    // 严格 0 改动：/proc/cpuinfo 仅由「伪装 CPU」开关控制
    if (norm == "/proc/cpuinfo") {
        if (!g_cfg.vfs.enable_cpu) return false;
        int cores = (g_cfg.vfs.cpu_cores > 0 && g_cfg.vfs.cpu_cores <= 16) ? g_cfg.vfs.cpu_cores : 8;
        std::ostringstream ss;
        ss << "Processor\t: AArch64 Processor rev 1 (aarch64)\n";
        for (int i = 0; i < cores; ++i) {
            const CpuClusterSpec *cl = find_cluster_for_core(i);
            uint32_t part_id = cl ? cl->cpu_part : (i < 6 ? 0xd85 : 0xd87);
            ss << "processor\t: " << i << "\n"
               << "BogoMIPS\t: 38.40\n"
               << "Features\t: fp asimd evtstrm aes pmull sha1 sha2 crc32 atomics fphp asimdhp cpuid asimdrdm jscvt fcma lrcpc dcpop sha3 sm3 sm4 asimddp sha512 sve asimdfhm dit uscat ilrcpc flagm ssbs sb paca pacg dcpodp sve2 sveaes svepmull svebitperm svesha3 svesm4 flagm2 frint svei8mm svebf16 i8mm bf16 dgh bti\n"
               << "CPU implementer\t: 0x41\n"
               << "CPU architecture: 8\n"
               << "CPU variant\t: 0x1\n"
               << "CPU part\t: 0x" << std::hex << part_id << std::dec << "\n"
               << "CPU revision\t: 1\n\n";
        }
        ss << "Hardware\t: " << g_cfg.vfs.cpu_hardware << "\n";
        *out_content = ss.str();
        return true;
    }

    // 严格 0 改动：/sys/devices/system/cpu/* 与 /sys/bus/cpu/devices/* 仅由「伪装 CPU/GPU 文件节点」控制
    if (!g_cfg.vfs.enable_vfs_nodes) return false;

    const char *cpu_sub = nullptr;
    if (norm.rfind("/sys/devices/system/cpu/", 0) == 0) {
        cpu_sub = norm.c_str() + 24;
    } else if (norm.rfind("/sys/bus/cpu/devices/", 0) == 0) {
        cpu_sub = norm.c_str() + 21;
    }
    if (!cpu_sub) return false;

    int cores = (g_cfg.vfs.cpu_cores > 0 && g_cfg.vfs.cpu_cores <= 16) ? g_cfg.vfs.cpu_cores : 8;
    if (std::strcmp(cpu_sub, "present") == 0 ||
        std::strcmp(cpu_sub, "possible") == 0 ||
        std::strcmp(cpu_sub, "online") == 0) {
        *out_content = "0-" + std::to_string(cores - 1) + "\n";
        return true;
    }
    if (std::strcmp(cpu_sub, "offline") == 0) {
        *out_content = "\n";
        return true;
    }
    if (std::strcmp(cpu_sub, "kernel_max") == 0) {
        *out_content = std::to_string(cores - 1) + "\n";
        return true;
    }

    if (std::strncmp(cpu_sub, "cpu", 3) == 0 && cpu_sub[3] >= '0' && cpu_sub[3] <= '9') {
        char *endp = nullptr;
        int core_id = static_cast<int>(std::strtol(cpu_sub + 3, &endp, 10));
        if (core_id >= 0 && core_id < cores && endp && *endp == '/') {
            const char *rest = endp + 1;
            if (std::strncmp(rest, "cpufreq/", 8) == 0) {
                return synthesize_cpufreq_leaf(core_id, rest + 8, out_content, out_dyn_core);
            }
            const CpuClusterSpec *cl = find_cluster_for_core(core_id);
            if (std::strcmp(rest, "online") == 0) { *out_content = "1\n"; return true; }
            if (std::strcmp(rest, "cpu_capacity") == 0 && cl) { *out_content = std::to_string(cl->capacity) + "\n"; return true; }
            if (std::strcmp(rest, "regs/identification/midr_el1") == 0) { *out_content = g_cfg.vfs.cpu_midr_el1 + "\n"; return true; }
            if (std::strcmp(rest, "regs/identification/revidr_el1") == 0) { *out_content = "0x0000000000000000\n"; return true; }
            if (std::strcmp(rest, "topology/core_id") == 0) { *out_content = std::to_string(core_id) + "\n"; return true; }
            if (std::strcmp(rest, "topology/cluster_id") == 0 && cl) { *out_content = std::to_string(cl->first_core) + "\n"; return true; }
            if (std::strcmp(rest, "topology/physical_package_id") == 0) { *out_content = "0\n"; return true; }
            if (std::strcmp(rest, "topology/core_cpus_list") == 0 && cl) {
                *out_content = std::to_string(cl->first_core) + "-" + std::to_string(cl->last_core) + "\n";
                return true;
            }
        }
    }

    if (std::strncmp(cpu_sub, "cpufreq/policy", 14) == 0 && cpu_sub[14] >= '0' && cpu_sub[14] <= '9') {
        char *endp = nullptr;
        int pol_id = static_cast<int>(std::strtol(cpu_sub + 14, &endp, 10));
        if (pol_id >= 0 && pol_id < cores && endp && *endp == '/') {
            return synthesize_cpufreq_leaf(pol_id, endp + 1, out_content, out_dyn_core);
        }
    }

    return false;
}

} // namespace mirage

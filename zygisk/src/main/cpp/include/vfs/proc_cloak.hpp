// SPDX-License-Identifier: GPL-3.0-only
// Copyright (c) 2025-2026 jjh138792
// MirageForge: https://github.com/jjh138792/MirageForge

#pragma once

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <sstream>
#include <string>
#include "vfs/vfs_common.hpp"

namespace mirage {

inline bool is_sensitive_proc_line(const char *line, bool is_mounts) noexcept {
    if (std::strstr(line, "mirage_forge") != nullptr ||
        std::strstr(line, "mnt_stage") != nullptr ||
        std::strstr(line, ".st_ns") != nullptr ||
        std::strstr(line, "zygisk") != nullptr ||
        std::strstr(line, "magisk") != nullptr ||
        std::strstr(line, "/data/adb") != nullptr ||
        std::strstr(line, "kernelsu") != nullptr ||
        std::strstr(line, "/memfd:") != nullptr ||
        std::strstr(line, "apatch") != nullptr ||
        std::strstr(line, "lsposed") != nullptr) {
        return true;
    }

    if (is_mounts) {
        bool any_ns = g_vfs_ns_mounted.load(std::memory_order_acquire) ||
                      g_prop_ns_mounted.load(std::memory_order_acquire);
        if (any_ns) {
            // 任何针对单个 /dev/__properties__/* 或 /__properties__/* 文件的 bind-mount 均直接清洗
            if (std::strstr(line, " /dev/__properties__/") != nullptr ||
                std::strstr(line, " /__properties__/") != nullptr) {
                return true;
            }
            // 清洗挂载在 /proc/* 或 /sys/* 子节点上的 tmpfs / 已删节点 bind-mount（保留原生 /proc 与 /sys 根挂载）
            if ((std::strstr(line, " /proc/") != nullptr || std::strstr(line, " /sys/") != nullptr) &&
                (std::strstr(line, "tmpfs") != nullptr ||
                 std::strstr(line, "//deleted") != nullptr ||
                 std::strstr(line, " (deleted)") != nullptr)) {
                return true;
            }
        }
    }
    return false;
}

inline bool sanitize_live_proc_file(const std::string &norm_path, std::string *out_clean) {
    bool is_maps = (norm_path == "/proc/self/maps" ||
                    norm_path == "/proc/thread-self/maps" ||
                    norm_path == "/proc/self/smaps" ||
                    norm_path == "/proc/thread-self/smaps");
    bool is_mounts = (norm_path == "/proc/self/mountinfo" ||
                      norm_path == "/proc/thread-self/mountinfo" ||
                      norm_path == "/proc/self/mounts" ||
                      norm_path == "/proc/thread-self/mounts" ||
                      norm_path == "/proc/mounts" ||
                      norm_path == "/proc/self/mountstats");

    if (!is_maps && !is_mounts) return false;

    FILE *real_fp = orig_fopen ? orig_fopen(norm_path.c_str(), "r") : nullptr;
    if (!real_fp) return false;

    std::ostringstream clean_ss;
    char line[1024];
    size_t stripped = 0;

    while (std::fgets(line, sizeof(line), real_fp)) {
        if (is_sensitive_proc_line(line, is_mounts)) {
            ++stripped;
            continue;
        }
        clean_ss << line;
    }
    if (orig_fclose) orig_fclose(real_fp); else std::fclose(real_fp);

    *out_clean = clean_ss.str();
    emit_probe("MAPS_CLOAK", norm_path, "Stripped " + std::to_string(stripped) + " traces");
    return true;
}

inline bool synthesize_build_prop_vfs_node(const std::string &norm, std::string *out_content) {
    if (!g_cfg.build.enabled) return false;
    if (norm == "/system/build.prop" || norm == "/system/etc/build.prop" ||
        norm == "/vendor/build.prop" || norm == "/vendor/etc/build.prop" ||
        norm == "/product/build.prop" || norm == "/product/etc/build.prop" ||
        norm == "/odm/etc/build.prop" || norm == "/odm/build.prop") {
        std::ostringstream ss;
        for (const auto &kv : g_cfg.build.prop_map) {
            ss << kv.first << "=" << kv.second << "\n";
        }
        *out_content = ss.str();
        return true;
    }
    return false;
}

inline FILE *hook_popen(const char *command, const char *type) {
    ScopedHookGuard guard;
    if (guard && command && g_cfg.build.enabled) {
        std::string cmd(command);
        size_t first = cmd.find_first_not_of(" \t");
        if (first != std::string::npos) cmd = cmd.substr(first);

        if (cmd.rfind("getprop", 0) == 0 || cmd.rfind("/system/bin/getprop", 0) == 0) {
            std::string output;
            size_t sp = cmd.find(' ');
            if (sp != std::string::npos) {
                std::string arg = cmd.substr(sp + 1);
                size_t a_start = arg.find_first_not_of(" \t\"'");
                size_t a_end = arg.find_last_not_of(" \t\"'\r\n");
                if (a_start != std::string::npos && a_end != std::string::npos && a_end >= a_start) {
                    std::string key = arg.substr(a_start, a_end - a_start + 1);
                    auto it = g_cfg.build.prop_map.find(key);
                    if (it != g_cfg.build.prop_map.end()) {
                        output = it->second + "\n";
                        emit_probe("POPEN_GETPROP", key, it->second);
                    }
                }
            } else {
                std::ostringstream ss;
                for (const auto &kv : g_cfg.build.prop_map) {
                    ss << "[" << kv.first << "]: [" << kv.second << "]\n";
                }
                output = ss.str();
                emit_probe("POPEN_GETPROP", "ALL", "Synthesized");
            }

            if (!output.empty()) {
                int fd = static_cast<int>(syscall(__NR_memfd_create, "", MFD_CLOEXEC));
                if (fd >= 0) {
                    write_full(fd, output.data(), output.size());
                    if (orig_lseek) orig_lseek(fd, 0, SEEK_SET); else lseek(fd, 0, SEEK_SET);
                    FILE *fp = fdopen(fd, "r");
                    if (fp) return fp;
                    if (orig_close) orig_close(fd); else close(fd);
                }
            }
        }
    }
    return orig_popen ? orig_popen(command, type) : nullptr;
}

} // namespace mirage

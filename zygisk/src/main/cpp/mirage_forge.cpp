// SPDX-License-Identifier: GPL-3.0-only
// Copyright (c) 2025-2026 jjh138792
// MirageForge: https://github.com/jjh138792/MirageForge

#include <cstdlib>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <string>
#include <vector>
#include <unordered_map>
#include <sstream>
#include <fstream>
#include <mutex>
#include <dlfcn.h>
#include <fcntl.h>
#include <dirent.h>
#include <pthread.h>
#include <sys/stat.h>

#include "zygisk.hpp"
#include "mirage_config.hpp"
#include "mirage_logger.hpp"
#include "jvm_art_engine.hpp"
#include "fd_cloak_tracker.hpp"
#include "bionic_prop_engine.hpp"
#include "vfs_sysinfo_engine.hpp"
#include "graphics_vulkan_gles.hpp"
#include "linker_plt_engine.hpp"

using zygisk::Api;
using zygisk::AppSpecializeArgs;
using zygisk::ServerSpecializeArgs;
using namespace mirage;

class MirageForgeModule : public zygisk::ModuleBase {
public:
    void onLoad(Api *api, JNIEnv *env) override {
        this->api = api;
        this->env = env;
        g_zygisk_api = api;
        if (env) {
            env->GetJavaVM(&g_jvm);
        }
    }

    void preAppSpecialize(AppSpecializeArgs *args) override {
        if (!args || !args->nice_name) {
            api->setOption(zygisk::Option::DLCLOSE_MODULE_LIBRARY);
            return;
        }

        if (args->uid < 10000 && args->uid != 2000) {
            api->setOption(zygisk::Option::DLCLOSE_MODULE_LIBRARY);
            return;
        }

        const char *raw_pkg = env->GetStringUTFChars(args->nice_name, nullptr);
        if (!raw_pkg) {
            api->setOption(zygisk::Option::DLCLOSE_MODULE_LIBRARY);
            return;
        }
        std::string pkg_name(raw_pkg);
        env->ReleaseStringUTFChars(args->nice_name, raw_pkg);

        size_t colon = pkg_name.find(':');
        if (colon != std::string::npos) {
            pkg_name = pkg_name.substr(0, colon);
        }

        int fd = api->connectCompanion();
        if (fd < 0) {
            api->setOption(zygisk::Option::DLCLOSE_MODULE_LIBRARY);
            return;
        }

        write_str(fd, pkg_name);
        int32_t app_pid = static_cast<int32_t>(getpid());
        write_full(fd, &app_pid, sizeof(app_pid));

        uint8_t hit = 0;
        if (!read_full(fd, &hit, sizeof(hit)) || !hit) {
            close(fd);
            api->setOption(zygisk::Option::DLCLOSE_MODULE_LIBRARY);
            return;
        }

        g_cfg.pkg_name = pkg_name;
        g_cfg.module_version = read_str(fd);
        uint32_t entry_count = 0;
        read_full(fd, &entry_count, sizeof(entry_count));

        std::vector<std::pair<std::string, std::string>> raw_kvs;
        raw_kvs.reserve(entry_count);
        for (uint32_t i = 0; i < entry_count; ++i) {
            std::string k = read_str(fd);
            std::string v = read_str(fd);
            if (!k.empty() && !v.empty()) {
                raw_kvs.emplace_back(std::move(k), std::move(v));
            }
        }

        load_modular_config(raw_kvs);
        for (auto &kv : raw_kvs) {
            volatile_scrub_string(kv.first);
            volatile_scrub_string(kv.second);
        }
        raw_kvs.clear();

        if (!g_cfg.is_any_enabled()) {
            uint8_t zero = 0;
            write_full(fd, &zero, sizeof(zero)); // want_ns = 0
            write_full(fd, &zero, sizeof(zero)); // has_status = 0
            close(fd);
            api->setOption(zygisk::Option::DLCLOSE_MODULE_LIBRARY);
            return;
        }

        // 严格初始化顺序：必须先就绪 VFS/Bionic 符号与属性默认表，再发起统一 MountNS 请求
        if (g_cfg.vfs.any_enabled() || g_cfg.build.enabled) {
            init_vfs_symbols();
        }
        if (g_cfg.vfs.enable_cpu || g_cfg.vfs.enable_vfs_nodes) {
            init_vfs_clusters_lightweight();
        }
        if (g_cfg.build.enabled || g_cfg.probe.enabled) {
            init_bionic_prop_symbols();
        }
        if (g_cfg.build.enabled) {
            prepare_bionic_prop_map_defaults();
        }

        // 1. 统一 MountNS 握手（同步处理 VFS 节点 + MountNS 零脏页属性区）
        MountNsExecSummary ns_summary = request_private_mount_ns_in_pre_specialize(fd);

        // 2. 系统属性三引擎自适应执行与容灾降级：MountNS -> 纯 COW -> Hook
        uint32_t final_prop_verified = 0;
        uint32_t final_prop_inserted = 0;
        std::string status_diag = ns_summary.diag;

        if (g_cfg.build.enabled) {
            g_cfg.build.effective_prop_mode = g_cfg.build.prop_mode;

            if (g_cfg.build.effective_prop_mode == PropSpoofMode::MOUNT_NS) {
                if (ns_summary.prop_verified > 0) {
                    final_prop_verified = ns_summary.prop_verified;
                    final_prop_inserted = ns_summary.prop_inserted;
                } else {
                    g_cfg.build.effective_prop_mode = PropSpoofMode::COW;
                    if (!status_diag.empty()) status_diag += "|";
                    status_diag += "prop_ns_fallback=cow";
                }
            }

            if (g_cfg.build.effective_prop_mode == PropSpoofMode::COW) {
                NativePropSpoofStats cow_st = apply_cow_prop_spoof(
                    orig_system_property_find,
                    orig_system_property_read_callback,
                    orig_system_property_get,
                    orig_fopen,
                    orig_fclose
                );
                if (cow_st.verified_ok > 0) {
                    final_prop_verified = cow_st.verified_ok;
                    final_prop_inserted = cow_st.inserted_new;
                    if (!status_diag.empty()) status_diag += "|";
                    status_diag += "cow_vmas=" + std::to_string(cow_st.remapped_vmas) +
                                   ",ins=" + std::to_string(cow_st.inserted_new);
                } else {
                    g_cfg.build.effective_prop_mode = PropSpoofMode::HOOK;
                    if (!status_diag.empty()) status_diag += "|";
                    status_diag += "cow_fallback=hook";
                }
            }

            // 仅在最终落入 HOOK 模式时构建只读匿名属性区（纯 COW / MountNS 模式 0 匿名内存痕迹）
            if (g_cfg.build.effective_prop_mode == PropSpoofMode::HOOK) {
                freeze_bionic_prop_arena();
                final_prop_verified = static_cast<uint32_t>(g_cfg.build.prop_map.size());
            }
        }

        if (g_cfg.gl.enabled) {
            init_graphics_symbols();
        }

        need_resident_hooks_ = needs_any_plt_hooks();
        uint8_t zero_residence = need_resident_hooks_ ? 0 : 1;

        // 3. 向 Companion 汇报真实落盘的运行时状态（用于生成 runtime_status.json 实时徽章）
        uint8_t has_status = 1;
        write_full(fd, &has_status, sizeof(has_status));
        write_str(fd, g_cfg.build.enabled ? prop_mode_to_str(g_cfg.build.prop_mode) : "off");
        write_str(fd, g_cfg.build.enabled ? prop_mode_to_str(g_cfg.build.effective_prop_mode) : "off");
        write_full(fd, &final_prop_verified, sizeof(final_prop_verified));
        write_full(fd, &final_prop_inserted, sizeof(final_prop_inserted));
        write_full(fd, &ns_summary.vfs_mounted, sizeof(ns_summary.vfs_mounted));
        write_full(fd, &ns_summary.vfs_requested, sizeof(ns_summary.vfs_requested));
        write_full(fd, &zero_residence, sizeof(zero_residence));
        write_str(fd, status_diag);

        uint8_t sink_ready = 0;
        read_full(fd, &sink_ready, sizeof(sink_ready));

        if (g_cfg.probe.enabled && sink_ready == 1) {
            api->exemptFd(fd);
            g_probe_log_fd = fd;
        } else {
            close(fd);
        }

        // 仅在用户显式开启「探针」时安装崩溃信号捕获，日常保持原厂信号处理器 0 改动
        if (g_cfg.probe.enabled) {
            install_crash_signal_logger(pkg_name);
        }

        g_cfg.active = true;

        if (need_resident_hooks_) {
            init_linker_symbols();
            scan_and_commit_plt_hooks();
        } else if (!g_cfg.build.enabled) {
            // 既无需驻留 PLT 也无需在 postAppSpecialize 反射 Java Build：立刻自卸载
            api->setOption(zygisk::Option::DLCLOSE_MODULE_LIBRARY);
        }
    }

    void postAppSpecialize(const AppSpecializeArgs *) override {
        if (!g_cfg.active) return;
        g_post_specialized.store(true, std::memory_order_release);

        if (g_cfg.build.enabled) {
            reflect_java_build_fields(env);
            g_cfg.build.wipe_volatile_reflection_maps();
        }
        if (needs_display_lazy_hook()) {
            try_lazy_spoof_display_modes();
        }

        // 终极零驻留隐身：纯 COW/MountNS 场景下，Java 反射完毕立即卸载自身 .so
        if (!need_resident_hooks_) {
            api->setOption(zygisk::Option::DLCLOSE_MODULE_LIBRARY);
        }
    }

    void preServerSpecialize(ServerSpecializeArgs *) override {
        api->setOption(zygisk::Option::DLCLOSE_MODULE_LIBRARY);
    }

private:
    Api *api = nullptr;
    JNIEnv *env = nullptr;
    bool need_resident_hooks_ = false;

    void load_modular_config(const std::vector<std::pair<std::string, std::string>> &raw_kvs) {
        bool legacy_vfs_on = false;
        bool has_granular_vfs = false;
        bool has_prop_mode = false;
        bool legacy_cow_on = false;

        for (const auto &kv : raw_kvs) {
            const std::string &k = kv.first;
            const std::string &v = kv.second;
            if (k == "META:ENABLE_BUILD")          g_cfg.build.enabled = (v == "1");
            else if (k == "META:PROP_MODE")        { g_cfg.build.prop_mode = parse_prop_mode(v); has_prop_mode = true; }
            else if (k == "META:ENABLE_COW_PROP")  legacy_cow_on = (v == "1");
            else if (k == "META:ENABLE_GL")        g_cfg.gl.enabled = (v == "1");
            else if (k == "META:ENABLE_HDR")       g_cfg.gl.enable_hdr_spoof = (v == "1");
            else if (k == "META:ENABLE_VFS")       legacy_vfs_on = (v == "1");
            else if (k == "META:ENABLE_CPU")       { g_cfg.vfs.enable_cpu = (v == "1"); has_granular_vfs = true; }
            else if (k == "META:ENABLE_MEM")       { g_cfg.vfs.enable_mem = (v == "1"); has_granular_vfs = true; }
            else if (k == "META:ENABLE_VFS_NODES") { g_cfg.vfs.enable_vfs_nodes = (v == "1"); has_granular_vfs = true; }
            else if (k == "META:ENABLE_PROBE")     g_cfg.probe.enabled = (v == "1");
            else if (k == "META:FORCE_SPOOF_SDK")  g_cfg.build.force_spoof_sdk = (v == "1");
            else if (k == "META:REFRESH_RATE")     g_cfg.gl.max_refresh_rate = std::atoi(v.c_str());
            else if (k == "META:ENABLE_MOUNT_NS")  g_cfg.vfs.enable_mount_ns = (v == "1");
        }

        if (!has_granular_vfs && legacy_vfs_on) {
            g_cfg.vfs.enable_cpu = true;
            g_cfg.vfs.enable_mem = true;
            g_cfg.vfs.enable_vfs_nodes = true;
        }
        g_cfg.vfs.enabled = g_cfg.vfs.any_enabled();

        if (!has_prop_mode && legacy_cow_on) {
            g_cfg.build.prop_mode = PropSpoofMode::COW;
        }
        if (!g_cfg.build.enabled) {
            g_cfg.build.prop_mode = PropSpoofMode::HOOK;
            g_cfg.build.force_spoof_sdk = false;
        }
        g_cfg.build.effective_prop_mode = g_cfg.build.prop_mode;

        for (const auto &kv : raw_kvs) {
            const std::string &k = kv.first;
            const std::string &v = kv.second;
            if (k.rfind("META:", 0) == 0) continue;

            if (k.rfind("GLES:", 0) == 0) {
                if (k == "GLES:REFRESH_RATE" && (g_cfg.build.enabled || g_cfg.gl.enabled) && g_cfg.gl.max_refresh_rate == 0) {
                    g_cfg.gl.max_refresh_rate = std::atoi(v.c_str());
                }
                if (!g_cfg.gl.enabled) continue;
                if (k == "GLES:VENDOR")        g_cfg.gl.gl_vendor = v;
                else if (k == "GLES:RENDERER") g_cfg.gl.gl_renderer = v;
                else if (k == "GLES:VERSION")  g_cfg.gl.gl_version = v;
                continue;
            }

            if (k.rfind("VULKAN:", 0) == 0) {
                if (!g_cfg.gl.enabled) continue;
                if (k == "VULKAN:DEVICE_NAME") {
                    g_cfg.gl.vk_device_name = v;
                } else if (k == "VULKAN:VENDOR_ID") {
                    g_cfg.gl.vk_vendor_id = parse_vk_u32(v);
                } else if (k == "VULKAN:DEVICE_ID") {
                    g_cfg.gl.vk_device_id = parse_vk_u32(v);
                } else if (k == "VULKAN:API_VERSION") {
                    g_cfg.gl.vk_api_version = v;
                    g_cfg.gl.vk_api_version_packed = pack_vk_version_str(v);
                } else if (k == "VULKAN:DRIVER_VERSION") {
                    g_cfg.gl.vk_driver_version = v;
                    g_cfg.gl.vk_driver_version_packed = pack_vk_version_str(v);
                } else if (k == "VULKAN:DRIVER_NAME") {
                    g_cfg.gl.vk_driver_name = v;
                } else if (k == "VULKAN:DRIVER_INFO") {
                    g_cfg.gl.vk_driver_info = v;
                }
                continue;
            }

            if (k.rfind("VFS:", 0) == 0) {
                if (!g_cfg.vfs.any_enabled()) continue;
                if (k == "VFS:CPU_HARDWARE")        g_cfg.vfs.cpu_hardware = v;
                else if (k == "VFS:CPU_ARCH")       g_cfg.vfs.cpu_arch = v;
                else if (k == "VFS:CPU_CORES")      g_cfg.vfs.cpu_cores = std::atoi(v.c_str());
                else if (k == "VFS:CPU_MAX_FREQ")   g_cfg.vfs.cpu_max_freq_khz = std::atoi(v.c_str());
                else if (k == "VFS:CPU_CLUSTERS")   g_cfg.vfs.cpu_clusters_spec = v;
                else if (k == "VFS:RAM_GB")         g_cfg.vfs.ram_gb = std::atoi(v.c_str());
                else if (k == "VFS:DDR_TYPE")       g_cfg.vfs.ddr_type = v;
                else if (k == "VFS:DDR_VENDOR")     g_cfg.vfs.ddr_vendor = v;
                else if (k == "VFS:SOC_ID")         g_cfg.vfs.soc_id = v;
                else if (k == "VFS:SOC_MACHINE")    g_cfg.vfs.soc_machine = v;
                else if (k == "VFS:KERNEL_RELEASE") g_cfg.vfs.kernel_release = v;
                else if (k == "VFS:KERNEL_VERSION") g_cfg.vfs.kernel_version = v;
                continue;
            }

            if (!g_cfg.build.enabled) continue;

            if (k.rfind("BUILD:", 0) == 0) {
                std::string b_field = k.substr(6);
                g_cfg.build.build_map[b_field] = v;
                if (b_field == "DEVICE") {
                    g_cfg.build.build_map["SKU"] = v;
                    g_cfg.build.build_map["ODM_SKU"] = v;
                }
            } else if (k.rfind("VERSION:", 0) == 0) {
                std::string v_field = k.substr(8);
                if (!g_cfg.build.force_spoof_sdk &&
                    (v_field == "SDK_INT" || v_field == "DEVICE_INITIAL_SDK_INT" ||
                     v_field == "RELEASE" || v_field == "CODENAME" || v_field == "PREVIEW_SDK_INT")) {
                    continue;
                }
                g_cfg.build.version_map[v_field] = v;
            } else {
                if (!g_cfg.build.force_spoof_sdk &&
                    (k.find("build.version.sdk") != std::string::npos ||
                     k.find("build.version.release") != std::string::npos ||
                     k.find("first_api_level") != std::string::npos ||
                     k.find("preview_sdk") != std::string::npos ||
                     k.find("version.codename") != std::string::npos)) {
                    continue;
                }
                g_cfg.build.prop_map[k] = v;
            }
        }

        if (g_cfg.gl.enabled) {
            g_cfg.gl.finalize_vulkan_defaults();
        }
    }
};

// ============================================================================
// Root Companion 守护进程（并发安全缓存 + 统一 MountNS + 实时状态落盘 + 探针）
// ============================================================================
static std::mutex g_file_write_mtx;
static std::mutex g_companion_cache_mtx;

struct RuntimeStatusRecord {
    int64_t timestamp = 0;
    int32_t pid = 0;
    std::string prop_req;
    std::string prop_eff;
    uint32_t prop_verified = 0;
    uint32_t prop_inserted = 0;
    uint32_t vfs_mounted = 0;
    uint32_t vfs_requested = 0;
    uint32_t live_nodes = 0;
    bool zero_residence = false;
    std::string diag;
};

static std::unordered_map<std::string, RuntimeStatusRecord> g_runtime_status_map;

static std::string json_escape_str(const std::string &in) {
    std::string out;
    out.reserve(in.size() + 8);
    for (unsigned char c : in) {
        switch (c) {
            case '"':  out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\n': out += "\\n"; break;
            case '\r': out += "\\r"; break;
            case '\t': out += "\\t"; break;
            default:
                if (c >= 0x20) out.push_back(static_cast<char>(c));
                break;
        }
    }
    return out;
}

static void record_runtime_status_as_root(const std::string &pkg, const RuntimeStatusRecord &rec) {
    std::lock_guard<std::mutex> lock(g_file_write_mtx);
    if (g_runtime_status_map.size() >= 128 && g_runtime_status_map.find(pkg) == g_runtime_status_map.end()) {
        g_runtime_status_map.erase(g_runtime_status_map.begin());
    }
    g_runtime_status_map[pkg] = rec;

    mkdir(DATA_ROOT_DIR, 0700);
    std::ostringstream ss;
    ss << "{\n";
    bool first = true;
    for (const auto &kv : g_runtime_status_map) {
        if (!first) ss << ",\n";
        first = false;
        const auto &r = kv.second;
        ss << "  \"" << json_escape_str(kv.first) << "\": {"
           << "\"timestamp\":" << r.timestamp
           << ",\"pid\":" << r.pid
           << ",\"propReq\":\"" << json_escape_str(r.prop_req) << "\""
           << ",\"propEff\":\"" << json_escape_str(r.prop_eff) << "\""
           << ",\"propVerified\":" << r.prop_verified
           << ",\"propInserted\":" << r.prop_inserted
           << ",\"vfsMounted\":" << r.vfs_mounted
           << ",\"vfsRequested\":" << r.vfs_requested
           << ",\"liveNodes\":" << r.live_nodes
           << ",\"zeroResidence\":" << (r.zero_residence ? "true" : "false")
           << ",\"diag\":\"" << json_escape_str(r.diag) << "\""
           << "}";
    }
    ss << "\n}\n";

    std::string payload = ss.str();
    std::string tmp_path = std::string(RUNTIME_STATUS_PATH) + ".tmp";
    FILE *fp = std::fopen(tmp_path.c_str(), "w");
    if (!fp) return;
    std::fwrite(payload.data(), 1, payload.size(), fp);
    std::fflush(fp);
    std::fclose(fp);
    chmod(tmp_path.c_str(), 0600);
    std::rename(tmp_path.c_str(), RUNTIME_STATUS_PATH);
}

static void append_line_as_root(const char *path, const std::string &line) {
    std::lock_guard<std::mutex> lock(g_file_write_mtx);
    FILE *fp = std::fopen(path, "a");
    if (!fp) return;
    std::fwrite(line.data(), 1, line.size(), fp);
    std::fputc('\n', fp);
    std::fflush(fp);
    std::fclose(fp);
    chmod(path, 0600);
}

static void *companion_log_sink_thread(void *arg) {
    int sock_fd = static_cast<int>(reinterpret_cast<intptr_t>(arg));
    std::string pending;
    std::string pending_crash_line;
    std::time_t pending_crash_ts = 0;
    char buf[1024];

    auto process_line = [&](const std::string &line) {
        if (line.empty()) return;
        std::time_t now = std::time(nullptr);

        if (line.rfind("[PENDING_CRASH]", 0) == 0) {
            if (pending_crash_line.empty() || (now - pending_crash_ts) > 8) {
                pending_crash_line = "[CRASH]" + line.substr(15);
                pending_crash_ts = now;
            }
            return;
        }
        if (line.rfind("[CRASH]", 0) == 0) {
            append_line_as_root(CRASH_LOG_PATH, line);
            return;
        }
        if (!pending_crash_line.empty() && (now - pending_crash_ts) >= 2) {
            pending_crash_line.clear();
            pending_crash_ts = 0;
        }
        append_line_as_root(PROBE_LOG_PATH, line);
    };

    while (true) {
        ssize_t n;
        do {
            n = read(sock_fd, buf, sizeof(buf));
        } while (n == -1 && errno == EINTR);

        if (n <= 0) break;
        pending.append(buf, static_cast<size_t>(n));

        size_t nl_pos = 0;
        while ((nl_pos = pending.find('\n')) != std::string::npos) {
            std::string line = pending.substr(0, nl_pos);
            pending.erase(0, nl_pos + 1);
            if (!line.empty() && line.back() == '\r') line.pop_back();
            process_line(line);
        }
    }

    if (!pending.empty()) {
        if (pending.back() == '\r') pending.pop_back();
        process_line(pending);
    }

    if (!pending_crash_line.empty()) {
        std::time_t now = std::time(nullptr);
        if (now >= pending_crash_ts && (now - pending_crash_ts) <= 8) {
            append_line_as_root(CRASH_LOG_PATH, pending_crash_line);
        }
    }

    close(sock_fd);
    return nullptr;
}

struct CompanionCache {
    time_t last_sec = 0;
    long last_nsec = 0;
    std::string cached_version;
    std::unordered_map<std::string, std::unordered_map<std::string, std::string>> pkg_configs;

    void refresh_if_needed() {
        if (cached_version.empty()) {
            cached_version = read_module_prop_version();
        }
        struct stat db_st{};
        if (stat(ROUTES_DB_PATH, &db_st) != 0) {
            pkg_configs.clear();
            last_sec = 0;
            last_nsec = 0;
            return;
        }
        if (db_st.st_mtim.tv_sec == last_sec &&
            db_st.st_mtim.tv_nsec == last_nsec &&
            !pkg_configs.empty()) {
            return;
        }

        cached_version = read_module_prop_version();
        pkg_configs.clear();
        DIR *dir = opendir(APPS_CONF_DIR);
        if (!dir) return;

        struct dirent *ent = nullptr;
        while ((ent = readdir(dir)) != nullptr) {
            std::string fn(ent->d_name);
            if (fn.size() <= 5 || fn.substr(fn.size() - 5) != ".conf") continue;
            std::string pkg = fn.substr(0, fn.size() - 5);

            std::ifstream ifs(std::string(APPS_CONF_DIR) + "/" + fn);
            if (!ifs.is_open()) continue;

            std::unordered_map<std::string, std::string> kv;
            std::string line;
            while (std::getline(ifs, line)) {
                if (!line.empty() && line.back() == '\r') line.pop_back();
                if (line.empty() || line[0] == '#') continue;
                size_t eq = line.find('=');
                if (eq == std::string::npos) continue;
                std::string k = line.substr(0, eq);
                std::string v = line.substr(eq + 1);
                if (!k.empty() && !v.empty()) kv[k] = v;
            }
            if (!kv.empty()) {
                pkg_configs[pkg] = std::move(kv);
            }
        }
        closedir(dir);
        last_sec = db_st.st_mtim.tv_sec;
        last_nsec = db_st.st_mtim.tv_nsec;
    }
};

static CompanionCache g_companion_cache;

static void record_launch_telemetry(const std::string &pkg, int32_t pid, const std::string &model, const std::string &gpu, int ram_gb) {
    char buf[512];
    std::time_t now = std::time(nullptr);
    std::snprintf(buf, sizeof(buf), "%lld|%s|%d|%s|%s|%dGB",
                  static_cast<long long>(now), pkg.c_str(), pid,
                  model.c_str(), gpu.c_str(), ram_gb);
    append_line_as_root(LAUNCH_LOG_PATH, buf);
}

static void companion_handler(int fd) {
    std::string pkg_name = read_str(fd);
    int32_t app_pid = 0;
    read_full(fd, &app_pid, sizeof(app_pid));

    if (pkg_name.empty()) {
        uint8_t zero = 0;
        write_full(fd, &zero, sizeof(zero));
        return;
    }

    std::unordered_map<std::string, std::string> entries;
    std::string version_snapshot;
    {
        std::lock_guard<std::mutex> lock(g_companion_cache_mtx);
        g_companion_cache.refresh_if_needed();
        auto it = g_companion_cache.pkg_configs.find(pkg_name);
        if (it == g_companion_cache.pkg_configs.end()) {
            uint8_t zero = 0;
            write_full(fd, &zero, sizeof(zero));
            return;
        }
        entries = it->second;
        version_snapshot = g_companion_cache.cached_version;
    }

    auto flag_on = [&](const char *k) -> bool {
        auto it = entries.find(k);
        return it != entries.end() && it->second == "1";
    };

    bool any_on = flag_on("META:ENABLE_BUILD") ||
                  flag_on("META:ENABLE_COW_PROP") ||
                  flag_on("META:ENABLE_GL") ||
                  flag_on("META:ENABLE_VFS") ||
                  flag_on("META:ENABLE_CPU") ||
                  flag_on("META:ENABLE_MEM") ||
                  flag_on("META:ENABLE_VFS_NODES") ||
                  flag_on("META:ENABLE_PROBE");

    if (!any_on) {
        uint8_t zero = 0;
        write_full(fd, &zero, sizeof(zero));
        return;
    }

    uint8_t one = 1;
    write_full(fd, &one, sizeof(one));
    write_str(fd, version_snapshot);
    uint32_t count = static_cast<uint32_t>(entries.size());
    write_full(fd, &count, sizeof(count));
    for (const auto &kv : entries) {
        write_str(fd, kv.first);
        write_str(fd, kv.second);
    }

    // 1. 统一 MountNS 挂载（VFS 文件节点 + 系统属性区 + 活体动态 pwrite 线程）
    MountNsExecSummary ns_summary = companion_handle_mount_ns_sync(fd, app_pid);

    // 2. 接收 App 端最终引擎状态汇报并落盘 runtime_status.json
    uint8_t has_status = 0;
    if (!read_full(fd, &has_status, sizeof(has_status)) || has_status == 0) {
        return;
    }

    RuntimeStatusRecord st{};
    st.timestamp = static_cast<int64_t>(std::time(nullptr));
    st.pid = app_pid;
    st.prop_req = read_str(fd);
    st.prop_eff = read_str(fd);
    read_full(fd, &st.prop_verified, sizeof(st.prop_verified));
    read_full(fd, &st.prop_inserted, sizeof(st.prop_inserted));
    read_full(fd, &st.vfs_mounted, sizeof(st.vfs_mounted));
    read_full(fd, &st.vfs_requested, sizeof(st.vfs_requested));
    uint8_t zero_res_u8 = 0;
    read_full(fd, &zero_res_u8, sizeof(zero_res_u8));
    st.zero_residence = (zero_res_u8 == 1);
    st.live_nodes = ns_summary.live_nodes;
    st.diag = read_str(fd);

    record_runtime_status_as_root(pkg_name, st);

    std::string model = entries.count("BUILD:MODEL") ? entries.at("BUILD:MODEL") : "Custom";
    std::string gpu   = entries.count("GLES:RENDERER") ? entries.at("GLES:RENDERER") : "DefaultGPU";
    int ram_gb        = entries.count("VFS:RAM_GB") ? std::atoi(entries.at("VFS:RAM_GB").c_str()) : 16;
    bool probe_on     = flag_on("META:ENABLE_PROBE");

    if (probe_on) {
        mkdir(DATA_ROOT_DIR, 0700);
        append_line_as_root(
            PROBE_LOG_PATH,
            "[PROBE][" + pkg_name + "][SESSION] MirageForge_v" + version_snapshot +
                "_Attached => " + model + " | " + gpu +
                " | prop=" + st.prop_eff + "(" + std::to_string(st.prop_verified) +
                ",+" + std::to_string(st.prop_inserted) + ")" +
                " | zero_res=" + std::string(st.zero_residence ? "1" : "0") +
                " @@ zygisk::preAppSpecialize(pid=" + std::to_string(app_pid) + ")"
        );
        if (!st.diag.empty() && st.diag != "disabled") {
            append_line_as_root(
                PROBE_LOG_PATH,
                "[PROBE][" + pkg_name + "][ENGINE_DIAG] " + st.prop_req + "->" + st.prop_eff +
                    " => " + st.diag + " @@ companion(pid=" + std::to_string(app_pid) + ")"
            );
        }

        int sink_fd = dup(fd);
        if (sink_fd >= 0) {
            pthread_t tid;
            if (pthread_create(&tid, nullptr, companion_log_sink_thread,
                               reinterpret_cast<void *>(static_cast<intptr_t>(sink_fd))) == 0) {
                pthread_detach(tid);
                uint8_t ready = 1;
                write_full(fd, &ready, sizeof(ready));
            } else {
                close(sink_fd);
                uint8_t no_ready = 0;
                write_full(fd, &no_ready, sizeof(no_ready));
            }
        } else {
            uint8_t no_ready = 0;
            write_full(fd, &no_ready, sizeof(no_ready));
        }
    } else {
        uint8_t no_ready = 0;
        write_full(fd, &no_ready, sizeof(no_ready));
    }

    record_launch_telemetry(pkg_name, app_pid, model, gpu, ram_gb);
}

REGISTER_ZYGISK_MODULE(MirageForgeModule)
REGISTER_ZYGISK_COMPANION(companion_handler)

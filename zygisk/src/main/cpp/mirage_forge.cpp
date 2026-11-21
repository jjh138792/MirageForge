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

        uint8_t sink_ready = 0;
        read_full(fd, &sink_ready, sizeof(sink_ready));

        load_modular_config(raw_kvs);
        for (auto &kv : raw_kvs) {
            volatile_scrub_string(kv.first);
            volatile_scrub_string(kv.second);
        }
        raw_kvs.clear();

        if (!g_cfg.is_any_enabled()) {
            close(fd);
            api->setOption(zygisk::Option::DLCLOSE_MODULE_LIBRARY);
            return;
        }

        if (g_cfg.probe.enabled && sink_ready == 1) {
            api->exemptFd(fd);
            g_probe_log_fd = fd;
        } else {
            close(fd);
        }

        init_linker_symbols();

        // 严格子模块 0 改动隔离：
        // 仅当 build 或 probe 开启时才解析 Bionic 属性符号；仅当 build 开启时才构建属性区与执行反射
        if (g_cfg.build.enabled || g_cfg.probe.enabled) {
            init_bionic_prop_symbols();
        }
        if (g_cfg.build.enabled) {
            freeze_bionic_prop_arena();
            reflect_java_build_fields(env);
        }
        if (g_cfg.vfs.enabled || g_cfg.build.enabled) {
            init_vfs_symbols();
        }
        if (g_cfg.vfs.enabled) {
            init_vfs_clusters_lightweight();
        }
        if (g_cfg.gl.enabled) {
            init_graphics_symbols();
        }
        if (g_cfg.probe.enabled) {
            install_crash_signal_logger(pkg_name);
        }

        g_cfg.active = true;
        scan_and_commit_plt_hooks();
    }

    void postAppSpecialize(const AppSpecializeArgs *) override {
        if (!g_cfg.active) return;
        g_post_specialized.store(true, std::memory_order_release);
        if (g_cfg.build.enabled) {
            reflect_java_build_fields(env);
            g_cfg.build.wipe_volatile_reflection_maps();
        }
        if (g_cfg.gl.enabled) {
            dlopen("libhardware.so", RTLD_NOW);
            dlopen("libvulkan.so", RTLD_NOW);
        }
        scan_and_commit_plt_hooks();
        if (g_cfg.vfs.enabled && g_cfg.vfs.enable_seccomp) {
            install_on_demand_seccomp_bpf();
        }
    }

    void preServerSpecialize(ServerSpecializeArgs *) override {
        api->setOption(zygisk::Option::DLCLOSE_MODULE_LIBRARY);
    }

private:
    Api *api = nullptr;
    JNIEnv *env = nullptr;

    void load_modular_config(const std::vector<std::pair<std::string, std::string>> &raw_kvs) {
        for (const auto &kv : raw_kvs) {
            const std::string &k = kv.first;
            const std::string &v = kv.second;
            if (k == "META:ENABLE_BUILD")         g_cfg.build.enabled = (v == "1");
            else if (k == "META:ENABLE_GL")       g_cfg.gl.enabled = (v == "1");
            else if (k == "META:ENABLE_VFS")      g_cfg.vfs.enabled = (v == "1");
            else if (k == "META:ENABLE_PROBE")    g_cfg.probe.enabled = (v == "1");
            else if (k == "META:FORCE_SPOOF_SDK") g_cfg.build.force_spoof_sdk = (v == "1");
            else if (k == "META:REFRESH_RATE")    g_cfg.gl.max_refresh_rate = std::atoi(v.c_str());
            else if (k == "META:ENABLE_SECCOMP")  g_cfg.vfs.enable_seccomp = (v == "1");
        }

        for (const auto &kv : raw_kvs) {
            const std::string &k = kv.first;
            const std::string &v = kv.second;
            if (k.rfind("META:", 0) == 0) continue;

            if (k.rfind("GLES:", 0) == 0) {
                if (!g_cfg.gl.enabled) continue;
                if (k == "GLES:VENDOR") {
                    g_cfg.gl.gl_vendor = v;
                } else if (k == "GLES:RENDERER") {
                    g_cfg.gl.gl_renderer = v;
                } else if (k == "GLES:VERSION") {
                    g_cfg.gl.gl_version = v;
                } else if (k == "GLES:REFRESH_RATE" && g_cfg.gl.max_refresh_rate == 0) {
                    g_cfg.gl.max_refresh_rate = std::atoi(v.c_str());
                }
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
                if (!g_cfg.vfs.enabled) continue;
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
// Root Companion 守护进程（含真实死亡核验崩溃日志落盘）
// ============================================================================
static std::mutex g_file_write_mtx;

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
            // 暂存待核验崩溃信号；若同一死亡窗口内已捕获首个致命信号则保留第一现场
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

        // 收到后续正常探针且距离上次信号已超过 1 秒 -> 证明进程安然存活，自动作废该非致命信号
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

    // Socket 断开 (EOF)：仅当进程在捕获致命信号后的 8 秒窗口内真正退出，才确认为真实崩溃并落盘！
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

    g_companion_cache.refresh_if_needed();
    auto it = g_companion_cache.pkg_configs.find(pkg_name);
    if (it == g_companion_cache.pkg_configs.end()) {
        uint8_t zero = 0;
        write_full(fd, &zero, sizeof(zero));
        return;
    }

    const auto &entries = it->second;
    bool any_enabled =
        (entries.count("META:ENABLE_BUILD") && entries.at("META:ENABLE_BUILD") == "1") ||
        (entries.count("META:ENABLE_GL")    && entries.at("META:ENABLE_GL") == "1")    ||
        (entries.count("META:ENABLE_VFS")   && entries.at("META:ENABLE_VFS") == "1")   ||
        (entries.count("META:ENABLE_PROBE") && entries.at("META:ENABLE_PROBE") == "1");

    if (!any_enabled) {
        uint8_t zero = 0;
        write_full(fd, &zero, sizeof(zero));
        return;
    }

    uint8_t one = 1;
    write_full(fd, &one, sizeof(one));
    write_str(fd, g_companion_cache.cached_version);
    uint32_t count = static_cast<uint32_t>(entries.size());
    write_full(fd, &count, sizeof(count));
    for (const auto &kv : entries) {
        write_str(fd, kv.first);
        write_str(fd, kv.second);
    }

    std::string model = entries.count("BUILD:MODEL") ? entries.at("BUILD:MODEL") : "Custom";
    std::string gpu   = entries.count("GLES:RENDERER") ? entries.at("GLES:RENDERER") : "DefaultGPU";
    int ram_gb        = entries.count("VFS:RAM_GB") ? std::atoi(entries.at("VFS:RAM_GB").c_str()) : 16;

    bool probe_enabled = (entries.count("META:ENABLE_PROBE") && entries.at("META:ENABLE_PROBE") == "1");
    if (probe_enabled) {
        mkdir(DATA_ROOT_DIR, 0700);
        append_line_as_root(
            PROBE_LOG_PATH,
            "[PROBE][" + pkg_name + "][SESSION] MirageForge_v" + g_companion_cache.cached_version +
                "_Attached => " + model + " | " + gpu + " @@ zygisk::preAppSpecialize(pid=" + std::to_string(app_pid) + ")"
        );

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

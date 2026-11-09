#include <cstdlib>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <string>
#include <vector>
#include <unordered_map>
#include <sstream>
#include <fstream>
#include <dlfcn.h>
#include <fcntl.h>
#include <dirent.h>
#include <sys/stat.h>

#include "zygisk.hpp"
#include "mirage_config.hpp"
#include "mirage_logger.hpp"
#include "fd_cloak_tracker.hpp"
#include "bionic_prop_engine.hpp"
#include "vfs_sysinfo_engine.hpp"
#include "graphics_vulkan_gles.hpp"
#include "linker_plt_engine.hpp"
#include "jvm_art_engine.hpp"

using zygisk::Api;
using zygisk::AppSpecializeArgs;
using zygisk::ServerSpecializeArgs;
using namespace mirage;

class MirageForgeModule : public zygisk::ModuleBase {
public:
    void onLoad(Api *api, JNIEnv *env) override {
        // 绝对 0 开销：onLoad 阶段不做任何 dlsym 符号查找或内存分配！
        this->api = api;
        this->env = env;
        g_zygisk_api = api;
    }

    void preAppSpecialize(AppSpecializeArgs *args) override {
        if (!args || !args->nice_name) {
            api->setOption(zygisk::Option::DLCLOSE_MODULE_LIBRARY);
            return;
        }

        // 快速排除系统 UID (如 system_server 或核心 radio/bluetooth 守护进程)
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
            // 未配置的应用：0 dlsym、0 内存改动，直接卸载动态库！
            api->setOption(zygisk::Option::DLCLOSE_MODULE_LIBRARY);
            return;
        }

        g_cfg.pkg_name = pkg_name;
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

        uint8_t has_scm_fds = 0;
        if (read_full(fd, &has_scm_fds, sizeof(has_scm_fds)) && has_scm_fds == 1) {
            int p_fd = -1, c_fd = -1;
            if (recv_log_fds(fd, &p_fd, &c_fd)) {
                if (p_fd >= 0) { api->exemptFd(p_fd); g_probe_log_fd = p_fd; }
                if (c_fd >= 0) { api->exemptFd(c_fd); g_crash_log_fd = c_fd; }
            }
        }
        close(fd);

        // 两阶段严格按需加载微模块配置
        load_modular_config(raw_kvs);

        // 若所有 4 个子开关均处于关闭状态，直接卸载模块，保证绝对 0 改动
        if (!g_cfg.build.enabled && !g_cfg.gl.enabled && !g_cfg.vfs.enabled && !g_cfg.probe.enabled) {
            api->setOption(zygisk::Option::DLCLOSE_MODULE_LIBRARY);
            return;
        }

        // 按需初始化被激活子模块的原生符号与数据结构
        init_linker_symbols();
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
            emit_probe("SESSION", "MirageForge_v1.1.0_Attached", pkg_name);
        }

        g_cfg.active = true;
        scan_and_commit_plt_hooks();
    }

    void postAppSpecialize(const AppSpecializeArgs *) override {
        if (!g_cfg.active) return;
        if (g_cfg.build.enabled) {
            reflect_java_build_fields(env);
        }
        scan_and_commit_plt_hooks();
    }

    void preServerSpecialize(ServerSpecializeArgs *) override {
        api->setOption(zygisk::Option::DLCLOSE_MODULE_LIBRARY);
    }

private:
    Api *api = nullptr;
    JNIEnv *env = nullptr;

    void load_modular_config(const std::vector<std::pair<std::string, std::string>> &raw_kvs) {
        // 第一遍：读取模块级开关
        for (const auto &kv : raw_kvs) {
            const std::string &k = kv.first;
            const std::string &v = kv.second;
            if (k == "META:ENABLE_BUILD")      g_cfg.build.enabled = (v == "1");
            else if (k == "META:ENABLE_GL")    g_cfg.gl.enabled = (v == "1");
            else if (k == "META:ENABLE_VFS")   g_cfg.vfs.enabled = (v == "1");
            else if (k == "META:ENABLE_PROBE") g_cfg.probe.enabled = (v == "1");
            else if (k == "META:FORCE_SPOOF_SDK") g_cfg.build.force_spoof_sdk = (v == "1");
        }

        // 第二遍：严格只装载已开启模块的数据，未开启模块完全跳过（杜绝先写后删）
        for (const auto &kv : raw_kvs) {
            const std::string &k = kv.first;
            const std::string &v = kv.second;
            if (k.rfind("META:", 0) == 0) continue;

            if (k.rfind("GLES:", 0) == 0) {
                if (!g_cfg.gl.enabled) continue;
                if (k == "GLES:VENDOR") {
                    g_cfg.gl.gl_vendor = v;
                    g_cfg.gl.vulkan_vendor_id = (v.find("ARM") != std::string::npos || v.find("Mali") != std::string::npos) ? 0x13B5 : 0x5143;
                } else if (k == "GLES:RENDERER") {
                    g_cfg.gl.gl_renderer = v;
                } else if (k == "GLES:VERSION") {
                    g_cfg.gl.gl_version = v;
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

            // 其余为 BUILD:*, VERSION:*, ro.*, persist.*，仅在开启了 Build 模块时载入
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
                if ((v_field == "SDK_INT" || v_field == "DEVICE_INITIAL_SDK_INT" || v_field == "RELEASE") &&
                    !g_cfg.build.force_spoof_sdk) {
                    continue; // 未开启强制 SDK 伪装时，直接不载入，绝不触碰原值
                }
                g_cfg.build.version_map[v_field] = v;
            } else {
                if (!g_cfg.build.force_spoof_sdk &&
                    (k == "ro.build.version.sdk" || k == "ro.product.build.version.sdk" ||
                     k == "ro.system.build.version.sdk" || k == "ro.build.version.release")) {
                    continue;
                }
                g_cfg.build.prop_map[k] = v;
            }
        }
    }
};

// ============================================================================
// Root Companion 守护进程：内存级缓存 (0 磁盘 I/O 快速判定未配置应用)
// ============================================================================
struct CompanionCache {
    time_t last_db_mtime = 0;
    std::unordered_map<std::string, std::unordered_map<std::string, std::string>> pkg_configs;

    void refresh_if_needed() {
        struct stat db_st{};
        if (stat(ROUTES_DB_PATH, &db_st) != 0) {
            pkg_configs.clear();
            last_db_mtime = 0;
            return;
        }
        if (db_st.st_mtime == last_db_mtime && !pkg_configs.empty()) {
            return; // 缓存命中，0 磁盘读取！
        }

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
        last_db_mtime = db_st.st_mtime;
    }
};

static CompanionCache g_companion_cache;

static void record_launch_telemetry(const std::string &pkg, int32_t pid, const std::string &model, const std::string &gpu, int ram_gb) {
    FILE *fp = std::fopen(LAUNCH_LOG_PATH, "a");
    if (!fp) return;
    std::time_t now = std::time(nullptr);
    std::fprintf(fp, "%lld|%s|%d|%s|%s|%dGB\n",
                 static_cast<long long>(now), pkg.c_str(), pid,
                 model.c_str(), gpu.c_str(), ram_gb);
    std::fclose(fp);
    chmod(LAUNCH_LOG_PATH, 0600);
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
        // 未配置伪装的应用：直接从内存哈希表返回 0，零文件打开开销！
        uint8_t zero = 0;
        write_full(fd, &zero, sizeof(zero));
        return;
    }

    const auto &entries = it->second;
    uint8_t one = 1;
    write_full(fd, &one, sizeof(one));
    uint32_t count = static_cast<uint32_t>(entries.size());
    write_full(fd, &count, sizeof(count));
    for (const auto &kv : entries) {
        write_str(fd, kv.first);
        write_str(fd, kv.second);
    }

    bool probe_enabled = (entries.count("META:ENABLE_PROBE") && entries.at("META:ENABLE_PROBE") == "1");
    if (probe_enabled) {
        mkdir(DATA_ROOT_DIR, 0700);
        int p_fd = open(PROBE_LOG_PATH, O_WRONLY | O_CREAT | O_APPEND | O_CLOEXEC, 0600);
        int c_fd = open(CRASH_LOG_PATH, O_WRONLY | O_CREAT | O_APPEND | O_CLOEXEC, 0600);
        if (p_fd >= 0 && c_fd >= 0) {
            uint8_t has_scm = 1;
            write_full(fd, &has_scm, sizeof(has_scm));
            send_log_fds(fd, p_fd, c_fd);
        } else {
            uint8_t no_scm = 0;
            write_full(fd, &no_scm, sizeof(no_scm));
        }
        if (p_fd >= 0) close(p_fd);
        if (c_fd >= 0) close(c_fd);
    } else {
        uint8_t no_scm = 0;
        write_full(fd, &no_scm, sizeof(no_scm));
    }

    std::string model = entries.count("BUILD:MODEL") ? entries.at("BUILD:MODEL") : "Custom";
    std::string gpu   = entries.count("GLES:RENDERER") ? entries.at("GLES:RENDERER") : "DefaultGPU";
    int ram_gb        = entries.count("VFS:RAM_GB") ? std::atoi(entries.at("VFS:RAM_GB").c_str()) : 16;
    record_launch_telemetry(pkg_name, app_pid, model, gpu, ram_gb);
}

REGISTER_ZYGISK_MODULE(MirageForgeModule)
REGISTER_ZYGISK_COMPANION(companion_handler)

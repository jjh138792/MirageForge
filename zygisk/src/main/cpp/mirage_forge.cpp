#include <cstdlib>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <string>
#include <sstream>
#include <fstream>
#include <dlfcn.h>
#include <sys/stat.h>

#include "zygisk.hpp"
#include "mirage_logger.hpp"
#include "mirage_shared.hpp"
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
        this->api = api;
        this->env = env;
        g_zygisk_api = api;

        orig_dlopen = reinterpret_cast<decltype(orig_dlopen)>(dlsym(RTLD_DEFAULT, "dlopen"));
        orig_android_dlopen_ext = reinterpret_cast<decltype(orig_android_dlopen_ext)>(dlsym(RTLD_DEFAULT, "android_dlopen_ext"));
        orig_dlsym = reinterpret_cast<decltype(orig_dlsym)>(dlsym(RTLD_DEFAULT, "dlsym"));

        orig_system_property_find = reinterpret_cast<decltype(orig_system_property_find)>(dlsym(RTLD_DEFAULT, "__system_property_find"));
        orig_system_property_read_callback = reinterpret_cast<decltype(orig_system_property_read_callback)>(dlsym(RTLD_DEFAULT, "__system_property_read_callback"));
        orig_system_property_read = reinterpret_cast<decltype(orig_system_property_read)>(dlsym(RTLD_DEFAULT, "__system_property_read"));
        orig_system_property_get = reinterpret_cast<decltype(orig_system_property_get)>(dlsym(RTLD_DEFAULT, "__system_property_get"));
        orig_system_property_foreach = reinterpret_cast<decltype(orig_system_property_foreach)>(dlsym(RTLD_DEFAULT, "__system_property_foreach"));
        orig_system_property_find_nth = reinterpret_cast<decltype(orig_system_property_find_nth)>(dlsym(RTLD_DEFAULT, "__system_property_find_nth"));

        orig_openat = reinterpret_cast<decltype(orig_openat)>(dlsym(RTLD_DEFAULT, "openat"));
        orig_openat_2 = reinterpret_cast<decltype(orig_openat_2)>(dlsym(RTLD_DEFAULT, "__openat_2"));
        orig_open = reinterpret_cast<decltype(orig_open)>(dlsym(RTLD_DEFAULT, "open"));
        orig_fopen = reinterpret_cast<decltype(orig_fopen)>(dlsym(RTLD_DEFAULT, "fopen"));
        orig_fopen64 = reinterpret_cast<decltype(orig_fopen64)>(dlsym(RTLD_DEFAULT, "fopen64"));
        orig_close = reinterpret_cast<decltype(orig_close)>(dlsym(RTLD_DEFAULT, "close"));
        orig_readlink = reinterpret_cast<decltype(orig_readlink)>(dlsym(RTLD_DEFAULT, "readlink"));
        orig_readlinkat = reinterpret_cast<decltype(orig_readlinkat)>(dlsym(RTLD_DEFAULT, "readlinkat"));
        orig_fstat = reinterpret_cast<decltype(orig_fstat)>(dlsym(RTLD_DEFAULT, "fstat"));
        orig_popen = reinterpret_cast<decltype(orig_popen)>(dlsym(RTLD_DEFAULT, "popen"));
        orig_uname = reinterpret_cast<decltype(orig_uname)>(dlsym(RTLD_DEFAULT, "uname"));
        orig_sysinfo = reinterpret_cast<decltype(orig_sysinfo)>(dlsym(RTLD_DEFAULT, "sysinfo"));
        orig_sysconf = reinterpret_cast<decltype(orig_sysconf)>(dlsym(RTLD_DEFAULT, "sysconf"));

        orig_glGetString = reinterpret_cast<decltype(orig_glGetString)>(dlsym(RTLD_DEFAULT, "glGetString"));
        orig_eglGetProcAddress = reinterpret_cast<decltype(orig_eglGetProcAddress)>(dlsym(RTLD_DEFAULT, "eglGetProcAddress"));
        orig_vkGetInstanceProcAddr = reinterpret_cast<decltype(orig_vkGetInstanceProcAddr)>(dlsym(RTLD_DEFAULT, "vkGetInstanceProcAddr"));
        orig_AMediaDrm_getPropertyString = reinterpret_cast<decltype(orig_AMediaDrm_getPropertyString)>(dlsym(RTLD_DEFAULT, "AMediaDrm_getPropertyString"));
    }

    void preAppSpecialize(AppSpecializeArgs *args) override {
        if (!args || !args->nice_name) {
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
            if (orig_close) orig_close(fd); else close(fd);
            api->setOption(zygisk::Option::DLCLOSE_MODULE_LIBRARY);
            return;
        }

        g_ctx.pkg_name = pkg_name;
        uint32_t entry_count = 0;
        read_full(fd, &entry_count, sizeof(entry_count));

        for (uint32_t i = 0; i < entry_count; ++i) {
            std::string k = read_str(fd);
            std::string v = read_str(fd);
            if (k.empty() || v.empty()) continue;
            parse_config_kv(k, v);
        }
        if (orig_close) orig_close(fd); else close(fd);

        // 若未强制要求篡改 SDK 版本，则移除 ro.build.version.sdk 覆盖，防止应用调错高版本 API 崩溃
        if (!g_ctx.force_spoof_sdk) {
            g_ctx.prop_map.erase("ro.build.version.sdk");
            g_ctx.prop_map.erase("ro.product.build.version.sdk");
            g_ctx.prop_map.erase("ro.system.build.version.sdk");
        }

        // 1. 预热 AOSP 1:1 内存布局的 Bionic prop_info 句柄池 (含 LPDDR 物理内存对称属性)
        prewarm_bionic_prop_arena();

        // 2. 预合成全套 VFS 硬件拓扑节点 (含 Carveout 折算后的 /proc/meminfo 与 /proc/ddr_info)
        build_vfs_topology();

        // 3. 若开启了幻境探针，同时挂载崩溃信号诊断器
        if (g_ctx.enable_probe) {
            install_crash_signal_logger(pkg_name);
        }

        g_ctx.active = true;
        LOGI("MirageForge v%s Active -> %s [Props:%zu, VFS:%zu, RAM:%dGB(%s), GPU:%s, Probe:%d]",
             MIRAGE_ENGINE_VERSION, pkg_name.c_str(), g_ctx.prop_map.size(),
             g_ctx.vfs_files.size(), g_ctx.ram_gb, g_ctx.ddr_type.c_str(),
             g_ctx.gl_renderer.c_str(), g_ctx.enable_probe);

        if (g_ctx.enable_build_props) {
            reflect_java_build_fields(env);
        }
        scan_and_commit_plt_hooks();
    }

    void postAppSpecialize(const AppSpecializeArgs *) override {
        if (!g_ctx.active) return;
        if (g_ctx.enable_build_props) {
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

    void parse_config_kv(const std::string &k, const std::string &v) {
        if (k == "META:ENABLE_BUILD") {
            g_ctx.enable_build_props = (v == "1");
        } else if (k == "META:ENABLE_GL") {
            g_ctx.enable_gl_vulkan = (v == "1");
        } else if (k == "META:ENABLE_VFS") {
            g_ctx.enable_vfs_shield = (v == "1");
            g_ctx.enable_maps_cloak = (v == "1");
        } else if (k == "META:ENABLE_PROBE") {
            g_ctx.enable_probe = (v == "1");
        } else if (k == "META:FORCE_SPOOF_SDK") {
            g_ctx.force_spoof_sdk = (v == "1");
        } else if (k == "GLES:VENDOR") {
            g_ctx.gl_vendor = v;
            if (v.find("ARM") != std::string::npos || v.find("Mali") != std::string::npos) {
                g_ctx.vulkan_vendor_id = 0x13B5;
                g_ctx.cpu_midr_el1 = "0x00000000410fd851";
            } else {
                g_ctx.vulkan_vendor_id = 0x5143;
                g_ctx.cpu_midr_el1 = "0x00000000511f8000";
            }
        } else if (k == "GLES:RENDERER") {
            g_ctx.gl_renderer = v;
        } else if (k == "GLES:VERSION") {
            g_ctx.gl_version = v;
        } else if (k == "VFS:CPU_HARDWARE") {
            g_ctx.cpu_hardware = v;
        } else if (k == "VFS:CPU_ARCH") {
            g_ctx.cpu_arch = v;
        } else if (k == "VFS:CPU_CORES") {
            g_ctx.cpu_cores = std::atoi(v.c_str());
        } else if (k == "VFS:CPU_MAX_FREQ") {
            g_ctx.cpu_max_freq_khz = std::atoi(v.c_str());
        } else if (k == "VFS:RAM_GB") {
            g_ctx.ram_gb = std::atoi(v.c_str());
        } else if (k == "VFS:DDR_TYPE") {
            g_ctx.ddr_type = v;
        } else if (k == "VFS:DDR_VENDOR") {
            g_ctx.ddr_vendor = v;
        } else if (k == "VFS:SOC_ID") {
            g_ctx.soc_id = v;
        } else if (k == "VFS:SOC_MACHINE") {
            g_ctx.soc_machine = v;
        } else if (k == "VFS:KERNEL_RELEASE") {
            g_ctx.kernel_release = v;
        } else if (k == "VFS:KERNEL_VERSION") {
            g_ctx.kernel_version = v;
        } else if (k.rfind("BUILD:", 0) == 0) {
            std::string b_field = k.substr(6);
            g_ctx.build_map[b_field] = v;
            if (b_field == "DEVICE") {
                g_ctx.build_map["SKU"] = v;
                g_ctx.build_map["ODM_SKU"] = v;
            }
        } else if (k.rfind("VERSION:", 0) == 0) {
            g_ctx.version_map[k.substr(8)] = v;
        } else {
            g_ctx.prop_map[k] = v;
        }
    }
};

// ============================================================================
// Root Companion 守护进程：极速直读 + CLI 自愈同步
// ============================================================================
static bool is_valid_pkg_name(const std::string &s) {
    if (s.empty() || s.size() > 128) return false;
    for (char c : s) {
        if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
              (c >= '0' && c <= '9') || c == '_' || c == '.')) {
            return false;
        }
    }
    return s.find("..") == std::string::npos;
}

static std::string fetch_profile_fast(const std::string &pkg_name) {
    std::string conf_path = std::string(APPS_CONF_DIR) + "/" + pkg_name + ".conf";

    // 1. 优先极速直读 mirageforge_cli 预编译的 .conf 文件 (< 0.1ms，零 fork 开销)
    struct stat db_st{}, conf_st{};
    bool conf_exists = (stat(conf_path.c_str(), &conf_st) == 0);
    bool db_exists   = (stat(ROUTES_DB_PATH, &db_st) == 0);

    // 仅当 routes.db 比 .conf 更新时，才调用一次 mirageforge_cli sync 重新生成缓存
    if (db_exists && (!conf_exists || db_st.st_mtime > conf_st.st_mtime)) {
        static time_t last_synced_mtime = 0;
        if (db_st.st_mtime > last_synced_mtime && access(CLI_BIN_PATH, X_OK) == 0) {
            last_synced_mtime = db_st.st_mtime;
            std::string cmd = std::string(CLI_BIN_PATH) + " sync >/dev/null 2>&1";
            std::system(cmd.c_str());
        }
    }

    std::ifstream ifs(conf_path);
    if (!ifs.is_open()) return "";
    std::ostringstream ss;
    ss << ifs.rdbuf();
    return ss.str();
}

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

    if (!is_valid_pkg_name(pkg_name)) {
        uint8_t zero = 0;
        write_full(fd, &zero, sizeof(zero));
        return;
    }

    std::string raw_conf = fetch_profile_fast(pkg_name);
    if (raw_conf.empty()) {
        uint8_t zero = 0;
        write_full(fd, &zero, sizeof(zero));
        return;
    }

    std::unordered_map<std::string, std::string> entries;
    std::istringstream iss(raw_conf);
    std::string line;
    while (std::getline(iss, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (line.empty() || line[0] == '#') continue;
        size_t eq = line.find('=');
        if (eq == std::string::npos) continue;
        std::string k = line.substr(0, eq);
        std::string v = line.substr(eq + 1);
        if (!k.empty() && !v.empty()) {
            entries[k] = v;
        }
    }

    if (entries.empty()) {
        uint8_t zero = 0;
        write_full(fd, &zero, sizeof(zero));
        return;
    }

    uint8_t one = 1;
    write_full(fd, &one, sizeof(one));
    uint32_t count = static_cast<uint32_t>(entries.size());
    write_full(fd, &count, sizeof(count));
    for (const auto &kv : entries) {
        write_str(fd, kv.first);
        write_str(fd, kv.second);
    }

    std::string model = entries.count("BUILD:MODEL") ? entries["BUILD:MODEL"] : "Custom";
    std::string gpu   = entries.count("GLES:RENDERER") ? entries["GLES:RENDERER"] : "DefaultGPU";
    int ram_gb        = entries.count("VFS:RAM_GB") ? std::atoi(entries["VFS:RAM_GB"].c_str()) : 16;
    record_launch_telemetry(pkg_name, app_pid, model, gpu, ram_gb);
}

REGISTER_ZYGISK_MODULE(MirageForgeModule)
REGISTER_ZYGISK_COMPANION(companion_handler)

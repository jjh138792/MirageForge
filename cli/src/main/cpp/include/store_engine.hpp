#pragma once

#include <dirent.h>
#include "cli_common.hpp"

namespace mirage::cli {

class StoreEngine {
public:
    bool init_storage(bool seed_defaults = false) {
        ensure_dir(DATA_ROOT_DIR, 0700);
        ensure_dir(TPL_DIR, 0700);
        ensure_dir(APPS_DIR, 0700);

        load_all();
        if (seed_defaults && templates_.empty()) {
            seed_flagship_presets();
            save_all_and_compile();
        }
        return true;
    }

    void load_all() {
        templates_.clear();
        routes_.clear();

        // 1. 加载 /data/adb/mirage_forge/templates/*.tpl
        DIR *dir = opendir(TPL_DIR);
        if (dir) {
            struct dirent *ent = nullptr;
            while ((ent = readdir(dir)) != nullptr) {
                std::string fname(ent->d_name);
                if (fname.size() > 4 && fname.substr(fname.size() - 4) == ".tpl") {
                    std::string content = read_file_to_string(std::string(TPL_DIR) + "/" + fname);
                    DeviceTemplate t = deserialize_tpl_file(content);
                    if (!t.id.empty()) {
                        templates_[t.id] = t;
                    }
                }
            }
            closedir(dir);
        }

        // 2. 加载 /data/adb/mirage_forge/routes.db
        std::string routes_raw = read_file_to_string(ROUTES_DB);
        for (const std::string &line : split_lines(routes_raw)) {
            if (line.empty() || line[0] == '#') continue;
            auto parts = split_char(line, '|');
            if (parts.size() >= 6 && is_safe_token(parts[0]) && !parts[1].empty()) {
                AppRoute r;
                r.pkg_name     = parts[0];
                r.tpl_id       = parts[1];
                r.enable_build = (parts[2] == "1");
                r.enable_gl    = (parts[3] == "1");
                r.enable_vfs   = (parts[4] == "1");
                r.enable_probe = (parts[5] == "1");
                routes_[r.pkg_name] = r;
            }
        }
    }

    bool save_all_and_compile() {
        ensure_dir(DATA_ROOT_DIR, 0700);
        ensure_dir(TPL_DIR, 0700);
        ensure_dir(APPS_DIR, 0700);

        // 1. 保存所有模板文件
        for (const auto &kv : templates_) {
            std::string path = std::string(TPL_DIR) + "/" + kv.first + ".tpl";
            write_file_atomic(path, serialize_tpl_file(kv.second), 0600);
        }

        // 2. 保存 routes.db
        std::ostringstream r_ss;
        r_ss << "# MirageForge v1.0.0 Routes Table\n";
        for (const auto &kv : routes_) {
            const AppRoute &r = kv.second;
            if (templates_.find(r.tpl_id) == templates_.end()) continue;
            r_ss << r.pkg_name << "|" << r.tpl_id << "|"
                 << (r.enable_build ? "1" : "0") << "|"
                 << (r.enable_gl ? "1" : "0") << "|"
                 << (r.enable_vfs ? "1" : "0") << "|"
                 << (r.enable_probe ? "1" : "0") << "\n";
        }
        write_file_atomic(ROUTES_DB, r_ss.str(), 0600);

        // 3. 清理旧的 apps/*.conf 并重新原子编译所有有效分流
        std::unordered_map<std::string, bool> valid_confs;
        for (const auto &kv : routes_) {
            const AppRoute &r = kv.second;
            auto it = templates_.find(r.tpl_id);
            if (it == templates_.end() || !is_safe_token(r.pkg_name)) continue;

            std::string conf_name = r.pkg_name + ".conf";
            valid_confs[conf_name] = true;
            std::string conf_path = std::string(APPS_DIR) + "/" + conf_name;
            write_file_atomic(conf_path, compile_runtime_conf(it->second, r), 0600);
        }

        DIR *adir = opendir(APPS_DIR);
        if (adir) {
            struct dirent *ent = nullptr;
            while ((ent = readdir(adir)) != nullptr) {
                std::string fn(ent->d_name);
                if (fn.size() > 5 && fn.substr(fn.size() - 5) == ".conf") {
                    if (valid_confs.find(fn) == valid_confs.end()) {
                        unlink((std::string(APPS_DIR) + "/" + fn).c_str());
                    }
                }
            }
            closedir(adir);
        }

        // 4. 同步导出标准 config.json 供外部备份与兼容读取
        write_file_atomic(CONFIG_JSON, export_full_json(), 0600);
        return true;
    }

    bool upsert_template(const DeviceTemplate &tpl) {
        if (!is_safe_token(tpl.id) || tpl.model.empty()) return false;
        templates_[tpl.id] = tpl;
        return save_all_and_compile();
    }

    bool delete_template(const std::string &id) {
        if (!is_safe_token(id)) return false;
        templates_.erase(id);
        unlink((std::string(TPL_DIR) + "/" + id + ".tpl").c_str());
        for (auto it = routes_.begin(); it != routes_.end();) {
            if (it->second.tpl_id == id) it = routes_.erase(it);
            else ++it;
        }
        return save_all_and_compile();
    }

    bool set_route(const AppRoute &route, bool force_stop = false) {
        if (!is_safe_token(route.pkg_name) || templates_.find(route.tpl_id) == templates_.end()) {
            return false;
        }
        routes_[route.pkg_name] = route;
        bool ok = save_all_and_compile();
        if (force_stop) stop_package(route.pkg_name);
        return ok;
    }

    bool remove_route(const std::string &pkg, bool force_stop = false) {
        if (!is_safe_token(pkg)) return false;
        routes_.erase(pkg);
        unlink((std::string(APPS_DIR) + "/" + pkg + ".conf").c_str());
        bool ok = save_all_and_compile();
        if (force_stop) stop_package(pkg);
        return ok;
    }

    static void stop_package(const std::string &pkg) {
        if (!is_safe_token(pkg)) return;
        std::string cmd = "am force-stop " + pkg + " >/dev/null 2>&1";
        std::system(cmd.c_str());
    }

    // 编译供 Zygisk Native 引擎极速读取的 .conf 内容
    static std::string compile_runtime_conf(const DeviceTemplate &tpl, const AppRoute &route) {
        std::string abis = tpl.supported_abis.empty() ? "arm64-v8a,armeabi-v7a,armeabi" : tpl.supported_abis;
        long long build_utc = std::atoll(tpl.build_time.c_str()) / 1000LL;
        if (build_utc <= 0) build_utc = 1736899200LL;

        std::vector<std::pair<std::string, std::string>> entries = {
            {"META:ENABLE_BUILD", route.enable_build ? "1" : "0"},
            {"META:ENABLE_GL",    route.enable_gl ? "1" : "0"},
            {"META:ENABLE_VFS",   route.enable_vfs ? "1" : "0"},
            {"META:ENABLE_PROBE", route.enable_probe ? "1" : "0"},
            // OpenGL ES & Vulkan
            {"GLES:VENDOR",       tpl.gl_vendor},
            {"GLES:RENDERER",     tpl.gl_renderer},
            {"GLES:VERSION",      tpl.gl_version},
            // VFS & Syscall
            {"VFS:CPU_HARDWARE",  tpl.cpu_hardware},
            {"VFS:CPU_ARCH",      "aarch64"},
            {"VFS:CPU_CORES",     std::to_string(tpl.cpu_cores)},
            {"VFS:CPU_MAX_FREQ",  std::to_string(tpl.cpu_max_freq)},
            {"VFS:RAM_GB",        std::to_string(tpl.ram_gb)},
            {"VFS:SOC_ID",        tpl.soc_id},
            {"VFS:SOC_MACHINE",   tpl.soc_machine},
            {"VFS:KERNEL_RELEASE",tpl.kernel_release},
            {"VFS:KERNEL_VERSION",tpl.kernel_version},
            // Java Build 反射
            {"BUILD:MODEL",               tpl.model},
            {"BUILD:BRAND",               tpl.brand},
            {"BUILD:MANUFACTURER",        tpl.manufacturer},
            {"BUILD:DEVICE",              tpl.device},
            {"BUILD:PRODUCT",             tpl.product},
            {"BUILD:HARDWARE",            tpl.hardware},
            {"BUILD:BOARD",               tpl.board},
            {"BUILD:BOOTLOADER",          tpl.bootloader},
            {"BUILD:SOC_MANUFACTURER",    tpl.soc_manufacturer},
            {"BUILD:SOC_MODEL",           tpl.soc_model},
            {"BUILD:ID",                  tpl.build_id},
            {"BUILD:FINGERPRINT",         tpl.fingerprint},
            {"BUILD:SUPPORTED_ABIS",      abis},
            {"BUILD:SUPPORTED_64_BIT_ABIS","arm64-v8a"},
            {"BUILD:SUPPORTED_32_BIT_ABIS","armeabi-v7a,armeabi"},
            {"BUILD:TAGS",                "release-keys"},
            {"BUILD:TYPE",                "user"},
            {"BUILD:TIME",                tpl.build_time},
            // Java Build$VERSION 反射
            {"VERSION:RELEASE",                tpl.release},
            {"VERSION:SDK_INT",                tpl.sdk_int},
            {"VERSION:DEVICE_INITIAL_SDK_INT", tpl.sdk_int},
            {"VERSION:PREVIEW_SDK_INT",        "0"},
            {"VERSION:SECURITY_PATCH",         tpl.security_patch},
            // Bionic SystemProperties 全分区覆盖
            {"ro.product.model",               tpl.model},
            {"ro.product.system.model",        tpl.model},
            {"ro.product.vendor.model",        tpl.model},
            {"ro.product.odm.model",           tpl.model},
            {"ro.product.product.model",       tpl.model},
            {"ro.product.brand",               tpl.brand},
            {"ro.product.system.brand",        tpl.brand},
            {"ro.product.vendor.brand",        tpl.brand},
            {"ro.product.odm.brand",           tpl.brand},
            {"ro.product.manufacturer",        tpl.manufacturer},
            {"ro.product.system.manufacturer", tpl.manufacturer},
            {"ro.product.vendor.manufacturer", tpl.manufacturer},
            {"ro.product.odm.manufacturer",    tpl.manufacturer},
            {"ro.product.device",              tpl.device},
            {"ro.product.system.device",       tpl.device},
            {"ro.product.vendor.device",       tpl.device},
            {"ro.product.odm.device",          tpl.device},
            {"ro.build.product",               tpl.device},
            {"ro.product.name",                tpl.product},
            {"ro.product.system.name",         tpl.product},
            {"ro.product.vendor.name",         tpl.product},
            {"ro.product.board",               tpl.board},
            {"ro.board.platform",              tpl.board},
            {"ro.hardware",                    tpl.hardware},
            {"ro.boot.hardware",               tpl.hardware},
            {"ro.soc.manufacturer",            tpl.soc_manufacturer},
            {"ro.soc.model",                   tpl.soc_model},
            {"ro.product.cpu.abi",             "arm64-v8a"},
            {"ro.product.cpu.abilist",         abis},
            {"ro.product.cpu.abilist64",       "arm64-v8a"},
            {"ro.product.cpu.abilist32",       "armeabi-v7a,armeabi"},
            {"ro.build.id",                    tpl.build_id},
            {"ro.build.date.utc",              std::to_string(build_utc)},
            {"ro.build.version.release",       tpl.release},
            {"ro.build.version.sdk",           tpl.sdk_int},
            {"ro.build.version.security_patch",tpl.security_patch},
            {"ro.build.fingerprint",           tpl.fingerprint},
            {"ro.product.build.fingerprint",   tpl.fingerprint},
            {"ro.system.build.fingerprint",    tpl.fingerprint},
            {"ro.vendor.build.fingerprint",    tpl.fingerprint},
            {"ro.odm.build.fingerprint",       tpl.fingerprint},
            {"ro.build.tags",                  "release-keys"},
            {"ro.build.type",                  "user"},
            {"ro.boot.vbmeta.device_state",    "locked"},
            {"ro.boot.verifiedbootstate",      "green"},
            {"ro.boot.flash.locked",           "1"},
            {"ro.boot.veritymode",             "enforcing"},
            {"ro.debuggable",                  "0"},
            {"ro.secure",                      "1"}
        };

        std::ostringstream ss;
        for (const auto &kv : entries) {
            if (!kv.first.empty() && !kv.second.empty()) {
                ss << kv.first << "=" << kv.second << "\n";
            }
        }

        for (const std::string &raw : split_lines(tpl.custom_props)) {
            std::string line = trim(raw);
            if (!line.empty() && line[0] != '#' && line.find('=') != std::string::npos) {
                ss << line << "\n";
            }
        }
        return ss.str();
    }

    // 导出完整状态 JSON 供 WebUI 首屏加载或备份
    std::string export_full_json() const {
        std::ostringstream ss;
        ss << "{"
           << "\"version\":\"" << MIRAGE_VERSION << "\","
           << "\"versionCode\":" << MIRAGE_VERSION_CODE << ","
           << "\"activeCount\":" << routes_.size() << ","
           << "\"templateCount\":" << templates_.size() << ","
           << "\"templates\":{";

        bool first_t = true;
        for (const auto &kv : templates_) {
            if (!first_t) ss << ",";
            first_t = false;
            ss << "\"" << json_escape(kv.first) << "\":" << template_to_json(kv.second);
        }
        ss << "},\"appRoutes\":{";

        bool first_r = true;
        for (const auto &kv : routes_) {
            const AppRoute &r = kv.second;
            if (!first_r) ss << ",";
            first_r = false;
            ss << "\"" << json_escape(r.pkg_name) << "\":{"
               << "\"tplId\":\"" << json_escape(r.tpl_id) << "\","
               << "\"build\":" << (r.enable_build ? "true" : "false") << ","
               << "\"gl\":" << (r.enable_gl ? "true" : "false") << ","
               << "\"vfs\":" << (r.enable_vfs ? "true" : "false") << ","
               << "\"probe\":" << (r.enable_probe ? "true" : "false")
               << "}";
        }
        ss << "}}";
        return ss.str();
    }

    const std::map<std::string, DeviceTemplate> &templates() const { return templates_; }
    std::map<std::string, DeviceTemplate> &templates_mut() { return templates_; }
    const std::map<std::string, AppRoute> &routes() const { return routes_; }

private:
    std::map<std::string, DeviceTemplate> templates_;
    std::map<std::string, AppRoute> routes_;

    static std::string serialize_tpl_file(const DeviceTemplate &t) {
        std::ostringstream ss;
        ss << "id=" << t.id << "\n"
           << "name=" << t.name << "\n"
           << "MODEL=" << t.model << "\n"
           << "BRAND=" << t.brand << "\n"
           << "MANUFACTURER=" << t.manufacturer << "\n"
           << "DEVICE=" << t.device << "\n"
           << "PRODUCT=" << t.product << "\n"
           << "BOARD=" << t.board << "\n"
           << "HARDWARE=" << t.hardware << "\n"
           << "BOOTLOADER=" << t.bootloader << "\n"
           << "SOC_MANUFACTURER=" << t.soc_manufacturer << "\n"
           << "SOC_MODEL=" << t.soc_model << "\n"
           << "SUPPORTED_ABIS=" << t.supported_abis << "\n"
           << "BUILD_ID=" << t.build_id << "\n"
           << "BUILD_TIME=" << t.build_time << "\n"
           << "RELEASE=" << t.release << "\n"
           << "SDK_INT=" << t.sdk_int << "\n"
           << "SECURITY_PATCH=" << t.security_patch << "\n"
           << "FINGERPRINT=" << t.fingerprint << "\n"
           << "GL_VENDOR=" << t.gl_vendor << "\n"
           << "GL_RENDERER=" << t.gl_renderer << "\n"
           << "GL_VERSION=" << t.gl_version << "\n"
           << "CPU_HARDWARE=" << t.cpu_hardware << "\n"
           << "CPU_CORES=" << t.cpu_cores << "\n"
           << "CPU_MAX_FREQ=" << t.cpu_max_freq << "\n"
           << "RAM_GB=" << t.ram_gb << "\n"
           << "SOC_ID=" << t.soc_id << "\n"
           << "SOC_MACHINE=" << t.soc_machine << "\n"
           << "KERNEL_RELEASE=" << t.kernel_release << "\n"
           << "KERNEL_VERSION=" << t.kernel_version << "\n";

        for (const std::string &line : split_lines(t.custom_props)) {
            std::string tr = trim(line);
            if (!tr.empty() && tr[0] != '#' && tr.find('=') != std::string::npos) {
                ss << "CUSTOM_PROP:" << tr << "\n";
            }
        }
        return ss.str();
    }

    static DeviceTemplate deserialize_tpl_file(const std::string &raw) {
        std::unordered_map<std::string, std::string> kv;
        std::ostringstream custom_ss;
        bool first_custom = true;

        for (const std::string &line : split_lines(raw)) {
            if (line.empty() || line[0] == '#') continue;
            if (line.rfind("CUSTOM_PROP:", 0) == 0) {
                if (!first_custom) custom_ss << "\n";
                first_custom = false;
                custom_ss << line.substr(12);
                continue;
            }
            size_t eq = line.find('=');
            if (eq == std::string::npos) continue;
            kv[line.substr(0, eq)] = line.substr(eq + 1);
        }
        kv["CUSTOM_PROPS"] = custom_ss.str();
        return template_from_kv_map(kv);
    }

    void seed_flagship_presets() {
        {
            DeviceTemplate t;
            t.id = "xiaomi_15_pro";
            t.name = "Xiaomi 15 Pro (骁龙8至尊版 / Adreno 830)";
            t.model = "2410DPN6CC";
            t.brand = "Xiaomi";
            t.manufacturer = "Xiaomi";
            t.device = "haotian";
            t.product = "haotian";
            t.board = "sun";
            t.soc_manufacturer = "Qualcomm";
            t.soc_model = "SM8750-AB";
            t.fingerprint = "Xiaomi/haotian/haotian:15/AP3A.240905.015.A2/OS2.0.102.0.VOBCNXM:user/release-keys";
            t.gl_vendor = "Qualcomm";
            t.gl_renderer = "Adreno (TM) 830";
            t.cpu_hardware = "Qualcomm Technologies, Inc SM8750";
            t.cpu_cores = 8;
            t.cpu_max_freq = 4320000;
            t.ram_gb = 16;
            t.soc_id = "618";
            t.soc_machine = "SM8750";
            t.custom_props = "ro.miui.ui.version.code=15\nro.miui.ui.version.name=V816\nro.product.marketname=Xiaomi 15 Pro";
            templates_[t.id] = t;
        }
        {
            DeviceTemplate t;
            t.id = "rog_9_pro";
            t.name = "ROG 游戏手机 9 Pro (24GB / 165Hz 满血电竞)";
            t.model = "ASUS_AI2501_A";
            t.brand = "asus";
            t.manufacturer = "asus";
            t.device = "AI2501";
            t.product = "CN_AI2501";
            t.board = "sun";
            t.soc_manufacturer = "Qualcomm";
            t.soc_model = "SM8750-AB";
            t.fingerprint = "asus/CN_AI2501/AI2501:15/AQ3A.240812.002/35.1810.1810.88:user/release-keys";
            t.gl_vendor = "Qualcomm";
            t.gl_renderer = "Adreno (TM) 830";
            t.cpu_hardware = "Qualcomm Technologies, Inc SM8750";
            t.cpu_cores = 8;
            t.cpu_max_freq = 4320000;
            t.ram_gb = 24;
            t.soc_id = "618";
            t.soc_machine = "SM8750";
            t.custom_props = "ro.product.asus.series=ROG Phone 9\nro.vendor.asus.gaming=1";
            templates_[t.id] = t;
        }
        {
            DeviceTemplate t;
            t.id = "redmagic_10_pro";
            t.name = "红魔 10 Pro+ (24GB 氘锋透明版)";
            t.model = "NX789J";
            t.brand = "nubia";
            t.manufacturer = "ZTE";
            t.device = "NX789J";
            t.product = "NX789J";
            t.board = "sun";
            t.soc_manufacturer = "Qualcomm";
            t.soc_model = "SM8750-AB";
            t.fingerprint = "nubia/NX789J/NX789J:15/AP3A.240905.015.A2/20250110.213512:user/release-keys";
            t.gl_vendor = "Qualcomm";
            t.gl_renderer = "Adreno (TM) 830";
            t.cpu_hardware = "Qualcomm Technologies, Inc SM8750";
            t.cpu_cores = 8;
            t.cpu_max_freq = 4320000;
            t.ram_gb = 24;
            t.soc_id = "618";
            t.soc_machine = "SM8750";
            t.custom_props = "ro.product.marketname=红魔 10 Pro+";
            templates_[t.id] = t;
        }
        {
            DeviceTemplate t;
            t.id = "vivo_x200_pro";
            t.name = "vivo X200 Pro (天玑9400 / Immortalis-G925)";
            t.model = "V2405A";
            t.brand = "vivo";
            t.manufacturer = "vivo";
            t.device = "PD2405";
            t.product = "PD2405";
            t.board = "mt6991";
            t.hardware = "mt6991";
            t.soc_manufacturer = "Mediatek";
            t.soc_model = "MT6991";
            t.fingerprint = "vivo/PD2405/PD2405:15/AP3A.240905.015.A2/compiler11201512:user/release-keys";
            t.gl_vendor = "ARM";
            t.gl_renderer = "Immortalis-G925 MC12";
            t.cpu_hardware = "MT6991";
            t.cpu_cores = 8;
            t.cpu_max_freq = 3620000;
            t.ram_gb = 16;
            t.soc_id = "6991";
            t.soc_machine = "MT6991";
            t.custom_props = "ro.vivo.os.build.display.id=OriginOS 5\nro.vivo.market.name=vivo X200 Pro";
            templates_[t.id] = t;
        }
    }
};

} // namespace mirage::cli

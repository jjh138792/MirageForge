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

        std::string routes_raw = read_file_to_string(ROUTES_DB);
        for (const std::string &line : split_lines(routes_raw)) {
            if (line.empty() || line[0] == '#') continue;
            auto parts = split_char(line, '|');
            if (parts.size() >= 6 && is_safe_token(parts[0]) && !parts[1].empty()) {
                AppRoute r;
                r.pkg_name        = parts[0];
                r.tpl_id          = parts[1];
                r.enable_build    = (parts[2] == "1");
                r.enable_gl       = (parts[3] == "1");
                r.enable_vfs      = (parts[4] == "1");
                r.enable_probe    = (parts[5] == "1");
                r.force_spoof_sdk = (parts.size() >= 7 && parts[6] == "1");
                routes_[r.pkg_name] = r;
            }
        }
    }

    bool save_all_and_compile() {
        ensure_dir(DATA_ROOT_DIR, 0700);
        ensure_dir(TPL_DIR, 0700);
        ensure_dir(APPS_DIR, 0700);

        for (const auto &kv : templates_) {
            std::string path = std::string(TPL_DIR) + "/" + kv.first + ".tpl";
            write_file_atomic(path, serialize_tpl_file(kv.second), 0600);
        }

        std::ostringstream r_ss;
        r_ss << "# MirageForge v" << MIRAGE_VERSION << " Routes Table\n";
        for (const auto &kv : routes_) {
            const AppRoute &r = kv.second;
            if (templates_.find(r.tpl_id) == templates_.end()) continue;
            r_ss << r.pkg_name << "|" << r.tpl_id << "|"
                 << (r.enable_build ? "1" : "0") << "|"
                 << (r.enable_gl ? "1" : "0") << "|"
                 << (r.enable_vfs ? "1" : "0") << "|"
                 << (r.enable_probe ? "1" : "0") << "|"
                 << (r.force_spoof_sdk ? "1" : "0") << "\n";
        }
        write_file_atomic(ROUTES_DB, r_ss.str(), 0600);

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

    static std::string compile_runtime_conf(const DeviceTemplate &tpl, const AppRoute &route) {
        std::string abis = tpl.supported_abis.empty() ? "arm64-v8a,armeabi-v7a,armeabi" : tpl.supported_abis;
        long long build_utc = std::atoll(tpl.build_time.c_str()) / 1000LL;
        if (build_utc <= 0) build_utc = 1736899200LL;

        int ram_gb = tpl.ram_gb > 0 ? tpl.ram_gb : 16;
        std::string ram_str = std::to_string(ram_gb) + "GB";
        std::string ram_mb_str = std::to_string(ram_gb * 1024);
        std::string ddr_type = tpl.ddr_type.empty() ? "LPDDR5X" : tpl.ddr_type;
        std::string ddr_vendor = tpl.ddr_vendor.empty() ? "SKhynix" : tpl.ddr_vendor;
        std::string ddr_full = ram_str + "," + ddr_vendor + "," + ddr_type;

        std::vector<std::pair<std::string, std::string>> entries = {
            {"META:ENABLE_BUILD",    route.enable_build ? "1" : "0"},
            {"META:ENABLE_GL",       route.enable_gl ? "1" : "0"},
            {"META:ENABLE_VFS",      route.enable_vfs ? "1" : "0"},
            {"META:ENABLE_PROBE",    route.enable_probe ? "1" : "0"},
            {"META:FORCE_SPOOF_SDK", route.force_spoof_sdk ? "1" : "0"},
            // OpenGL ES & Vulkan
            {"GLES:VENDOR",          tpl.gl_vendor},
            {"GLES:RENDERER",        tpl.gl_renderer},
            {"GLES:VERSION",         tpl.gl_version},
            // VFS & 异构多簇单核频率 & 物理内存对称
            {"VFS:CPU_HARDWARE",     tpl.cpu_hardware},
            {"VFS:CPU_ARCH",         "aarch64"},
            {"VFS:CPU_CORES",        std::to_string(tpl.cpu_cores)},
            {"VFS:CPU_MAX_FREQ",     std::to_string(tpl.cpu_max_freq)},
            {"VFS:CPU_CLUSTERS",     tpl.cpu_clusters},
            {"VFS:RAM_GB",           std::to_string(ram_gb)},
            {"VFS:DDR_TYPE",         ddr_type},
            {"VFS:DDR_VENDOR",       ddr_vendor},
            {"VFS:SOC_ID",           tpl.soc_id},
            {"VFS:SOC_MACHINE",      tpl.soc_machine},
            {"VFS:KERNEL_RELEASE",   tpl.kernel_release},
            {"VFS:KERNEL_VERSION",   tpl.kernel_version},
            // Java Build 反射
            {"BUILD:MODEL",                tpl.model},
            {"BUILD:BRAND",                tpl.brand},
            {"BUILD:MANUFACTURER",         tpl.manufacturer},
            {"BUILD:DEVICE",               tpl.device},
            {"BUILD:PRODUCT",              tpl.product},
            {"BUILD:HARDWARE",             tpl.hardware},
            {"BUILD:BOARD",                tpl.board},
            {"BUILD:BOOTLOADER",           tpl.bootloader},
            {"BUILD:SOC_MANUFACTURER",     tpl.soc_manufacturer},
            {"BUILD:SOC_MODEL",            tpl.soc_model},
            {"BUILD:ID",                   tpl.build_id},
            {"BUILD:FINGERPRINT",          tpl.fingerprint},
            {"BUILD:SUPPORTED_ABIS",       abis},
            {"BUILD:SUPPORTED_64_BIT_ABIS","arm64-v8a"},
            {"BUILD:SUPPORTED_32_BIT_ABIS","armeabi-v7a,armeabi"},
            {"BUILD:TAGS",                 "release-keys"},
            {"BUILD:TYPE",                 "user"},
            {"BUILD:TIME",                 tpl.build_time},
            // Java Build$VERSION 反射
            {"VERSION:RELEASE",                tpl.release},
            {"VERSION:SDK_INT",                tpl.sdk_int},
            {"VERSION:DEVICE_INITIAL_SDK_INT", tpl.sdk_int},
            {"VERSION:PREVIEW_SDK_INT",        "0"},
            {"VERSION:SECURITY_PATCH",         tpl.security_patch},
            // Bionic SystemProperties 全分区机型覆盖
            {"ro.product.model",                tpl.model},
            {"ro.product.system.model",         tpl.model},
            {"ro.product.vendor.model",         tpl.model},
            {"ro.product.odm.model",            tpl.model},
            {"ro.product.product.model",        tpl.model},
            {"ro.product.brand",                tpl.brand},
            {"ro.product.system.brand",         tpl.brand},
            {"ro.product.vendor.brand",         tpl.brand},
            {"ro.product.odm.brand",            tpl.brand},
            {"ro.product.manufacturer",         tpl.manufacturer},
            {"ro.product.system.manufacturer",  tpl.manufacturer},
            {"ro.product.vendor.manufacturer",  tpl.manufacturer},
            {"ro.product.odm.manufacturer",     tpl.manufacturer},
            {"ro.product.device",               tpl.device},
            {"ro.product.system.device",        tpl.device},
            {"ro.product.vendor.device",        tpl.device},
            {"ro.product.odm.device",           tpl.device},
            {"ro.build.product",                tpl.device},
            {"ro.product.name",                 tpl.product},
            {"ro.product.system.name",          tpl.product},
            {"ro.product.vendor.name",          tpl.product},
            {"ro.product.board",                tpl.board},
            {"ro.board.platform",               tpl.board},
            {"ro.hardware",                     tpl.hardware},
            {"ro.boot.hardware",                tpl.hardware},
            {"ro.soc.manufacturer",             tpl.soc_manufacturer},
            {"ro.soc.model",                    tpl.soc_model},
            {"ro.product.cpu.abi",              "arm64-v8a"},
            {"ro.product.cpu.abilist",          abis},
            {"ro.product.cpu.abilist64",        "arm64-v8a"},
            {"ro.product.cpu.abilist32",        "armeabi-v7a,armeabi"},
            {"ro.build.id",                     tpl.build_id},
            {"ro.build.date.utc",               std::to_string(build_utc)},
            {"ro.build.version.security_patch", tpl.security_patch},
            {"ro.build.fingerprint",            tpl.fingerprint},
            {"ro.product.build.fingerprint",    tpl.fingerprint},
            {"ro.system.build.fingerprint",     tpl.fingerprint},
            {"ro.vendor.build.fingerprint",     tpl.fingerprint},
            {"ro.odm.build.fingerprint",        tpl.fingerprint},
            {"ro.build.tags",                   "release-keys"},
            {"ro.build.type",                   "user"},
            {"ro.boot.vbmeta.device_state",     "locked"},
            {"ro.boot.verifiedbootstate",       "green"},
            {"ro.boot.flash.locked",            "1"},
            {"ro.boot.veritymode",              "enforcing"},
            {"ro.debuggable",                   "0"},
            {"ro.secure",                       "1"},
            // 物理安装内存与 LPDDR 规格对称属性池
            {"ro.boot.hardware.ddr",            ddr_full},
            {"ro.boot.ddr_size",                ram_str},
            {"ro.boot.ddr_type",                ddr_type},
            {"ro.boot.ddr_info",                ddr_full},
            {"ro.boot.ramsize",                 ram_str},
            {"ro.boot.memsize",                 ram_str},
            {"ro.hardware.ddr",                 ddr_full},
            {"ro.vendor.hw.ram",                ram_str},
            {"ro.product.ram",                  ram_str},
            {"ro.config.ram_size",              ram_mb_str},
            {"ro.sys.ram_size",                 ram_str},
            {"persist.sys.ram_size",            ram_str}
        };

        if (route.force_spoof_sdk) {
            entries.emplace_back("ro.build.version.release", tpl.release);
            entries.emplace_back("ro.build.version.sdk", tpl.sdk_int);
            entries.emplace_back("ro.product.build.version.sdk", tpl.sdk_int);
            entries.emplace_back("ro.system.build.version.sdk", tpl.sdk_int);
        }

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
               << "\"probe\":" << (r.enable_probe ? "true" : "false") << ","
               << "\"sdkSpoof\":" << (r.force_spoof_sdk ? "true" : "false")
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
           << "CPU_CLUSTERS=" << t.cpu_clusters << "\n"
           << "RAM_GB=" << t.ram_gb << "\n"
           << "DDR_TYPE=" << t.ddr_type << "\n"
           << "DDR_VENDOR=" << t.ddr_vendor << "\n"
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
            t.name = "Xiaomi 15 Pro (骁龙8至尊版 / 16GB LPDDR5X)";
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
            t.cpu_clusters = "0-5:614400:3532800;6-7:806400:4320000";
            t.ram_gb = 16;
            t.ddr_type = "LPDDR5X";
            t.ddr_vendor = "SKhynix";
            t.soc_id = "618";
            t.soc_machine = "SM8750";
            t.custom_props = "ro.miui.ui.version.code=15\nro.miui.ui.version.name=V816\nro.product.marketname=Xiaomi 15 Pro";
            templates_[t.id] = t;
        }
        {
            DeviceTemplate t;
            t.id = "rog_9_pro";
            t.name = "ROG 游戏手机 9 Pro (24GB LPDDR5X / 165Hz)";
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
            t.cpu_clusters = "0-5:614400:3532800;6-7:806400:4320000";
            t.ram_gb = 24;
            t.ddr_type = "LPDDR5X";
            t.ddr_vendor = "SKhynix";
            t.soc_id = "618";
            t.soc_machine = "SM8750";
            t.custom_props = "ro.product.asus.series=ROG Phone 9\nro.vendor.asus.gaming=1";
            templates_[t.id] = t;
        }
        {
            DeviceTemplate t;
            t.id = "redmagic_10_pro";
            t.name = "红魔 10 Pro+ (24GB LPDDR5T 氘锋版)";
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
            t.cpu_clusters = "0-5:614400:3532800;6-7:806400:4320000";
            t.ram_gb = 24;
            t.ddr_type = "LPDDR5T";
            t.ddr_vendor = "SKhynix";
            t.soc_id = "618";
            t.soc_machine = "SM8750";
            t.custom_props = "ro.product.marketname=红魔 10 Pro+";
            templates_[t.id] = t;
        }
        {
            DeviceTemplate t;
            t.id = "vivo_x200_pro";
            t.name = "vivo X200 Pro (天玑9400 / 16GB LPDDR5T)";
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
            t.cpu_clusters = "0-3:800000:2400000;4-6:1000000:3300000;7-7:1200000:3620000";
            t.ram_gb = 16;
            t.ddr_type = "LPDDR5T";
            t.ddr_vendor = "SKhynix";
            t.soc_id = "6991";
            t.soc_machine = "MT6991";
            t.custom_props = "ro.vivo.os.build.display.id=OriginOS 5\nro.vivo.market.name=vivo X200 Pro";
            templates_[t.id] = t;
        }
    }
};

} // namespace mirage::cli

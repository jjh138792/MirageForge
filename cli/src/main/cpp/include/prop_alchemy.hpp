#pragma once

#include <ctime>
#include <unordered_set>
#include "cli_common.hpp"

namespace mirage::cli {

class PropAlchemy {
public:
    struct InferredTopology {
        std::string gl_vendor;
        std::string gl_renderer;
        std::string cpu_hw;
        std::string soc_id;
        std::string soc_machine;
        int max_freq_khz;
        std::string ddr_type;
    };

    static InferredTopology infer_topology(const std::string &board, const std::string &soc_model) {
        std::string token = board + " " + soc_model;
        for (char &c : token) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));

        if (token.find("sun") != std::string::npos || token.find("sm8750") != std::string::npos) {
            return {"Qualcomm", "Adreno (TM) 830", "Qualcomm Technologies, Inc SM8750", "618", "SM8750", 4320000, "LPDDR5X"};
        }
        if (token.find("pineapple") != std::string::npos || token.find("sm8650") != std::string::npos) {
            return {"Qualcomm", "Adreno (TM) 750", "Qualcomm Technologies, Inc SM8650", "557", "SM8650", 3300000, "LPDDR5X"};
        }
        if (token.find("kalama") != std::string::npos || token.find("sm8550") != std::string::npos) {
            return {"Qualcomm", "Adreno (TM) 740", "Qualcomm Technologies, Inc SM8550", "519", "SM8550", 3190000, "LPDDR5X"};
        }
        if (token.find("taro") != std::string::npos || token.find("sm8475") != std::string::npos) {
            return {"Qualcomm", "Adreno (TM) 730", "Qualcomm Technologies, Inc SM8475", "530", "SM8475", 3190000, "LPDDR5"};
        }
        if (token.find("mt6991") != std::string::npos) {
            return {"ARM", "Immortalis-G925 MC12", "MT6991", "6991", "MT6991", 3620000, "LPDDR5X"};
        }
        if (token.find("mt6989") != std::string::npos) {
            return {"ARM", "Immortalis-G720 MC12", "MT6989", "6989", "MT6989", 3250000, "LPDDR5X"};
        }
        if (token.find("mt6985") != std::string::npos) {
            return {"ARM", "Immortalis-G715 MC11", "MT6985", "6985", "MT6985", 3050000, "LPDDR5X"};
        }
        std::string fallback_soc = !soc_model.empty() ? soc_model : (!board.empty() ? board : "SM8750");
        return {"Qualcomm", "Adreno (TM) 830", "Qualcomm Technologies, Inc " + fallback_soc, "618", fallback_soc, 4320000, "LPDDR5X"};
    }

    static bool parse_build_prop(const std::string &raw_text, DeviceTemplate *out_tpl, const std::string &custom_id = "") {
        if (!out_tpl) return false;

        std::unordered_map<std::string, std::string> prop_map;
        std::vector<std::string> ordered_keys;

        for (const std::string &raw_line : split_lines(raw_text)) {
            std::string line = trim(raw_line);
            if (line.empty() || line[0] == '#') continue;
            size_t eq = line.find('=');
            if (eq == std::string::npos) continue;
            std::string k = trim(line.substr(0, eq));
            std::string v = trim(line.substr(eq + 1));
            if (!k.empty() && !v.empty()) {
                if (prop_map.find(k) == prop_map.end()) ordered_keys.push_back(k);
                prop_map[k] = v;
            }
        }

        std::unordered_set<std::string> consumed;
        auto pick = [&](std::initializer_list<const char *> keys) -> std::string {
            for (const char *k : keys) {
                consumed.insert(k);
            }
            for (const char *k : keys) {
                auto it = prop_map.find(k);
                if (it != prop_map.end() && !it->second.empty()) {
                    return it->second;
                }
            }
            return "";
        };

        std::string model = pick({
            "ro.product.model", "ro.product.vendor.model",
            "ro.product.odm.model", "ro.product.system.model", "ro.product.product.model"
        });
        std::string brand = pick({
            "ro.product.brand", "ro.product.vendor.brand",
            "ro.product.odm.brand", "ro.product.system.brand"
        });
        std::string manufacturer = pick({
            "ro.product.manufacturer", "ro.product.vendor.manufacturer",
            "ro.product.odm.manufacturer", "ro.product.system.manufacturer"
        });
        std::string device = pick({
            "ro.product.device", "ro.product.vendor.device",
            "ro.product.odm.device", "ro.product.system.device", "ro.build.product"
        });
        std::string product = pick({
            "ro.product.name", "ro.product.vendor.name",
            "ro.product.odm.name", "ro.product.system.name"
        });
        std::string board = pick({"ro.board.platform", "ro.product.board"});
        std::string hardware = pick({"ro.hardware", "ro.boot.hardware"});
        std::string soc_man = pick({"ro.soc.manufacturer"});
        std::string soc_model = pick({"ro.soc.model", "ro.mediatek.platform", "ro.chipname"});
        std::string abis = pick({"ro.product.cpu.abilist", "ro.system.product.cpu.abilist"});
        std::string build_id = pick({"ro.build.id", "ro.product.build.id", "ro.system.build.id"});
        std::string utc_sec = pick({"ro.build.date.utc", "ro.system.build.date.utc"});
        std::string release = pick({"ro.build.version.release", "ro.system.build.version.release"});
        std::string sdk = pick({"ro.build.version.sdk", "ro.system.build.version.sdk"});
        std::string patch = pick({"ro.build.version.security_patch", "ro.vendor.build.security_patch"});
        std::string fingerprint = pick({
            "ro.build.fingerprint", "ro.product.build.fingerprint",
            "ro.vendor.build.fingerprint", "ro.system.build.fingerprint", "ro.odm.build.fingerprint"
        });

        // 提取可能存在的原厂 DDR 信息
        std::string raw_ddr = pick({"ro.boot.hardware.ddr", "ro.boot.ddr_info", "ro.hardware.ddr"});
        std::string raw_ddr_type = pick({"ro.boot.ddr_type"});
        std::string raw_ram_size = pick({"ro.boot.ddr_size", "ro.vendor.hw.ram", "ro.boot.ramsize"});

        if (model.empty() && brand.empty() && fingerprint.empty()) {
            return false;
        }

        InferredTopology topo = infer_topology(board, soc_model);

        int detected_ram_gb = 16;
        if (!raw_ram_size.empty()) {
            int parsed_ram = std::atoi(raw_ram_size.c_str());
            if (parsed_ram >= 4 && parsed_ram <= 32) detected_ram_gb = parsed_ram;
        }

        std::string detected_ddr_type = !raw_ddr_type.empty() ? raw_ddr_type : topo.ddr_type;
        if (raw_ddr.find("LPDDR5T") != std::string::npos) detected_ddr_type = "LPDDR5T";
        else if (raw_ddr.find("LPDDR5X") != std::string::npos) detected_ddr_type = "LPDDR5X";
        else if (raw_ddr.find("LPDDR5") != std::string::npos) detected_ddr_type = "LPDDR5";
        else if (raw_ddr.find("LPDDR4X") != std::string::npos) detected_ddr_type = "LPDDR4X";

        std::string detected_ddr_vendor = "SKhynix";
        if (raw_ddr.find("Samsung") != std::string::npos || raw_ddr.find("SAMSUNG") != std::string::npos) {
            detected_ddr_vendor = "Samsung";
        } else if (raw_ddr.find("Micron") != std::string::npos || raw_ddr.find("MICRON") != std::string::npos) {
            detected_ddr_vendor = "Micron";
        }

        std::string market_name;
        if (prop_map.count("ro.product.marketname")) market_name = prop_map["ro.product.marketname"];
        else if (prop_map.count("ro.vivo.market.name")) market_name = prop_map["ro.vivo.market.name"];
        else if (prop_map.count("ro.vendor.oplus.market.name")) market_name = prop_map["ro.vendor.oplus.market.name"];

        DeviceTemplate t;
        t.id = !custom_id.empty() ? custom_id : ("tpl_" + std::to_string(std::time(nullptr)));
        t.name = !market_name.empty()
                     ? (brand + " " + market_name)
                     : ((brand.empty() ? "" : brand + " ") + (!model.empty() ? model : device));
        t.model            = model;
        t.brand            = brand;
        t.manufacturer     = !manufacturer.empty() ? manufacturer : brand;
        t.device           = device;
        t.product          = !product.empty() ? product : device;
        t.board            = board;
        t.hardware         = !hardware.empty() ? hardware : "qcom";
        t.bootloader       = "unknown";
        t.soc_manufacturer = !soc_man.empty() ? soc_man : (topo.gl_vendor == "ARM" ? "Mediatek" : "Qualcomm");
        t.soc_model        = !soc_model.empty() ? soc_model : topo.soc_machine;
        t.supported_abis   = !abis.empty() ? abis : "arm64-v8a,armeabi-v7a,armeabi";
        t.build_id         = !build_id.empty() ? build_id : "AP3A.240905.015.A2";
        t.build_time       = !utc_sec.empty() ? std::to_string(std::atoll(utc_sec.c_str()) * 1000LL) : "1736899200000";
        t.release          = !release.empty() ? release : "15";
        t.sdk_int          = !sdk.empty() ? sdk : "35";
        t.security_patch   = !patch.empty() ? patch : "2025-02-01";
        t.fingerprint      = fingerprint;

        t.gl_vendor        = topo.gl_vendor;
        t.gl_renderer      = topo.gl_renderer;
        t.gl_version       = "OpenGL ES 3.2 V@0800.0";
        t.cpu_hardware     = topo.cpu_hw;
        t.cpu_cores        = 8;
        t.cpu_max_freq     = topo.max_freq_khz;
        t.ram_gb           = detected_ram_gb;
        t.ddr_type         = detected_ddr_type;
        t.ddr_vendor       = detected_ddr_vendor;
        t.soc_id           = topo.soc_id;
        t.soc_machine      = topo.soc_machine;
        t.kernel_release   = "6.6.30-android15-8-g94b8f12ab4c1";
        t.kernel_version   = "#1 SMP PREEMPT Fri Nov 15 12:00:00 UTC 2025";

        std::ostringstream extra_ss;
        int extra_count = 0;
        for (const std::string &k : ordered_keys) {
            if (consumed.count(k) > 0) continue;
            if (k.rfind("ro.", 0) == 0 || k.rfind("persist.", 0) == 0) {
                if (extra_count > 0) extra_ss << "\n";
                extra_ss << k << "=" << prop_map[k];
                if (++extra_count >= 100) break;
            }
        }
        t.custom_props = extra_ss.str();

        *out_tpl = t;
        return true;
    }
};

} // namespace mirage::cli

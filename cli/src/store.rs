// SPDX-License-Identifier: GPL-3.0-only
// Copyright (c) 2025-2026 jjh138792
// MirageForge: https://github.com/jjh138792/MirageForge

use crate::compiler::compile_modular_conf;
use crate::fs_atomic::{
    ensure_dir, ensure_empty_log_file, is_safe_token, read_file_str, write_file_atomic,
};
use crate::models::*;
use std::collections::{BTreeMap, HashSet};
use std::fs;
use std::process::Command;

pub struct StoreEngine {
    pub meta: ModuleMeta,
    pub templates: BTreeMap<String, DeviceTemplate>,
    pub routes: BTreeMap<String, AppRoute>,
    pub auto_spoof: AutoSpoofConfig,
}

impl StoreEngine {
    pub fn new() -> Self {
        Self {
            meta: read_module_meta(),
            templates: BTreeMap::new(),
            routes: BTreeMap::new(),
            auto_spoof: AutoSpoofConfig::default(),
        }
    }

    pub fn init_storage(&mut self, seed_defaults: bool) {
        ensure_dir(DATA_ROOT_DIR, 0o700);
        ensure_dir(TPL_DIR, 0o700);
        ensure_dir(APPS_DIR, 0o700);
        ensure_empty_log_file(PROBE_LOG_FILE, 0o600);
        ensure_empty_log_file(CRASH_LOG_FILE, 0o600);

        self.load_all();
        if seed_defaults && self.templates.is_empty() {
            self.seed_flagship_presets();
            self.save_all_and_compile();
        }
    }

    pub fn load_all(&mut self) {
        self.meta = read_module_meta();
        self.templates.clear();
        self.routes.clear();
        self.auto_spoof = Self::load_auto_spoof_file();

        if let Ok(entries) = fs::read_dir(TPL_DIR) {
            for entry in entries.flatten() {
                let path = entry.path();
                if path.extension().and_then(|s| s.to_str()) == Some("tpl") {
                    if let Ok(raw) = fs::read_to_string(&path) {
                        if let Some(mut tpl) = Self::deserialize_tpl(&raw) {
                            if is_safe_token(&tpl.id) {
                                tpl.normalize_vulkan_fields();
                                self.templates.insert(tpl.id.clone(), tpl);
                            }
                        }
                    }
                }
            }
        }

        if self.templates.is_empty() {
            let cfg_raw = read_file_str(CONFIG_JSON);
            if let Ok(parsed) = serde_json::from_str::<FullStateExport>(&cfg_raw) {
                for (id, mut tpl) in parsed.templates {
                    tpl.normalize_vulkan_fields();
                    self.templates.insert(id, tpl);
                }
                for (pkg, mut r) in parsed.app_routes {
                    r.pkg_name = pkg.clone();
                    r.normalize();
                    if r.is_any_enabled() {
                        self.routes.insert(pkg, r);
                    }
                }
                if self.auto_spoof.tpl_id.is_empty() && !parsed.auto_spoof.tpl_id.is_empty() {
                    self.auto_spoof = parsed.auto_spoof;
                    self.auto_spoof.normalize();
                }
            }
        }

        let routes_raw = read_file_str(ROUTES_DB);
        for line in routes_raw.lines() {
            let tr = line.trim();
            if tr.is_empty() || tr.starts_with('#') {
                continue;
            }
            let parts: Vec<&str> = tr.split('|').collect();
            if parts.len() >= 6 && is_safe_token(parts[0]) && !parts[1].is_empty() {
                let raw_col7 = parts.get(7).and_then(|s| s.parse::<i32>().ok()).unwrap_or(0);
                let legacy_vfs = parts[4] == "1";
                let (legacy_cow, cpu, mem, vfs_nodes) = if parts.len() >= 13 {
                    (
                        parts[9] == "1",
                        parts[10] == "1",
                        parts[11] == "1",
                        parts[12] == "1",
                    )
                } else {
                    (false, legacy_vfs, legacy_vfs, legacy_vfs)
                };

                let prop_mode = if let Some(m) = parts.get(13).filter(|s| !s.is_empty()) {
                    sanitize_prop_mode(m)
                } else if legacy_cow {
                    "cow".into()
                } else {
                    "hook".into()
                };

                let hdr_spoof = parts.get(14).copied() == Some("1");
                let overrides = parts
                    .get(15)
                    .filter(|s| !s.is_empty())
                    .and_then(|b64| base64_url_decode(b64))
                    .and_then(|bytes| serde_json::from_slice::<AppOverrides>(&bytes).ok())
                    .unwrap_or_default();

                let mut r = AppRoute {
                    pkg_name: parts[0].to_string(),
                    tpl_id: parts[1].to_string(),
                    build: parts[2] == "1",
                    prop_mode,
                    cow_prop: false,
                    gl: parts[3] == "1",
                    vfs: cpu || mem || vfs_nodes,
                    cpu,
                    mem,
                    vfs_nodes,
                    probe: parts[5] == "1",
                    sdk_spoof: parts.get(6).copied() == Some("1"),
                    hdr_spoof,
                    refresh_rate: sanitize_refresh_rate(raw_col7),
                    mount_ns: parts.get(8).copied() == Some("1"),
                    overrides,
                };
                r.normalize();
                if r.is_any_enabled() {
                    self.routes.insert(r.pkg_name.clone(), r);
                }
            }
        }
    }

    pub fn load_auto_spoof_file() -> AutoSpoofConfig {
        let raw = read_file_str(AUTO_SPOOF_JSON);
        if let Ok(mut cfg) = serde_json::from_str::<AutoSpoofConfig>(&raw) {
            cfg.normalize();
            return cfg;
        }
        AutoSpoofConfig::default()
    }

    pub fn save_auto_spoof(&mut self, mut cfg: AutoSpoofConfig) -> bool {
        cfg.normalize();
        if cfg.enabled && (cfg.tpl_id.is_empty() || !self.templates.contains_key(&cfg.tpl_id)) {
            if let Some(first_id) = self.templates.keys().next() {
                cfg.tpl_id = first_id.clone();
            } else {
                return false;
            }
        }
        self.auto_spoof = cfg.clone();
        ensure_dir(DATA_ROOT_DIR, 0o700);
        let payload = serde_json::to_string_pretty(&cfg).unwrap_or_else(|_| "{}".into());
        let ok = write_file_atomic(AUTO_SPOOF_JSON, &(payload + "\n"), 0o600);
        write_file_atomic(CONFIG_JSON, &self.export_json(false), 0o600);
        ok
    }

    pub fn load_runtime_status() -> BTreeMap<String, RuntimeStatusEntry> {
        let raw = read_file_str(RUNTIME_STATUS_FILE);
        if raw.trim().is_empty() {
            return BTreeMap::new();
        }
        serde_json::from_str::<BTreeMap<String, RuntimeStatusEntry>>(&raw).unwrap_or_default()
    }

    pub fn clear_runtime_status() {
        ensure_dir(DATA_ROOT_DIR, 0o700);
        write_file_atomic(RUNTIME_STATUS_FILE, "{}\n", 0o600);
    }

    pub fn save_all_and_compile(&mut self) -> bool {
        self.meta = read_module_meta();
        ensure_dir(DATA_ROOT_DIR, 0o700);
        ensure_dir(TPL_DIR, 0o700);
        ensure_dir(APPS_DIR, 0o700);

        for (id, tpl) in &self.templates {
            let path = format!("{}/{}.tpl", TPL_DIR, id);
            write_file_atomic(&path, &Self::serialize_tpl(tpl), 0o600);
        }

        let mut valid_confs: HashSet<String> = HashSet::new();
        for r in self.routes.values_mut() {
            r.normalize();
        }
        self.routes
            .retain(|_, r| self.templates.contains_key(&r.tpl_id) && r.is_any_enabled());

        for (pkg, r) in &self.routes {
            if !is_safe_token(pkg) {
                continue;
            }
            if let Some(tpl) = self.templates.get(&r.tpl_id) {
                let conf_name = format!("{}.conf", pkg);
                let conf_path = format!("{}/{}", APPS_DIR, conf_name);
                let compiled = compile_modular_conf(tpl, r);
                write_file_atomic(&conf_path, &compiled, 0o600);
                valid_confs.insert(conf_name);
            }
        }

        if let Ok(entries) = fs::read_dir(APPS_DIR) {
            for entry in entries.flatten() {
                if let Some(fname) = entry.file_name().to_str() {
                    if fname.ends_with(".conf") && !valid_confs.contains(fname) {
                        let _ = fs::remove_file(entry.path());
                    }
                }
            }
        }

        let mut r_lines = Vec::with_capacity(self.routes.len() + 1);
        r_lines.push(format!(
            "# MirageForge v{} Routes Table (v3 16-col)",
            self.meta.version
        ));
        for r in self.routes.values() {
            let ovr_b64 = if r.overrides.is_empty() {
                String::new()
            } else {
                let j = serde_json::to_string(&r.overrides).unwrap_or_default();
                base64_url_encode(j.as_bytes())
            };
            r_lines.push(format!(
                "{}|{}|{}|{}|{}|{}|{}|{}|{}|{}|{}|{}|{}|{}|{}|{}",
                r.pkg_name,
                r.tpl_id,
                if r.build { "1" } else { "0" },
                if r.gl { "1" } else { "0" },
                if r.vfs { "1" } else { "0" },
                if r.probe { "1" } else { "0" },
                if r.sdk_spoof { "1" } else { "0" },
                sanitize_refresh_rate(r.refresh_rate),
                if r.mount_ns { "1" } else { "0" },
                if r.cow_prop { "1" } else { "0" },
                if r.cpu { "1" } else { "0" },
                if r.mem { "1" } else { "0" },
                if r.vfs_nodes { "1" } else { "0" },
                r.prop_mode,
                if r.hdr_spoof { "1" } else { "0" },
                ovr_b64
            ));
        }
        write_file_atomic(ROUTES_DB, &(r_lines.join("\n") + "\n"), 0o600);
        write_file_atomic(CONFIG_JSON, &self.export_json(false), 0o600);
        true
    }

    pub fn upsert_template(&mut self, mut tpl: DeviceTemplate) -> bool {
        if !is_safe_token(&tpl.id) || tpl.model.is_empty() {
            return false;
        }
        if tpl.manufacturer.is_empty() {
            tpl.manufacturer = tpl.brand.clone();
        }
        if tpl.product.is_empty() {
            tpl.product = tpl.device.clone();
        }
        if tpl.display_id.is_empty() {
            tpl.display_id = format!("{} release-keys", tpl.build_id);
        }
        if tpl.incremental.is_empty() {
            tpl.incremental = tpl.build_id.clone();
        }
        if sanitize_refresh_rate(tpl.max_refresh_rate) == 0 {
            tpl.max_refresh_rate = 120;
        }
        tpl.normalize_vulkan_fields();
        self.templates.insert(tpl.id.clone(), tpl);
        self.save_all_and_compile()
    }

    pub fn delete_template(&mut self, id: &str) -> bool {
        if !is_safe_token(id) {
            return false;
        }
        self.templates.remove(id);
        let _ = fs::remove_file(format!("{}/{}.tpl", TPL_DIR, id));
        self.routes.retain(|_, r| r.tpl_id != id);
        if self.auto_spoof.tpl_id == id {
            self.auto_spoof.enabled = false;
            self.auto_spoof.tpl_id.clear();
            let _ = write_file_atomic(
                AUTO_SPOOF_JSON,
                &(serde_json::to_string_pretty(&self.auto_spoof).unwrap_or_default() + "\n"),
                0o600,
            );
        }
        self.save_all_and_compile()
    }

    pub fn set_route(&mut self, mut route: AppRoute, restart: bool) -> bool {
        if !is_safe_token(&route.pkg_name) || !self.templates.contains_key(&route.tpl_id) {
            return false;
        }
        route.normalize();
        let pkg = route.pkg_name.clone();
        if route.is_any_enabled() {
            self.routes.insert(pkg.clone(), route);
        } else {
            self.routes.remove(&pkg);
        }
        let ok = self.save_all_and_compile();
        if restart {
            Self::stop_package(&pkg);
        }
        ok
    }

    pub fn remove_route(&mut self, pkg: &str, restart: bool) -> bool {
        if !is_safe_token(pkg) {
            return false;
        }
        self.routes.remove(pkg);
        let _ = fs::remove_file(format!("{}/{}.conf", APPS_DIR, pkg));
        let ok = self.save_all_and_compile();
        if restart {
            Self::stop_package(pkg);
        }
        ok
    }

    pub fn stop_package(pkg: &str) {
        if is_safe_token(pkg) {
            let _ = Command::new("am").args(["force-stop", pkg]).status();
        }
    }

    pub fn export_full_json(&self) -> String {
        self.export_json(true)
    }

    pub fn export_json(&self, include_runtime: bool) -> String {
        let state = FullStateExport {
            version: self.meta.version.clone(),
            version_code: self.meta.version_code,
            active_count: self.routes.len(),
            template_count: self.templates.len(),
            templates: self.templates.clone(),
            app_routes: self.routes.clone(),
            auto_spoof: self.auto_spoof.clone(),
            runtime_status: if include_runtime {
                Self::load_runtime_status()
            } else {
                BTreeMap::new()
            },
        };
        serde_json::to_string_pretty(&state).unwrap_or_else(|_| "{}".into())
    }

    fn serialize_tpl(t: &DeviceTemplate) -> String {
        serde_json::to_string_pretty(t).unwrap_or_default()
    }

    fn deserialize_tpl(raw: &str) -> Option<DeviceTemplate> {
        if raw.trim_start().starts_with('{') {
            let mut t = serde_json::from_str::<DeviceTemplate>(raw).ok()?;
            t.normalize_vulkan_fields();
            return Some(t);
        }
        let mut map = serde_json::Map::new();
        let mut custom = Vec::new();
        for line in raw.lines() {
            if line.is_empty() || line.starts_with('#') {
                continue;
            }
            if let Some(rest) = line.strip_prefix("CUSTOM_PROP:") {
                custom.push(rest.to_string());
                continue;
            }
            if let Some(eq) = line.find('=') {
                map.insert(
                    line[..eq].to_string(),
                    serde_json::Value::String(line[eq + 1..].to_string()),
                );
            }
        }
        map.insert("CUSTOM_PROPS".into(), serde_json::Value::String(custom.join("\n")));
        let mut t: DeviceTemplate = serde_json::from_value(serde_json::Value::Object(map)).ok()?;
        t.normalize_vulkan_fields();
        Some(t)
    }

    fn seed_flagship_presets(&mut self) {
        let presets = vec![
            DeviceTemplate {
                id: "xiaomi_15_pro".into(),
                name: "Xiaomi 15 Pro (骁龙8至尊版 / 16GB LPDDR5X)".into(),
                model: "2410DPN6CC".into(),
                brand: "Xiaomi".into(),
                manufacturer: "Xiaomi".into(),
                device: "haotian".into(),
                product: "haotian".into(),
                board: "sun".into(),
                hardware: "qcom".into(),
                bootloader: "unknown".into(),
                soc_manufacturer: "Qualcomm".into(),
                soc_model: "SM8750-AB".into(),
                supported_abis: "arm64-v8a,armeabi-v7a,armeabi".into(),
                build_id: "AP3A.240905.015.A2".into(),
                display_id: "AP3A.240905.015.A2 release-keys".into(),
                incremental: "OS2.0.102.0.VOBCNXM".into(),
                build_host: "pangu-build-component-system".into(),
                build_user: "builder".into(),
                build_time: "1736899200000".into(),
                release: "15".into(),
                sdk_int: "35".into(),
                security_patch: "2025-02-01".into(),
                fingerprint: "Xiaomi/haotian/haotian:15/AP3A.240905.015.A2/OS2.0.102.0.VOBCNXM:user/release-keys".into(),
                gl_vendor: "Qualcomm".into(),
                gl_renderer: "Adreno (TM) 830".into(),
                gl_version: "OpenGL ES 3.2 V@0800.0".into(),
                vk_device_name: "Adreno (TM) 830".into(),
                vk_vendor_id: "0x5143".into(),
                vk_device_id: "0x43050A01".into(),
                vk_api_version: "1.3.295".into(),
                vk_driver_version: "512.800.0".into(),
                vk_driver_name: "Qualcomm Technologies Inc. Adreno Vulkan Driver".into(),
                vk_driver_info: "Driver Build: 0800.0".into(),
                max_refresh_rate: 120,
                cpu_hardware: "Qualcomm Technologies, Inc SM8750".into(),
                cpu_cores: 8,
                cpu_max_freq: 4320000,
                cpu_clusters: "0-5:614400:3532800;6-7:806400:4320000".into(),
                ram_gb: 16,
                ddr_type: "LPDDR5X".into(),
                ddr_vendor: "SKhynix".into(),
                soc_id: "618".into(),
                soc_machine: "SM8750".into(),
                kernel_release: "6.6.30-android15-8-g94b8f12ab4c1".into(),
                kernel_version: "#1 SMP PREEMPT Fri Nov 15 12:00:00 UTC 2025".into(),
                custom_props: "ro.miui.ui.version.code=15\nro.miui.ui.version.name=V816\nro.product.marketname=Xiaomi 15 Pro".into(),
            },
            DeviceTemplate {
                id: "rog_9_pro".into(),
                name: "ROG 游戏手机 9 Pro (24GB LPDDR5X / 165Hz)".into(),
                model: "ASUS_AI2501_A".into(),
                brand: "asus".into(),
                manufacturer: "asus".into(),
                device: "AI2501".into(),
                product: "CN_AI2501".into(),
                board: "sun".into(),
                hardware: "qcom".into(),
                bootloader: "unknown".into(),
                soc_manufacturer: "Qualcomm".into(),
                soc_model: "SM8750-AB".into(),
                supported_abis: "arm64-v8a,armeabi-v7a,armeabi".into(),
                build_id: "AQ3A.240812.002".into(),
                display_id: "WW_AI2501-35.1810.1810.88-0".into(),
                incremental: "35.1810.1810.88".into(),
                build_host: "asus-rog-build".into(),
                build_user: "jenkins".into(),
                build_time: "1737504000000".into(),
                release: "15".into(),
                sdk_int: "35".into(),
                security_patch: "2025-02-05".into(),
                fingerprint: "asus/CN_AI2501/AI2501:15/AQ3A.240812.002/35.1810.1810.88:user/release-keys".into(),
                gl_vendor: "Qualcomm".into(),
                gl_renderer: "Adreno (TM) 830".into(),
                gl_version: "OpenGL ES 3.2 V@0800.0".into(),
                vk_device_name: "Adreno (TM) 830".into(),
                vk_vendor_id: "0x5143".into(),
                vk_device_id: "0x43050A01".into(),
                vk_api_version: "1.3.295".into(),
                vk_driver_version: "512.800.0".into(),
                vk_driver_name: "Qualcomm Technologies Inc. Adreno Vulkan Driver".into(),
                vk_driver_info: "Driver Build: 0800.0".into(),
                max_refresh_rate: 165,
                cpu_hardware: "Qualcomm Technologies, Inc SM8750".into(),
                cpu_cores: 8,
                cpu_max_freq: 4320000,
                cpu_clusters: "0-5:614400:3532800;6-7:806400:4320000".into(),
                ram_gb: 24,
                ddr_type: "LPDDR5X".into(),
                ddr_vendor: "SKhynix".into(),
                soc_id: "618".into(),
                soc_machine: "SM8750".into(),
                kernel_release: "6.6.30-android15-8-rog9".into(),
                kernel_version: "#1 SMP PREEMPT Mon Jan 20 08:30:00 UTC 2025".into(),
                custom_props: "ro.product.asus.series=ROG Phone 9\nro.vendor.asus.gaming=1".into(),
            },
            DeviceTemplate {
                id: "redmagic_10_pro".into(),
                name: "红魔 10 Pro+ (24GB LPDDR5T / 144Hz)".into(),
                model: "NX789J".into(),
                brand: "nubia".into(),
                manufacturer: "ZTE".into(),
                device: "NX789J".into(),
                product: "NX789J".into(),
                board: "sun".into(),
                hardware: "qcom".into(),
                bootloader: "unknown".into(),
                soc_manufacturer: "Qualcomm".into(),
                soc_model: "SM8750-AB".into(),
                supported_abis: "arm64-v8a,armeabi-v7a,armeabi".into(),
                build_id: "AP3A.240905.015.A2".into(),
                display_id: "RedMagicOS10.0.12_NX789J_GB".into(),
                incremental: "20250110.213512".into(),
                build_host: "nubia-build-server".into(),
                build_user: "nubia".into(),
                build_time: "1736467200000".into(),
                release: "15".into(),
                sdk_int: "35".into(),
                security_patch: "2025-01-01".into(),
                fingerprint: "nubia/NX789J/NX789J:15/AP3A.240905.015.A2/20250110.213512:user/release-keys".into(),
                gl_vendor: "Qualcomm".into(),
                gl_renderer: "Adreno (TM) 830".into(),
                gl_version: "OpenGL ES 3.2 V@0800.0".into(),
                vk_device_name: "Adreno (TM) 830".into(),
                vk_vendor_id: "0x5143".into(),
                vk_device_id: "0x43050A01".into(),
                vk_api_version: "1.3.295".into(),
                vk_driver_version: "512.800.0".into(),
                vk_driver_name: "Qualcomm Technologies Inc. Adreno Vulkan Driver".into(),
                vk_driver_info: "Driver Build: 0800.0".into(),
                max_refresh_rate: 144,
                cpu_hardware: "Qualcomm Technologies, Inc SM8750".into(),
                cpu_cores: 8,
                cpu_max_freq: 4320000,
                cpu_clusters: "0-5:614400:3532800;6-7:806400:4320000".into(),
                ram_gb: 24,
                ddr_type: "LPDDR5T".into(),
                ddr_vendor: "SKhynix".into(),
                soc_id: "618".into(),
                soc_machine: "SM8750".into(),
                kernel_release: "6.6.30-android15-8-redmagic".into(),
                kernel_version: "#1 SMP PREEMPT Fri Jan 10 21:35:12 CST 2025".into(),
                custom_props: "ro.product.marketname=红魔 10 Pro+".into(),
            },
            DeviceTemplate {
                id: "vivo_x200_pro".into(),
                name: "vivo X200 Pro (天玑9400 / 16GB LPDDR5T)".into(),
                model: "V2405A".into(),
                brand: "vivo".into(),
                manufacturer: "vivo".into(),
                device: "PD2405".into(),
                product: "PD2405".into(),
                board: "mt6991".into(),
                hardware: "mt6991".into(),
                bootloader: "unknown".into(),
                soc_manufacturer: "Mediatek".into(),
                soc_model: "MT6991".into(),
                supported_abis: "arm64-v8a,armeabi-v7a,armeabi".into(),
                build_id: "AP3A.240905.015.A2".into(),
                display_id: "OriginOS 5 PD2405_A_15.0.14.2".into(),
                incremental: "compiler11201512".into(),
                build_host: "vivo-osc-compile".into(),
                build_user: "compiler".into(),
                build_time: "1732105920000".into(),
                release: "15".into(),
                sdk_int: "35".into(),
                security_patch: "2025-02-01".into(),
                fingerprint: "vivo/PD2405/PD2405:15/AP3A.240905.015.A2/compiler11201512:user/release-keys".into(),
                gl_vendor: "ARM".into(),
                gl_renderer: "Immortalis-G925 MC12".into(),
                gl_version: "OpenGL ES 3.2 v1.r49p0-01eac0".into(),
                vk_device_name: "Immortalis-G925 MC12".into(),
                vk_vendor_id: "0x13B5".into(),
                vk_device_id: "0x12870000".into(),
                vk_api_version: "1.3.295".into(),
                vk_driver_version: "49.0.0".into(),
                vk_driver_name: "ARM".into(),
                vk_driver_info: "r49p0-01eac0".into(),
                max_refresh_rate: 120,
                cpu_hardware: "MT6991".into(),
                cpu_cores: 8,
                cpu_max_freq: 3620000,
                cpu_clusters: "0-3:800000:2400000;4-6:1000000:3300000;7-7:1200000:3620000".into(),
                ram_gb: 16,
                ddr_type: "LPDDR5T".into(),
                ddr_vendor: "SKhynix".into(),
                soc_id: "6991".into(),
                soc_machine: "MT6991".into(),
                kernel_release: "6.6.30-android15-8-mt6991".into(),
                kernel_version: "#1 SMP PREEMPT Wed Nov 20 15:12:00 CST 2024".into(),
                custom_props: "ro.vivo.os.build.display.id=OriginOS 5\nro.vivo.market.name=vivo X200 Pro".into(),
            },
        ];

        for p in presets {
            self.templates.insert(p.id.clone(), p);
        }
    }
}

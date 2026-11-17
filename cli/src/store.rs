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
}

impl StoreEngine {
    pub fn new() -> Self {
        Self {
            meta: read_module_meta(),
            templates: BTreeMap::new(),
            routes: BTreeMap::new(),
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

        if let Ok(entries) = fs::read_dir(TPL_DIR) {
            for entry in entries.flatten() {
                let path = entry.path();
                if path.extension().and_then(|s| s.to_str()) == Some("tpl") {
                    if let Ok(raw) = fs::read_to_string(&path) {
                        if let Some(tpl) = Self::deserialize_tpl(&raw) {
                            if is_safe_token(&tpl.id) {
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
                self.templates = parsed.templates;
                for (pkg, mut r) in parsed.app_routes {
                    r.pkg_name = pkg.clone();
                    self.routes.insert(pkg, r);
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
                let r = AppRoute {
                    pkg_name: parts[0].to_string(),
                    tpl_id: parts[1].to_string(),
                    build: parts[2] == "1",
                    gl: parts[3] == "1",
                    vfs: parts[4] == "1",
                    probe: parts[5] == "1",
                    sdk_spoof: parts.get(6).copied() == Some("1"),
                    dpi: parts.get(7).and_then(|s| s.parse::<i32>().ok()).unwrap_or(0),
                };
                if r.is_any_enabled() {
                    self.routes.insert(r.pkg_name.clone(), r);
                }
            }
        }
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
        r_lines.push(format!("# MirageForge v{} Routes Table", self.meta.version));
        for r in self.routes.values() {
            r_lines.push(format!(
                "{}|{}|{}|{}|{}|{}|{}|{}",
                r.pkg_name,
                r.tpl_id,
                if r.build { "1" } else { "0" },
                if r.gl { "1" } else { "0" },
                if r.vfs { "1" } else { "0" },
                if r.probe { "1" } else { "0" },
                if r.sdk_spoof { "1" } else { "0" },
                r.dpi
            ));
        }
        write_file_atomic(ROUTES_DB, &(r_lines.join("\n") + "\n"), 0o600);
        write_file_atomic(CONFIG_JSON, &self.export_full_json(), 0o600);
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
        self.save_all_and_compile()
    }

    pub fn set_route(&mut self, route: AppRoute, restart: bool) -> bool {
        if !is_safe_token(&route.pkg_name) || !self.templates.contains_key(&route.tpl_id) {
            return false;
        }
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
        let state = FullStateExport {
            version: self.meta.version.clone(),
            version_code: self.meta.version_code,
            active_count: self.routes.len(),
            template_count: self.templates.len(),
            templates: self.templates.clone(),
            app_routes: self.routes.clone(),
        };
        serde_json::to_string_pretty(&state).unwrap_or_else(|_| "{}".into())
    }

    fn serialize_tpl(t: &DeviceTemplate) -> String {
        serde_json::to_string_pretty(t).unwrap_or_default()
    }

    fn deserialize_tpl(raw: &str) -> Option<DeviceTemplate> {
        if raw.trim_start().starts_with('{') {
            return serde_json::from_str::<DeviceTemplate>(raw).ok();
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
        serde_json::from_value(serde_json::Value::Object(map)).ok()
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
                default_dpi: 0,
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
                default_dpi: 0,
                custom_props: "ro.product.asus.series=ROG Phone 9\nro.vendor.asus.gaming=1".into(),
            },
            DeviceTemplate {
                id: "redmagic_10_pro".into(),
                name: "红魔 10 Pro+ (24GB LPDDR5T 氘锋版)".into(),
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
                default_dpi: 0,
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
                default_dpi: 0,
                custom_props: "ro.vivo.os.build.display.id=OriginOS 5\nro.vivo.market.name=vivo X200 Pro".into(),
            },
        ];

        for p in presets {
            self.templates.insert(p.id.clone(), p);
        }
    }
}

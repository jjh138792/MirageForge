use serde::{Deserialize, Deserializer, Serialize};
use std::collections::BTreeMap;
use std::fs;

pub const MODULE_PROP_PATH: &str = "/data/adb/modules/mirage_forge/module.prop";
pub const DATA_ROOT_DIR: &str = "/data/adb/mirage_forge";
pub const TPL_DIR: &str = "/data/adb/mirage_forge/templates";
pub const APPS_DIR: &str = "/data/adb/mirage_forge/apps";
pub const ROUTES_DB: &str = "/data/adb/mirage_forge/routes.db";
pub const CONFIG_JSON: &str = "/data/adb/mirage_forge/config.json";
pub const LAUNCH_LOG_FILE: &str = "/data/adb/mirage_forge/launch_history.log";
pub const PROBE_LOG_FILE: &str = "/data/adb/mirage_forge/probe_events.log";
pub const CRASH_LOG_FILE: &str = "/data/adb/mirage_forge/crash_events.log";

#[derive(Debug, Clone)]
pub struct ModuleMeta {
    pub version: String,
    pub version_code: u32,
}

/// 从 module.prop 动态读取版本号与 versionCode，告别硬编码
pub fn read_module_meta() -> ModuleMeta {
    let candidates = [MODULE_PROP_PATH, "./module.prop", "../module/module.prop"];
    let mut version = String::new();
    let mut version_code = 0u32;

    for path in candidates {
        if let Ok(raw) = fs::read_to_string(path) {
            for line in raw.lines() {
                let tr = line.trim();
                if let Some(v) = tr.strip_prefix("version=") {
                    version = v.trim().trim_start_matches('v').to_string();
                } else if let Some(vc) = tr.strip_prefix("versionCode=") {
                    version_code = vc.trim().parse::<u32>().unwrap_or(0);
                }
            }
            if !version.is_empty() {
                break;
            }
        }
    }

    if version.is_empty() {
        version = env!("CARGO_PKG_VERSION").to_string();
    }
    if version_code == 0 {
        version_code = 120;
    }

    ModuleMeta {
        version,
        version_code,
    }
}

fn de_i32_flex<'de, D>(deserializer: D) -> Result<i32, D::Error>
where
    D: Deserializer<'de>,
{
    #[derive(Deserialize)]
    #[serde(untagged)]
    enum IntOrStr {
        Int(i32),
        Str(String),
    }
    match IntOrStr::deserialize(deserializer)? {
        IntOrStr::Int(v) => Ok(v),
        IntOrStr::Str(s) => Ok(s.trim().parse::<i32>().unwrap_or(0)),
    }
}

fn default_hardware() -> String { "qcom".into() }
fn default_bootloader() -> String { "unknown".into() }
fn default_soc_man() -> String { "Qualcomm".into() }
fn default_abis() -> String { "arm64-v8a,armeabi-v7a,armeabi".into() }
fn default_build_id() -> String { "AP3A.240905.015.A2".into() }
fn default_display_id() -> String { "AP3A.240905.015.A2 release-keys".into() }
fn default_incremental() -> String { "OS2.0.102.0.VOBCNXM".into() }
fn default_build_host() -> String { "pangu-build-component-system".into() }
fn default_build_user() -> String { "builder".into() }
fn default_build_time() -> String { "1736899200000".into() }
fn default_release() -> String { "15".into() }
fn default_sdk() -> String { "35".into() }
fn default_patch() -> String { "2025-02-01".into() }
fn default_gl_vendor() -> String { "Qualcomm".into() }
fn default_gl_renderer() -> String { "Adreno (TM) 830".into() }
fn default_gl_version() -> String { "OpenGL ES 3.2 V@0800.0".into() }
fn default_cpu_hw() -> String { "Qualcomm Technologies, Inc SM8750".into() }
fn default_cores() -> i32 { 8 }
fn default_max_freq() -> i32 { 4320000 }
fn default_clusters() -> String { "0-5:614400:3532800;6-7:806400:4320000".into() }
fn default_ram() -> i32 { 16 }
fn default_ddr_type() -> String { "LPDDR5X".into() }
fn default_ddr_vendor() -> String { "SKhynix".into() }
fn default_soc_id() -> String { "618".into() }
fn default_soc_machine() -> String { "SM8750".into() }
fn default_kernel_rel() -> String { "6.6.30-android15-8-g94b8f12ab4c1".into() }
fn default_kernel_ver() -> String { "#1 SMP PREEMPT Fri Nov 15 12:00:00 UTC 2025".into() }
fn default_true() -> bool { true }

#[derive(Debug, Clone, Serialize, Deserialize)]
pub struct DeviceTemplate {
    pub id: String,
    pub name: String,
    #[serde(rename = "MODEL", default)]
    pub model: String,
    #[serde(rename = "BRAND", default)]
    pub brand: String,
    #[serde(rename = "MANUFACTURER", default)]
    pub manufacturer: String,
    #[serde(rename = "DEVICE", default)]
    pub device: String,
    #[serde(rename = "PRODUCT", default)]
    pub product: String,
    #[serde(rename = "BOARD", default)]
    pub board: String,
    #[serde(rename = "HARDWARE", default = "default_hardware")]
    pub hardware: String,
    #[serde(rename = "BOOTLOADER", default = "default_bootloader")]
    pub bootloader: String,
    #[serde(rename = "SOC_MANUFACTURER", default = "default_soc_man")]
    pub soc_manufacturer: String,
    #[serde(rename = "SOC_MODEL", default)]
    pub soc_model: String,
    #[serde(rename = "SUPPORTED_ABIS", default = "default_abis")]
    pub supported_abis: String,
    #[serde(rename = "BUILD_ID", default = "default_build_id")]
    pub build_id: String,
    #[serde(rename = "DISPLAY_ID", default = "default_display_id")]
    pub display_id: String,
    #[serde(rename = "INCREMENTAL", default = "default_incremental")]
    pub incremental: String,
    #[serde(rename = "BUILD_HOST", default = "default_build_host")]
    pub build_host: String,
    #[serde(rename = "BUILD_USER", default = "default_build_user")]
    pub build_user: String,
    #[serde(rename = "BUILD_TIME", default = "default_build_time")]
    pub build_time: String,
    #[serde(rename = "RELEASE", default = "default_release")]
    pub release: String,
    #[serde(rename = "SDK_INT", default = "default_sdk")]
    pub sdk_int: String,
    #[serde(rename = "SECURITY_PATCH", default = "default_patch")]
    pub security_patch: String,
    #[serde(rename = "FINGERPRINT", default)]
    pub fingerprint: String,

    #[serde(rename = "GL_VENDOR", default = "default_gl_vendor")]
    pub gl_vendor: String,
    #[serde(rename = "GL_RENDERER", default = "default_gl_renderer")]
    pub gl_renderer: String,
    #[serde(rename = "GL_VERSION", default = "default_gl_version")]
    pub gl_version: String,

    #[serde(rename = "CPU_HARDWARE", default = "default_cpu_hw")]
    pub cpu_hardware: String,
    #[serde(rename = "CPU_CORES", default = "default_cores", deserialize_with = "de_i32_flex")]
    pub cpu_cores: i32,
    #[serde(rename = "CPU_MAX_FREQ", default = "default_max_freq", deserialize_with = "de_i32_flex")]
    pub cpu_max_freq: i32,
    #[serde(rename = "CPU_CLUSTERS", default = "default_clusters")]
    pub cpu_clusters: String,
    #[serde(rename = "RAM_GB", default = "default_ram", deserialize_with = "de_i32_flex")]
    pub ram_gb: i32,
    #[serde(rename = "DDR_TYPE", default = "default_ddr_type")]
    pub ddr_type: String,
    #[serde(rename = "DDR_VENDOR", default = "default_ddr_vendor")]
    pub ddr_vendor: String,
    #[serde(rename = "SOC_ID", default = "default_soc_id")]
    pub soc_id: String,
    #[serde(rename = "SOC_MACHINE", default = "default_soc_machine")]
    pub soc_machine: String,
    #[serde(rename = "KERNEL_RELEASE", default = "default_kernel_rel")]
    pub kernel_release: String,
    #[serde(rename = "KERNEL_VERSION", default = "default_kernel_ver")]
    pub kernel_version: String,
    #[serde(rename = "DEFAULT_DPI", default, deserialize_with = "de_i32_flex")]
    pub default_dpi: i32,

    #[serde(rename = "CUSTOM_PROPS", default)]
    pub custom_props: String,
}

#[derive(Debug, Clone, Serialize, Deserialize)]
pub struct AppRoute {
    #[serde(skip_deserializing, skip_serializing_if = "String::is_empty")]
    pub pkg_name: String,
    #[serde(rename = "tplId")]
    pub tpl_id: String,
    #[serde(default = "default_true")]
    pub build: bool,
    #[serde(default = "default_true")]
    pub gl: bool,
    #[serde(default = "default_true")]
    pub vfs: bool,
    #[serde(default)]
    pub probe: bool,
    #[serde(rename = "sdkSpoof", default)]
    pub sdk_spoof: bool,
    #[serde(default, deserialize_with = "de_i32_flex")]
    pub dpi: i32, // 0 表示不额外覆盖或跟随模板 DEFAULT_DPI，120..=960 表示按应用独立 DPI
}

impl AppRoute {
    pub fn is_any_enabled(&self) -> bool {
        self.build || self.gl || self.vfs || self.probe
    }
}

#[derive(Debug, Serialize, Deserialize)]
pub struct FullStateExport {
    pub version: String,
    #[serde(rename = "versionCode")]
    pub version_code: u32,
    #[serde(rename = "activeCount")]
    pub active_count: usize,
    #[serde(rename = "templateCount")]
    pub template_count: usize,
    pub templates: BTreeMap<String, DeviceTemplate>,
    #[serde(rename = "appRoutes")]
    pub app_routes: BTreeMap<String, AppRoute>,
}

#[derive(Debug, Serialize)]
pub struct ProbeEvent {
    pub pkg: String,
    pub domain: String,
    pub key: String,
    pub value: String,
    pub stack: String,
    pub unmapped: bool,
}

#[derive(Debug, Serialize)]
pub struct CrashEvent {
    pub pkg: String,
    pub signal: String,
    pub detail: String,
    pub stack: String,
}

#[derive(Debug, Serialize)]
pub struct TelemetryEvent {
    pub timestamp: i64,
    pub pkg: String,
    pub pid: i32,
    pub model: String,
    pub gpu: String,
    pub ram: String,
}

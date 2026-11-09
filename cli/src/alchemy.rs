use crate::models::DeviceTemplate;
use std::collections::{HashMap, HashSet};
use std::time::{SystemTime, UNIX_EPOCH};

pub struct InferredTopology {
    pub gl_vendor: &'static str,
    pub gl_renderer: &'static str,
    pub cpu_hw: String,
    pub soc_id: &'static str,
    pub soc_machine: String,
    pub max_freq_khz: i32,
    pub cpu_clusters: &'static str,
    pub ddr_type: &'static str,
}

pub fn infer_topology(board: &str, soc_model: &str) -> InferredTopology {
    let token = format!("{} {}", board, soc_model).to_ascii_lowercase();

    if token.contains("sun") || token.contains("sm8750") {
        return InferredTopology {
            gl_vendor: "Qualcomm",
            gl_renderer: "Adreno (TM) 830",
            cpu_hw: "Qualcomm Technologies, Inc SM8750".into(),
            soc_id: "618",
            soc_machine: "SM8750".into(),
            max_freq_khz: 4320000,
            cpu_clusters: "0-5:614400:3532800;6-7:806400:4320000",
            ddr_type: "LPDDR5X",
        };
    }
    if token.contains("pineapple") || token.contains("sm8650") {
        return InferredTopology {
            gl_vendor: "Qualcomm",
            gl_renderer: "Adreno (TM) 750",
            cpu_hw: "Qualcomm Technologies, Inc SM8650".into(),
            soc_id: "557",
            soc_machine: "SM8650".into(),
            max_freq_khz: 3302400,
            cpu_clusters: "0-1:556800:2265600;2-6:691200:3148800;7-7:806400:3302400",
            ddr_type: "LPDDR5X",
        };
    }
    if token.contains("kalama") || token.contains("sm8550") {
        return InferredTopology {
            gl_vendor: "Qualcomm",
            gl_renderer: "Adreno (TM) 740",
            cpu_hw: "Qualcomm Technologies, Inc SM8550".into(),
            soc_id: "519",
            soc_machine: "SM8550".into(),
            max_freq_khz: 3187200,
            cpu_clusters: "0-2:595200:2016000;3-6:691200:2803200;7-7:806400:3187200",
            ddr_type: "LPDDR5X",
        };
    }
    if token.contains("mt6991") {
        return InferredTopology {
            gl_vendor: "ARM",
            gl_renderer: "Immortalis-G925 MC12",
            cpu_hw: "MT6991".into(),
            soc_id: "6991",
            soc_machine: "MT6991".into(),
            max_freq_khz: 3620000,
            cpu_clusters: "0-3:800000:2400000;4-6:1000000:3300000;7-7:1200000:3620000",
            ddr_type: "LPDDR5X",
        };
    }
    if token.contains("mt6989") {
        return InferredTopology {
            gl_vendor: "ARM",
            gl_renderer: "Immortalis-G720 MC12",
            cpu_hw: "MT6989".into(),
            soc_id: "6989",
            soc_machine: "MT6989".into(),
            max_freq_khz: 3250000,
            cpu_clusters: "0-3:800000:2000000;4-6:1000000:2850000;7-7:1100000:3250000",
            ddr_type: "LPDDR5X",
        };
    }

    let fallback = if !soc_model.is_empty() {
        soc_model.to_string()
    } else if !board.is_empty() {
        board.to_string()
    } else {
        "SM8750".to_string()
    };

    InferredTopology {
        gl_vendor: "Qualcomm",
        gl_renderer: "Adreno (TM) 830",
        cpu_hw: format!("Qualcomm Technologies, Inc {}", fallback),
        soc_id: "618",
        soc_machine: fallback,
        max_freq_khz: 4320000,
        cpu_clusters: "0-5:614400:3532800;6-7:806400:4320000",
        ddr_type: "LPDDR5X",
    }
}

pub fn parse_build_prop(raw_text: &str, custom_id: Option<&str>) -> Option<DeviceTemplate> {
    let mut prop_map: HashMap<String, String> = HashMap::new();
    let mut ordered_keys: Vec<String> = Vec::new();

    for raw_line in raw_text.lines() {
        let line = raw_line.trim();
        if line.is_empty() || line.starts_with('#') {
            continue;
        }
        if let Some(eq_idx) = line.find('=') {
            let k = line[..eq_idx].trim().to_string();
            let v = line[eq_idx + 1..].trim().to_string();
            if !k.is_empty() && !v.is_empty() {
                if !prop_map.contains_key(&k) {
                    ordered_keys.push(k.clone());
                }
                prop_map.insert(k, v);
            }
        }
    }

    let mut consumed: HashSet<String> = HashSet::new();
    let mut pick = |keys: &[&str]| -> String {
        for &k in keys {
            consumed.insert(k.to_string());
        }
        for &k in keys {
            if let Some(v) = prop_map.get(k) {
                if !v.is_empty() {
                    return v.clone();
                }
            }
        }
        String::new()
    };

    let model = pick(&[
        "ro.product.model",
        "ro.product.vendor.model",
        "ro.product.odm.model",
        "ro.product.system.model",
        "ro.product.product.model",
    ]);
    let brand = pick(&[
        "ro.product.brand",
        "ro.product.vendor.brand",
        "ro.product.odm.brand",
        "ro.product.system.brand",
    ]);
    let manufacturer = pick(&[
        "ro.product.manufacturer",
        "ro.product.vendor.manufacturer",
        "ro.product.odm.manufacturer",
        "ro.product.system.manufacturer",
    ]);
    let device = pick(&[
        "ro.product.device",
        "ro.product.vendor.device",
        "ro.product.odm.device",
        "ro.product.system.device",
        "ro.build.product",
    ]);
    let product = pick(&[
        "ro.product.name",
        "ro.product.vendor.name",
        "ro.product.odm.name",
        "ro.product.system.name",
    ]);
    let board = pick(&["ro.board.platform", "ro.product.board"]);
    let hardware = pick(&["ro.hardware", "ro.boot.hardware"]);
    let soc_man = pick(&["ro.soc.manufacturer"]);
    let soc_model = pick(&["ro.soc.model", "ro.mediatek.platform", "ro.chipname"]);
    let abis = pick(&["ro.product.cpu.abilist", "ro.system.product.cpu.abilist"]);
    let build_id = pick(&["ro.build.id", "ro.product.build.id", "ro.system.build.id"]);
    let utc_sec = pick(&["ro.build.date.utc", "ro.system.build.date.utc"]);
    let release = pick(&["ro.build.version.release", "ro.system.build.version.release"]);
    let sdk = pick(&["ro.build.version.sdk", "ro.system.build.version.sdk"]);
    let patch = pick(&["ro.build.version.security_patch", "ro.vendor.build.security_patch"]);
    let fingerprint = pick(&[
        "ro.build.fingerprint",
        "ro.product.build.fingerprint",
        "ro.vendor.build.fingerprint",
        "ro.system.build.fingerprint",
        "ro.odm.build.fingerprint",
    ]);

    let raw_ddr = pick(&["ro.boot.hardware.ddr", "ro.boot.ddr_info", "ro.hardware.ddr"]);
    let raw_ddr_type = pick(&["ro.boot.ddr_type"]);
    let raw_ram_size = pick(&["ro.boot.ddr_size", "ro.vendor.hw.ram", "ro.boot.ramsize"]);

    if model.is_empty() && brand.is_empty() && fingerprint.is_empty() {
        return None;
    }

    let topo = infer_topology(&board, &soc_model);

    let detected_ram_gb = raw_ram_size
        .trim_end_matches(|c: char| !c.is_ascii_digit())
        .parse::<i32>()
        .ok()
        .filter(|&r| (4..=32).contains(&r))
        .unwrap_or(16);

    let detected_ddr_type = if raw_ddr.contains("LPDDR5T") {
        "LPDDR5T"
    } else if raw_ddr.contains("LPDDR5X") {
        "LPDDR5X"
    } else if raw_ddr.contains("LPDDR5") {
        "LPDDR5"
    } else if !raw_ddr_type.is_empty() {
        &raw_ddr_type
    } else {
        topo.ddr_type
    };

    let detected_ddr_vendor = if raw_ddr.to_ascii_lowercase().contains("samsung") {
        "Samsung"
    } else if raw_ddr.to_ascii_lowercase().contains("micron") {
        "Micron"
    } else {
        "SKhynix"
    };

    let market_name = prop_map
        .get("ro.product.marketname")
        .or_else(|| prop_map.get("ro.vivo.market.name"))
        .or_else(|| prop_map.get("ro.vendor.oplus.market.name"))
        .cloned()
        .unwrap_or_default();

    let now_sec = SystemTime::now()
        .duration_since(UNIX_EPOCH)
        .map(|d| d.as_secs())
        .unwrap_or(1736899200);

    let tpl_id = custom_id
        .filter(|s| !s.is_empty())
        .map(|s| s.to_string())
        .unwrap_or_else(|| format!("tpl_{}", now_sec));

    let tpl_name = if !market_name.is_empty() {
        format!("{} {}", brand, market_name).trim().to_string()
    } else {
        format!("{} {}", brand, if !model.is_empty() { &model } else { &device })
            .trim()
            .to_string()
    };

    let build_time_ms = utc_sec
        .parse::<i64>()
        .ok()
        .map(|s| (s * 1000).to_string())
        .unwrap_or_else(|| "1736899200000".into());

    let mut extra_lines = Vec::new();
    for k in ordered_keys {
        if consumed.contains(&k) {
            continue;
        }
        if k.starts_with("ro.") || k.starts_with("persist.") {
            if let Some(v) = prop_map.get(&k) {
                extra_lines.push(format!("{}={}", k, v));
                if extra_lines.len() >= 100 {
                    break;
                }
            }
        }
    }

    Some(DeviceTemplate {
        id: tpl_id,
        name: tpl_name,
        model,
        brand: brand.clone(),
        manufacturer: if !manufacturer.is_empty() { manufacturer } else { brand },
        device: device.clone(),
        product: if !product.is_empty() { product } else { device },
        board,
        hardware: if !hardware.is_empty() { hardware } else { "qcom".into() },
        bootloader: "unknown".into(),
        soc_manufacturer: if !soc_man.is_empty() {
            soc_man
        } else if topo.gl_vendor == "ARM" {
            "Mediatek".into()
        } else {
            "Qualcomm".into()
        },
        soc_model: if !soc_model.is_empty() { soc_model } else { topo.soc_machine.clone() },
        supported_abis: if !abis.is_empty() { abis } else { "arm64-v8a,armeabi-v7a,armeabi".into() },
        build_id: if !build_id.is_empty() { build_id } else { "AP3A.240905.015.A2".into() },
        build_time: build_time_ms,
        release: if !release.is_empty() { release } else { "15".into() },
        sdk_int: if !sdk.is_empty() { sdk } else { "35".into() },
        security_patch: if !patch.is_empty() { patch } else { "2025-02-01".into() },
        fingerprint,
        gl_vendor: topo.gl_vendor.into(),
        gl_renderer: topo.gl_renderer.into(),
        gl_version: "OpenGL ES 3.2 V@0800.0".into(),
        cpu_hardware: topo.cpu_hw,
        cpu_cores: 8,
        cpu_max_freq: topo.max_freq_khz,
        cpu_clusters: topo.cpu_clusters.into(),
        ram_gb: detected_ram_gb,
        ddr_type: detected_ddr_type.into(),
        ddr_vendor: detected_ddr_vendor.into(),
        soc_id: topo.soc_id.into(),
        soc_machine: topo.soc_machine,
        kernel_release: "6.6.30-android15-8-g94b8f12ab4c1".into(),
        kernel_version: "#1 SMP PREEMPT Fri Nov 15 12:00:00 UTC 2025".into(),
        custom_props: extra_lines.join("\n"),
    })
}

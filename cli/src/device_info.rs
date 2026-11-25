// SPDX-License-Identifier: GPL-3.0-only
// Copyright (c) 2025-2026 jjh138792
// MirageForge: https://github.com/jjh138792/MirageForge

use crate::alchemy::{infer_topology, parse_build_prop};
use crate::fs_atomic::read_file_str;
use crate::models::{DeviceTemplate, HostDeviceInfo};
use std::collections::HashMap;
use std::fs;
use std::path::Path;
use std::process::Command;

fn run_cmd_stdout(bin: &str, args: &[&str]) -> String {
    Command::new(bin)
        .args(args)
        .output()
        .ok()
        .filter(|o| o.status.success() || !o.stdout.is_empty())
        .map(|o| String::from_utf8_lossy(&o.stdout).trim().to_string())
        .unwrap_or_default()
}

fn read_prop(key: &str) -> String {
    run_cmd_stdout("getprop", &[key])
}

fn first_non_empty(keys: &[&str]) -> String {
    for &k in keys {
        let v = read_prop(k);
        if !v.is_empty() {
            return v;
        }
    }
    String::new()
}

fn parse_cpu_cores() -> i32 {
    let raw = read_file_str("/proc/cpuinfo");
    let count = raw
        .lines()
        .filter(|l| l.trim_start().starts_with("processor"))
        .count();
    if count > 0 {
        return count as i32;
    }
    let present = read_file_str("/sys/devices/system/cpu/present");
    if let Some((_, right)) = present.trim().split_once('-') {
        if let Ok(last) = right.parse::<i32>() {
            return last + 1;
        }
    }
    8
}

fn parse_cpu_hardware_from_proc() -> String {
    let raw = read_file_str("/proc/cpuinfo");
    for line in raw.lines() {
        if let Some(rest) = line.strip_prefix("Hardware") {
            if let Some((_, val)) = rest.split_once(':') {
                let v = val.trim();
                if !v.is_empty() {
                    return v.to_string();
                }
            }
        }
    }
    String::new()
}

fn read_live_cpu_clusters() -> (String, i32) {
    let mut entries: Vec<(i32, i32, i32, i32)> = Vec::new();
    let dir = match fs::read_dir("/sys/devices/system/cpu/cpufreq") {
        Ok(d) => d,
        Err(_) => return (String::new(), 0),
    };

    for ent in dir.flatten() {
        let fname = ent.file_name();
        let name = fname.to_string_lossy();
        if !name.starts_with("policy") {
            continue;
        }
        let base = ent.path();
        let rel_raw = fs::read_to_string(base.join("related_cpus"))
            .or_else(|_| fs::read_to_string(base.join("affected_cpus")))
            .unwrap_or_default();
        let min_f = fs::read_to_string(base.join("cpuinfo_min_freq"))
            .unwrap_or_default()
            .trim()
            .parse::<i32>()
            .unwrap_or(0);
        let max_f = fs::read_to_string(base.join("cpuinfo_max_freq"))
            .unwrap_or_default()
            .trim()
            .parse::<i32>()
            .unwrap_or(0);
        if max_f <= 0 {
            continue;
        }

        let mut cpus: Vec<i32> = Vec::new();
        for tok in rel_raw.split_whitespace() {
            if let Some((a, b)) = tok.split_once('-') {
                if let (Ok(s), Ok(e)) = (a.parse::<i32>(), b.parse::<i32>()) {
                    cpus.push(s);
                    cpus.push(e);
                }
            } else if let Ok(n) = tok.parse::<i32>() {
                cpus.push(n);
            }
        }
        if cpus.is_empty() {
            if let Ok(p_idx) = name.trim_start_matches("policy").parse::<i32>() {
                cpus.push(p_idx);
            }
        }
        if let (Some(&first), Some(&last)) = (cpus.iter().min(), cpus.iter().max()) {
            let eff_min = if min_f > 0 { min_f } else { 614400 };
            entries.push((first, last, eff_min, max_f));
        }
    }

    if entries.is_empty() {
        return (String::new(), 0);
    }

    entries.sort_by_key(|e| e.0);
    let max_freq = entries.iter().map(|e| e.3).max().unwrap_or(0);
    let spec = entries
        .into_iter()
        .map(|(s, e, min_f, max_f)| format!("{}-{}:{}:{}", s, e, min_f, max_f))
        .collect::<Vec<_>>()
        .join(";");
    (spec, max_freq)
}

fn detect_ram_gb() -> i32 {
    let raw = read_file_str("/proc/meminfo");
    for line in raw.lines() {
        if let Some(rest) = line.strip_prefix("MemTotal:") {
            if let Some(kb_str) = rest.split_whitespace().next() {
                if let Ok(kb) = kb_str.parse::<u64>() {
                    let gb = (kb as f64) / (1024.0 * 1024.0);
                    for &std_gb in &[4, 6, 8, 12, 16, 18, 24, 32] {
                        if gb <= (std_gb as f64) + 0.4 {
                            return std_gb;
                        }
                    }
                    return gb.ceil() as i32;
                }
            }
        }
    }
    16
}

fn detect_gpu_renderer(board: &str, soc_model: &str) -> String {
    let kgsl = read_file_str("/sys/class/kgsl/kgsl-3d0/gpu_model")
        .trim()
        .to_string();
    if !kgsl.is_empty() {
        return kgsl;
    }

    let sf = run_cmd_stdout("dumpsys", &["SurfaceFlinger"]);
    for line in sf.lines() {
        if let Some(pos) = line.find("GLES:") {
            let rest = line[pos + 5..].trim();
            let parts: Vec<&str> = rest.split(',').collect();
            if parts.len() >= 2 && !parts[1].trim().is_empty() {
                return parts[1].trim().to_string();
            }
        }
    }

    let egl = first_non_empty(&["ro.hardware.vulkan", "ro.hardware.egl"]);
    if !egl.is_empty() && egl != "adreno" && egl != "mali" {
        return egl;
    }
    infer_topology(board, soc_model).gl_renderer.to_string()
}

fn find_exec(candidates: &[&str], cmd_name: &str) -> Option<String> {
    for &p in candidates {
        if Path::new(p).exists() {
            return Some(p.to_string());
        }
    }
    let found = run_cmd_stdout("sh", &["-c", &format!("command -v {} 2>/dev/null", cmd_name)]);
    if !found.is_empty() {
        Some(found)
    } else {
        None
    }
}

fn detect_root_manager() -> (String, String) {
    let ksu_bin = find_exec(&["/data/adb/ksud"], "ksud");
    let apd_bin = find_exec(&["/data/adb/apd"], "apd");
    let mag_bin = find_exec(
        &["/sbin/magisk", "/debug_ramdisk/magisk", "/system/bin/magisk"],
        "magisk",
    );

    if let Some(ref ksu) = ksu_bin {
        if Path::new("/data/adb/ksu").is_dir() {
            let ver = run_cmd_stdout(ksu, &["-V"]);
            if !ver.is_empty() {
                let label = if ver.to_ascii_lowercase().contains("next") {
                    "KernelSU Next"
                } else {
                    "KernelSU"
                };
                return (label.into(), ver.lines().next().unwrap_or("").to_string());
            }
        }
    }

    if let Some(ref apd) = apd_bin {
        if Path::new("/data/adb/ap").is_dir() {
            let ver = run_cmd_stdout(apd, &["-V"]);
            if !ver.is_empty() {
                return ("APatch".into(), ver.lines().next().unwrap_or("").to_string());
            }
        }
    }

    if let Some(ref mag) = mag_bin {
        let ver = run_cmd_stdout(mag, &["-v"]);
        if !ver.is_empty() {
            return ("Magisk".into(), ver.lines().next().unwrap_or("").to_string());
        }
    }

    if let Some(ref ksu) = ksu_bin {
        let ver = run_cmd_stdout(ksu, &["-V"]);
        return ("KernelSU".into(), ver);
    }
    if let Some(ref apd) = apd_bin {
        let ver = run_cmd_stdout(apd, &["-V"]);
        return ("APatch".into(), ver);
    }

    ("Unknown".into(), String::new())
}

fn read_prop_file_kv(path: &Path) -> HashMap<String, String> {
    let mut map = HashMap::new();
    if let Ok(raw) = fs::read_to_string(path) {
        for line in raw.lines() {
            let tr = line.trim();
            if let Some((k, v)) = tr.split_once('=') {
                map.insert(k.trim().to_string(), v.trim().to_string());
            }
        }
    }
    map
}

fn detect_zygisk_impl(root_mgr: &str) -> (String, String) {
    let modules_dir = Path::new("/data/adb/modules");
    if let Ok(entries) = fs::read_dir(modules_dir) {
        for ent in entries.flatten() {
            let dir = ent.path();
            if !dir.is_dir() || dir.join("disable").exists() || dir.join("remove").exists() {
                continue;
            }
            let prop_path = dir.join("module.prop");
            let kv = read_prop_file_kv(&prop_path);
            let id = kv.get("id").map(|s| s.to_ascii_lowercase()).unwrap_or_default();
            let name = kv.get("name").cloned().unwrap_or_default();
            let name_l = name.to_ascii_lowercase();
            let ver = kv.get("version").cloned().unwrap_or_default();

            if id == "rezygisk" || name_l.contains("rezygisk") {
                return ("ReZygisk".into(), ver);
            }
            if id == "zygisksu" || name_l.contains("zygisk next") || name_l.contains("zygisknext") {
                return ("Zygisk Next".into(), ver);
            }
            if id.contains("neozygisk") || name_l.contains("neozygisk") {
                return ("NeoZygisk".into(), ver);
            }
        }
    }

    if root_mgr == "Magisk" {
        let sql = run_cmd_stdout(
            "magisk",
            &["--sqlite", "SELECT value FROM settings WHERE key='zygisk';"],
        );
        if sql.contains("value=1") {
            return ("Magisk Zygisk".into(), "Built-in".into());
        }
        return ("Magisk Zygisk (Disabled)".into(), String::new());
    }

    ("Not Detected".into(), String::new())
}

pub fn collect_host_device_info() -> HostDeviceInfo {
    let model = first_non_empty(&["ro.product.model", "ro.product.vendor.model"]);
    let brand = first_non_empty(&["ro.product.brand", "ro.product.vendor.brand"]);
    let market_name = first_non_empty(&[
        "ro.product.marketname",
        "ro.vivo.market.name",
        "ro.vendor.oplus.market.name",
        "ro.config.marketing_name",
    ]);
    let device = first_non_empty(&["ro.product.device", "ro.product.vendor.device"]);
    let board = first_non_empty(&["ro.product.board", "ro.board.platform"]);
    let soc_model = first_non_empty(&[
        "ro.soc.model",
        "ro.mediatek.platform",
        "ro.chipname",
        "ro.board.platform",
    ]);
    let android_release = first_non_empty(&["ro.build.version.release", "ro.system.build.version.release"]);
    let sdk_int = first_non_empty(&["ro.build.version.sdk", "ro.system.build.version.sdk"]);
    let security_patch = first_non_empty(&["ro.build.version.security_patch", "ro.vendor.build.security_patch"]);

    let kernel_release = {
        let k = read_file_str("/proc/sys/kernel/osrelease").trim().to_string();
        if !k.is_empty() { k } else { run_cmd_stdout("uname", &["-r"]) }
    };
    let arch = {
        let a = run_cmd_stdout("uname", &["-m"]);
        if !a.is_empty() { a } else { "aarch64".into() }
    };

    let cpu_cores = parse_cpu_cores();
    let (live_clusters, live_max_freq) = read_live_cpu_clusters();
    let topo = infer_topology(&board, &soc_model);

    let cpu_hardware = {
        let h = parse_cpu_hardware_from_proc();
        if !h.is_empty() {
            h
        } else if !soc_model.is_empty() {
            soc_model.clone()
        } else {
            topo.cpu_hw.clone()
        }
    };

    let gpu_renderer = detect_gpu_renderer(&board, &soc_model);
    let ram_gb = detect_ram_gb();
    let (root_manager, root_version) = detect_root_manager();
    let (zygisk_impl, zygisk_version) = detect_zygisk_impl(&root_manager);

    HostDeviceInfo {
        model: if !model.is_empty() { model } else { "Unknown".into() },
        brand: if !brand.is_empty() { brand } else { "Android".into() },
        market_name,
        device,
        android_release: if !android_release.is_empty() { android_release } else { "?".into() },
        sdk_int: if !sdk_int.is_empty() { sdk_int } else { "?".into() },
        security_patch,
        kernel_release,
        arch,
        soc_model: if !soc_model.is_empty() { soc_model } else { topo.soc_machine },
        cpu_hardware,
        cpu_cores,
        cpu_max_freq: if live_max_freq > 0 { live_max_freq } else { topo.max_freq_khz },
        cpu_clusters: if !live_clusters.is_empty() { live_clusters } else { topo.cpu_clusters.into() },
        gpu_renderer,
        ram_gb,
        root_manager,
        root_version,
        zygisk_impl,
        zygisk_version,
    }
}

/// 一键克隆当前真机为完整全息模板（采集全部真实 getprop + cpufreq 频点表 + 内存 + GPU）
pub fn clone_host_as_template(custom_id: Option<&str>) -> Option<DeviceTemplate> {
    let raw_getprop = run_cmd_stdout("getprop", &[]);
    if raw_getprop.is_empty() {
        return None;
    }

    let mut prop_lines = Vec::with_capacity(256);
    for line in raw_getprop.lines() {
        let tr = line.trim();
        if let Some(rest) = tr.strip_prefix('[') {
            if let Some((k, v_part)) = rest.split_once("]: [") {
                if let Some(v) = v_part.strip_suffix(']') {
                    if !k.is_empty() && !v.is_empty() {
                        prop_lines.push(format!("{}={}", k, v));
                    }
                }
            }
        }
    }

    let joined = prop_lines.join("\n");
    let mut tpl = parse_build_prop(&joined, custom_id)?;
    let info = collect_host_device_info();

    tpl.cpu_cores = info.cpu_cores;
    tpl.cpu_hardware = info.cpu_hardware;
    if !info.cpu_clusters.is_empty() {
        tpl.cpu_clusters = info.cpu_clusters;
    }
    if info.cpu_max_freq > 0 {
        tpl.cpu_max_freq = info.cpu_max_freq;
    }
    if info.ram_gb > 0 {
        tpl.ram_gb = info.ram_gb;
    }
    if !info.kernel_release.is_empty() {
        tpl.kernel_release = info.kernel_release;
    }
    let uname_v = run_cmd_stdout("uname", &["-v"]);
    if !uname_v.is_empty() {
        tpl.kernel_version = uname_v;
    }

    let soc_id = read_file_str("/sys/devices/soc0/soc_id").trim().to_string();
    if !soc_id.is_empty() {
        tpl.soc_id = soc_id;
    }
    let soc_machine = read_file_str("/sys/devices/soc0/machine").trim().to_string();
    if !soc_machine.is_empty() {
        tpl.soc_machine = soc_machine;
    }

    if !info.gpu_renderer.is_empty() {
        tpl.gl_renderer = info.gpu_renderer.clone();
        tpl.vk_device_name = info.gpu_renderer;
    }

    tpl.normalize_vulkan_fields();
    Some(tpl)
}

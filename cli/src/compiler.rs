// SPDX-License-Identifier: GPL-3.0-only
// Copyright (c) 2025-2026 jjh138792
// MirageForge: https://github.com/jjh138792/MirageForge

use crate::models::{sanitize_prop_mode, sanitize_refresh_rate, AppRoute, DeviceTemplate};

pub fn compile_modular_conf(tpl: &DeviceTemplate, route: &AppRoute) -> String {
    let mut normalized = tpl.clone();
    normalized.normalize_vulkan_fields();
    let tpl = &normalized;

    let mut lines: Vec<String> = Vec::with_capacity(160);

    let push_kv = |out: &mut Vec<String>, k: &str, v: &str| {
        if !k.is_empty() && !v.is_empty() {
            out.push(format!("{}={}", k, v));
        }
    };

    let prop_mode = if route.build {
        sanitize_prop_mode(&route.prop_mode)
    } else {
        "hook".into()
    };
    let any_vfs = route.cpu || route.mem || route.vfs_nodes;
    let effective_fps = sanitize_refresh_rate(route.refresh_rate);
    let enable_hdr = (route.build || route.gl) && route.hdr_spoof;

    // 1. 模块级总控元数据（严格零改动门控 + 三引擎属性模式声明）
    push_kv(&mut lines, "META:ENABLE_BUILD", if route.build { "1" } else { "0" });
    push_kv(&mut lines, "META:PROP_MODE", &prop_mode);
    push_kv(
        &mut lines,
        "META:ENABLE_COW_PROP",
        if route.build && prop_mode == "cow" { "1" } else { "0" },
    );
    push_kv(&mut lines, "META:ENABLE_GL", if route.gl { "1" } else { "0" });
    push_kv(&mut lines, "META:ENABLE_HDR", if enable_hdr { "1" } else { "0" });
    push_kv(&mut lines, "META:ENABLE_VFS", if any_vfs { "1" } else { "0" });
    push_kv(&mut lines, "META:ENABLE_CPU", if route.cpu { "1" } else { "0" });
    push_kv(&mut lines, "META:ENABLE_MEM", if route.mem { "1" } else { "0" });
    push_kv(&mut lines, "META:ENABLE_VFS_NODES", if route.vfs_nodes { "1" } else { "0" });
    push_kv(&mut lines, "META:ENABLE_MOUNT_NS", if any_vfs && route.mount_ns { "1" } else { "0" });
    push_kv(&mut lines, "META:ENABLE_PROBE", if route.probe { "1" } else { "0" });
    push_kv(
        &mut lines,
        "META:FORCE_SPOOF_SDK",
        if route.build && route.sdk_spoof { "1" } else { "0" },
    );
    if (route.build || route.gl) && effective_fps >= 90 {
        push_kv(&mut lines, "META:REFRESH_RATE", &effective_fps.to_string());
    }

    // 2. OpenGL ES 与独立 Vulkan 渲染模块 (仅在开启 GPU 开关时输出)
    if route.gl {
        push_kv(&mut lines, "GLES:VENDOR", &tpl.gl_vendor);
        push_kv(&mut lines, "GLES:RENDERER", &tpl.gl_renderer);
        push_kv(&mut lines, "GLES:VERSION", &tpl.gl_version);
        if effective_fps >= 90 {
            push_kv(&mut lines, "GLES:REFRESH_RATE", &effective_fps.to_string());
        }

        push_kv(&mut lines, "VULKAN:DEVICE_NAME", &tpl.vk_device_name);
        push_kv(&mut lines, "VULKAN:VENDOR_ID", &tpl.vk_vendor_id);
        push_kv(&mut lines, "VULKAN:DEVICE_ID", &tpl.vk_device_id);
        push_kv(&mut lines, "VULKAN:API_VERSION", &tpl.vk_api_version);
        push_kv(&mut lines, "VULKAN:DRIVER_VERSION", &tpl.vk_driver_version);
        push_kv(&mut lines, "VULKAN:DRIVER_NAME", &tpl.vk_driver_name);
        push_kv(&mut lines, "VULKAN:DRIVER_INFO", &tpl.vk_driver_info);
    }

    // 3. 硬件与文件节点拓扑模块（支持单应用局部覆写 RAM 与 CPU 核数）
    let ram_gb = route
        .overrides
        .ram_gb
        .filter(|&v| v > 0)
        .unwrap_or_else(|| if tpl.ram_gb > 0 { tpl.ram_gb } else { 16 });
    let cpu_cores = route
        .overrides
        .cpu_cores
        .filter(|&v| v > 0)
        .unwrap_or(tpl.cpu_cores);
    let ddr_type = if tpl.ddr_type.is_empty() { "LPDDR5X" } else { &tpl.ddr_type };
    let ddr_vendor = if tpl.ddr_vendor.is_empty() { "SKhynix" } else { &tpl.ddr_vendor };

    if route.cpu || route.vfs_nodes {
        push_kv(&mut lines, "VFS:CPU_HARDWARE", &tpl.cpu_hardware);
        push_kv(&mut lines, "VFS:CPU_ARCH", "aarch64");
        if cpu_cores > 0 {
            push_kv(&mut lines, "VFS:CPU_CORES", &cpu_cores.to_string());
        }
        push_kv(&mut lines, "VFS:CPU_MAX_FREQ", &tpl.cpu_max_freq.to_string());
        push_kv(&mut lines, "VFS:CPU_CLUSTERS", &tpl.cpu_clusters);
        push_kv(&mut lines, "VFS:SOC_ID", &tpl.soc_id);
        push_kv(&mut lines, "VFS:SOC_MACHINE", &tpl.soc_machine);
        push_kv(&mut lines, "VFS:KERNEL_RELEASE", &tpl.kernel_release);
        push_kv(&mut lines, "VFS:KERNEL_VERSION", &tpl.kernel_version);
    }

    if route.mem {
        push_kv(&mut lines, "VFS:RAM_GB", &ram_gb.to_string());
        push_kv(&mut lines, "VFS:DDR_TYPE", ddr_type);
        push_kv(&mut lines, "VFS:DDR_VENDOR", ddr_vendor);
    }

    // 4. 系统属性三引擎全量属性与 Java Build 反射表（Hook / COW / MountNS 均同步生效）
    if route.build {
        let abis = if tpl.supported_abis.is_empty() {
            "arm64-v8a,armeabi-v7a,armeabi"
        } else {
            &tpl.supported_abis
        };
        let build_utc = tpl.build_time.parse::<i64>().unwrap_or(1736899200000) / 1000;

        let display_id = if !tpl.display_id.is_empty() {
            &tpl.display_id
        } else {
            &tpl.build_id
        };
        let incremental = if !tpl.incremental.is_empty() {
            &tpl.incremental
        } else {
            &tpl.build_id
        };
        let build_host = if !tpl.build_host.is_empty() {
            &tpl.build_host
        } else {
            "pangu-build-component-system"
        };
        let build_user = if !tpl.build_user.is_empty() {
            &tpl.build_user
        } else {
            "builder"
        };

        push_kv(&mut lines, "BUILD:MODEL", &tpl.model);
        push_kv(&mut lines, "BUILD:BRAND", &tpl.brand);
        push_kv(&mut lines, "BUILD:MANUFACTURER", &tpl.manufacturer);
        push_kv(&mut lines, "BUILD:DEVICE", &tpl.device);
        push_kv(&mut lines, "BUILD:PRODUCT", &tpl.product);
        push_kv(&mut lines, "BUILD:HARDWARE", &tpl.hardware);
        push_kv(&mut lines, "BUILD:BOARD", &tpl.board);
        push_kv(&mut lines, "BUILD:BOOTLOADER", &tpl.bootloader);
        push_kv(&mut lines, "BUILD:SOC_MANUFACTURER", &tpl.soc_manufacturer);
        push_kv(&mut lines, "BUILD:SOC_MODEL", &tpl.soc_model);
        push_kv(&mut lines, "BUILD:ID", &tpl.build_id);
        push_kv(&mut lines, "BUILD:DISPLAY", display_id);
        push_kv(&mut lines, "BUILD:HOST", build_host);
        push_kv(&mut lines, "BUILD:USER", build_user);
        push_kv(&mut lines, "BUILD:FINGERPRINT", &tpl.fingerprint);
        push_kv(&mut lines, "BUILD:SUPPORTED_ABIS", abis);
        push_kv(&mut lines, "BUILD:SUPPORTED_64_BIT_ABIS", "arm64-v8a");
        push_kv(&mut lines, "BUILD:SUPPORTED_32_BIT_ABIS", "armeabi-v7a,armeabi");
        push_kv(&mut lines, "BUILD:TAGS", "release-keys");
        push_kv(&mut lines, "BUILD:TYPE", "user");
        push_kv(&mut lines, "BUILD:TIME", &tpl.build_time);

        push_kv(&mut lines, "VERSION:INCREMENTAL", incremental);
        push_kv(&mut lines, "VERSION:SECURITY_PATCH", &tpl.security_patch);
        if route.sdk_spoof {
            push_kv(&mut lines, "VERSION:CODENAME", "REL");
            push_kv(&mut lines, "VERSION:PREVIEW_SDK_INT", "0");
            push_kv(&mut lines, "VERSION:RELEASE", &tpl.release);
            push_kv(&mut lines, "VERSION:SDK_INT", &tpl.sdk_int);
            push_kv(&mut lines, "VERSION:DEVICE_INITIAL_SDK_INT", &tpl.sdk_int);
        }

        for part in &["", "system.", "vendor.", "odm.", "product.", "system_ext."] {
            push_kv(&mut lines, &format!("ro.product.{}model", part), &tpl.model);
            push_kv(&mut lines, &format!("ro.product.{}brand", part), &tpl.brand);
            push_kv(&mut lines, &format!("ro.product.{}manufacturer", part), &tpl.manufacturer);
            push_kv(&mut lines, &format!("ro.product.{}device", part), &tpl.device);
            push_kv(&mut lines, &format!("ro.product.{}name", part), &tpl.product);
            push_kv(&mut lines, &format!("ro.{}build.id", part), &tpl.build_id);
            push_kv(&mut lines, &format!("ro.{}build.display.id", part), display_id);
            push_kv(&mut lines, &format!("ro.{}build.version.incremental", part), incremental);
            push_kv(&mut lines, &format!("ro.{}build.fingerprint", part), &tpl.fingerprint);
        }
        push_kv(&mut lines, "ro.product.build.id", &tpl.build_id);
        push_kv(&mut lines, "ro.product.build.fingerprint", &tpl.fingerprint);
        push_kv(&mut lines, "ro.product.build.version.incremental", incremental);
        push_kv(&mut lines, "ro.build.product", &tpl.device);
        push_kv(&mut lines, "ro.product.board", &tpl.board);
        push_kv(&mut lines, "ro.board.platform", &tpl.board);
        push_kv(&mut lines, "ro.hardware", &tpl.hardware);
        push_kv(&mut lines, "ro.boot.hardware", &tpl.hardware);
        push_kv(&mut lines, "ro.bootloader", &tpl.bootloader);
        push_kv(&mut lines, "ro.boot.bootloader", &tpl.bootloader);
        push_kv(&mut lines, "ro.soc.manufacturer", &tpl.soc_manufacturer);
        push_kv(&mut lines, "ro.soc.model", &tpl.soc_model);
        push_kv(&mut lines, "ro.product.cpu.abi", "arm64-v8a");
        push_kv(&mut lines, "ro.product.cpu.abilist", abis);
        push_kv(&mut lines, "ro.product.cpu.abilist64", "arm64-v8a");
        push_kv(&mut lines, "ro.product.cpu.abilist32", "armeabi-v7a,armeabi");
        push_kv(&mut lines, "ro.build.date.utc", &build_utc.to_string());
        push_kv(&mut lines, "ro.build.host", build_host);
        push_kv(&mut lines, "ro.build.user", build_user);
        push_kv(&mut lines, "ro.build.version.security_patch", &tpl.security_patch);
        push_kv(&mut lines, "ro.vendor.build.security_patch", &tpl.security_patch);
        push_kv(&mut lines, "ro.build.tags", "release-keys");
        push_kv(&mut lines, "ro.build.type", "user");

        push_kv(&mut lines, "ro.boot.vbmeta.device_state", "locked");
        push_kv(&mut lines, "ro.boot.vbmeta.avb_version", "1.2");
        push_kv(&mut lines, "ro.boot.vbmeta.hash_alg", "sha256");
        push_kv(&mut lines, "ro.boot.verifiedbootstate", "green");
        push_kv(&mut lines, "ro.boot.flash.locked", "1");
        push_kv(&mut lines, "ro.boot.veritymode", "enforcing");
        push_kv(&mut lines, "ro.boot.warranty_bit", "0");
        push_kv(&mut lines, "ro.warranty_bit", "0");
        push_kv(&mut lines, "ro.build.selinux", "1");
        push_kv(&mut lines, "ro.debuggable", "0");
        push_kv(&mut lines, "ro.secure", "1");
        push_kv(&mut lines, "ro.adb.secure", "1");
        push_kv(&mut lines, "sys.oem_unlock_allowed", "0");
        push_kv(&mut lines, "sys.usb.state", "none");
        push_kv(&mut lines, "sys.usb.config", "none");
        push_kv(&mut lines, "init.svc.adbd", "stopped");
        push_kv(&mut lines, "ro.sys.cloud.magisk", "0");
        push_kv(&mut lines, "persist.sys.cloud.root.global", "0");
        push_kv(&mut lines, "ro.sys.cloud.root.global", "0");
        push_kv(&mut lines, "ro.boot.hardware.sku", &tpl.device);
        push_kv(&mut lines, "ro.boot.product.hardware.sku", &tpl.device);

        if route.mem {
            let ram_str = format!("{}GB", ram_gb);
            let ram_mb_str = (ram_gb * 1024).to_string();
            let ddr_full = format!("{},{},{}", ram_str, ddr_vendor, ddr_type);
            push_kv(&mut lines, "ro.boot.hardware.ddr", &ddr_full);
            push_kv(&mut lines, "ro.boot.ddr_size", &ram_str);
            push_kv(&mut lines, "ro.boot.ddr_type", ddr_type);
            push_kv(&mut lines, "ro.boot.ddr_info", &ddr_full);
            push_kv(&mut lines, "ro.boot.ramsize", &ram_str);
            push_kv(&mut lines, "ro.boot.memsize", &ram_str);
            push_kv(&mut lines, "ro.hardware.ddr", &ddr_full);
            push_kv(&mut lines, "ro.vendor.hw.ram", &ram_str);
            push_kv(&mut lines, "ro.product.ram", &ram_str);
            push_kv(&mut lines, "ro.config.ram_size", &ram_mb_str);
            push_kv(&mut lines, "ro.sys.ram_size", &ram_str);
            push_kv(&mut lines, "persist.sys.ram_size", &ram_str);
        }

        if route.gl {
            if !tpl.vk_device_name.is_empty() {
                push_kv(&mut lines, "persist.sys.cloud.gpu.vk_devicename", &tpl.vk_device_name);
            }
            if !tpl.gl_renderer.is_empty() {
                push_kv(&mut lines, "persist.sys.cloud.gpu.gl_renderer", &tpl.gl_renderer);
                push_kv(&mut lines, "persist.sys.gpu.gl_renderer", &tpl.gl_renderer);
            }
            if !tpl.gl_vendor.is_empty() {
                push_kv(&mut lines, "persist.sys.cloud.gpu.gl_vendor", &tpl.gl_vendor);
                push_kv(&mut lines, "persist.sys.gpu.gl_vendor", &tpl.gl_vendor);
            }
        }

        if route.sdk_spoof {
            for part in &["", "system.", "vendor.", "odm.", "product.", "system_ext."] {
                push_kv(&mut lines, &format!("ro.{}build.version.release", part), &tpl.release);
                push_kv(&mut lines, &format!("ro.{}build.version.sdk", part), &tpl.sdk_int);
            }
        }

        let append_custom_block = |out: &mut Vec<String>, block: &str| {
            for raw in block.lines() {
                let tr = raw.trim();
                if tr.is_empty() || tr.starts_with('#') {
                    continue;
                }
                if let Some(eq) = tr.find('=') {
                    let k = tr[..eq].trim();
                    if k.is_empty() || k.contains("lcd_density") || k.ends_with(".dpi") {
                        continue;
                    }
                    if !route.sdk_spoof
                        && (k.contains("build.version.sdk")
                            || k.contains("build.version.release")
                            || k.contains("first_api_level")
                            || k.contains("preview_sdk")
                            || k.contains("version.codename"))
                    {
                        continue;
                    }
                    out.push(tr.to_string());
                }
            }
        };

        append_custom_block(&mut lines, &tpl.custom_props);
        if !route.overrides.custom_props.is_empty() {
            append_custom_block(&mut lines, &route.overrides.custom_props);
        }
    }

    lines.join("\n") + "\n"
}

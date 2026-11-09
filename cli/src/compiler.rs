use crate::models::{AppRoute, DeviceTemplate};

pub fn compile_modular_conf(tpl: &DeviceTemplate, route: &AppRoute) -> String {
    let mut lines: Vec<String> = Vec::with_capacity(96);

    let push_kv = |out: &mut Vec<String>, k: &str, v: &str| {
        if !k.is_empty() && !v.is_empty() {
            out.push(format!("{}={}", k, v));
        }
    };

    // 1. 模块级总控元数据
    push_kv(&mut lines, "META:ENABLE_BUILD", if route.build { "1" } else { "0" });
    push_kv(&mut lines, "META:ENABLE_GL", if route.gl { "1" } else { "0" });
    push_kv(&mut lines, "META:ENABLE_VFS", if route.vfs { "1" } else { "0" });
    push_kv(&mut lines, "META:ENABLE_PROBE", if route.probe { "1" } else { "0" });
    push_kv(&mut lines, "META:FORCE_SPOOF_SDK", if route.sdk_spoof { "1" } else { "0" });

    // 2. 图形模块 (仅在开启 GL 开关时输出，关闭时 0 输出)
    if route.gl {
        push_kv(&mut lines, "GLES:VENDOR", &tpl.gl_vendor);
        push_kv(&mut lines, "GLES:RENDERER", &tpl.gl_renderer);
        push_kv(&mut lines, "GLES:VERSION", &tpl.gl_version);
    }

    // 3. VFS 与硬件拓扑模块 (仅在开启 VFS 开关时输出，关闭时 0 输出)
    let ram_gb = if tpl.ram_gb > 0 { tpl.ram_gb } else { 16 };
    let ddr_type = if tpl.ddr_type.is_empty() { "LPDDR5X" } else { &tpl.ddr_type };
    let ddr_vendor = if tpl.ddr_vendor.is_empty() { "SKhynix" } else { &tpl.ddr_vendor };

    if route.vfs {
        push_kv(&mut lines, "VFS:CPU_HARDWARE", &tpl.cpu_hardware);
        push_kv(&mut lines, "VFS:CPU_ARCH", "aarch64");
        push_kv(&mut lines, "VFS:CPU_CORES", &tpl.cpu_cores.to_string());
        push_kv(&mut lines, "VFS:CPU_MAX_FREQ", &tpl.cpu_max_freq.to_string());
        push_kv(&mut lines, "VFS:CPU_CLUSTERS", &tpl.cpu_clusters);
        push_kv(&mut lines, "VFS:RAM_GB", &ram_gb.to_string());
        push_kv(&mut lines, "VFS:DDR_TYPE", ddr_type);
        push_kv(&mut lines, "VFS:DDR_VENDOR", ddr_vendor);
        push_kv(&mut lines, "VFS:SOC_ID", &tpl.soc_id);
        push_kv(&mut lines, "VFS:SOC_MACHINE", &tpl.soc_machine);
        push_kv(&mut lines, "VFS:KERNEL_RELEASE", &tpl.kernel_release);
        push_kv(&mut lines, "VFS:KERNEL_VERSION", &tpl.kernel_version);
    }

    // 4. Build 反射与 Bionic 属性模块 (仅在开启 Build 开关时输出，关闭时 0 输出)
    if route.build {
        let abis = if tpl.supported_abis.is_empty() {
            "arm64-v8a,armeabi-v7a,armeabi"
        } else {
            &tpl.supported_abis
        };
        let build_utc = tpl.build_time.parse::<i64>().unwrap_or(1736899200000) / 1000;
        let ram_str = format!("{}GB", ram_gb);
        let ram_mb_str = (ram_gb * 1024).to_string();
        let ddr_full = format!("{},{},{}", ram_str, ddr_vendor, ddr_type);

        // Java Build 反射字段
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
        push_kv(&mut lines, "BUILD:FINGERPRINT", &tpl.fingerprint);
        push_kv(&mut lines, "BUILD:SUPPORTED_ABIS", abis);
        push_kv(&mut lines, "BUILD:SUPPORTED_64_BIT_ABIS", "arm64-v8a");
        push_kv(&mut lines, "BUILD:SUPPORTED_32_BIT_ABIS", "armeabi-v7a,armeabi");
        push_kv(&mut lines, "BUILD:TAGS", "release-keys");
        push_kv(&mut lines, "BUILD:TYPE", "user");
        push_kv(&mut lines, "BUILD:TIME", &tpl.build_time);

        // Java Build$VERSION 反射字段
        push_kv(&mut lines, "VERSION:SECURITY_PATCH", &tpl.security_patch);
        push_kv(&mut lines, "VERSION:PREVIEW_SDK_INT", "0");
        if route.sdk_spoof {
            push_kv(&mut lines, "VERSION:RELEASE", &tpl.release);
            push_kv(&mut lines, "VERSION:SDK_INT", &tpl.sdk_int);
            push_kv(&mut lines, "VERSION:DEVICE_INITIAL_SDK_INT", &tpl.sdk_int);
        }

        // Bionic SystemProperties 全分区覆盖
        for part in &["", "system.", "vendor.", "odm.", "product."] {
            push_kv(&mut lines, &format!("ro.product.{}model", part), &tpl.model);
            push_kv(&mut lines, &format!("ro.product.{}brand", part), &tpl.brand);
            push_kv(&mut lines, &format!("ro.product.{}manufacturer", part), &tpl.manufacturer);
            push_kv(&mut lines, &format!("ro.product.{}device", part), &tpl.device);
            push_kv(&mut lines, &format!("ro.product.{}name", part), &tpl.product);
        }
        push_kv(&mut lines, "ro.build.product", &tpl.device);
        push_kv(&mut lines, "ro.product.board", &tpl.board);
        push_kv(&mut lines, "ro.board.platform", &tpl.board);
        push_kv(&mut lines, "ro.hardware", &tpl.hardware);
        push_kv(&mut lines, "ro.boot.hardware", &tpl.hardware);
        push_kv(&mut lines, "ro.soc.manufacturer", &tpl.soc_manufacturer);
        push_kv(&mut lines, "ro.soc.model", &tpl.soc_model);
        push_kv(&mut lines, "ro.product.cpu.abi", "arm64-v8a");
        push_kv(&mut lines, "ro.product.cpu.abilist", abis);
        push_kv(&mut lines, "ro.product.cpu.abilist64", "arm64-v8a");
        push_kv(&mut lines, "ro.product.cpu.abilist32", "armeabi-v7a,armeabi");
        push_kv(&mut lines, "ro.build.id", &tpl.build_id);
        push_kv(&mut lines, "ro.build.date.utc", &build_utc.to_string());
        push_kv(&mut lines, "ro.build.version.security_patch", &tpl.security_patch);
        push_kv(&mut lines, "ro.build.fingerprint", &tpl.fingerprint);
        push_kv(&mut lines, "ro.product.build.fingerprint", &tpl.fingerprint);
        push_kv(&mut lines, "ro.system.build.fingerprint", &tpl.fingerprint);
        push_kv(&mut lines, "ro.vendor.build.fingerprint", &tpl.fingerprint);
        push_kv(&mut lines, "ro.odm.build.fingerprint", &tpl.fingerprint);
        push_kv(&mut lines, "ro.build.tags", "release-keys");
        push_kv(&mut lines, "ro.build.type", "user");
        push_kv(&mut lines, "ro.boot.vbmeta.device_state", "locked");
        push_kv(&mut lines, "ro.boot.verifiedbootstate", "green");
        push_kv(&mut lines, "ro.boot.flash.locked", "1");
        push_kv(&mut lines, "ro.boot.veritymode", "enforcing");
        push_kv(&mut lines, "ro.debuggable", "0");
        push_kv(&mut lines, "ro.secure", "1");

        // 物理 DDR 对称属性池
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

        if route.sdk_spoof {
            push_kv(&mut lines, "ro.build.version.release", &tpl.release);
            push_kv(&mut lines, "ro.build.version.sdk", &tpl.sdk_int);
            push_kv(&mut lines, "ro.product.build.version.sdk", &tpl.sdk_int);
            push_kv(&mut lines, "ro.system.build.version.sdk", &tpl.sdk_int);
        }

        for raw in tpl.custom_props.lines() {
            let tr = raw.trim();
            if !tr.is_empty() && !tr.starts_with('#') && tr.contains('=') {
                lines.push(tr.to_string());
            }
        }
    }

    lines.join("\n") + "\n"
}

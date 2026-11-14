use crate::models::{AppRoute, DeviceTemplate};

pub fn compile_modular_conf(tpl: &DeviceTemplate, route: &AppRoute) -> String {
    let mut lines: Vec<String> = Vec::with_capacity(160);

    let push_kv = |out: &mut Vec<String>, k: &str, v: &str| {
        if !k.is_empty() && !v.is_empty() {
            out.push(format!("{}={}", k, v));
        }
    };

    // 计算生效的 DPI：优先使用应用路由单独指定的 DPI，其次使用模板自带的 DEFAULT_DPI
    let effective_dpi = if (120..=960).contains(&route.dpi) {
        route.dpi
    } else if (120..=960).contains(&tpl.default_dpi) {
        tpl.default_dpi
    } else {
        0
    };

    // 1. 模块级总控元数据
    push_kv(&mut lines, "META:ENABLE_BUILD", if route.build { "1" } else { "0" });
    push_kv(&mut lines, "META:ENABLE_GL", if route.gl { "1" } else { "0" });
    push_kv(&mut lines, "META:ENABLE_VFS", if route.vfs { "1" } else { "0" });
    push_kv(&mut lines, "META:ENABLE_PROBE", if route.probe { "1" } else { "0" });
    push_kv(&mut lines, "META:FORCE_SPOOF_SDK", if route.sdk_spoof { "1" } else { "0" });
    if effective_dpi > 0 {
        push_kv(&mut lines, "META:APP_DPI", &effective_dpi.to_string());
    }

    // 2. 图形模块 (仅在开启 GL 开关时输出)
    if route.gl {
        push_kv(&mut lines, "GLES:VENDOR", &tpl.gl_vendor);
        push_kv(&mut lines, "GLES:RENDERER", &tpl.gl_renderer);
        push_kv(&mut lines, "GLES:VERSION", &tpl.gl_version);
    }

    // 3. VFS 与硬件拓扑模块 (仅在开启 VFS 开关时输出)
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

    // 4. Build 反射、全分区属性与 SELinux 受限属性区模块 (仅在开启 Build 开关时输出)
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
        let soc_model = if !tpl.soc_model.is_empty() {
            &tpl.soc_model
        } else {
            &tpl.soc_machine
        };

        // Java android.os.Build 反射字段 (含 BOARD, SOC_MODEL, DISPLAY, HOST, USER)
        push_kv(&mut lines, "BUILD:MODEL", &tpl.model);
        push_kv(&mut lines, "BUILD:BRAND", &tpl.brand);
        push_kv(&mut lines, "BUILD:MANUFACTURER", &tpl.manufacturer);
        push_kv(&mut lines, "BUILD:DEVICE", &tpl.device);
        push_kv(&mut lines, "BUILD:PRODUCT", &tpl.product);
        push_kv(&mut lines, "BUILD:HARDWARE", &tpl.hardware);
        push_kv(&mut lines, "BUILD:BOARD", &tpl.board);
        push_kv(&mut lines, "BUILD:BOOTLOADER", &tpl.bootloader);
        push_kv(&mut lines, "BUILD:SOC_MANUFACTURER", &tpl.soc_manufacturer);
        push_kv(&mut lines, "BUILD:SOC_MODEL", soc_model);
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

        // Java android.os.Build$VERSION 反射字段 (含 INCREMENTAL, SECURITY_PATCH)
        push_kv(&mut lines, "VERSION:INCREMENTAL", incremental);
        push_kv(&mut lines, "VERSION:SECURITY_PATCH", &tpl.security_patch);
        push_kv(&mut lines, "VERSION:CODENAME", "REL");
        push_kv(&mut lines, "VERSION:PREVIEW_SDK_INT", "0");
        if route.sdk_spoof {
            push_kv(&mut lines, "VERSION:RELEASE", &tpl.release);
            push_kv(&mut lines, "VERSION:SDK_INT", &tpl.sdk_int);
            push_kv(&mut lines, "VERSION:DEVICE_INITIAL_SDK_INT", &tpl.sdk_int);
        }

        // Bionic SystemProperties 全分区覆盖 (system / vendor / odm / product / system_ext)
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
        push_kv(&mut lines, "ro.target.product", &tpl.device);
        push_kv(&mut lines, "ro.product.board", &tpl.board);
        push_kv(&mut lines, "ro.board.platform", &tpl.board);
        push_kv(&mut lines, "ro.hardware", &tpl.hardware);
        push_kv(&mut lines, "ro.boot.hardware", &tpl.hardware);
        push_kv(&mut lines, "ro.bootloader", &tpl.bootloader);
        push_kv(&mut lines, "ro.boot.bootloader", &tpl.bootloader);

        // 全方位 SoC 与处理器型号属性合围 (解决 DevCheck / AIDA64 通过厂商属性读取真实 CPU 的问题)
        push_kv(&mut lines, "ro.soc.manufacturer", &tpl.soc_manufacturer);
        push_kv(&mut lines, "ro.vendor.soc.manufacturer", &tpl.soc_manufacturer);
        push_kv(&mut lines, "ro.soc.model", soc_model);
        push_kv(&mut lines, "ro.vendor.soc.model", soc_model);
        push_kv(&mut lines, "ro.vendor.soc.model.part_name", soc_model);
        push_kv(&mut lines, "ro.vendor.soc.model.external_name", &tpl.cpu_hardware);
        push_kv(&mut lines, "ro.chipname", soc_model);
        push_kv(&mut lines, "ro.hardware.chipname", soc_model);
        push_kv(&mut lines, "ro.boot.chipname", soc_model);
        push_kv(&mut lines, "ro.unicpu.model", &tpl.cpu_hardware);
        push_kv(&mut lines, "ro.mediatek.platform", soc_model);
        push_kv(&mut lines, "ro.arch", "arm64");

        // 指令集与版本衍生属性
        push_kv(&mut lines, "ro.product.cpu.abi", "arm64-v8a");
        push_kv(&mut lines, "ro.product.cpu.abilist", abis);
        push_kv(&mut lines, "ro.product.cpu.abilist64", "arm64-v8a");
        push_kv(&mut lines, "ro.product.cpu.abilist32", "armeabi-v7a,armeabi");
        push_kv(&mut lines, "ro.dalvik.vm.isa.arm", "arm");
        push_kv(&mut lines, "ro.dalvik.vm.isa.arm64", "arm64");
        push_kv(&mut lines, "ro.build.date.utc", &build_utc.to_string());
        push_kv(&mut lines, "ro.build.host", build_host);
        push_kv(&mut lines, "ro.build.user", build_user);
        push_kv(&mut lines, "ro.build.version.security_patch", &tpl.security_patch);
        push_kv(&mut lines, "ro.vendor.build.security_patch", &tpl.security_patch);
        push_kv(&mut lines, "ro.build.version.codename", "REL");
        push_kv(&mut lines, "ro.build.version.all_codenames", "REL");
        push_kv(&mut lines, "ro.build.version.preview_sdk", "0");
        push_kv(&mut lines, "ro.product.first_api_level", &tpl.sdk_int);
        push_kv(&mut lines, "ro.vndk.version", &tpl.sdk_int);
        push_kv(&mut lines, "ro.build.tags", "release-keys");
        push_kv(&mut lines, "ro.build.type", "user");

        // SELinux app 域不可读/受限属性区虚拟化 (通过 ReadOnlyMmapPropArena 暴露为合法只读属性)
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
        push_kv(&mut lines, "ro.boot.hardware.sku", &tpl.device);
        push_kv(&mut lines, "ro.boot.product.hardware.sku", &tpl.device);

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

        if effective_dpi > 0 {
            let dpi_s = effective_dpi.to_string();
            push_kv(&mut lines, "ro.sf.lcd_density", &dpi_s);
            push_kv(&mut lines, "qemu.sf.lcd_density", &dpi_s);
            push_kv(&mut lines, "persist.sys.dpi", &dpi_s);
            push_kv(&mut lines, "ro.vendor.display.default_dpi", &dpi_s);
            push_kv(&mut lines, "ro.config.density_dpi", &dpi_s);
        }

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

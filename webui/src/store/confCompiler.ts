// SPDX-License-Identifier: GPL-3.0-only
// Copyright (c) 2025-2026 jjh138792
// MirageForge: https://github.com/jjh138792/MirageForge

import { sanitizeRefreshRate, type AppRoute, type DeviceTemplate } from './types';

export function normalizeTemplateVulkanFields(raw: Partial<DeviceTemplate>): DeviceTemplate {
  const tpl = { ...raw } as DeviceTemplate;
  if (!tpl.VK_DEVICE_NAME || !tpl.VK_DEVICE_NAME.trim()) {
    tpl.VK_DEVICE_NAME = tpl.GL_RENDERER || 'Adreno (TM) 830';
  }
  const token = `${tpl.GL_VENDOR || ''} ${tpl.GL_RENDERER || ''} ${tpl.VK_DEVICE_NAME || ''}`.toLowerCase();
  const isArm =
    (tpl.VK_VENDOR_ID || '').toLowerCase() === '0x13b5' ||
    token.includes('arm') ||
    token.includes('mali') ||
    token.includes('immortalis');
  const isSamsung =
    (tpl.VK_VENDOR_ID || '').toLowerCase() === '0x144d' || token.includes('xclipse');

  if (!tpl.VK_VENDOR_ID || !tpl.VK_VENDOR_ID.trim()) {
    tpl.VK_VENDOR_ID = isArm ? '0x13B5' : isSamsung ? '0x144D' : '0x5143';
  }
  if (!tpl.VK_DEVICE_ID || !tpl.VK_DEVICE_ID.trim()) {
    if (isArm) {
      tpl.VK_DEVICE_ID = token.includes('g925') ? '0x12870000' : '0x10860000';
    } else if (isSamsung) {
      tpl.VK_DEVICE_ID = '0x73A00000';
    } else if (token.includes('750')) {
      tpl.VK_DEVICE_ID = '0x43050000';
    } else if (token.includes('740')) {
      tpl.VK_DEVICE_ID = '0x43030000';
    } else {
      tpl.VK_DEVICE_ID = '0x43050A01';
    }
  }
  if (!tpl.VK_API_VERSION || !tpl.VK_API_VERSION.trim()) {
    tpl.VK_API_VERSION = '1.3.295';
  }
  if (!tpl.VK_DRIVER_VERSION || !tpl.VK_DRIVER_VERSION.trim()) {
    if (isArm) tpl.VK_DRIVER_VERSION = '49.0.0';
    else if (isSamsung) tpl.VK_DRIVER_VERSION = '24.1.0';
    else if (token.includes('750') || token.includes('740')) tpl.VK_DRIVER_VERSION = '512.762.0';
    else tpl.VK_DRIVER_VERSION = '512.800.0';
  }
  if (!tpl.VK_DRIVER_NAME || !tpl.VK_DRIVER_NAME.trim()) {
    tpl.VK_DRIVER_NAME = isArm
      ? 'ARM'
      : isSamsung
      ? 'Samsung Electronics'
      : 'Qualcomm Technologies Inc. Adreno Vulkan Driver';
  }
  if (!tpl.VK_DRIVER_INFO || !tpl.VK_DRIVER_INFO.trim()) {
    if (isArm) tpl.VK_DRIVER_INFO = 'r49p0-01eac0';
    else if (isSamsung) tpl.VK_DRIVER_INFO = 'Xclipse Vulkan Driver 1.3';
    else if (token.includes('750') || token.includes('740')) tpl.VK_DRIVER_INFO = 'Driver Build: 0762.0';
    else tpl.VK_DRIVER_INFO = 'Driver Build: 0800.0';
  }
  tpl.MAX_REFRESH_RATE = sanitizeRefreshRate(tpl.MAX_REFRESH_RATE) || 120;
  return tpl;
}

export function normalizeAppRoute(raw: Partial<AppRoute> & { tplId: string }): AppRoute {
  const build = raw.build !== false;
  const legacyVfs = raw.vfs !== false;
  const cpu = typeof raw.cpu === 'boolean' ? raw.cpu : legacyVfs;
  const mem = typeof raw.mem === 'boolean' ? raw.mem : legacyVfs;
  const vfsNodes = typeof raw.vfsNodes === 'boolean' ? raw.vfsNodes : legacyVfs;
  const vfs = cpu || mem || vfsNodes;
  return {
    tplId: String(raw.tplId),
    build,
    cowProp: Boolean(raw.cowProp) && build,
    gl: raw.gl !== false,
    vfs,
    cpu,
    mem,
    vfsNodes,
    probe: Boolean(raw.probe),
    sdkSpoof: Boolean(raw.sdkSpoof) && build,
    refreshRate: sanitizeRefreshRate(raw.refreshRate),
    mountNs: Boolean(raw.mountNs) && vfs,
  };
}

export function isRouteAnyEnabled(r: AppRoute): boolean {
  return Boolean(r.build || r.cowProp || r.gl || r.cpu || r.mem || r.vfsNodes || r.probe);
}

export function compileModularConfInTs(rawTpl: DeviceTemplate, rawRoute: AppRoute): string {
  const tpl = normalizeTemplateVulkanFields(rawTpl);
  const route = normalizeAppRoute(rawRoute);
  const lines: string[] = [];
  const push = (k: string, v: string | number | undefined) => {
    if (k && v !== undefined && String(v).length > 0) {
      lines.push(`${k}=${v}`);
    }
  };

  const anyProp = route.build;
  const anyVfs = route.cpu || route.mem || route.vfsNodes;
  const effectiveFps = sanitizeRefreshRate(route.refreshRate);

  push('META:ENABLE_BUILD', route.build ? '1' : '0');
  push('META:ENABLE_COW_PROP', route.build && route.cowProp ? '1' : '0');
  push('META:ENABLE_GL', route.gl ? '1' : '0');
  push('META:ENABLE_VFS', anyVfs ? '1' : '0');
  push('META:ENABLE_CPU', route.cpu ? '1' : '0');
  push('META:ENABLE_MEM', route.mem ? '1' : '0');
  push('META:ENABLE_VFS_NODES', route.vfsNodes ? '1' : '0');
  push('META:ENABLE_MOUNT_NS', anyVfs && route.mountNs ? '1' : '0');
  push('META:ENABLE_PROBE', route.probe ? '1' : '0');
  push('META:FORCE_SPOOF_SDK', route.build && route.sdkSpoof ? '1' : '0');
  if ((route.build || route.gl) && effectiveFps >= 90) {
    push('META:REFRESH_RATE', effectiveFps);
  }

  if (route.gl) {
    push('GLES:VENDOR', tpl.GL_VENDOR);
    push('GLES:RENDERER', tpl.GL_RENDERER);
    push('GLES:VERSION', tpl.GL_VERSION);
    if (effectiveFps >= 90) {
      push('GLES:REFRESH_RATE', effectiveFps);
    }

    push('VULKAN:DEVICE_NAME', tpl.VK_DEVICE_NAME);
    push('VULKAN:VENDOR_ID', tpl.VK_VENDOR_ID);
    push('VULKAN:DEVICE_ID', tpl.VK_DEVICE_ID);
    push('VULKAN:API_VERSION', tpl.VK_API_VERSION);
    push('VULKAN:DRIVER_VERSION', tpl.VK_DRIVER_VERSION);
    push('VULKAN:DRIVER_NAME', tpl.VK_DRIVER_NAME);
    push('VULKAN:DRIVER_INFO', tpl.VK_DRIVER_INFO);
  }

  const ramGb = tpl.RAM_GB > 0 ? tpl.RAM_GB : 16;
  const ddrType = tpl.DDR_TYPE || 'LPDDR5X';
  const ddrVendor = tpl.DDR_VENDOR || 'SKhynix';

  if (route.cpu || route.vfsNodes) {
    push('VFS:CPU_HARDWARE', tpl.CPU_HARDWARE);
    push('VFS:CPU_ARCH', 'aarch64');
    if (tpl.CPU_CORES > 0) push('VFS:CPU_CORES', tpl.CPU_CORES);
    push('VFS:CPU_MAX_FREQ', tpl.CPU_MAX_FREQ || 4320000);
    push('VFS:CPU_CLUSTERS', tpl.CPU_CLUSTERS);
    push('VFS:SOC_ID', tpl.SOC_ID);
    push('VFS:SOC_MACHINE', tpl.SOC_MACHINE);
    push('VFS:KERNEL_RELEASE', tpl.KERNEL_RELEASE);
    push('VFS:KERNEL_VERSION', tpl.KERNEL_VERSION);
  }

  if (route.mem) {
    push('VFS:RAM_GB', ramGb);
    push('VFS:DDR_TYPE', ddrType);
    push('VFS:DDR_VENDOR', ddrVendor);
  }

  if (anyProp) {
    const abis = tpl.SUPPORTED_ABIS || 'arm64-v8a,armeabi-v7a,armeabi';
    const buildUtc = Math.floor(
      (parseInt(tpl.BUILD_TIME || '1736899200000', 10) || 1736899200000) / 1000
    );
    const displayId = tpl.DISPLAY_ID || `${tpl.BUILD_ID} release-keys`;
    const incremental = tpl.INCREMENTAL || tpl.BUILD_ID;
    const host = tpl.BUILD_HOST || 'pangu-build-component-system';
    const user = tpl.BUILD_USER || 'builder';

    push('BUILD:MODEL', tpl.MODEL);
    push('BUILD:BRAND', tpl.BRAND);
    push('BUILD:MANUFACTURER', tpl.MANUFACTURER || tpl.BRAND);
    push('BUILD:DEVICE', tpl.DEVICE);
    push('BUILD:PRODUCT', tpl.PRODUCT || tpl.DEVICE);
    push('BUILD:HARDWARE', tpl.HARDWARE || 'qcom');
    push('BUILD:BOARD', tpl.BOARD);
    push('BUILD:BOOTLOADER', tpl.BOOTLOADER || 'unknown');
    push('BUILD:SOC_MANUFACTURER', tpl.SOC_MANUFACTURER);
    push('BUILD:SOC_MODEL', tpl.SOC_MODEL);
    push('BUILD:ID', tpl.BUILD_ID);
    push('BUILD:DISPLAY', displayId);
    push('BUILD:HOST', host);
    push('BUILD:USER', user);
    push('BUILD:FINGERPRINT', tpl.FINGERPRINT);
    push('BUILD:SUPPORTED_ABIS', abis);
    push('BUILD:SUPPORTED_64_BIT_ABIS', 'arm64-v8a');
    push('BUILD:SUPPORTED_32_BIT_ABIS', 'armeabi-v7a,armeabi');
    push('BUILD:TAGS', 'release-keys');
    push('BUILD:TYPE', 'user');
    push('BUILD:TIME', tpl.BUILD_TIME);

    push('VERSION:INCREMENTAL', incremental);
    push('VERSION:SECURITY_PATCH', tpl.SECURITY_PATCH);
    if (route.sdkSpoof) {
      push('VERSION:CODENAME', 'REL');
      push('VERSION:PREVIEW_SDK_INT', '0');
      push('VERSION:RELEASE', tpl.RELEASE);
      push('VERSION:SDK_INT', tpl.SDK_INT);
      push('VERSION:DEVICE_INITIAL_SDK_INT', tpl.SDK_INT);
    }

    for (const part of ['', 'system.', 'vendor.', 'odm.', 'product.', 'system_ext.']) {
      push(`ro.product.${part}model`, tpl.MODEL);
      push(`ro.product.${part}brand`, tpl.BRAND);
      push(`ro.product.${part}manufacturer`, tpl.MANUFACTURER || tpl.BRAND);
      push(`ro.product.${part}device`, tpl.DEVICE);
      push(`ro.product.${part}name`, tpl.PRODUCT || tpl.DEVICE);
      push(`ro.${part}build.id`, tpl.BUILD_ID);
      push(`ro.${part}build.display.id`, displayId);
      push(`ro.${part}build.version.incremental`, incremental);
      push(`ro.${part}build.fingerprint`, tpl.FINGERPRINT);
    }
    push('ro.product.build.id', tpl.BUILD_ID);
    push('ro.product.build.fingerprint', tpl.FINGERPRINT);
    push('ro.product.build.version.incremental', incremental);
    push('ro.build.product', tpl.DEVICE);
    push('ro.product.board', tpl.BOARD);
    push('ro.board.platform', tpl.BOARD);
    push('ro.hardware', tpl.HARDWARE);
    push('ro.boot.hardware', tpl.HARDWARE);
    push('ro.bootloader', tpl.BOOTLOADER || 'unknown');
    push('ro.boot.bootloader', tpl.BOOTLOADER || 'unknown');
    push('ro.soc.manufacturer', tpl.SOC_MANUFACTURER);
    push('ro.soc.model', tpl.SOC_MODEL);
    push('ro.product.cpu.abi', 'arm64-v8a');
    push('ro.product.cpu.abilist', abis);
    push('ro.product.cpu.abilist64', 'arm64-v8a');
    push('ro.product.cpu.abilist32', 'armeabi-v7a,armeabi');
    push('ro.build.date.utc', buildUtc);
    push('ro.build.host', host);
    push('ro.build.user', user);
    push('ro.build.version.security_patch', tpl.SECURITY_PATCH);
    push('ro.vendor.build.security_patch', tpl.SECURITY_PATCH);
    push('ro.build.tags', 'release-keys');
    push('ro.build.type', 'user');

    push('ro.boot.vbmeta.device_state', 'locked');
    push('ro.boot.vbmeta.avb_version', '1.2');
    push('ro.boot.vbmeta.hash_alg', 'sha256');
    push('ro.boot.verifiedbootstate', 'green');
    push('ro.boot.flash.locked', '1');
    push('ro.boot.veritymode', 'enforcing');
    push('ro.boot.warranty_bit', '0');
    push('ro.warranty_bit', '0');
    push('ro.build.selinux', '1');
    push('ro.debuggable', '0');
    push('ro.secure', '1');
    push('ro.adb.secure', '1');
    push('sys.oem_unlock_allowed', '0');
    push('sys.usb.state', 'none');
    push('sys.usb.config', 'none');
    push('init.svc.adbd', 'stopped');
    push('ro.sys.cloud.magisk', '0');
    push('persist.sys.cloud.root.global', '0');
    push('ro.sys.cloud.root.global', '0');
    push('ro.boot.hardware.sku', tpl.DEVICE);
    push('ro.boot.product.hardware.sku', tpl.DEVICE);

    if (route.mem) {
      const ramStr = `${ramGb}GB`;
      const ddrFull = `${ramStr},${ddrVendor},${ddrType}`;
      push('ro.boot.hardware.ddr', ddrFull);
      push('ro.boot.ddr_size', ramStr);
      push('ro.boot.ddr_type', ddrType);
      push('ro.boot.ddr_info', ddrFull);
      push('ro.boot.ramsize', ramStr);
      push('ro.boot.memsize', ramStr);
      push('ro.hardware.ddr', ddrFull);
      push('ro.vendor.hw.ram', ramStr);
      push('ro.product.ram', ramStr);
      push('ro.config.ram_size', ramGb * 1024);
      push('ro.sys.ram_size', ramStr);
      push('persist.sys.ram_size', ramStr);
    }

    if (route.gl) {
      if (tpl.VK_DEVICE_NAME) {
        push('persist.sys.cloud.gpu.vk_devicename', tpl.VK_DEVICE_NAME);
      }
      if (tpl.GL_RENDERER) {
        push('persist.sys.cloud.gpu.gl_renderer', tpl.GL_RENDERER);
        push('persist.sys.gpu.gl_renderer', tpl.GL_RENDERER);
      }
      if (tpl.GL_VENDOR) {
        push('persist.sys.cloud.gpu.gl_vendor', tpl.GL_VENDOR);
        push('persist.sys.gpu.gl_vendor', tpl.GL_VENDOR);
      }
    }

    if (route.sdkSpoof) {
      for (const part of ['', 'system.', 'vendor.', 'odm.', 'product.', 'system_ext.']) {
        push(`ro.${part}build.version.release`, tpl.RELEASE);
        push(`ro.${part}build.version.sdk`, tpl.SDK_INT);
      }
    }

    for (const raw of (tpl.CUSTOM_PROPS || '').split(/\r?\n/)) {
      const tr = raw.trim();
      if (!tr || tr.startsWith('#')) continue;
      const eq = tr.indexOf('=');
      if (eq > 0) {
        const k = tr.slice(0, eq).trim();
        if (k.includes('lcd_density') || k.endsWith('.dpi')) continue;
        if (
          !route.sdkSpoof &&
          (k.includes('build.version.sdk') ||
            k.includes('build.version.release') ||
            k.includes('first_api_level') ||
            k.includes('preview_sdk') ||
            k.includes('version.codename'))
        ) {
          continue;
        }
        lines.push(tr);
      }
    }
  }

  return lines.join('\n') + '\n';
}

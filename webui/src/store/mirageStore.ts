// SPDX-License-Identifier: GPL-3.0-only
// Copyright (c) 2025-2026 jjh138792
// MirageForge: https://github.com/jjh138792/MirageForge

import { reactive, ref } from 'vue';
import {
  execRootShell,
  hasZeroForkIo,
  readFileZeroFork,
  writeFileZeroFork,
  removeFileZeroFork,
  fetchInstalledAppsZeroFork,
  syncSystemBarsTheme,
  showToast,
  triggerHaptic,
  type InstalledAppInfo,
} from '../bridge/ksu-io';

export const CLI_BIN = '/data/adb/modules/mirage_forge/bin/mirageforge_cli';
export const MODULE_PROP_FILE = '/data/adb/modules/mirage_forge/module.prop';
export const CONFIG_JSON_FILE = '/data/adb/mirage_forge/config.json';
export const ROUTES_DB_FILE = '/data/adb/mirage_forge/routes.db';
export const APPS_DIR_PATH = '/data/adb/mirage_forge/apps';
export const TPL_DIR_PATH = '/data/adb/mirage_forge/templates';
export const PROBE_LOG_FILE = '/data/adb/mirage_forge/probe_events.log';
export const CRASH_LOG_FILE = '/data/adb/mirage_forge/crash_events.log';
export const LAUNCH_LOG_FILE = '/data/adb/mirage_forge/launch_history.log';

export const UPDATE_JSON_URL =
  'https://raw.githubusercontent.com/jjh138792/MirageForge/main/update.json';
export const ONLINE_HUB_URL =
  'https://raw.githubusercontent.com/jjh138792/MirageForge/main/templates-hub.json';
export const ONLINE_HUB_CDN_URL =
  'https://cdn.jsdelivr.net/gh/jjh138792/MirageForge@main/templates-hub.json';

export type ThemeMode = 'auto' | 'light' | 'dark';

export interface DeviceTemplate {
  id: string;
  name: string;
  MODEL: string;
  BRAND: string;
  MANUFACTURER: string;
  DEVICE: string;
  PRODUCT: string;
  BOARD: string;
  HARDWARE: string;
  BOOTLOADER: string;
  SOC_MANUFACTURER: string;
  SOC_MODEL: string;
  SUPPORTED_ABIS: string;
  BUILD_ID: string;
  DISPLAY_ID: string;
  INCREMENTAL: string;
  BUILD_HOST: string;
  BUILD_USER: string;
  BUILD_TIME: string;
  RELEASE: string;
  SDK_INT: string;
  SECURITY_PATCH: string;
  FINGERPRINT: string;
  // OpenGL ES 参数
  GL_VENDOR: string;
  GL_RENDERER: string;
  GL_VERSION: string;
  // Vulkan 独立参数
  VK_DEVICE_NAME: string;
  VK_VENDOR_ID: string;
  VK_DEVICE_ID: string;
  VK_API_VERSION: string;
  VK_DRIVER_VERSION: string;
  VK_DRIVER_NAME: string;
  VK_DRIVER_INFO: string;
  MAX_REFRESH_RATE: number;
  // VFS 硬件拓扑参数
  CPU_HARDWARE: string;
  CPU_CORES: number;
  CPU_MAX_FREQ: number;
  CPU_CLUSTERS: string;
  RAM_GB: number;
  DDR_TYPE: string;
  DDR_VENDOR: string;
  SOC_ID: string;
  SOC_MACHINE: string;
  KERNEL_RELEASE: string;
  KERNEL_VERSION: string;
  CUSTOM_PROPS: string;
}

export interface AppRoute {
  tplId: string;
  build: boolean;
  gl: boolean;
  vfs: boolean;
  probe: boolean;
  sdkSpoof: boolean;
  refreshRate: number;
  mountNs: boolean;
}

export interface ProbeEvent {
  pkg: string;
  domain: string;
  key: string;
  value: string;
  stack: string;
  unmapped: boolean;
}

export interface CrashEvent {
  pkg: string;
  signal: string;
  detail: string;
  stack: string;
}

export interface TelemetryEvent {
  timestamp: number;
  pkg: string;
  pid: number;
  model: string;
  gpu: string;
  ram: string;
}

export interface UpdateInfo {
  checking: boolean;
  hasUpdate: boolean;
  latestVersion: string;
  latestCode: number;
  zipUrl: string;
  changelog: string;
}

export interface HubSyncStatus {
  state: 'idle' | 'loading' | 'online' | 'offline';
  source: string;
  remoteCount: number;
  error: string;
  customUrl: string;
}

export function sanitizeRefreshRate(hz: number | undefined): number {
  const n = Number(hz || 0);
  return n >= 90 && n <= 240 ? n : 0;
}

/**
 * 自动补齐缺失的独立 Vulkan 字段，兼容旧版模板并防止驱动名与 OpenGL ES 串台
 */
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

// ============================================================================
// 内置离线兜底精选库 (仅在断网或拉取全部失败时兜底展示)
// ============================================================================
export const BUILTIN_HUB_TEMPLATES: DeviceTemplate[] = [
  {
    id: 'oneplus_13',
    name: 'OnePlus 13 (骁龙8至尊版 / 24GB LPDDR5X / 120Hz)',
    MODEL: 'PJZ110',
    BRAND: 'OnePlus',
    MANUFACTURER: 'OnePlus',
    DEVICE: 'OP5D0BL1',
    PRODUCT: 'PJZ110',
    BOARD: 'sun',
    HARDWARE: 'qcom',
    BOOTLOADER: 'unknown',
    SOC_MANUFACTURER: 'Qualcomm',
    SOC_MODEL: 'SM8750-AB',
    SUPPORTED_ABIS: 'arm64-v8a,armeabi-v7a,armeabi',
    BUILD_ID: 'AP3A.240617.008',
    DISPLAY_ID: 'PJZ110_15.0.0.405(CN01)',
    INCREMENTAL: 'R.1b4f2a1_1',
    BUILD_HOST: 'dg02-pool05-kvm87',
    BUILD_USER: 'root',
    BUILD_TIME: '1735948800000',
    RELEASE: '15',
    SDK_INT: '35',
    SECURITY_PATCH: '2025-02-01',
    FINGERPRINT: 'OnePlus/PJZ110/OP5D0BL1:15/AP3A.240617.008/R.1b4f2a1_1:user/release-keys',
    GL_VENDOR: 'Qualcomm',
    GL_RENDERER: 'Adreno (TM) 830',
    GL_VERSION: 'OpenGL ES 3.2 V@0800.0',
    VK_DEVICE_NAME: 'Adreno (TM) 830',
    VK_VENDOR_ID: '0x5143',
    VK_DEVICE_ID: '0x43050A01',
    VK_API_VERSION: '1.3.295',
    VK_DRIVER_VERSION: '512.800.0',
    VK_DRIVER_NAME: 'Qualcomm Technologies Inc. Adreno Vulkan Driver',
    VK_DRIVER_INFO: 'Driver Build: 0800.0',
    MAX_REFRESH_RATE: 120,
    CPU_HARDWARE: 'Qualcomm Technologies, Inc SM8750',
    CPU_CORES: 8,
    CPU_MAX_FREQ: 4320000,
    CPU_CLUSTERS: '0-5:614400:3532800;6-7:806400:4320000',
    RAM_GB: 24,
    DDR_TYPE: 'LPDDR5X',
    DDR_VENDOR: 'SKhynix',
    SOC_ID: '618',
    SOC_MACHINE: 'SM8750',
    KERNEL_RELEASE: '6.6.30-android15-8-o-g94b8f12ab4c1',
    KERNEL_VERSION: '#1 SMP PREEMPT Mon Jan 6 14:20:00 CST 2025',
    CUSTOM_PROPS: 'ro.vendor.oplus.market.name=一加 13\nro.build.version.oplusrom=V15.0.0',
  },
  {
    id: 'iqoo_13',
    name: 'iQOO 13 (骁龙8至尊版 / 16GB LPDDR5X / 144Hz)',
    MODEL: 'V2408A',
    BRAND: 'vivo',
    MANUFACTURER: 'vivo',
    DEVICE: 'PD2408',
    PRODUCT: 'PD2408',
    BOARD: 'sun',
    HARDWARE: 'qcom',
    BOOTLOADER: 'unknown',
    SOC_MANUFACTURER: 'Qualcomm',
    SOC_MODEL: 'SM8750-AB',
    SUPPORTED_ABIS: 'arm64-v8a,armeabi-v7a,armeabi',
    BUILD_ID: 'AP3A.240905.015.A2',
    DISPLAY_ID: 'OriginOS 5 PD2408_A_15.0.11.8',
    INCREMENTAL: 'compiler01151820',
    BUILD_HOST: 'vivo-osc-compile',
    BUILD_USER: 'compiler',
    BUILD_TIME: '1736936400000',
    RELEASE: '15',
    SDK_INT: '35',
    SECURITY_PATCH: '2025-02-01',
    FINGERPRINT: 'vivo/PD2408/PD2408:15/AP3A.240905.015.A2/compiler01151820:user/release-keys',
    GL_VENDOR: 'Qualcomm',
    GL_RENDERER: 'Adreno (TM) 830',
    GL_VERSION: 'OpenGL ES 3.2 V@0800.0',
    VK_DEVICE_NAME: 'Adreno (TM) 830',
    VK_VENDOR_ID: '0x5143',
    VK_DEVICE_ID: '0x43050A01',
    VK_API_VERSION: '1.3.295',
    VK_DRIVER_VERSION: '512.800.0',
    VK_DRIVER_NAME: 'Qualcomm Technologies Inc. Adreno Vulkan Driver',
    VK_DRIVER_INFO: 'Driver Build: 0800.0',
    MAX_REFRESH_RATE: 144,
    CPU_HARDWARE: 'Qualcomm Technologies, Inc SM8750',
    CPU_CORES: 8,
    CPU_MAX_FREQ: 4320000,
    CPU_CLUSTERS: '0-5:614400:3532800;6-7:806400:4320000',
    RAM_GB: 16,
    DDR_TYPE: 'LPDDR5X',
    DDR_VENDOR: 'SKhynix',
    SOC_ID: '618',
    SOC_MACHINE: 'SM8750',
    KERNEL_RELEASE: '6.6.30-android15-8-iqoo13',
    KERNEL_VERSION: '#1 SMP PREEMPT Wed Jan 15 18:20:00 CST 2025',
    CUSTOM_PROPS: 'ro.vivo.market.name=iQOO 13\nro.vivo.os.build.display.id=OriginOS 5',
  },
  {
    id: 'galaxy_s25_ultra',
    name: 'Samsung Galaxy S25 Ultra (骁龙8 Elite for Galaxy / 16GB)',
    MODEL: 'SM-S9380',
    BRAND: 'samsung',
    MANUFACTURER: 'samsung',
    DEVICE: 'pa3q',
    PRODUCT: 'pa3qzcx',
    BOARD: 'sun',
    HARDWARE: 'qcom',
    BOOTLOADER: 'S9380ZCU1AYA1',
    SOC_MANUFACTURER: 'Qualcomm',
    SOC_MODEL: 'SM8750-AC',
    SUPPORTED_ABIS: 'arm64-v8a,armeabi-v7a,armeabi',
    BUILD_ID: 'AP3A.240905.015.A2',
    DISPLAY_ID: 'AP3A.240905.015.A2.S9380ZCU1AYA1',
    INCREMENTAL: 'S9380ZCU1AYA1',
    BUILD_HOST: 'SWDJ2914',
    BUILD_USER: 'dpi',
    BUILD_TIME: '1737590400000',
    RELEASE: '15',
    SDK_INT: '35',
    SECURITY_PATCH: '2025-02-01',
    FINGERPRINT: 'samsung/pa3qzcx/pa3q:15/AP3A.240905.015.A2/S9380ZCU1AYA1:user/release-keys',
    GL_VENDOR: 'Qualcomm',
    GL_RENDERER: 'Adreno (TM) 830',
    GL_VERSION: 'OpenGL ES 3.2 V@0800.0',
    VK_DEVICE_NAME: 'Adreno (TM) 830',
    VK_VENDOR_ID: '0x5143',
    VK_DEVICE_ID: '0x43050A01',
    VK_API_VERSION: '1.3.295',
    VK_DRIVER_VERSION: '512.800.0',
    VK_DRIVER_NAME: 'Qualcomm Technologies Inc. Adreno Vulkan Driver',
    VK_DRIVER_INFO: 'Driver Build: 0800.0',
    MAX_REFRESH_RATE: 120,
    CPU_HARDWARE: 'Qualcomm Technologies, Inc SM8750',
    CPU_CORES: 8,
    CPU_MAX_FREQ: 4473600,
    CPU_CLUSTERS: '0-5:614400:3532800;6-7:806400:4473600',
    RAM_GB: 16,
    DDR_TYPE: 'LPDDR5X',
    DDR_VENDOR: 'Samsung',
    SOC_ID: '618',
    SOC_MACHINE: 'SM8750',
    KERNEL_RELEASE: '6.6.30-android15-8-s25u',
    KERNEL_VERSION: '#1 SMP PREEMPT Thu Jan 23 10:00:00 UTC 2025',
    CUSTOM_PROPS: 'ro.product.model=SM-S9380',
  },
  {
    id: 'legion_y700_gen3',
    name: '联想拯救者平板 Y700 三代 (骁龙8 Gen3 / 16GB / 165Hz 电竞平板)',
    MODEL: 'Lenovo TB321FU',
    BRAND: 'Lenovo',
    MANUFACTURER: 'LENOVO',
    DEVICE: 'TB321FU',
    PRODUCT: 'TB321FU_PRC',
    BOARD: 'pineapple',
    HARDWARE: 'qcom',
    BOOTLOADER: 'unknown',
    SOC_MANUFACTURER: 'Qualcomm',
    SOC_MODEL: 'SM8650-AB',
    SUPPORTED_ABIS: 'arm64-v8a,armeabi-v7a,armeabi',
    BUILD_ID: 'UKQ1.240116.001',
    DISPLAY_ID: 'TB321FU_CN_OPEN_USER_Q00020.0_U_ZUI_16.1.11.048_ST_241120',
    INCREMENTAL: '16.1.11.048',
    BUILD_HOST: 'lenovo-zui-ota',
    BUILD_USER: 'jenkins',
    BUILD_TIME: '1732089600000',
    RELEASE: '14',
    SDK_INT: '34',
    SECURITY_PATCH: '2025-01-01',
    FINGERPRINT: 'Lenovo/TB321FU_PRC/TB321FU:14/UKQ1.240116.001/16.1.11.048:user/release-keys',
    GL_VENDOR: 'Qualcomm',
    GL_RENDERER: 'Adreno (TM) 750',
    GL_VERSION: 'OpenGL ES 3.2 V@0762.0',
    VK_DEVICE_NAME: 'Adreno (TM) 750',
    VK_VENDOR_ID: '0x5143',
    VK_DEVICE_ID: '0x43050000',
    VK_API_VERSION: '1.3.279',
    VK_DRIVER_VERSION: '512.762.0',
    VK_DRIVER_NAME: 'Qualcomm Technologies Inc. Adreno Vulkan Driver',
    VK_DRIVER_INFO: 'Driver Build: 0762.0',
    MAX_REFRESH_RATE: 165,
    CPU_HARDWARE: 'Qualcomm Technologies, Inc SM8650',
    CPU_CORES: 8,
    CPU_MAX_FREQ: 3302400,
    CPU_CLUSTERS: '0-1:556800:2265600;2-6:691200:3148800;7-7:806400:3302400',
    RAM_GB: 16,
    DDR_TYPE: 'LPDDR5X',
    DDR_VENDOR: 'SKhynix',
    SOC_ID: '557',
    SOC_MACHINE: 'SM8650',
    KERNEL_RELEASE: '6.1.68-android14-11-y700',
    KERNEL_VERSION: '#1 SMP PREEMPT Wed Nov 20 16:00:00 CST 2024',
    CUSTOM_PROPS: 'ro.build.characteristics=tablet\nro.product.marketname=拯救者 Y700',
  },
  {
    id: 'redmi_k80_pro',
    name: 'Redmi K80 Pro (骁龙8至尊版 / 16GB LPDDR5X / 120Hz)',
    MODEL: '24122RKC7C',
    BRAND: 'Redmi',
    MANUFACTURER: 'Xiaomi',
    DEVICE: 'miro',
    PRODUCT: 'miro',
    BOARD: 'sun',
    HARDWARE: 'qcom',
    BOOTLOADER: 'unknown',
    SOC_MANUFACTURER: 'Qualcomm',
    SOC_MODEL: 'SM8750-AB',
    SUPPORTED_ABIS: 'arm64-v8a,armeabi-v7a,armeabi',
    BUILD_ID: 'AP3A.240905.015.A2',
    DISPLAY_ID: 'AP3A.240905.015.A2 release-keys',
    INCREMENTAL: 'OS2.0.105.0.VOMCNXM',
    BUILD_HOST: 'pangu-build-component-system',
    BUILD_USER: 'builder',
    BUILD_TIME: '1737158400000',
    RELEASE: '15',
    SDK_INT: '35',
    SECURITY_PATCH: '2025-02-01',
    FINGERPRINT: 'Redmi/miro/miro:15/AP3A.240905.015.A2/OS2.0.105.0.VOMCNXM:user/release-keys',
    GL_VENDOR: 'Qualcomm',
    GL_RENDERER: 'Adreno (TM) 830',
    GL_VERSION: 'OpenGL ES 3.2 V@0800.0',
    VK_DEVICE_NAME: 'Adreno (TM) 830',
    VK_VENDOR_ID: '0x5143',
    VK_DEVICE_ID: '0x43050A01',
    VK_API_VERSION: '1.3.295',
    VK_DRIVER_VERSION: '512.800.0',
    VK_DRIVER_NAME: 'Qualcomm Technologies Inc. Adreno Vulkan Driver',
    VK_DRIVER_INFO: 'Driver Build: 0800.0',
    MAX_REFRESH_RATE: 120,
    CPU_HARDWARE: 'Qualcomm Technologies, Inc SM8750',
    CPU_CORES: 8,
    CPU_MAX_FREQ: 4320000,
    CPU_CLUSTERS: '0-5:614400:3532800;6-7:806400:4320000',
    RAM_GB: 16,
    DDR_TYPE: 'LPDDR5X',
    DDR_VENDOR: 'SKhynix',
    SOC_ID: '618',
    SOC_MACHINE: 'SM8750',
    KERNEL_RELEASE: '6.6.30-android15-8-g94b8f12ab4c1',
    KERNEL_VERSION: '#1 SMP PREEMPT Sat Jan 18 12:00:00 UTC 2025',
    CUSTOM_PROPS: 'ro.miui.ui.version.code=15\nro.miui.ui.version.name=V816\nro.product.marketname=Redmi K80 Pro',
  },
];

export const mirageStore = reactive({
  version: '...',
  versionCode: 0,
  themeMode: ((localStorage.getItem('mirage_theme') as ThemeMode) || 'auto') as ThemeMode,
  resolvedTheme: 'dark' as 'light' | 'dark',
  loading: false,
  templates: {} as Record<string, DeviceTemplate>,
  appRoutes: {} as Record<string, AppRoute>,
  installedApps: [] as InstalledAppInfo[],
  includeSystemApps: false,
  probeEvents: [] as ProbeEvent[],
  crashEvents: [] as CrashEvent[],
  telemetryEvents: [] as TelemetryEvent[],
  hubTemplates: [...BUILTIN_HUB_TEMPLATES] as DeviceTemplate[],
  hubLoading: false,
  hubStatus: {
    state: 'idle',
    source: '',
    remoteCount: 0,
    error: '',
    customUrl: localStorage.getItem('mirage_hub_url') || '',
  } as HubSyncStatus,
  updateInfo: {
    checking: false,
    hasUpdate: false,
    latestVersion: '',
    latestCode: 0,
    zipUrl: 'https://github.com/jjh138792/MirageForge/releases/latest',
    changelog: '',
  } as UpdateInfo,
});

export const activeToastMsg = ref<string>('');
let toastTimer: any = null;

export function triggerLiquidToast(msg: string) {
  activeToastMsg.value = msg;
  if (toastTimer) clearTimeout(toastTimer);
  toastTimer = setTimeout(() => {
    activeToastMsg.value = '';
  }, 2800);
}

/**
 * 1. 日夜与跟随系统主题控制器
 */
export function applyThemeMode(mode?: ThemeMode) {
  if (mode) {
    mirageStore.themeMode = mode;
    localStorage.setItem('mirage_theme', mode);
  }
  const sysLight =
    typeof window !== 'undefined' &&
    window.matchMedia &&
    window.matchMedia('(prefers-color-scheme: light)').matches;

  const target: 'light' | 'dark' =
    mirageStore.themeMode === 'auto'
      ? sysLight
        ? 'light'
        : 'dark'
      : mirageStore.themeMode;

  mirageStore.resolvedTheme = target;
  if (typeof document !== 'undefined') {
    document.documentElement.setAttribute('data-theme', target);
    const metaTheme = document.querySelector('meta[name="theme-color"]');
    if (metaTheme) {
      metaTheme.setAttribute('content', target === 'light' ? '#eef2ff' : '#060913');
    }
  }
  syncSystemBarsTheme(target === 'light');
}

export function cycleThemeMode(): ThemeMode {
  const order: ThemeMode[] = ['auto', 'light', 'dark'];
  const next = order[(order.indexOf(mirageStore.themeMode) + 1) % order.length];
  triggerHaptic('medium');
  applyThemeMode(next);
  return next;
}

if (typeof window !== 'undefined' && window.matchMedia) {
  window
    .matchMedia('(prefers-color-scheme: light)')
    .addEventListener('change', () => {
      if (mirageStore.themeMode === 'auto') applyThemeMode();
    });
}

/**
 * 2. 动态读取 module.prop 版本号
 */
export async function loadVersionFromModuleProp() {
  const raw = await readFileZeroFork(MODULE_PROP_FILE);
  if (!raw) return;
  for (const line of raw.split(/\r?\n/)) {
    const tr = line.trim();
    if (tr.startsWith('version=')) {
      mirageStore.version = tr.slice(8).trim().replace(/^v/, '');
    } else if (tr.startsWith('versionCode=')) {
      mirageStore.versionCode = parseInt(tr.slice(12).trim(), 10) || 0;
    }
  }
}

/**
 * 通用多通道文本拉取器：WebView fetch (6s 超时) -> Root curl 兜底
 */
function isSafeHttpUrl(url: string): boolean {
  return /^https?:\/\/[^\s"'`$\\;|&<>]+$/i.test(url.trim());
}

async function fetchJsonTextWithFallback(url: string): Promise<{ text: string; via: 'fetch' | 'curl' }> {
  const cleanUrl = url.trim();
  if (!isSafeHttpUrl(cleanUrl)) {
    throw new Error('URL 格式不合法');
  }

  const sep = cleanUrl.includes('?') ? '&' : '?';
  const bustedUrl = `${cleanUrl}${sep}t=${Date.now()}`;

  // 1. 优先尝试浏览器原生 fetch
  try {
    const controller = new AbortController();
    const timer = setTimeout(() => controller.abort(), 6000);
    const resp = await fetch(bustedUrl, { cache: 'no-store', signal: controller.signal });
    clearTimeout(timer);
    if (!resp.ok) {
      throw new Error(`HTTP ${resp.status}`);
    }
    const text = await resp.text();
    if (text && text.trim().length > 2) {
      return { text, via: 'fetch' };
    }
  } catch (fetchErr: any) {
    // 2. WebView 网络受限/跨域时自动降级到 Root curl 拉取
    const curlRes = await execRootShell(
      `curl -fsSL --connect-timeout 6 --max-time 12 -A "MirageForge-WebUI" "${bustedUrl}"`
    );
    if (curlRes.errno === 0 && curlRes.stdout && curlRes.stdout.trim().length > 2) {
      return { text: curlRes.stdout, via: 'curl' };
    }
    throw new Error(fetchErr?.message || `curl exit ${curlRes.errno}`);
  }
  throw new Error('empty response');
}

/**
 * 3. 在线检查更新 (支持 CDN 与 curl 双兜底)
 */
export async function checkOnlineUpdate(t: (k: string, p?: any) => string) {
  mirageStore.updateInfo.checking = true;
  triggerHaptic('light');
  const candidates = [
    'https://cdn.jsdelivr.net/gh/jjh138792/MirageForge@main/update.json',
    UPDATE_JSON_URL,
  ];
  let parsed: any = null;
  for (const url of candidates) {
    try {
      const { text } = await fetchJsonTextWithFallback(url);
      parsed = JSON.parse(text);
      if (parsed && parsed.version) break;
    } catch (_) {}
  }

  try {
    if (!parsed) throw new Error('all sources failed');
    const remoteCode = Number(parsed.versionCode || 0);
    const remoteVer = String(parsed.version || '').replace(/^v/, '');

    mirageStore.updateInfo.latestCode = remoteCode;
    mirageStore.updateInfo.latestVersion = remoteVer;
    if (parsed.zipUrl) mirageStore.updateInfo.zipUrl = parsed.zipUrl;

    if (remoteCode > mirageStore.versionCode && mirageStore.versionCode > 0) {
      mirageStore.updateInfo.hasUpdate = true;
      triggerHaptic('success');
      showToast(t('app.hasUpdate', { ver: remoteVer }));
    } else {
      mirageStore.updateInfo.hasUpdate = false;
      showToast(t('app.upToDate', { ver: mirageStore.version }));
    }
  } catch (_) {
    showToast(t('app.updateError'));
  } finally {
    mirageStore.updateInfo.checking = false;
  }
}

/**
 * 4. URL-Safe Base64 模板分享码编解码器 (与 Rust CLI mf:// 格式 100% 互通)
 */
export function encodeTemplateShareCode(tpl: DeviceTemplate): string {
  const normalized = normalizeTemplateVulkanFields(tpl);
  const json = JSON.stringify(normalized);
  const b64 = btoa(unescape(encodeURIComponent(json)))
    .replace(/\+/g, '-')
    .replace(/\//g, '_')
    .replace(/=+$/, '');
  return `mf://${b64}`;
}

export function decodeTemplateShareCode(rawCode: string): DeviceTemplate | null {
  try {
    const trimmed = rawCode.trim();
    if (!trimmed) return null;
    if (trimmed.startsWith('{')) {
      const obj = JSON.parse(trimmed);
      return obj && obj.MODEL ? normalizeTemplateVulkanFields(obj) : null;
    }
    const payload = trimmed.replace(/^mf:\/\//i, '').replace(/-/g, '+').replace(/_/g, '/');
    const padLen = (4 - (payload.length % 4)) % 4;
    const padded = payload + '='.repeat(padLen);
    const json = decodeURIComponent(escape(atob(padded)));
    const parsed = JSON.parse(json);
    if (parsed && parsed.MODEL) {
      if (!parsed.id) parsed.id = `tpl_${Date.now()}`;
      return normalizeTemplateVulkanFields(parsed);
    }
  } catch (_) {}
  return null;
}

export function saveCustomHubUrl(url: string) {
  const clean = url.trim();
  mirageStore.hubStatus.customUrl = clean;
  if (clean) {
    localStorage.setItem('mirage_hub_url', clean);
  } else {
    localStorage.removeItem('mirage_hub_url');
  }
}

/**
 * 5. 在线机型库多通道真联网拉取 (自定义源 -> jsDelivr CDN -> GitHub Raw，含 Root curl 兜底)
 */
export async function fetchOnlineTemplateHub(forceToast = false) {
  mirageStore.hubLoading = true;
  mirageStore.hubStatus.state = 'loading';
  mirageStore.hubStatus.error = '';

  const sources: Array<{ label: string; url: string }> = [];
  if (mirageStore.hubStatus.customUrl.trim()) {
    sources.push({ label: '自定义云端源', url: mirageStore.hubStatus.customUrl.trim() });
  }
  sources.push({ label: 'jsDelivr CDN', url: ONLINE_HUB_CDN_URL });
  sources.push({ label: 'GitHub Raw', url: ONLINE_HUB_URL });

  let lastErr = '网络不可用';

  for (const src of sources) {
    try {
      const { text, via } = await fetchJsonTextWithFallback(src.url);
      const raw = JSON.parse(text);
      const list = Array.isArray(raw) ? raw : Array.isArray(raw?.templates) ? raw.templates : null;

      if (list && list.length > 0) {
        const mergedMap = new Map<string, DeviceTemplate>();
        for (const item of BUILTIN_HUB_TEMPLATES) {
          mergedMap.set(item.id, normalizeTemplateVulkanFields(item));
        }
        let validRemote = 0;
        for (const item of list) {
          if (item && item.id && item.MODEL) {
            mergedMap.set(item.id, normalizeTemplateVulkanFields(item));
            validRemote++;
          }
        }
        if (validRemote > 0) {
          mirageStore.hubTemplates = Array.from(mergedMap.values());
          mirageStore.hubStatus.state = 'online';
          mirageStore.hubStatus.source = `${src.label} (${via})`;
          mirageStore.hubStatus.remoteCount = validRemote;
          mirageStore.hubStatus.error = '';
          mirageStore.hubLoading = false;
          if (forceToast) {
            triggerHaptic('success');
            showToast(`已从 ${src.label} 同步 ${validRemote} 款云端机型`);
          }
          return;
        }
      }
      lastErr = 'JSON 格式不含有效机型列表';
    } catch (e: any) {
      lastErr = `${src.label}: ${e?.message || '连接失败'}`;
    }
  }

  mirageStore.hubTemplates = BUILTIN_HUB_TEMPLATES.map((t) => normalizeTemplateVulkanFields(t));
  mirageStore.hubStatus.state = 'offline';
  mirageStore.hubStatus.source = '本地离线精选库';
  mirageStore.hubStatus.remoteCount = 0;
  mirageStore.hubStatus.error = lastErr;
  mirageStore.hubLoading = false;
  if (forceToast) {
    showToast(`云端拉取失败，已切换至离线精选库 (${lastErr})`);
  }
}

/**
 * 6. TypeScript 内存级微模块 .conf 编译器 (与 Rust CLI compiler.rs 100% 同步)
 */
function compileModularConfInTs(rawTpl: DeviceTemplate, route: AppRoute): string {
  const tpl = normalizeTemplateVulkanFields(rawTpl);
  const lines: string[] = [];
  const push = (k: string, v: string | number | undefined) => {
    if (k && v !== undefined && String(v).length > 0) {
      lines.push(`${k}=${v}`);
    }
  };

  const effectiveFps = sanitizeRefreshRate(route.refreshRate);

  push('META:ENABLE_BUILD', route.build ? '1' : '0');
  push('META:ENABLE_GL', route.gl ? '1' : '0');
  push('META:ENABLE_VFS', route.vfs ? '1' : '0');
  push('META:ENABLE_PROBE', route.probe ? '1' : '0');
  push('META:FORCE_SPOOF_SDK', route.sdkSpoof ? '1' : '0');
  push('META:ENABLE_MOUNT_NS', route.vfs && route.mountNs ? '1' : '0');
  if (route.gl && effectiveFps >= 90) {
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

  if (route.vfs) {
    push('VFS:CPU_HARDWARE', tpl.CPU_HARDWARE);
    push('VFS:CPU_ARCH', 'aarch64');
    push('VFS:CPU_CORES', tpl.CPU_CORES || 8);
    push('VFS:CPU_MAX_FREQ', tpl.CPU_MAX_FREQ || 4320000);
    push('VFS:CPU_CLUSTERS', tpl.CPU_CLUSTERS);
    push('VFS:RAM_GB', ramGb);
    push('VFS:DDR_TYPE', ddrType);
    push('VFS:DDR_VENDOR', ddrVendor);
    push('VFS:SOC_ID', tpl.SOC_ID);
    push('VFS:SOC_MACHINE', tpl.SOC_MACHINE);
    push('VFS:KERNEL_RELEASE', tpl.KERNEL_RELEASE);
    push('VFS:KERNEL_VERSION', tpl.KERNEL_VERSION);
  }

  if (route.build) {
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

    if (route.vfs) {
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
            k.includes('first_api_level'))
        ) {
          continue;
        }
        lines.push(tr);
      }
    }
  }

  return lines.join('\n') + '\n';
}

async function flushStateZeroFork() {
  const rLines = [`# MirageForge v${mirageStore.version} Routes Table`];
  for (const [pkg, r] of Object.entries(mirageStore.appRoutes)) {
    const tpl = mirageStore.templates[r.tplId];
    if (!tpl || !(r.build || r.gl || r.vfs || r.probe)) {
      await removeFileZeroFork(`${APPS_DIR_PATH}/${pkg}.conf`);
      delete mirageStore.appRoutes[pkg];
      continue;
    }
    r.refreshRate = sanitizeRefreshRate(r.refreshRate);
    const confText = compileModularConfInTs(tpl, r);
    await writeFileZeroFork(`${APPS_DIR_PATH}/${pkg}.conf`, confText);
    rLines.push(
      `${pkg}|${r.tplId}|${r.build ? 1 : 0}|${r.gl ? 1 : 0}|${r.vfs ? 1 : 0}|${r.probe ? 1 : 0}|${r.sdkSpoof ? 1 : 0}|${r.refreshRate}|${r.mountNs ? 1 : 0}`
    );
  }
  await writeFileZeroFork(ROUTES_DB_FILE, rLines.join('\n') + '\n');
  await writeFileZeroFork(
    CONFIG_JSON_FILE,
    JSON.stringify(
      {
        version: mirageStore.version,
        versionCode: mirageStore.versionCode,
        activeCount: Object.keys(mirageStore.appRoutes).length,
        templateCount: Object.keys(mirageStore.templates).length,
        templates: mirageStore.templates,
        appRoutes: mirageStore.appRoutes,
      },
      null,
      2
    )
  );
}

function normalizeLoadedTemplates(rawMap: Record<string, any>): Record<string, DeviceTemplate> {
  const out: Record<string, DeviceTemplate> = {};
  for (const [id, tpl] of Object.entries(rawMap || {})) {
    if (tpl && tpl.MODEL) {
      out[id] = normalizeTemplateVulkanFields({ ...tpl, id });
    }
  }
  return out;
}

function normalizeLoadedRoutes(rawRoutes: Record<string, any>): Record<string, AppRoute> {
  const out: Record<string, AppRoute> = {};
  for (const [pkg, r] of Object.entries(rawRoutes || {})) {
    if (!r || !r.tplId) continue;
    out[pkg] = {
      tplId: String(r.tplId),
      build: r.build !== false,
      gl: r.gl !== false,
      vfs: r.vfs !== false,
      probe: Boolean(r.probe),
      sdkSpoof: Boolean(r.sdkSpoof),
      refreshRate: sanitizeRefreshRate(r.refreshRate),
      mountNs: Boolean(r.mountNs),
    };
  }
  return out;
}

export function parseClusterBars(clustersStr: string) {
  if (!clustersStr || !clustersStr.includes(':')) return [];
  return clustersStr
    .split(';')
    .map((s) => s.trim())
    .filter(Boolean)
    .map((seg) => {
      const [range, minK, maxK] = seg.split(':');
      const [start, end] = (range || '0-0').split('-').map((n) => parseInt(n, 10));
      const cores = !isNaN(start) && !isNaN(end) && end >= start ? end - start + 1 : 1;
      const minGhz = (parseInt(minK || '600000', 10) / 1e6).toFixed(2);
      const maxGhz = (parseInt(maxK || '3500000', 10) / 1e6).toFixed(2);
      return { range, cores, minGhz, maxGhz, rawMax: parseInt(maxK || '3500000', 10) };
    });
}

export async function syncStateFromRustCli() {
  await loadVersionFromModuleProp();

  if (hasZeroForkIo()) {
    const rawJson = await readFileZeroFork(CONFIG_JSON_FILE);
    if (rawJson.trim()) {
      try {
        const data = JSON.parse(rawJson);
        mirageStore.templates = normalizeLoadedTemplates(data.templates);
        mirageStore.appRoutes = normalizeLoadedRoutes(data.appRoutes);
        if (!mirageStore.version || mirageStore.version === '...') {
          mirageStore.version = data.version || '1.3.0';
          mirageStore.versionCode = data.versionCode || 130;
        }
        return;
      } catch (_) {}
    }
  }

  const res = await execRootShell(`"${CLI_BIN}" state --json`);
  if (res.errno === 0 && res.stdout.trim()) {
    try {
      const data = JSON.parse(res.stdout);
      if (!mirageStore.version || mirageStore.version === '...') {
        mirageStore.version = data.version || '1.3.0';
        mirageStore.versionCode = data.versionCode || 130;
      }
      mirageStore.templates = normalizeLoadedTemplates(data.templates);
      mirageStore.appRoutes = normalizeLoadedRoutes(data.appRoutes);
    } catch (_) {}
  }
}

export async function reloadAppList() {
  mirageStore.loading = true;
  const configured = Object.keys(mirageStore.appRoutes);
  const apps = await fetchInstalledAppsZeroFork(mirageStore.includeSystemApps, configured);
  mirageStore.installedApps = apps.sort((a, b) => {
    const aConf = Boolean(mirageStore.appRoutes[a.packageName]);
    const bConf = Boolean(mirageStore.appRoutes[b.packageName]);
    if (aConf !== bConf) return aConf ? -1 : 1;
    return a.appLabel.localeCompare(b.appLabel, 'zh-CN');
  });
  mirageStore.loading = false;
}

export async function saveTemplateViaCli(rawTpl: DeviceTemplate) {
  const tpl = normalizeTemplateVulkanFields(rawTpl);
  if (hasZeroForkIo()) {
    mirageStore.templates[tpl.id] = { ...tpl };
    await writeFileZeroFork(`${TPL_DIR_PATH}/${tpl.id}.tpl`, JSON.stringify(tpl, null, 2));
    await flushStateZeroFork();
  } else {
    const b64 = btoa(unescape(encodeURIComponent(JSON.stringify(tpl))));
    await execRootShell(`echo "${b64}" | base64 -d | "${CLI_BIN}" template set-stdin`);
    await syncStateFromRustCli();
  }
  triggerHaptic('success');
  showToast(`已保存并编译模板: ${tpl.name}`);
}

export async function deleteTemplateViaCli(id: string) {
  if (hasZeroForkIo()) {
    delete mirageStore.templates[id];
    await removeFileZeroFork(`${TPL_DIR_PATH}/${id}.tpl`);
    await flushStateZeroFork();
  } else {
    await execRootShell(`"${CLI_BIN}" template delete "${id}" --json`);
    await syncStateFromRustCli();
  }
  triggerHaptic('medium');
  showToast('已删除模板');
}

export async function parsePropTextViaCli(rawText: string): Promise<DeviceTemplate | null> {
  const b64 = btoa(unescape(encodeURIComponent(rawText)));
  const res = await execRootShell(`echo "${b64}" | base64 -d | "${CLI_BIN}" parse-prop -`);
  if (res.errno === 0 && res.stdout.trim()) {
    try {
      const parsed = JSON.parse(res.stdout);
      if (parsed && parsed.MODEL) {
        triggerHaptic('success');
        return normalizeTemplateVulkanFields(parsed);
      }
    } catch (_) {}
  }
  showToast('未识别到有效的 build.prop 字段');
  return null;
}

export async function updateAppRouteViaCli(
  pkg: string,
  route: AppRoute | null,
  restart = false
) {
  if (hasZeroForkIo()) {
    if (!route || !route.tplId || !(route.build || route.gl || route.vfs || route.probe)) {
      delete mirageStore.appRoutes[pkg];
      await removeFileZeroFork(`${APPS_DIR_PATH}/${pkg}.conf`);
    } else {
      mirageStore.appRoutes[pkg] = {
        ...route,
        refreshRate: sanitizeRefreshRate(route.refreshRate),
      };
    }
    await flushStateZeroFork();
    if (restart) await execRootShell(`am force-stop "${pkg}"`);
    return;
  }

  if (!route || !route.tplId) {
    await execRootShell(`"${CLI_BIN}" route remove "${pkg}" ${restart ? '--restart' : ''}`);
  } else {
    const fps = sanitizeRefreshRate(route.refreshRate);
    const cmd = `"${CLI_BIN}" route set "${pkg}" "${route.tplId}" --build ${route.build ? 1 : 0} --gl ${route.gl ? 1 : 0} --vfs ${route.vfs ? 1 : 0} --probe ${route.probe ? 1 : 0} --sdk ${route.sdkSpoof ? 1 : 0} --fps ${fps} --mount-ns ${route.mountNs ? 1 : 0} ${restart ? '--restart' : ''}`;
    await execRootShell(cmd);
  }
  await syncStateFromRustCli();
}

export async function forceStopApp(pkg: string, label: string) {
  triggerHaptic('medium');
  await execRootShell(`am force-stop "${pkg}"`);
  showToast(`已重启 ${label}`);
}

export async function refreshAllProbeData(filterQuery = '', unmappedOnly = false) {
  const pkgFlag = filterQuery ? `--pkg "${filterQuery.replace(/"/g, '')}"` : '';
  const unmappedFlag = unmappedOnly ? '--unmapped' : '';

  const [probeRes, crashRes, teleRes] = await Promise.all([
    execRootShell(`"${CLI_BIN}" probe list ${pkgFlag} ${unmappedFlag} --limit 150 --json`),
    execRootShell(`"${CLI_BIN}" probe crashes --limit 25 --json`),
    execRootShell(`"${CLI_BIN}" probe telemetry --limit 25 --json`),
  ]);

  try { mirageStore.probeEvents = JSON.parse(probeRes.stdout || '[]'); } catch (_) { mirageStore.probeEvents = []; }
  try { mirageStore.crashEvents = JSON.parse(crashRes.stdout || '[]'); } catch (_) { mirageStore.crashEvents = []; }
  try { mirageStore.telemetryEvents = JSON.parse(teleRes.stdout || '[]'); } catch (_) { mirageStore.telemetryEvents = []; }
}

export async function captureUnmappedPropViaCli(pkg: string, key: string, val: string) {
  const safeVal = val.trim().replace(/"/g, '');
  await execRootShell(`"${CLI_BIN}" probe capture "${pkg}" "${key}" "${safeVal}"`);
  await syncStateFromRustCli();
  triggerHaptic('success');
  showToast(`已捕获 ${key}=${safeVal}`);
}

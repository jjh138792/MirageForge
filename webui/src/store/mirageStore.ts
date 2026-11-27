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
  showToast,
  triggerHaptic,
  detectRuntimeEnv,
  type InstalledAppInfo,
} from '../bridge/ksu-io';
import {
  compileModularConfInTs,
  encodeBase64Url,
  isRouteAnyEnabled,
  normalizeAppRoute,
  normalizeTemplateVulkanFields,
} from './confCompiler';
import {
  settingsStore,
  applyThemeMode,
  cycleThemeMode,
  setAccentColor,
  setDockStyle,
  setRefractionLevel,
  setDefaultPropMode,
  setEngineSetting,
  applyAllVisualSettings,
} from './settingsStore';
import {
  hostDeviceState,
  fetchHostDeviceInfo,
  cloneHostDeviceViaCli,
} from './deviceInfoStore';
import {
  createDefaultAutoSpoof,
  isAppOverridesEmpty,
  sanitizePropMode,
  sanitizeRefreshRate,
  type AppRoute,
  type AutoSpoofConfig,
  type CrashEvent,
  type DeviceTemplate,
  type HubSyncStatus,
  type OtaMirrorMode,
  type OtaRuntimeState,
  type OtaStage,
  type ProbeEvent,
  type PropSpoofMode,
  type RootManagerKind,
  type RuntimeStatusEntry,
  type TelemetryEvent,
  type ThemeMode,
  type UpdateInfo,
  type WatcherRuntimeState,
} from './types';

export * from './types';
export {
  encodeBase64Url,
  normalizeTemplateVulkanFields,
  normalizeAppRoute,
  isRouteAnyEnabled,
  compileModularConfInTs,
  settingsStore,
  applyThemeMode,
  cycleThemeMode,
  setAccentColor,
  setDockStyle,
  setRefractionLevel,
  setDefaultPropMode,
  setEngineSetting,
  applyAllVisualSettings,
  hostDeviceState,
  fetchHostDeviceInfo,
  cloneHostDeviceViaCli,
};

export const CLI_BIN = '/data/adb/modules/mirage_forge/bin/mirageforge_cli';
export const MODULE_PROP_FILE = '/data/adb/modules/mirage_forge/module.prop';
export const CONFIG_JSON_FILE = '/data/adb/mirage_forge/config.json';
export const AUTO_SPOOF_JSON_FILE = '/data/adb/mirage_forge/auto_spoof.json';
export const RUNTIME_STATUS_FILE = '/data/adb/mirage_forge/runtime_status.json';
export const ROUTES_DB_FILE = '/data/adb/mirage_forge/routes.db';
export const APPS_DIR_PATH = '/data/adb/mirage_forge/apps';
export const TPL_DIR_PATH = '/data/adb/mirage_forge/templates';
export const PROBE_LOG_FILE = '/data/adb/mirage_forge/probe_events.log';
export const CRASH_LOG_FILE = '/data/adb/mirage_forge/crash_events.log';
export const LAUNCH_LOG_FILE = '/data/adb/mirage_forge/launch_history.log';

export const OTA_SCRIPT_FILE = '/data/local/tmp/mirage_forge_ota.sh';
export const OTA_LOG_FILE = '/data/local/tmp/mirage_forge_ota.log';
export const OTA_EXIT_FILE = '/data/local/tmp/mirage_forge_ota.exit';
export const OTA_ZIP_FILE = '/data/local/tmp/mirage_forge_ota.zip';

export const UPDATE_JSON_URL =
  'https://raw.githubusercontent.com/jjh138792/MirageForge/main/update.json';
export const ONLINE_HUB_URL =
  'https://raw.githubusercontent.com/jjh138792/MirageForge/main/templates-hub.json';
export const ONLINE_HUB_CDN_URL =
  'https://cdn.jsdelivr.net/gh/jjh138792/MirageForge@main/templates-hub.json';

export const BUILTIN_HUB_TEMPLATES: DeviceTemplate[] = [
  normalizeTemplateVulkanFields({
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
  }),
  normalizeTemplateVulkanFields({
    id: 'legion_y700_gen3',
    name: '联想拯救者平板 Y700 三代 (骁龙8 Gen3 / 16GB / 165Hz)',
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
  }),
];

export const mirageStore = reactive({
  version: '...',
  versionCode: 0,
  get themeMode(): ThemeMode {
    return settingsStore.themeMode;
  },
  set themeMode(v: ThemeMode) {
    settingsStore.themeMode = v;
  },
  get resolvedTheme(): 'light' | 'dark' {
    return settingsStore.resolvedTheme;
  },
  loading: false,
  templates: {} as Record<string, DeviceTemplate>,
  appRoutes: {} as Record<string, AppRoute>,
  autoSpoof: createDefaultAutoSpoof() as AutoSpoofConfig,
  watcherStatus: {
    running: false,
    pid: 0,
  } as WatcherRuntimeState,
  runtimeStatus: {} as Record<string, RuntimeStatusEntry>,
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
    changelogUrl: '',
    changelogText: '',
    changelogLoading: false,
    changelogError: '',
  } as UpdateInfo,
  ota: {
    stage: 'idle',
    running: false,
    detectedManager: 'none',
    selectedManager: 'auto',
    managerVersion: '',
    availableManagers: { ksu: false, apatch: false, magisk: false },
    mirrorMode: ((localStorage.getItem('mirage_ota_mirror') as OtaMirrorMode) || 'auto') as OtaMirrorMode,
    logs: [] as string[],
    errorMsg: '',
  } as OtaRuntimeState,
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

export function isSafeHttpUrl(url: string): boolean {
  return /^https?:\/\/[^\s"'`$\\;|&<>]+$/i.test(url.trim());
}

function toJsDelivrMirror(url: string): string | null {
  const m = url
    .trim()
    .match(/^https:\/\/raw\.githubusercontent\.com\/([^/]+)\/([^/]+)\/([^/]+)\/(.+)$/i);
  if (!m) return null;
  return `https://cdn.jsdelivr.net/gh/${m[1]}/${m[2]}@${m[3]}/${m[4]}`;
}

async function fetchTextWithFallback(url: string): Promise<{ text: string; via: 'fetch' | 'curl' }> {
  const cleanUrl = url.trim();
  if (!isSafeHttpUrl(cleanUrl)) throw new Error('URL 格式不合法');
  const sep = cleanUrl.includes('?') ? '&' : '?';
  const bustedUrl = `${cleanUrl}${sep}t=${Date.now()}`;

  try {
    const controller = new AbortController();
    const timer = setTimeout(() => controller.abort(), 6500);
    const resp = await fetch(bustedUrl, { cache: 'no-store', signal: controller.signal });
    clearTimeout(timer);
    if (!resp.ok) throw new Error(`HTTP ${resp.status}`);
    const text = await resp.text();
    if (text && text.trim().length > 0) return { text, via: 'fetch' };
  } catch (fetchErr: any) {
    const curlRes = await execRootShell(
      `curl -fsSL --connect-timeout 6 --max-time 12 -A "MirageForge-WebUI" "${bustedUrl}"`
    );
    if (curlRes.errno === 0 && curlRes.stdout && curlRes.stdout.trim().length > 0) {
      return { text: curlRes.stdout, via: 'curl' };
    }
    throw new Error(fetchErr?.message || `curl exit ${curlRes.errno}`);
  }
  throw new Error('empty response');
}

function compareSemver(a: string, b: string): number {
  const pa = a.replace(/^v/i, '').split('.').map((n) => parseInt(n, 10) || 0);
  const pb = b.replace(/^v/i, '').split('.').map((n) => parseInt(n, 10) || 0);
  const len = Math.max(pa.length, pb.length);
  for (let i = 0; i < len; i++) {
    const da = pa[i] || 0;
    const db = pb[i] || 0;
    if (da !== db) return da > db ? 1 : -1;
  }
  return 0;
}

export async function fetchChangelogContent(force = false): Promise<string> {
  const url = mirageStore.updateInfo.changelogUrl.trim();
  if (!url) {
    mirageStore.updateInfo.changelogError = '';
    return mirageStore.updateInfo.changelogText;
  }
  if (!force && mirageStore.updateInfo.changelogText.trim()) {
    return mirageStore.updateInfo.changelogText;
  }
  if (!isSafeHttpUrl(url)) {
    mirageStore.updateInfo.changelogText = url;
    return url;
  }

  mirageStore.updateInfo.changelogLoading = true;
  mirageStore.updateInfo.changelogError = '';

  const candidates: string[] = [];
  const cdnUrl = toJsDelivrMirror(url);
  if (cdnUrl) candidates.push(cdnUrl);
  candidates.push(url);

  let lastErr = '';
  for (const target of candidates) {
    try {
      const { text } = await fetchTextWithFallback(target);
      if (text && text.trim()) {
        mirageStore.updateInfo.changelogText = text.trim();
        mirageStore.updateInfo.changelogLoading = false;
        return mirageStore.updateInfo.changelogText;
      }
    } catch (e: any) {
      lastErr = e?.message || '拉取失败';
    }
  }

  mirageStore.updateInfo.changelogError = lastErr || '无法获取更新日志';
  mirageStore.updateInfo.changelogLoading = false;
  return '';
}

export async function checkOnlineUpdate(
  t: (k: string, p?: any) => string,
  silent = false
): Promise<boolean> {
  mirageStore.updateInfo.checking = true;
  if (!silent) triggerHaptic('light');

  const candidates = [
    'https://cdn.jsdelivr.net/gh/jjh138792/MirageForge@main/update.json',
    UPDATE_JSON_URL,
  ];
  let parsed: any = null;
  for (const url of candidates) {
    try {
      const { text } = await fetchTextWithFallback(url);
      parsed = JSON.parse(text);
      if (parsed && parsed.version) break;
    } catch (_) {}
  }

  try {
    if (!parsed) throw new Error('all sources failed');
    const remoteCode = Number(parsed.versionCode || 0);
    const remoteVer = String(parsed.version || '').replace(/^v/i, '');
    const rawChangelog = String(parsed.changelog || '').trim();

    mirageStore.updateInfo.latestCode = remoteCode;
    mirageStore.updateInfo.latestVersion = remoteVer;
    if (parsed.zipUrl) mirageStore.updateInfo.zipUrl = String(parsed.zipUrl).trim();

    if (isSafeHttpUrl(rawChangelog)) {
      if (mirageStore.updateInfo.changelogUrl !== rawChangelog) {
        mirageStore.updateInfo.changelogUrl = rawChangelog;
        mirageStore.updateInfo.changelogText = '';
      }
    } else {
      mirageStore.updateInfo.changelogUrl = '';
      mirageStore.updateInfo.changelogText = rawChangelog;
    }

    const isNewer =
      (remoteCode > 0 && mirageStore.versionCode > 0 && remoteCode > mirageStore.versionCode) ||
      (remoteVer.length > 0 &&
        mirageStore.version !== '...' &&
        compareSemver(remoteVer, mirageStore.version) > 0);

    mirageStore.updateInfo.hasUpdate = isNewer;

    if (isNewer) {
      fetchChangelogContent(false);
      if (!silent) {
        triggerHaptic('success');
        showToast(t('app.hasUpdate', { ver: remoteVer }));
      }
      return true;
    }

    if (!silent) showToast(t('app.upToDate', { ver: mirageStore.version }));
    return false;
  } catch (_) {
    if (!silent) showToast(t('app.updateError'));
    return false;
  } finally {
    mirageStore.updateInfo.checking = false;
  }
}

export async function detectRootManagerForOta() {
  if (detectRuntimeEnv() === 'BrowserMock') {
    mirageStore.ota.detectedManager = 'ksu';
    mirageStore.ota.managerVersion = 'KernelSU Mock v1.0.9';
    mirageStore.ota.availableManagers = { ksu: true, apatch: false, magisk: true };
    return;
  }

  const probeCmd = [
    'KSU_BIN=""; [ -x /data/adb/ksud ] && KSU_BIN=/data/adb/ksud || KSU_BIN=$(command -v ksud 2>/dev/null)',
    'APD_BIN=""; [ -x /data/adb/apd ] && APD_BIN=/data/adb/apd || APD_BIN=$(command -v apd 2>/dev/null)',
    'MAG_BIN=""; for p in "$(command -v magisk 2>/dev/null)" /sbin/magisk /debug_ramdisk/magisk /system/bin/magisk; do [ -n "$p" ] && [ -x "$p" ] && MAG_BIN="$p" && break; done',
    'HAS_KSU=0; [ -n "$KSU_BIN" ] && HAS_KSU=1',
    'HAS_AP=0; [ -n "$APD_BIN" ] && HAS_AP=1',
    'HAS_MAG=0; [ -n "$MAG_BIN" ] && HAS_MAG=1',
    'ACT="none"; VER=""',
    'if [ -n "$MAG_BIN" ] && "$MAG_BIN" -v >/dev/null 2>&1; then ACT="magisk"; VER=$("$MAG_BIN" -v 2>/dev/null | head -n 1); fi',
    'if [ -n "$APD_BIN" ] && [ -d /data/adb/ap ] && "$APD_BIN" -V >/dev/null 2>&1; then if [ "$ACT" = "none" ] || [ -f /data/adb/ap/package_config ]; then ACT="apatch"; VER=$("$APD_BIN" -V 2>/dev/null | head -n 1); fi; fi',
    'if [ -n "$KSU_BIN" ] && [ -d /data/adb/ksu ] && "$KSU_BIN" -V >/dev/null 2>&1; then if [ "$ACT" = "none" ] || [ "$ACT" != "magisk" ]; then ACT="ksu"; VER=$("$KSU_BIN" -V 2>/dev/null | head -n 1); fi; fi',
    'if [ "$ACT" = "none" ]; then if [ "$HAS_KSU" = "1" ]; then ACT="ksu"; VER=$("$KSU_BIN" -V 2>/dev/null | head -n 1); elif [ "$HAS_AP" = "1" ]; then ACT="apatch"; VER=$("$APD_BIN" -V 2>/dev/null | head -n 1); elif [ "$HAS_MAG" = "1" ]; then ACT="magisk"; VER=$("$MAG_BIN" -v 2>/dev/null | head -n 1); fi; fi',
    'echo "ACT=$ACT"',
    'echo "VER=$VER"',
    'echo "HAS_KSU=$HAS_KSU"',
    'echo "HAS_AP=$HAS_AP"',
    'echo "HAS_MAG=$HAS_MAG"',
  ].join('\n');

  const res = await execRootShell(probeCmd);
  const kv: Record<string, string> = {};
  for (const line of (res.stdout || '').split(/\r?\n/)) {
    const eq = line.indexOf('=');
    if (eq > 0) kv[line.slice(0, eq).trim()] = line.slice(eq + 1).trim();
  }

  const act = kv.ACT as 'ksu' | 'apatch' | 'magisk' | 'none';
  mirageStore.ota.detectedManager =
    act === 'ksu' || act === 'apatch' || act === 'magisk' ? act : 'none';
  mirageStore.ota.managerVersion = kv.VER || '';
  mirageStore.ota.availableManagers = {
    ksu: kv.HAS_KSU === '1',
    apatch: kv.HAS_AP === '1',
    magisk: kv.HAS_MAG === '1',
  };
}

export function setOtaMirrorMode(mode: OtaMirrorMode) {
  mirageStore.ota.mirrorMode = mode;
  localStorage.setItem('mirage_ota_mirror', mode);
}

let otaPollTimer: any = null;
let otaPollStartTs = 0;

export function stopOtaPolling() {
  if (otaPollTimer) {
    clearInterval(otaPollTimer);
    otaPollTimer = null;
  }
}

function stripAnsiAndParseLogs(rawLog: string): { stage: OtaStage | null; lines: string[] } {
  const cleaned = rawLog.replace(/\x1b\[[0-9;?]*[a-zA-Z]/g, '');
  const rawLines = cleaned.split('\n');
  const out: string[] = [];
  let latestStage: OtaStage | null = null;

  for (const rawLine of rawLines) {
    const parts = rawLine.split('\r');
    let line = '';
    for (let i = parts.length - 1; i >= 0; i--) {
      if (parts[i].trim().length > 0) {
        line = parts[i];
        break;
      }
    }
    const trimmed = line.trim();
    if (!trimmed) continue;

    const stageMatch = trimmed.match(/^\[MF_STAGE:(detecting|downloading|verifying|installing|done)\]$/);
    if (stageMatch) {
      const s = stageMatch[1];
      latestStage = s === 'done' ? 'success' : (s as OtaStage);
      continue;
    }
    out.push(line);
  }
  return { stage: latestStage, lines: out };
}

function buildDownloadUrlList(zipUrl: string, mirror: OtaMirrorMode): string[] {
  const isGithub = /^https:\/\/github\.com\//i.test(zipUrl);
  if (!isGithub || mirror === 'direct') return [zipUrl];
  const ghfast = `https://ghfast.top/${zipUrl}`;
  const ghproxy = `https://gh-proxy.com/${zipUrl}`;
  if (mirror === 'ghfast') return [ghfast, zipUrl];
  if (mirror === 'ghproxy') return [ghproxy, zipUrl];
  return [ghfast, ghproxy, zipUrl];
}

function buildOtaInstallScript(
  urls: string[],
  preferredManager: RootManagerKind,
  targetVersion: string
): string {
  const quotedUrls = urls.map((u) => `"${u}"`).join(' ');
  return `#!/system/bin/sh
LOG="${OTA_LOG_FILE}"
EXITF="${OTA_EXIT_FILE}"
ZIP="${OTA_ZIP_FILE}"
rm -f "$LOG" "$EXITF" "$ZIP"
: > "$LOG"
chmod 600 "$LOG"
exec > "$LOG" 2>&1

finish() {
  code="$1"
  rm -f "$ZIP"
  echo "$code" > "$EXITF"
  chmod 600 "$EXITF"
  exit "$code"
}

echo "[MF_STAGE:detecting]"
echo "=========================================="
echo "  MirageForge OTA Updater -> v${targetVersion}"
echo "=========================================="

PREF="${preferredManager}"
KSU_BIN=""; [ -x /data/adb/ksud ] && KSU_BIN=/data/adb/ksud || KSU_BIN=$(command -v ksud 2>/dev/null)
APD_BIN=""; [ -x /data/adb/apd ] && APD_BIN=/data/adb/apd || APD_BIN=$(command -v apd 2>/dev/null)
MAG_BIN=""
for p in "$(command -v magisk 2>/dev/null)" /sbin/magisk /debug_ramdisk/magisk /system/bin/magisk; do
  if [ -n "$p" ] && [ -x "$p" ]; then MAG_BIN="$p"; break; fi
done

TARGET_MGR="$PREF"
if [ "$TARGET_MGR" = "auto" ]; then
  TARGET_MGR="none"
  if [ -n "$MAG_BIN" ] && "$MAG_BIN" -v >/dev/null 2>&1; then TARGET_MGR="magisk"; fi
  if [ -n "$APD_BIN" ] && [ -d /data/adb/ap ] && "$APD_BIN" -V >/dev/null 2>&1; then
    if [ "$TARGET_MGR" = "none" ] || [ -f /data/adb/ap/package_config ]; then TARGET_MGR="apatch"; fi
  fi
  if [ -n "$KSU_BIN" ] && [ -d /data/adb/ksu ] && "$KSU_BIN" -V >/dev/null 2>&1; then
    if [ "$TARGET_MGR" = "none" ] || [ "$TARGET_MGR" != "magisk" ]; then TARGET_MGR="ksu"; fi
  fi
  if [ "$TARGET_MGR" = "none" ]; then
    if [ -n "$KSU_BIN" ]; then TARGET_MGR="ksu"
    elif [ -n "$APD_BIN" ]; then TARGET_MGR="apatch"
    elif [ -n "$MAG_BIN" ]; then TARGET_MGR="magisk"
    fi
  fi
fi

INSTALL_BIN=""
case "$TARGET_MGR" in
  ksu) INSTALL_BIN="$KSU_BIN"; echo "[*] 选定刷入后端: KernelSU ($INSTALL_BIN)" ;;
  apatch) INSTALL_BIN="$APD_BIN"; echo "[*] 选定刷入后端: APatch ($INSTALL_BIN)" ;;
  magisk) INSTALL_BIN="$MAG_BIN"; echo "[*] 选定刷入后端: Magisk ($INSTALL_BIN)" ;;
  *) echo "[!] 未检测到可用的 KernelSU (ksud) / APatch (apd) / Magisk 命令行！"; finish 10 ;;
esac

if [ -z "$INSTALL_BIN" ] || [ ! -x "$INSTALL_BIN" ]; then
  echo "[!] 所选管理器二进制不存在或不可执行: $TARGET_MGR"
  finish 11
fi

echo "[MF_STAGE:downloading]"
CURL_BIN=$(command -v curl 2>/dev/null)
WGET_BIN=$(command -v wget 2>/dev/null)
if [ -z "$WGET_BIN" ]; then
  for bb in /data/adb/ksu/bin/busybox /data/adb/ap/bin/busybox /data/adb/magisk/busybox; do
    if [ -x "$bb" ]; then WGET_BIN="$bb wget"; break; fi
  done
fi
if [ -z "$CURL_BIN" ] && [ -z "$WGET_BIN" ]; then
  echo "[!] 系统缺少 curl 与 wget，无法下载更新包。"
  finish 12
fi

DL_OK=0
for URL in ${quotedUrls}; do
  echo "[*] 正在下载更新包: $URL"
  rm -f "$ZIP"
  if [ -n "$CURL_BIN" ]; then
    "$CURL_BIN" -fSL --connect-timeout 12 --max-time 180 -A "MirageForge-OTA" -o "$ZIP" "$URL"
    RC=$?
  else
    $WGET_BIN -T 20 -O "$ZIP" "$URL"
    RC=$?
  fi
  if [ "$RC" -eq 0 ] && [ -s "$ZIP" ]; then
    DL_OK=1
    echo "[+] 下载完成，已保存至临时文件"
    break
  else
    echo "[!] 当前线路下载失败 (code=$RC)，尝试下一线路..."
  fi
done
if [ "$DL_OK" -ne 1 ]; then echo "[!] 所有下载线路均失败。"; finish 13; fi

echo "[MF_STAGE:verifying]"
FILE_SIZE=$(wc -c < "$ZIP" 2>/dev/null | tr -d ' ')
echo "[*] 更新包大小: \${FILE_SIZE:-0} bytes"
if [ -z "$FILE_SIZE" ] || [ "$FILE_SIZE" -lt 10240 ]; then echo "[!] 更新包体积异常过小。"; finish 14; fi
if [ "$(head -c 2 "$ZIP" 2>/dev/null)" != "PK" ]; then echo "[!] 文件头校验失败：非有效 ZIP。"; finish 15; fi
UNZIP_BIN=$(command -v unzip 2>/dev/null)
if [ -n "$UNZIP_BIN" ]; then
  if "$UNZIP_BIN" -tq "$ZIP" >/dev/null 2>&1; then echo "[+] ZIP 完整性校验通过"
  else echo "[!] ZIP 已损坏！"; finish 16; fi
else
  echo "[+] ZIP 魔数头校验通过 (PK)"
fi

echo "[MF_STAGE:installing]"
echo "------------------------------------------"
echo "[*] 正在调用 $TARGET_MGR 安装模块，以下为脚本实时输出:"
echo "------------------------------------------"
case "$TARGET_MGR" in
  ksu) "$INSTALL_BIN" module install "$ZIP"; INST_RC=$? ;;
  apatch) "$INSTALL_BIN" module install "$ZIP"; INST_RC=$? ;;
  magisk) "$INSTALL_BIN" --install-module "$ZIP"; INST_RC=$? ;;
esac
echo "------------------------------------------"
if [ "$INST_RC" -eq 0 ]; then
  echo "[+] 模块刷入成功 (exit=0)！请重启设备使新版本生效。"
  echo "[MF_STAGE:done]"
  finish 0
else
  echo "[!] 模块刷入失败 (exit=$INST_RC)"
  finish "$INST_RC"
fi
`;
}

export async function startOtaInstall() {
  if (mirageStore.ota.running) return;
  const zipUrl = mirageStore.updateInfo.zipUrl.trim();
  if (!zipUrl || !isSafeHttpUrl(zipUrl)) {
    mirageStore.ota.stage = 'error';
    mirageStore.ota.errorMsg = '更新包下载地址无效，请先重新检查更新';
    showToast(mirageStore.ota.errorMsg);
    return;
  }

  stopOtaPolling();
  mirageStore.ota.running = true;
  mirageStore.ota.stage = 'detecting';
  mirageStore.ota.errorMsg = '';
  mirageStore.ota.logs = ['[*] 正在初始化 OTA 刷入环境...'];
  triggerHaptic('medium');

  if (detectRuntimeEnv() === 'BrowserMock') {
    const mockSteps: Array<{ delay: number; stage: OtaStage; line: string }> = [
      { delay: 400, stage: 'detecting', line: '[*] 选定刷入后端: KernelSU (/data/adb/ksud)' },
      { delay: 900, stage: 'downloading', line: `[*] 正在下载更新包: ${zipUrl}` },
      { delay: 1600, stage: 'downloading', line: '[+] 下载完成，已保存至临时文件 (2.4 MB)' },
      { delay: 2000, stage: 'verifying', line: '[+] ZIP 压缩包 CRC 完整性校验通过' },
      { delay: 2500, stage: 'installing', line: '- Unzipping & installing MirageForge...' },
      { delay: 3200, stage: 'success', line: '[+] 模块刷入成功 (exit=0)！请重启设备使新版本生效。' },
    ];
    for (const s of mockSteps) {
      setTimeout(() => {
        mirageStore.ota.stage = s.stage;
        mirageStore.ota.logs.push(s.line);
        if (s.stage === 'success') {
          mirageStore.ota.running = false;
          triggerHaptic('success');
        }
      }, s.delay);
    }
    return;
  }

  await detectRootManagerForOta();
  const urls = buildDownloadUrlList(zipUrl, mirageStore.ota.mirrorMode);
  const scriptContent = buildOtaInstallScript(
    urls,
    mirageStore.ota.selectedManager,
    mirageStore.updateInfo.latestVersion || 'latest'
  );

  await removeFileZeroFork(OTA_EXIT_FILE);
  await removeFileZeroFork(OTA_LOG_FILE);

  const written = await writeFileZeroFork(OTA_SCRIPT_FILE, scriptContent);
  if (!written) {
    mirageStore.ota.running = false;
    mirageStore.ota.stage = 'error';
    mirageStore.ota.errorMsg = '无法写入临时刷入脚本至 /data/local/tmp';
    mirageStore.ota.logs.push(`[!] ${mirageStore.ota.errorMsg}`);
    return;
  }

  const launchRes = await execRootShell(
    `chmod 700 "${OTA_SCRIPT_FILE}" && ( /system/bin/sh "${OTA_SCRIPT_FILE}" </dev/null >/dev/null 2>&1 & )`
  );
  if (launchRes.errno !== 0) {
    mirageStore.ota.running = false;
    mirageStore.ota.stage = 'error';
    mirageStore.ota.errorMsg = `启动 OTA 后台进程失败 (errno=${launchRes.errno})`;
    mirageStore.ota.logs.push(`[!] ${mirageStore.ota.errorMsg}`);
    return;
  }

  otaPollStartTs = Date.now();
  let pollingBusy = false;

  otaPollTimer = setInterval(async () => {
    if (pollingBusy) return;
    pollingBusy = true;
    try {
      const rawLog = await readFileZeroFork(OTA_LOG_FILE);
      if (rawLog) {
        const parsed = stripAnsiAndParseLogs(rawLog);
        if (parsed.lines.length > 0) mirageStore.ota.logs = parsed.lines;
        if (parsed.stage) mirageStore.ota.stage = parsed.stage;
      }

      const exitRaw = (await readFileZeroFork(OTA_EXIT_FILE)).trim();
      if (exitRaw.length > 0) {
        stopOtaPolling();
        const code = parseInt(exitRaw, 10);
        mirageStore.ota.running = false;
        await removeFileZeroFork(OTA_SCRIPT_FILE);
        await removeFileZeroFork(OTA_EXIT_FILE);

        if (code === 0) {
          mirageStore.ota.stage = 'success';
          mirageStore.ota.errorMsg = '';
          triggerHaptic('success');
          showToast('更新刷入完成，重启设备后生效');
        } else {
          mirageStore.ota.stage = 'error';
          mirageStore.ota.errorMsg = `刷入中断，退出码: ${isNaN(code) ? exitRaw : code}`;
          triggerHaptic('heavy');
          showToast(mirageStore.ota.errorMsg);
        }
      } else if (Date.now() - otaPollStartTs > 300000) {
        stopOtaPolling();
        mirageStore.ota.running = false;
        mirageStore.ota.stage = 'error';
        mirageStore.ota.errorMsg = '更新执行超时 (超过 5 分钟)，请重试';
        mirageStore.ota.logs.push(`[!] ${mirageStore.ota.errorMsg}`);
      }
    } finally {
      pollingBusy = false;
    }
  }, 250);
}

export async function rebootDeviceNow() {
  triggerHaptic('heavy');
  showToast('正在重启设备...');
  await execRootShell('svc power reboot || reboot');
}

export function encodeTemplateShareCode(tpl: DeviceTemplate): string {
  const normalized = normalizeTemplateVulkanFields(tpl);
  const json = JSON.stringify(normalized);
  return `mf://${encodeBase64Url(json)}`;
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
  if (clean) localStorage.setItem('mirage_hub_url', clean);
  else localStorage.removeItem('mirage_hub_url');
}

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
      const { text, via } = await fetchTextWithFallback(src.url);
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
  if (forceToast) showToast(`云端拉取失败，已切换至离线精选库 (${lastErr})`);
}

function normalizeAutoSpoofConfig(raw?: Partial<AutoSpoofConfig> | null): AutoSpoofConfig {
  const def = createDefaultAutoSpoof();
  if (!raw) return def;
  const build = raw.build !== false;
  const cpu = raw.cpu !== false;
  const mem = raw.mem !== false;
  const vfsNodes = raw.vfsNodes !== false;
  const anyVfs = cpu || mem || vfsNodes;
  const excludes = Array.isArray(raw.excludePackages)
    ? Array.from(new Set(raw.excludePackages.map((s) => String(s || '').trim()).filter(Boolean)))
    : [];

  return {
    enabled: Boolean(raw.enabled),
    tplId: String(raw.tplId || ''),
    build,
    propMode: build ? sanitizePropMode(raw.propMode || 'cow') : 'hook',
    gl: raw.gl !== false,
    cpu,
    mem,
    vfsNodes,
    mountNs: Boolean(raw.mountNs) && anyVfs,
    hdrSpoof: Boolean(raw.hdrSpoof) && (build || raw.gl !== false),
    refreshRate: sanitizeRefreshRate(raw.refreshRate),
    excludePackages: excludes,
  };
}

async function flushStateZeroFork() {
  const rLines = [`# MirageForge v${mirageStore.version} Routes Table (v3 16-col)`];
  for (const [pkg, rawR] of Object.entries(mirageStore.appRoutes)) {
    const r = normalizeAppRoute(rawR);
    const tpl = mirageStore.templates[r.tplId];
    if (!tpl || !isRouteAnyEnabled(r)) {
      await removeFileZeroFork(`${APPS_DIR_PATH}/${pkg}.conf`);
      delete mirageStore.appRoutes[pkg];
      continue;
    }
    mirageStore.appRoutes[pkg] = r;
    const confText = compileModularConfInTs(tpl, r);
    await writeFileZeroFork(`${APPS_DIR_PATH}/${pkg}.conf`, confText);
    const ovrB64 = !isAppOverridesEmpty(r.overrides)
      ? encodeBase64Url(JSON.stringify(r.overrides))
      : '';
    rLines.push(
      `${pkg}|${r.tplId}|${r.build ? 1 : 0}|${r.gl ? 1 : 0}|${r.vfs ? 1 : 0}|${r.probe ? 1 : 0}|${r.sdkSpoof ? 1 : 0}|${r.refreshRate}|${r.mountNs ? 1 : 0}|${r.cowProp ? 1 : 0}|${r.cpu ? 1 : 0}|${r.mem ? 1 : 0}|${r.vfsNodes ? 1 : 0}|${r.propMode}|${r.hdrSpoof ? 1 : 0}|${ovrB64}`
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
        autoSpoof: mirageStore.autoSpoof,
      },
      null,
      2
    )
  );
}

function normalizeLoadedTemplates(rawMap: Record<string, any>): Record<string, DeviceTemplate> {
  const out: Record<string, DeviceTemplate> = {};
  for (const [id, tpl] of Object.entries(rawMap || {})) {
    if (tpl && tpl.MODEL) out[id] = normalizeTemplateVulkanFields({ ...tpl, id });
  }
  return out;
}

function normalizeLoadedRoutes(rawRoutes: Record<string, any>): Record<string, AppRoute> {
  const out: Record<string, AppRoute> = {};
  for (const [pkg, r] of Object.entries(rawRoutes || {})) {
    if (!r || !r.tplId) continue;
    const nr = normalizeAppRoute(r);
    if (isRouteAnyEnabled(nr)) out[pkg] = nr;
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

export async function refreshRuntimeStatus() {
  if (hasZeroForkIo()) {
    const raw = await readFileZeroFork(RUNTIME_STATUS_FILE);
    if (raw.trim()) {
      try {
        const parsed = JSON.parse(raw);
        if (parsed && typeof parsed === 'object') {
          mirageStore.runtimeStatus = parsed;
          return;
        }
      } catch (_) {}
    }
  }
  const res = await execRootShell(`"${CLI_BIN}" runtime-status list --json`);
  if (res.errno === 0 && res.stdout.trim()) {
    try {
      const parsed = JSON.parse(res.stdout);
      if (parsed && typeof parsed === 'object') {
        mirageStore.runtimeStatus = parsed;
      }
    } catch (_) {}
  }
}

export async function refreshWatcherStatus() {
  if (detectRuntimeEnv() === 'BrowserMock') {
    mirageStore.watcherStatus = { running: mirageStore.autoSpoof.enabled, pid: 1024 };
    return;
  }
  const res = await execRootShell(`"${CLI_BIN}" auto-spoof status`);
  if (res.errno === 0 && res.stdout.trim()) {
    try {
      const parsed = JSON.parse(res.stdout);
      mirageStore.watcherStatus = {
        running: Boolean(parsed.watcherRunning),
        pid: Number(parsed.watcherPid || 0),
      };
    } catch (_) {}
  }
}

export async function ensureWatcherRunningViaCli() {
  if (detectRuntimeEnv() === 'BrowserMock') {
    mirageStore.watcherStatus = { running: true, pid: 1024 };
    return;
  }
  await execRootShell(`( nohup "${CLI_BIN}" watch-installs </dev/null >/dev/null 2>&1 & )`);
  await new Promise((r) => setTimeout(r, 320));
  await refreshWatcherStatus();
}

export async function saveAutoSpoofViaCli(rawCfg: AutoSpoofConfig, silent = false) {
  const cfg = normalizeAutoSpoofConfig(rawCfg);
  if (cfg.enabled && !cfg.tplId) {
    const firstTpl = Object.keys(mirageStore.templates)[0] || '';
    if (!firstTpl) {
      showToast('请先至少保留 1 套硬件模板');
      return;
    }
    cfg.tplId = firstTpl;
  }

  mirageStore.autoSpoof = cfg;
  const payload = JSON.stringify(cfg);

  if (detectRuntimeEnv() === 'BrowserMock') {
    mirageStore.watcherStatus = { running: cfg.enabled, pid: cfg.enabled ? 1024 : 0 };
    if (!silent) showToast(cfg.enabled ? '已开启新应用自动伪装' : '已关闭新应用自动伪装');
    return;
  }

  const b64 = btoa(unescape(encodeURIComponent(payload)));
  const res = await execRootShell(`echo "${b64}" | base64 -d | "${CLI_BIN}" auto-spoof set-stdin`);
  if (res.errno !== 0 && hasZeroForkIo()) {
    await writeFileZeroFork(AUTO_SPOOF_JSON_FILE, JSON.stringify(cfg, null, 2) + '\n');
  }
  if (cfg.enabled) {
    await ensureWatcherRunningViaCli();
  } else {
    await refreshWatcherStatus();
  }
  triggerHaptic('success');
  if (!silent) {
    showToast(cfg.enabled ? '已更新新应用自动托管策略' : '已关闭新应用自动托管');
  }
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
        if (data.autoSpoof) {
          mirageStore.autoSpoof = normalizeAutoSpoofConfig(data.autoSpoof);
        }
        if (!mirageStore.version || mirageStore.version === '...') {
          mirageStore.version = data.version || '1.3.0';
          mirageStore.versionCode = data.versionCode || 130;
        }
        const rawAuto = await readFileZeroFork(AUTO_SPOOF_JSON_FILE);
        if (rawAuto.trim()) {
          try {
            mirageStore.autoSpoof = normalizeAutoSpoofConfig(JSON.parse(rawAuto));
          } catch (_) {}
        }
        await refreshRuntimeStatus();
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
      if (data.autoSpoof) {
        mirageStore.autoSpoof = normalizeAutoSpoofConfig(data.autoSpoof);
      }
      if (data.runtimeStatus && typeof data.runtimeStatus === 'object') {
        mirageStore.runtimeStatus = data.runtimeStatus;
      }
    } catch (_) {}
  }
}

export async function reloadAppList() {
  mirageStore.loading = true;
  const configured = Object.keys(mirageStore.appRoutes);
  const [apps] = await Promise.all([
    fetchInstalledAppsZeroFork(mirageStore.includeSystemApps, configured),
    refreshRuntimeStatus(),
  ]);
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
    if (mirageStore.autoSpoof.tplId === id) {
      mirageStore.autoSpoof.enabled = false;
      mirageStore.autoSpoof.tplId = '';
      await writeFileZeroFork(
        AUTO_SPOOF_JSON_FILE,
        JSON.stringify(mirageStore.autoSpoof, null, 2) + '\n'
      );
    }
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
  const shouldRestart = restart || settingsStore.autoRestartOnSave;

  if (hasZeroForkIo()) {
    if (!route || !route.tplId) {
      delete mirageStore.appRoutes[pkg];
      await removeFileZeroFork(`${APPS_DIR_PATH}/${pkg}.conf`);
    } else {
      const nr = normalizeAppRoute(route);
      if (!isRouteAnyEnabled(nr)) {
        delete mirageStore.appRoutes[pkg];
        await removeFileZeroFork(`${APPS_DIR_PATH}/${pkg}.conf`);
      } else {
        mirageStore.appRoutes[pkg] = nr;
      }
    }
    await flushStateZeroFork();
    if (shouldRestart) await execRootShell(`am force-stop "${pkg}"`);
    return;
  }

  if (!route || !route.tplId) {
    await execRootShell(`"${CLI_BIN}" route remove "${pkg}" ${shouldRestart ? '--restart' : ''}`);
  } else {
    const nr = normalizeAppRoute(route);
    const ovrB64 = !isAppOverridesEmpty(nr.overrides)
      ? encodeBase64Url(JSON.stringify(nr.overrides))
      : '';
    const cmd = [
      `"${CLI_BIN}" route set "${pkg}" "${nr.tplId}"`,
      `--build ${nr.build ? 1 : 0}`,
      `--prop-mode ${nr.propMode}`,
      `--cow-prop ${nr.cowProp ? 1 : 0}`,
      `--gl ${nr.gl ? 1 : 0}`,
      `--hdr ${nr.hdrSpoof ? 1 : 0}`,
      `--vfs ${nr.vfs ? 1 : 0}`,
      `--cpu ${nr.cpu ? 1 : 0}`,
      `--mem ${nr.mem ? 1 : 0}`,
      `--vfs-nodes ${nr.vfsNodes ? 1 : 0}`,
      `--probe ${nr.probe ? 1 : 0}`,
      `--sdk ${nr.sdkSpoof ? 1 : 0}`,
      `--fps ${nr.refreshRate}`,
      `--mount-ns ${nr.mountNs ? 1 : 0}`,
      ovrB64 ? `--overrides-b64 "${ovrB64}"` : '',
      shouldRestart ? '--restart' : '',
    ]
      .filter(Boolean)
      .join(' ');
    await execRootShell(cmd);
  }
  await syncStateFromRustCli();
}

export async function applyGlobalPropModeToAllRoutes(mode: PropSpoofMode) {
  const clean = sanitizePropMode(mode);
  const entries = Object.entries(mirageStore.appRoutes);
  if (entries.length === 0) {
    showToast('当前没有已配置的应用');
    return;
  }
  let changed = 0;
  for (const [pkg, r] of entries) {
    if (!r.build) continue;
    mirageStore.appRoutes[pkg] = normalizeAppRoute({
      ...r,
      propMode: clean,
    });
    changed++;
  }
  if (changed === 0) {
    showToast('当前没有开启「伪装系统属性」的应用');
    return;
  }
  if (hasZeroForkIo()) {
    await flushStateZeroFork();
  } else {
    for (const [pkg, r] of Object.entries(mirageStore.appRoutes)) {
      if (r.build) await updateAppRouteViaCli(pkg, r, false);
    }
  }
  const label = clean === 'mount_ns' ? 'MountNS' : clean === 'cow' ? '纯 COW' : '传统 Hook';
  triggerHaptic('success');
  showToast(`已将系统属性引擎 (${label}) 同步至 ${changed} 款应用`);
}

export async function applyGlobalEngineFlagToAllRoutes(
  field: 'mountNs' | 'cowProp',
  enabled: boolean
) {
  if (field === 'cowProp') {
    await applyGlobalPropModeToAllRoutes(enabled ? 'cow' : 'hook');
    return;
  }
  const entries = Object.entries(mirageStore.appRoutes);
  if (entries.length === 0) {
    showToast('当前没有已配置的应用');
    return;
  }
  for (const [pkg, r] of entries) {
    const next = normalizeAppRoute({
      ...r,
      mountNs: enabled && r.vfs,
    });
    mirageStore.appRoutes[pkg] = next;
  }
  if (hasZeroForkIo()) {
    await flushStateZeroFork();
  } else {
    for (const [pkg, r] of Object.entries(mirageStore.appRoutes)) {
      await updateAppRouteViaCli(pkg, r, false);
    }
  }
  triggerHaptic('success');
  showToast(`已将 MountNS 策略同步至 ${entries.length} 款应用`);
}

export async function exportDiagnosticBundleViaCli() {
  triggerHaptic('medium');
  const outPath = `/sdcard/Download/MirageForge_Diag_${Date.now()}.txt`;
  const res = await execRootShell(`"${CLI_BIN}" probe bundle "${outPath}"`);
  if (res.errno === 0 && res.stdout.includes('true')) {
    triggerHaptic('success');
    showToast(`诊断包已导出至 ${outPath}`);
  } else {
    showToast('导出诊断包失败');
  }
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

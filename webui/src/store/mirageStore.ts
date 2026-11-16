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
  GL_VENDOR: string;
  GL_RENDERER: string;
  GL_VERSION: string;
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

export async function checkOnlineUpdate(t: (k: string, p?: any) => string) {
  mirageStore.updateInfo.checking = true;
  triggerHaptic('light');
  try {
    const resp = await fetch(`${UPDATE_JSON_URL}?t=${Date.now()}`, { cache: 'no-store' });
    if (!resp.ok) throw new Error(`HTTP ${resp.status}`);
    const data = await resp.json();
    const remoteCode = Number(data.versionCode || 0);
    const remoteVer = String(data.version || '').replace(/^v/, '');

    mirageStore.updateInfo.latestCode = remoteCode;
    mirageStore.updateInfo.latestVersion = remoteVer;
    if (data.zipUrl) mirageStore.updateInfo.zipUrl = data.zipUrl;

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

function isSdkOrReleasePropKey(k: string): boolean {
  return (
    k.includes('version.sdk') ||
    k.includes('version.release') ||
    k.includes('version.preview_sdk') ||
    k.includes('version.codename') ||
    k.includes('first_api_level')
  );
}

function isDpiPropKey(k: string): boolean {
  return (
    k.includes('lcd_density') ||
    k === 'persist.sys.dpi' ||
    k === 'ro.vendor.display.default_dpi'
  );
}

/**
 * TypeScript 内存级微模块 .conf 编译器（与 Rust cli/src/compiler.rs 100% 严格对齐）
 */
function compileModularConfInTs(tpl: DeviceTemplate, route: AppRoute): string {
  const lines: string[] = [];
  const push = (k: string, v: string | number | undefined) => {
    if (k && v !== undefined && String(v).length > 0) {
      lines.push(`${k}=${v}`);
    }
  };

  push('META:ENABLE_BUILD', route.build ? '1' : '0');
  push('META:ENABLE_GL', route.gl ? '1' : '0');
  push('META:ENABLE_VFS', route.vfs ? '1' : '0');
  push('META:ENABLE_PROBE', route.probe ? '1' : '0');
  push('META:FORCE_SPOOF_SDK', route.sdkSpoof ? '1' : '0');

  if (route.gl) {
    push('GLES:VENDOR', tpl.GL_VENDOR);
    push('GLES:RENDERER', tpl.GL_RENDERER);
    push('GLES:VERSION', tpl.GL_VERSION);
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
    const ramStr = `${ramGb}GB`;
    const ddrFull = `${ramStr},${ddrVendor},${ddrType}`;
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

    // 严格遵守 sdkSpoof 0 改动边界
    push('VERSION:INCREMENTAL', incremental);
    push('VERSION:SECURITY_PATCH', tpl.SECURITY_PATCH);
    if (route.sdkSpoof) {
      push('VERSION:RELEASE', tpl.RELEASE);
      push('VERSION:SDK_INT', tpl.SDK_INT);
      push('VERSION:DEVICE_INITIAL_SDK_INT', tpl.SDK_INT);
      push('VERSION:CODENAME', 'REL');
      push('VERSION:PREVIEW_SDK_INT', '0');
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
      if (route.sdkSpoof) {
        push(`ro.${part}build.version.release`, tpl.RELEASE);
        push(`ro.${part}build.version.sdk`, tpl.SDK_INT);
      }
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

    // 静态 Bootloader 锁状态属性（不干预 sys.usb / adbd 运行态属性）
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
    push('ro.boot.hardware.sku', tpl.DEVICE);
    push('ro.boot.product.hardware.sku', tpl.DEVICE);

    // 物理 DDR 对称属性
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

    for (const raw of (tpl.CUSTOM_PROPS || '').split(/\r?\n/)) {
      const tr = raw.trim();
      if (!tr || tr.startsWith('#')) continue;
      const eqIdx = tr.indexOf('=');
      if (eqIdx <= 0) continue;
      const k = tr.slice(0, eqIdx).trim();
      if (isDpiPropKey(k)) continue;
      if (!route.sdkSpoof && isSdkOrReleasePropKey(k)) continue;
      lines.push(tr);
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
    const confText = compileModularConfInTs(tpl, r);
    await writeFileZeroFork(`${APPS_DIR_PATH}/${pkg}.conf`, confText);
    rLines.push(
      `${pkg}|${r.tplId}|${r.build ? 1 : 0}|${r.gl ? 1 : 0}|${r.vfs ? 1 : 0}|${r.probe ? 1 : 0}|${r.sdkSpoof ? 1 : 0}`
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
        mirageStore.templates = data.templates || {};
        mirageStore.appRoutes = data.appRoutes || {};
        if (!mirageStore.version || mirageStore.version === '...') {
          mirageStore.version = data.version || '1.2.0';
          mirageStore.versionCode = data.versionCode || 120;
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
        mirageStore.version = data.version || '1.2.0';
        mirageStore.versionCode = data.versionCode || 120;
      }
      mirageStore.templates = data.templates || {};
      mirageStore.appRoutes = data.appRoutes || {};
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

export async function saveTemplateViaCli(tpl: DeviceTemplate) {
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
        return parsed as DeviceTemplate;
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
      mirageStore.appRoutes[pkg] = { ...route };
    }
    await flushStateZeroFork();
    if (restart) await execRootShell(`am force-stop "${pkg}"`);
    return;
  }

  if (!route || !route.tplId) {
    await execRootShell(`"${CLI_BIN}" route remove "${pkg}" ${restart ? '--restart' : ''}`);
  } else {
    const cmd = `"${CLI_BIN}" route set "${pkg}" "${route.tplId}" --build ${route.build ? 1 : 0} --gl ${route.gl ? 1 : 0} --vfs ${route.vfs ? 1 : 0} --probe ${route.probe ? 1 : 0} --sdk ${route.sdkSpoof ? 1 : 0} ${restart ? '--restart' : ''}`;
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

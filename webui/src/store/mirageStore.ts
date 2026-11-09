import { reactive, ref } from 'vue';
import {
  execRootShell,
  encodeUtf8Base64,
  fetchInstalledApps,
  showToast,
  triggerHaptic,
  type InstalledAppInfo,
} from '../bridge/webui-x';

export const CLI_BIN = '/data/adb/modules/mirage_forge/bin/mirageforge_cli';

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
  BOOTLOADER?: string;
  SOC_MANUFACTURER: string;
  SOC_MODEL: string;
  SUPPORTED_ABIS: string;
  BUILD_ID: string;
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

export const mirageStore = reactive({
  version: '1.1.0',
  loading: false,
  templates: {} as Record<string, DeviceTemplate>,
  appRoutes: {} as Record<string, AppRoute>,
  installedApps: [] as InstalledAppInfo[],
  includeSystemApps: false,
  probeEvents: [] as ProbeEvent[],
  crashEvents: [] as CrashEvent[],
  telemetryEvents: [] as TelemetryEvent[],
});

export const activeToastMsg = ref<string>('');
let toastTimer: any = null;

export function triggerLiquidToast(msg: string) {
  activeToastMsg.value = msg;
  if (toastTimer) clearTimeout(toastTimer);
  toastTimer = setTimeout(() => {
    activeToastMsg.value = '';
  }, 2600);
}

/**
 * 将 "0-5:614400:3532800;6-7:806400:4320000" 解析为可视化分簇条数组
 */
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
  const res = await execRootShell(`"${CLI_BIN}" state --json`);
  if (res.errno === 0 && res.stdout.trim()) {
    try {
      const data = JSON.parse(res.stdout);
      mirageStore.version = data.version || '1.1.0';
      mirageStore.templates = data.templates || {};
      mirageStore.appRoutes = data.appRoutes || {};
    } catch (e) {
      console.error('CLI state JSON parse failed:', e);
    }
  }
}

export async function reloadAppList() {
  mirageStore.loading = true;
  const configured = Object.keys(mirageStore.appRoutes);
  const apps = await fetchInstalledApps(mirageStore.includeSystemApps, configured);
  mirageStore.installedApps = apps.sort((a, b) => {
    const aConf = Boolean(mirageStore.appRoutes[a.packageName]);
    const bConf = Boolean(mirageStore.appRoutes[b.packageName]);
    if (aConf !== bConf) return aConf ? -1 : 1;
    return a.appLabel.localeCompare(b.appLabel, 'zh-CN');
  });
  mirageStore.loading = false;
}

export async function saveTemplateViaCli(tpl: DeviceTemplate) {
  const b64 = encodeUtf8Base64(JSON.stringify(tpl));
  await execRootShell(`echo "${b64}" | base64 -d | "${CLI_BIN}" template set-stdin`);
  await syncStateFromRustCli();
  triggerHaptic('success');
  showToast(`已铸造并编译模板: ${tpl.name}`);
}

export async function deleteTemplateViaCli(id: string) {
  await execRootShell(`"${CLI_BIN}" template delete "${id}" --json`);
  await syncStateFromRustCli();
  triggerHaptic('medium');
  showToast('已删除模板并解绑关联应用');
}

export async function parsePropTextViaCli(rawText: string): Promise<DeviceTemplate | null> {
  const b64 = encodeUtf8Base64(rawText);
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

  try {
    mirageStore.probeEvents = JSON.parse(probeRes.stdout || '[]');
  } catch (_) {
    mirageStore.probeEvents = [];
  }
  try {
    mirageStore.crashEvents = JSON.parse(crashRes.stdout || '[]');
  } catch (_) {
    mirageStore.crashEvents = [];
  }
  try {
    mirageStore.telemetryEvents = JSON.parse(teleRes.stdout || '[]');
  } catch (_) {
    mirageStore.telemetryEvents = [];
  }
}

export async function captureUnmappedPropViaCli(pkg: string, key: string, val: string) {
  const safeVal = val.trim().replace(/"/g, '');
  await execRootShell(`"${CLI_BIN}" probe capture "${pkg}" "${key}" "${safeVal}"`);
  await syncStateFromRustCli();
  triggerHaptic('success');
  showToast(`已捕获 ${key}=${safeVal}`);
}

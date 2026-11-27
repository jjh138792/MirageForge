// SPDX-License-Identifier: GPL-3.0-only
// Copyright (c) 2025-2026 jjh138792
// MirageForge: https://github.com/jjh138792/MirageForge

export type ThemeMode = 'auto' | 'light' | 'dark';
export type AccentTheme = 'violet' | 'cyan' | 'emerald' | 'rose' | 'amber';
export type DockStyle = 'floating' | 'docked';
export type RefractionLevel = 'standard' | 'vivid' | 'off';
export type PropSpoofMode = 'hook' | 'cow' | 'mount_ns';

export type RootManagerKind = 'auto' | 'ksu' | 'apatch' | 'magisk';
export type OtaMirrorMode = 'auto' | 'ghfast' | 'ghproxy' | 'direct';
export type OtaStage =
  | 'idle'
  | 'detecting'
  | 'downloading'
  | 'verifying'
  | 'installing'
  | 'success'
  | 'error';

export interface AppSettings {
  themeMode: ThemeMode;
  accentColor: AccentTheme;
  dockStyle: DockStyle;
  refractionLevel: RefractionLevel;
  defaultMountNs: boolean;
  defaultPropMode: PropSpoofMode;
  /** 兼容保留位，始终与 defaultPropMode === 'cow' 同步 */
  defaultCowProp: boolean;
  autoRestartOnSave: boolean;
}

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
  VK_DEVICE_NAME: string;
  VK_VENDOR_ID: string;
  VK_DEVICE_ID: string;
  VK_API_VERSION: string;
  VK_DRIVER_VERSION: string;
  VK_DRIVER_NAME: string;
  VK_DRIVER_INFO: string;
  MAX_REFRESH_RATE: number;
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

export interface AppOverrides {
  ramGb?: number;
  cpuCores?: number;
  customProps?: string;
}

export interface AppRoute {
  tplId: string;
  build: boolean;
  propMode: PropSpoofMode;
  /** 兼容衍生位：build && propMode === 'cow' */
  cowProp: boolean;
  gl: boolean;
  vfs: boolean;
  cpu: boolean;
  mem: boolean;
  vfsNodes: boolean;
  probe: boolean;
  sdkSpoof: boolean;
  hdrSpoof: boolean;
  refreshRate: number;
  mountNs: boolean;
  overrides?: AppOverrides;
}

export interface AutoSpoofConfig {
  enabled: boolean;
  tplId: string;
  build: boolean;
  propMode: PropSpoofMode;
  gl: boolean;
  cpu: boolean;
  mem: boolean;
  vfsNodes: boolean;
  mountNs: boolean;
  hdrSpoof: boolean;
  refreshRate: number;
  excludePackages: string[];
}

export interface RuntimeStatusEntry {
  timestamp: number;
  pid: number;
  propReq: string;
  propEff: string;
  propVerified: number;
  propInserted: number;
  vfsMounted: number;
  vfsRequested: number;
  liveNodes: number;
  zeroResidence: boolean;
  diag: string;
}

export interface WatcherRuntimeState {
  running: boolean;
  pid: number;
}

export interface HostDeviceInfo {
  model: string;
  brand: string;
  marketName: string;
  device: string;
  androidRelease: string;
  sdkInt: string;
  securityPatch: string;
  kernelRelease: string;
  arch: string;
  socModel: string;
  cpuHardware: string;
  cpuCores: number;
  cpuMaxFreq: number;
  cpuClusters: string;
  gpuRenderer: string;
  ramGb: number;
  rootManager: string;
  rootVersion: string;
  zygiskImpl: string;
  zygiskVersion: string;
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
  changelogUrl: string;
  changelogText: string;
  changelogLoading: boolean;
  changelogError: string;
}

export interface OtaRuntimeState {
  stage: OtaStage;
  running: boolean;
  detectedManager: 'ksu' | 'apatch' | 'magisk' | 'none';
  selectedManager: RootManagerKind;
  managerVersion: string;
  availableManagers: {
    ksu: boolean;
    apatch: boolean;
    magisk: boolean;
  };
  mirrorMode: OtaMirrorMode;
  logs: string[];
  errorMsg: string;
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

export function sanitizePropMode(raw: unknown): PropSpoofMode {
  const s = String(raw || '').trim().toLowerCase();
  if (s === 'cow') return 'cow';
  if (s === 'mount_ns' || s === 'mountns') return 'mount_ns';
  return 'hook';
}

export function normalizeAppOverrides(raw?: Partial<AppOverrides> | null): AppOverrides {
  if (!raw) return {};
  const out: AppOverrides = {};
  const ram = Number(raw.ramGb || 0);
  if (Number.isInteger(ram) && ram >= 1 && ram <= 128) {
    out.ramGb = ram;
  }
  const cores = Number(raw.cpuCores || 0);
  if (Number.isInteger(cores) && cores >= 1 && cores <= 32) {
    out.cpuCores = cores;
  }
  const cp = String(raw.customProps || '').trim();
  if (cp.length > 0) {
    out.customProps = cp;
  }
  return out;
}

export function isAppOverridesEmpty(o?: AppOverrides | null): boolean {
  if (!o) return true;
  return !o.ramGb && !o.cpuCores && !(o.customProps && o.customProps.trim().length > 0);
}

export function createDefaultAutoSpoof(): AutoSpoofConfig {
  return {
    enabled: false,
    tplId: '',
    build: true,
    propMode: 'cow',
    gl: true,
    cpu: true,
    mem: true,
    vfsNodes: true,
    mountNs: false,
    hdrSpoof: false,
    refreshRate: 0,
    excludePackages: [],
  };
}

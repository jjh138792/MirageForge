// SPDX-License-Identifier: GPL-3.0-only
// Copyright (c) 2025-2026 jjh138792
// MirageForge: https://github.com/jjh138792/MirageForge

export type ThemeMode = 'auto' | 'light' | 'dark';
export type AccentTheme = 'violet' | 'cyan' | 'emerald' | 'rose' | 'amber';
export type DockStyle = 'floating' | 'docked';
export type RefractionLevel = 'standard' | 'vivid' | 'off';

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

export interface AppRoute {
  tplId: string;
  build: boolean;
  cowProp: boolean;
  gl: boolean;
  vfs: boolean;
  cpu: boolean;
  mem: boolean;
  vfsNodes: boolean;
  probe: boolean;
  sdkSpoof: boolean;
  refreshRate: number;
  mountNs: boolean;
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

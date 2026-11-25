// SPDX-License-Identifier: GPL-3.0-only
// Copyright (c) 2025-2026 jjh138792
// MirageForge: https://github.com/jjh138792/MirageForge

import { reactive } from 'vue';
import { detectRuntimeEnv, execRootShell, showToast, triggerHaptic } from '../bridge/ksu-io';
import { normalizeTemplateVulkanFields } from './confCompiler';
import type { DeviceTemplate, HostDeviceInfo } from './types';

const CLI_BIN_PATH = '/data/adb/modules/mirage_forge/bin/mirageforge_cli';

export const hostDeviceState = reactive<{
  loading: boolean;
  loaded: boolean;
  info: HostDeviceInfo;
}>({
  loading: false,
  loaded: false,
  info: {
    model: '...',
    brand: '',
    marketName: '',
    device: '',
    androidRelease: '...',
    sdkInt: '...',
    securityPatch: '',
    kernelRelease: '...',
    arch: 'aarch64',
    socModel: '...',
    cpuHardware: '...',
    cpuCores: 8,
    cpuMaxFreq: 0,
    cpuClusters: '',
    gpuRenderer: '...',
    ramGb: 0,
    rootManager: '...',
    rootVersion: '',
    zygiskImpl: '...',
    zygiskVersion: '',
  },
});

export async function fetchHostDeviceInfo(force = false) {
  if (hostDeviceState.loading) return;
  if (hostDeviceState.loaded && !force) return;
  hostDeviceState.loading = true;

  if (detectRuntimeEnv() === 'BrowserMock') {
    hostDeviceState.info = {
      model: 'CPH2841',
      brand: 'OPPO',
      marketName: 'OPPO Find X9 Ultra',
      device: 'OP627CL1',
      androidRelease: '16',
      sdkInt: '36',
      securityPatch: '2026-06-05',
      kernelRelease: '6.12.69-android16-6-g242e1a',
      arch: 'aarch64',
      socModel: 'SM8850',
      cpuHardware: 'Qualcomm Technologies, Inc SM8850',
      cpuCores: 8,
      cpuMaxFreq: 4610000,
      cpuClusters: '0-5:614400:3630000;6-7:806400:4610000',
      gpuRenderer: 'Adreno (TM) 840',
      ramGb: 16,
      rootManager: 'KernelSU Next',
      rootVersion: 'v1.0.9 (12880)',
      zygiskImpl: 'ReZygisk',
      zygiskVersion: 'v1.0.0-rc.2',
    };
    hostDeviceState.loaded = true;
    hostDeviceState.loading = false;
    return;
  }

  try {
    const res = await execRootShell(`"${CLI_BIN_PATH}" device-info --json`);
    if (res.errno === 0 && res.stdout.trim()) {
      const parsed = JSON.parse(res.stdout);
      if (parsed && parsed.model) {
        hostDeviceState.info = { ...hostDeviceState.info, ...parsed };
        hostDeviceState.loaded = true;
      }
    }
  } catch (_) {}

  hostDeviceState.loading = false;
}

export async function cloneHostDeviceViaCli(): Promise<DeviceTemplate | null> {
  triggerHaptic('medium');
  const res = await execRootShell(`"${CLI_BIN_PATH}" clone-device`);
  if (res.errno === 0 && res.stdout.trim()) {
    try {
      const parsed = JSON.parse(res.stdout);
      if (parsed && parsed.MODEL) {
        triggerHaptic('success');
        return normalizeTemplateVulkanFields(parsed);
      }
    } catch (_) {}
  }
  showToast('克隆本机硬件信息失败');
  return null;
}

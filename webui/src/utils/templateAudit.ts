// SPDX-License-Identifier: GPL-3.0-only
// Copyright (c) 2025-2026 jjh138792
// MirageForge: https://github.com/jjh138792/MirageForge

import type { DeviceTemplate } from '../store/types';

export interface AuditIssue {
  level: 'warn' | 'error';
  category: 'system' | 'cpu' | 'gpu' | 'memory';
  message: string;
}

export function auditTemplateConsistency(tpl: DeviceTemplate): AuditIssue[] {
  const issues: AuditIssue[] = [];

  if (!tpl.name.trim() || !tpl.MODEL.trim()) {
    issues.push({
      level: 'error',
      category: 'system',
      message: '模板显示名称与手机型号 (MODEL) 不能为空',
    });
  }

  // 1. 平台与 GPU 阵营一致性检测
  const socToken = `${tpl.BOARD} ${tpl.SOC_MODEL} ${tpl.SOC_MANUFACTURER} ${tpl.CPU_HARDWARE}`.toLowerCase();
  const gpuToken = `${tpl.GL_VENDOR} ${tpl.GL_RENDERER} ${tpl.VK_DEVICE_NAME} ${tpl.VK_VENDOR_ID}`.toLowerCase();

  const isQcomSoc =
    socToken.includes('qcom') ||
    socToken.includes('qualcomm') ||
    socToken.includes('sm8') ||
    socToken.includes('sm7') ||
    socToken.includes('sun') ||
    socToken.includes('pineapple') ||
    socToken.includes('kalama');
  const isMtkSoc =
    socToken.includes('mediatek') ||
    socToken.includes('mt6') ||
    socToken.includes('dimensity');

  const isAdrenoGpu = gpuToken.includes('adreno') || gpuToken.includes('0x5143');
  const isArmGpu =
    gpuToken.includes('mali') || gpuToken.includes('immortalis') || gpuToken.includes('0x13b5');

  if (isQcomSoc && isArmGpu) {
    issues.push({
      level: 'warn',
      category: 'gpu',
      message: '高通骁龙平台却配置了 ARM Mali / Immortalis GPU，建议对齐为 Adreno',
    });
  } else if (isMtkSoc && isAdrenoGpu) {
    issues.push({
      level: 'warn',
      category: 'gpu',
      message: '联发科天玑平台却配置了高通 Adreno GPU，建议对齐为 Immortalis / Mali',
    });
  }

  // 2. CPU 核数与分簇范围一致性检测
  if (tpl.CPU_CLUSTERS && tpl.CPU_CLUSTERS.includes(':')) {
    let maxCoreIdx = -1;
    let totalCores = 0;
    for (const seg of tpl.CPU_CLUSTERS.split(';')) {
      const tr = seg.trim();
      if (!tr) continue;
      const [range] = tr.split(':');
      const [sStr, eStr] = (range || '').split('-');
      const s = parseInt(sStr, 10);
      const e = parseInt(eStr ?? sStr, 10);
      if (!isNaN(s) && !isNaN(e) && e >= s) {
        totalCores += e - s + 1;
        if (e > maxCoreIdx) maxCoreIdx = e;
      }
    }
    if (tpl.CPU_CORES > 0 && totalCores > 0 && totalCores !== tpl.CPU_CORES) {
      issues.push({
        level: 'warn',
        category: 'cpu',
        message: `CPU_CORES (${tpl.CPU_CORES}) 与分簇核心总数 (${totalCores}) 不一致`,
      });
    }
  }

  // 3. FINGERPRINT 与 BUILD_ID / RELEASE 一致性检测
  if (tpl.FINGERPRINT && tpl.FINGERPRINT.includes('/')) {
    if (tpl.BUILD_ID && !tpl.FINGERPRINT.includes(tpl.BUILD_ID)) {
      issues.push({
        level: 'warn',
        category: 'system',
        message: `FINGERPRINT 中未包含当前 BUILD_ID (${tpl.BUILD_ID})，可能导致指纹校验穿帮`,
      });
    }
    if (tpl.RELEASE && !tpl.FINGERPRINT.includes(`:${tpl.RELEASE}/`)) {
      issues.push({
        level: 'warn',
        category: 'system',
        message: `FINGERPRINT 中的 Android 版本与 RELEASE (${tpl.RELEASE}) 不一致`,
      });
    }
  }

  // 4. 内存规格合理性检测
  if (tpl.RAM_GB < 2 || tpl.RAM_GB > 64) {
    issues.push({
      level: 'warn',
      category: 'memory',
      message: `物理内存 (${tpl.RAM_GB}GB) 超出常规范围，建议设为 8 / 12 / 16 / 24`,
    });
  }

  return issues;
}

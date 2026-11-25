// SPDX-License-Identifier: GPL-3.0-only
// Copyright (c) 2025-2026 jjh138792
// MirageForge: https://github.com/jjh138792/MirageForge

import type { RefractionLevel } from '../store/types';

function clamp(v: number, lo: number, hi: number): number {
  return Math.max(lo, Math.min(hi, v));
}

function clampByte(v: number): number {
  return clamp(Math.round(v), 0, 255);
}

/**
 * 基于精确圆角胶囊距离场 (Rounded Capsule SDF) + Snell 折射定理生成位移贴图：
 * - 按玻璃球真实 DOM 宽高比生成，保证左右半圆弧曲率与法线方向 100% 物理精确
 * - 中心 56% 顶面：极平滑微放大（位移近乎 0），图标与文字完全端正锐利、零重影、零色散
 * - 外圈 44% 液滴弧壁：沿真实胶囊圆弧法线强烈弯曲包裹，滑过相邻图标与汉字时呈现真液态玻璃边缘拉伸效果
 */
function buildCapsuleSdfRefractionMap(aspectWidth: number, aspectHeight: number): string {
  const safeH = Math.max(40, aspectHeight);
  const ratio = clamp(aspectWidth / safeH, 1.15, 3.2);
  const height = 128;
  const width = Math.round(height * ratio);

  const canvas = document.createElement('canvas');
  canvas.width = width;
  canvas.height = height;
  const ctx = canvas.getContext('2d');
  if (!ctx) return '';

  const imgData = ctx.createImageData(width, height);
  const data = imgData.data;

  const halfW = (width - 1) * 0.5;
  const halfH = (height - 1) * 0.5;
  const radius = halfH; // 完美胶囊：圆角半径等于半高
  const segHalfLen = Math.max(0, halfW - radius);

  // 顶面平坦区比例（0 ~ 0.56 保持清澈端正，0.56 ~ 1.0 为强曲率液滴边缘弧面）
  const rimStart = 0.56;

  for (let y = 0; y < height; y++) {
    const py = y - halfH;
    for (let x = 0; x < width; x++) {
      const px = x - halfW;

      // 到胶囊中心线段的最近点，得出严格垂直于胶囊轮廓的外法线
      const cx = clamp(px, -segHalfLen, segHalfLen);
      const vx = px - cx;
      const vy = py;
      const dist = Math.hypot(vx, vy);
      const t = dist / radius; // 0 = 中心轴线, 1 = 胶囊最外缘

      let dx = 0;
      let dy = 0;

      if (dist > 0.001 && t < 1.0) {
        const nx = vx / dist;
        const ny = vy / dist;

        let mag = 0;
        if (t <= rimStart) {
          // 顶面极微弱凸透镜过渡（最大仅 0.04，确保文字绝不分裂重影）
          const uCenter = t / rimStart;
          mag = 0.04 * uCenter * uCenter;
        } else {
          // 弧壁 Snell 陡峭弯曲：随靠近圆弧外缘急剧拉升，产生真实水面边缘包裹拉伸
          const u = (t - rimStart) / (1.0 - rimStart); // 0..1
          const curve = Math.pow(u, 2.15);
          const meniscus = 1 - Math.sqrt(Math.max(0.01, 1 - u * u));
          mag = 0.04 + 0.96 * (0.55 * curve + 0.45 * meniscus);
        }

        // 最外缘 3% 柔和收边，彻底杜绝边缘越界采样拉出彩条
        if (t > 0.97) {
          mag *= Math.max(0, (1.0 - t) / 0.03);
        }

        // 沿法线向内采样 -> 靠近圆弧的像素被向外包裹拉伸并贴合弧面弯曲
        dx = -nx * mag;
        dy = -ny * mag;
      }

      const idx = (y * width + x) * 4;
      data[idx] = clampByte(128 + dx * 127);
      data[idx + 1] = clampByte(128 + dy * 127);
      data[idx + 2] = 128;
      data[idx + 3] = 255;
    }
  }

  ctx.putImageData(imgData, 0, 0);
  return canvas.toDataURL('image/png');
}

function setFeImageHref(id: string, dataUrl: string) {
  const el = document.getElementById(id);
  if (!el || !dataUrl) return;
  el.setAttribute('href', dataUrl);
  el.setAttributeNS('http://www.w3.org/1999/xlink', 'xlink:href', dataUrl);
}

let refractionMultiplier = 1.0;
let lastRatioKey = '';

export function updateOrbLensGeometry(width: number, height: number) {
  if (typeof document === 'undefined' || width <= 0 || height <= 0) return;
  const ratioKey = (width / height).toFixed(2);
  if (ratioKey === lastRatioKey) return;
  lastRatioKey = ratioKey;
  setFeImageHref('mirage-spherical-map', buildCapsuleSdfRefractionMap(width, height));
}

export function initOpticalLensEngine() {
  if (typeof document === 'undefined') return;
  lastRatioKey = '';
  updateOrbLensGeometry(112, 54);
}

export function setRefractionIntensity(level: RefractionLevel) {
  refractionMultiplier = level === 'off' ? 0 : level === 'vivid' ? 1.32 : 1.0;
  setOrbRefractionScale(22);
}

/**
 * 动态调节液滴折射强度：
 * - 色散固定在极克制的 ±1.5px ~ ±2.0px（仅在最外圈弧面出现淡蓝紫光学边缘，中心 0 重影）
 */
export function setOrbRefractionScale(baseScale: number) {
  const eff = Math.max(0, baseScale * refractionMultiplier);
  const spread = eff > 1 ? Math.min(2.0, eff * 0.075) : 0;

  const dispR = document.getElementById('mirage-orb-disp-r');
  const dispG = document.getElementById('mirage-orb-disp-g');
  const dispB = document.getElementById('mirage-orb-disp-b');
  if (dispR) dispR.setAttribute('scale', (eff + spread).toFixed(2));
  if (dispG) dispG.setAttribute('scale', eff.toFixed(2));
  if (dispB) dispB.setAttribute('scale', Math.max(0, eff - spread).toFixed(2));
}

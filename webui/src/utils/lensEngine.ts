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
 * 物理 Snell 折射公式计算玻璃倒角弧面的归一化位移强度 (0..1)
 * - distFromEdge >= bezelPx（中心平坦区）：严格返回 0 -> R=128, G=128，零偏移、零重影
 * - 0 < distFromEdge < bezelPx（玻璃边缘弧壁）：沿真实外法线按凸面圆弧折射率急剧递增，
 *   让穿过玻璃边缘的底层文字与图标顺着圆弧真实拉长、弯曲、扭曲
 */
function snellBezelDisplacement(distFromEdge: number, bezelPx: number, ior = 1.52): number {
  if (distFromEdge >= bezelPx || distFromEdge <= 0) return 0;

  // u = 0 在平区与弧壁交界处，u = 1 在玻璃最外边缘
  const u = 1 - distFromEdge / bezelPx;
  const sinThetaI = clamp(u * 0.96, 0, 0.96);
  const thetaI = Math.asin(sinThetaI);
  const thetaT = Math.asin(sinThetaI / ior);
  const height = Math.sqrt(Math.max(0.01, 1 - sinThetaI * sinThetaI));

  // 光线穿过曲面玻璃厚度产生的物理横向折射偏移量
  const rawShift = Math.tan(thetaI - thetaT) * (1.25 - 0.55 * height);
  // 归一化到 0..1 并做平滑高曲率增强
  let mag = clamp(rawShift * 1.48, 0, 1);

  // 最外缘 1.5px 柔顺收边，杜绝贴边采样越界产生杂色条
  if (distFromEdge < 1.5) {
    mag *= distFromEdge / 1.5;
  }
  return mag;
}

/**
 * 基于精确像素级圆角矩形 SDF (Exact Pixel Rounded-Rect SDF) 生成法线位移贴图：
 * - 无论是宽扁的整条底栏还是饱满的液滴滑块泡，都能保证上下直边与左右圆角的倒角厚度完全一致、法线 100% 精准
 */
function buildPixelRoundedRectSdfMap(
  cssW: number,
  cssH: number,
  cornerRadiusPx: number,
  bezelPx: number,
  ior = 1.52
): string {
  const w = Math.max(32, Math.round(cssW));
  const h = Math.max(24, Math.round(cssH));
  const r = clamp(cornerRadiusPx, 2, Math.min(w, h) * 0.5);
  const effBezel = clamp(bezelPx, 4, Math.min(w, h) * 0.48);

  const canvas = document.createElement('canvas');
  canvas.width = w;
  canvas.height = h;
  const ctx = canvas.getContext('2d');
  if (!ctx) return '';

  const imgData = ctx.createImageData(w, h);
  const data = imgData.data;

  const halfW = w * 0.5;
  const halfH = h * 0.5;
  const bx = Math.max(0, halfW - r);
  const by = Math.max(0, halfH - r);

  for (let y = 0; y < h; y++) {
    const py = y + 0.5 - halfH;
    const ay = Math.abs(py);
    const qy = ay - by;

    for (let x = 0; x < w; x++) {
      const px = x + 0.5 - halfW;
      const ax = Math.abs(px);
      const qx = ax - bx;

      // Exact Rounded-Rect SDF：内部为负，边界为 0
      const outX = Math.max(qx, 0);
      const outY = Math.max(qy, 0);
      const outsideDist = Math.hypot(outX, outY);
      const insideDist = Math.min(Math.max(qx, qy), 0);
      const sdf = outsideDist + insideDist - r;

      let dx = 0;
      let dy = 0;

      if (sdf < 0) {
        const distFromEdge = -sdf;
        if (distFromEdge < effBezel) {
          // 计算严格垂直于圆角矩形外边界的单位外法线 (nx, ny)
          let nx = 0;
          let ny = 0;
          if (qx > 0 && qy > 0) {
            const len = Math.hypot(qx, qy) || 1;
            nx = Math.sign(px) * (qx / len);
            ny = Math.sign(py) * (qy / len);
          } else if (qx > qy) {
            nx = Math.sign(px);
            ny = 0;
          } else {
            nx = 0;
            ny = Math.sign(py);
          }

          const mag = snellBezelDisplacement(distFromEdge, effBezel, ior);
          // 沿外法线向内采样 -> 边缘底层像素沿圆弧切面真实向外拉伸、弯曲包裹
          dx = -nx * mag;
          dy = -ny * mag;
        }
      }

      const idx = (y * w + x) * 4;
      data[idx] = clampByte(128 + dx * 127);
      data[idx + 1] = clampByte(128 + dy * 127);
      data[idx + 2] = 128;
      data[idx + 3] = 255;
    }
  }

  ctx.putImageData(imgData, 0, 0);
  return canvas.toDataURL('image/png');
}

function applyFilterBox(filterId: string, feImageId: string, w: number, h: number, dataUrl: string) {
  const wStr = String(Math.round(w));
  const hStr = String(Math.round(h));

  const filterEl = document.getElementById(filterId);
  if (filterEl) {
    filterEl.setAttribute('x', '0');
    filterEl.setAttribute('y', '0');
    filterEl.setAttribute('width', wStr);
    filterEl.setAttribute('height', hStr);
  }

  const feImg = document.getElementById(feImageId);
  if (feImg && dataUrl) {
    feImg.setAttribute('x', '0');
    feImg.setAttribute('y', '0');
    feImg.setAttribute('width', wStr);
    feImg.setAttribute('height', hStr);
    feImg.setAttribute('href', dataUrl);
    feImg.setAttributeNS('http://www.w3.org/1999/xlink', 'xlink:href', dataUrl);
  }
}

let refractionMultiplier = 1.0;
let lastOrbKey = '';
let lastDockKey = '';

/**
 * 同步底栏整条液态玻璃折射几何尺寸（底栏下方滚过页面内容时，直接透视并在边缘弧面拉伸扭曲）
 */
export function updateDockLensGeometry(width: number, height: number, docked: boolean) {
  if (typeof document === 'undefined' || width < 40 || height < 24) return;
  const w = Math.round(width);
  const h = Math.round(height);
  const radius = docked ? 22 : h * 0.5;
  const key = `${w}x${h}_${docked ? 1 : 0}`;
  if (key === lastDockKey) return;
  lastDockKey = key;

  const bezel = Math.min(20, h * 0.34);
  const url = buildPixelRoundedRectSdfMap(w, h, radius, bezel, 1.50);
  applyFilterBox('mirage-dock-lens', 'mirage-dock-map', w, h, url);
}

/**
 * 同步液滴滑块泡的真实 CSS 像素几何尺寸（1:1 对齐零错位）
 */
export function updateOrbLensGeometry(width: number, height: number) {
  if (typeof document === 'undefined' || width < 24 || height < 20) return;
  const w = Math.round(width);
  const h = Math.round(height);
  const key = `${w}x${h}`;
  if (key === lastOrbKey) return;
  lastOrbKey = key;

  // 滑块泡采用更厚的水滴弧壁（半径的 ~44%），中心仍保持零偏移无重影，边缘拉伸张力更强
  const bezel = Math.min(22, h * 0.44);
  const url = buildPixelRoundedRectSdfMap(w, h, h * 0.5, bezel, 1.56);
  applyFilterBox('mirage-orb-lens', 'mirage-spherical-map', w, h, url);
}

export function initOpticalLensEngine() {
  if (typeof document === 'undefined') return;
  lastOrbKey = '';
  lastDockKey = '';
  updateDockLensGeometry(360, 64, false);
  updateOrbLensGeometry(108, 52);
}

export function setRefractionIntensity(level: RefractionLevel) {
  refractionMultiplier = level === 'off' ? 0 : level === 'vivid' ? 1.35 : 1.0;

  const dockDisp = document.getElementById('mirage-dock-disp');
  if (dockDisp) {
    dockDisp.setAttribute('scale', (22 * refractionMultiplier).toFixed(1));
  }
  setOrbRefractionScale(24);
}

/**
 * 动态调节滑块液滴折射拉伸强度：
 * - R/G/B 通道色散严格控在 ±1.1px ~ ±1.4px，且仅作用于非零弧壁边缘（中心 0 重影）
 */
export function setOrbRefractionScale(baseScale: number) {
  const eff = Math.max(0, baseScale * refractionMultiplier);
  const spread = eff > 2 ? Math.min(1.4, eff * 0.05) : 0;

  const dispR = document.getElementById('mirage-orb-disp-r');
  const dispG = document.getElementById('mirage-orb-disp-g');
  const dispB = document.getElementById('mirage-orb-disp-b');
  if (dispR) dispR.setAttribute('scale', (eff + spread).toFixed(2));
  if (dispG) dispG.setAttribute('scale', eff.toFixed(2));
  if (dispB) dispB.setAttribute('scale', Math.max(0, eff - spread).toFixed(2));
}

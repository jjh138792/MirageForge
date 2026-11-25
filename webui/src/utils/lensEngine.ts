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
 * 物理 Snell 折射曲线：
 * - 中心平坦区 (distFromEdge >= bezelPx)：严格返回 0 (R=128, G=128)，清晰端正无畸变
 * - 边缘弧壁区 (distFromEdge < bezelPx)：沿真实外法线陡峭递增，使底层内容顺着玻璃圆弧弯曲拉伸
 */
function snellBezelDisplacement(distFromEdge: number, bezelPx: number, ior = 1.54): number {
  if (distFromEdge >= bezelPx || distFromEdge <= 0) return 0;

  const u = 1 - distFromEdge / bezelPx;
  const sinThetaI = clamp(u * 0.96, 0, 0.96);
  const thetaI = Math.asin(sinThetaI);
  const thetaT = Math.asin(sinThetaI / ior);
  const height = Math.sqrt(Math.max(0.01, 1 - sinThetaI * sinThetaI));

  const rawShift = Math.tan(thetaI - thetaT) * (1.28 - 0.52 * height);
  let mag = clamp(rawShift * 1.55, 0, 1);

  if (distFromEdge < 1.5) {
    mag *= distFromEdge / 1.5;
  }
  return mag;
}

/**
 * 精确像素级圆角矩形 SDF 法线贴图生成器
 */
function buildPixelRoundedRectSdfMap(
  cssW: number,
  cssH: number,
  cornerRadiusPx: number,
  bezelPx: number,
  ior = 1.54
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

export function updateDockLensGeometry(width: number, height: number, docked: boolean) {
  if (typeof document === 'undefined' || width < 40 || height < 24) return;
  const w = Math.round(width);
  const h = Math.round(height);
  const radius = docked ? 22 : h * 0.5;
  const key = `${w}x${h}_${docked ? 1 : 0}`;
  if (key === lastDockKey) return;
  lastDockKey = key;

  const bezel = Math.min(22, h * 0.36);
  const url = buildPixelRoundedRectSdfMap(w, h, radius, bezel, 1.52);
  applyFilterBox('mirage-dock-lens', 'mirage-dock-map', w, h, url);
}

export function updateOrbLensGeometry(width: number, height: number) {
  if (typeof document === 'undefined' || width < 24 || height < 20) return;
  const w = Math.round(width);
  const h = Math.round(height);
  const key = `${w}x${h}`;
  if (key === lastOrbKey) return;
  lastOrbKey = key;

  const bezel = Math.min(24, h * 0.46);
  const url = buildPixelRoundedRectSdfMap(w, h, h * 0.5, bezel, 1.58);
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
    dockDisp.setAttribute('scale', (24 * refractionMultiplier).toFixed(1));
  }
  setOrbRefractionScale(28);
}

export function setOrbRefractionScale(baseScale: number) {
  const eff = Math.max(0, baseScale * refractionMultiplier);
  const disp = document.getElementById('mirage-orb-disp');
  if (disp) {
    disp.setAttribute('scale', eff.toFixed(1));
  }
}

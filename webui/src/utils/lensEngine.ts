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
 * 1. 整条底栏全域曲面拉伸贴图（拉伸底栏背后所有滚动内容）
 */
function buildFullStretchDockMap(cssW: number, cssH: number, cornerRadiusPx: number): string {
  const w = Math.max(40, Math.round(cssW));
  const h = Math.max(24, Math.round(cssH));
  const r = clamp(cornerRadiusPx, 2, Math.min(w, h) * 0.5);

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
    const ny = py / halfH;
    const ay = Math.abs(py);
    const qy = ay - by;

    for (let x = 0; x < w; x++) {
      const px = x + 0.5 - halfW;
      const nx = px / halfW;
      const ax = Math.abs(px);
      const qx = ax - bx;

      const outX = Math.max(qx, 0);
      const outY = Math.max(qy, 0);
      const sdf = Math.hypot(outX, outY) + Math.min(Math.max(qx, qy), 0) - r;

      let dx = 0;
      let dy = 0;

      if (sdf < 0) {
        const distFromEdge = -sdf;
        const ayNorm = Math.abs(ny);
        const axNorm = Math.abs(nx);

        dy = -Math.sign(ny) * (0.48 * ayNorm + 0.52 * Math.pow(ayNorm, 2.0));
        dx = -Math.sign(nx) * (0.30 * axNorm + 0.70 * Math.pow(axNorm, 2.4));

        if (distFromEdge < 1.5) {
          const fade = distFromEdge / 1.5;
          dx *= fade;
          dy *= fade;
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

/**
 * 2. 静止态泡（位于图标下方）：全域凸面水滴法线贴图，只拉伸背后的页面背景（绝不触碰图标）
 */
function buildFullDomeOrbMap(cssW: number, cssH: number): string {
  const w = Math.max(24, Math.round(cssW));
  const h = Math.max(20, Math.round(cssH));
  const r = h * 0.5;

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

  for (let y = 0; y < h; y++) {
    const py = y + 0.5 - halfH;
    const ny = py / halfH;
    const ay = Math.abs(py);

    for (let x = 0; x < w; x++) {
      const px = x + 0.5 - halfW;
      const nx = px / halfW;
      const ax = Math.abs(px);
      const qx = ax - bx;

      const outX = Math.max(qx, 0);
      const sdf = Math.hypot(outX, ay) - r;

      let dx = 0;
      let dy = 0;

      if (sdf < 0) {
        const distFromEdge = -sdf;
        const radial = clamp(Math.hypot(nx, ny), 0, 1);
        const domeGain = 0.58 + 0.42 * Math.pow(radial, 1.5);
        dx = -nx * domeGain;
        dy = -ny * domeGain;

        if (distFromEdge < 1.5) {
          const fade = distFromEdge / 1.5;
          dx *= fade;
          dy *= fade;
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

/**
 * 3. 交互态泡（位于图标上方）：0 模糊、中心清澈无畸变，仅外圈 Snell 弧壁强力弯曲拉伸
 */
function buildRimOnlyOrbMap(cssW: number, cssH: number): string {
  const w = Math.max(24, Math.round(cssW));
  const h = Math.max(20, Math.round(cssH));
  const r = h * 0.5;
  const bezelPx = Math.min(22, h * 0.44);
  const ior = 1.60;

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

  for (let y = 0; y < h; y++) {
    const py = y + 0.5 - halfH;
    const ay = Math.abs(py);

    for (let x = 0; x < w; x++) {
      const px = x + 0.5 - halfW;
      const ax = Math.abs(px);
      const qx = ax - bx;

      const outX = Math.max(qx, 0);
      const sdf = Math.hypot(outX, ay) - r;

      let dx = 0;
      let dy = 0;

      if (sdf < 0) {
        const distFromEdge = -sdf;
        if (distFromEdge < bezelPx) {
          let nx = 0;
          let ny = 0;
          if (qx > 0) {
            const len = Math.hypot(qx, ay) || 1;
            nx = Math.sign(px) * (qx / len);
            ny = Math.sign(py) * (ay / len);
          } else {
            nx = 0;
            ny = Math.sign(py);
          }

          const u = 1 - distFromEdge / bezelPx;
          const sinThetaI = clamp(u * 0.96, 0, 0.96);
          const thetaI = Math.asin(sinThetaI);
          const thetaT = Math.asin(sinThetaI / ior);
          const height = Math.sqrt(Math.max(0.01, 1 - sinThetaI * sinThetaI));
          let mag = clamp(Math.tan(thetaI - thetaT) * (1.35 - 0.5 * height) * 1.62, 0, 1);

          if (distFromEdge < 1.5) {
            mag *= distFromEdge / 1.5;
          }
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

function setFeImage(id: string, w: number, h: number, dataUrl: string) {
  const el = document.getElementById(id);
  if (!el || !dataUrl) return;
  el.setAttribute('x', '0');
  el.setAttribute('y', '0');
  el.setAttribute('width', String(Math.round(w)));
  el.setAttribute('height', String(Math.round(h)));
  el.setAttribute('href', dataUrl);
  el.setAttributeNS('http://www.w3.org/1999/xlink', 'xlink:href', dataUrl);
}

function setFilterBox(id: string, w: number, h: number) {
  const el = document.getElementById(id);
  if (!el) return;
  el.setAttribute('x', '0');
  el.setAttribute('y', '0');
  el.setAttribute('width', String(Math.round(w)));
  el.setAttribute('height', String(Math.round(h)));
}

let refractionMultiplier = 1.0;
let lastDockKey = '';
let lastOrbKey = '';

export function updateDockLensGeometry(width: number, height: number, docked: boolean) {
  if (typeof document === 'undefined' || width < 40 || height < 24) return;
  const w = Math.round(width);
  const h = Math.round(height);
  const radius = docked ? 22 : h * 0.5;
  const key = `${w}x${h}_${docked ? 1 : 0}`;
  if (key === lastDockKey) return;
  lastDockKey = key;

  setFilterBox('mirage-dock-lens', w, h);
  setFeImage('mirage-dock-map', w, h, buildFullStretchDockMap(w, h, radius));
}

export function updateOrbLensGeometry(width: number, height: number) {
  if (typeof document === 'undefined' || width < 24 || height < 20) return;
  const w = Math.round(width);
  const h = Math.round(height);
  const key = `${w}x${h}`;
  if (key === lastOrbKey) return;
  lastOrbKey = key;

  setFilterBox('mirage-orb-dome-lens', w, h);
  setFeImage('mirage-orb-dome-map', w, h, buildFullDomeOrbMap(w, h));

  setFilterBox('mirage-orb-rim-lens', w, h);
  setFeImage('mirage-orb-rim-map', w, h, buildRimOnlyOrbMap(w, h));
}

export function initOpticalLensEngine() {
  if (typeof document === 'undefined') return;
  lastDockKey = '';
  lastOrbKey = '';
  updateDockLensGeometry(360, 64, false);
  updateOrbLensGeometry(108, 52);
}

export function setOrbRefractionScale(rimScale: number) {
  const eff = Math.max(0, rimScale * refractionMultiplier);
  const rimDisp = document.getElementById('mirage-orb-rim-disp');
  if (rimDisp) {
    rimDisp.setAttribute('scale', eff.toFixed(1));
  }
}

export function setRefractionIntensity(level: RefractionLevel) {
  refractionMultiplier = level === 'off' ? 0 : level === 'vivid' ? 1.35 : 1.0;

  const dockDisp = document.getElementById('mirage-dock-disp');
  if (dockDisp) {
    dockDisp.setAttribute('scale', (24 * refractionMultiplier).toFixed(1));
  }
  const domeDisp = document.getElementById('mirage-orb-dome-disp');
  if (domeDisp) {
    domeDisp.setAttribute('scale', (20 * refractionMultiplier).toFixed(1));
  }
  setOrbRefractionScale(level === 'off' ? 0 : 30);
}

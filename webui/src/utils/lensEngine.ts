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
 * 1. 底栏全域柱面曲面拉伸法线贴图（不止拉边缘，整条底栏背后全部内容均连续受玻璃曲面拉伸）
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
    const ny = py / halfH; // -1..1
    const ay = Math.abs(py);
    const qy = ay - by;

    for (let x = 0; x < w; x++) {
      const px = x + 0.5 - halfW;
      const nx = px / halfW; // -1..1
      const ax = Math.abs(px);
      const qx = ax - bx;

      const outX = Math.max(qx, 0);
      const outY = Math.max(qy, 0);
      const sdf = Math.hypot(outX, outY) + Math.min(Math.max(qx, qy), 0) - r;

      let dx = 0;
      let dy = 0;

      if (sdf < 0) {
        const distFromEdge = -sdf;
        // 纵向全高度柱面放大拉伸（即使正中心也能看到背后文字被玻璃弧面拉伸）
        const ayNorm = Math.abs(ny);
        dy = -Math.sign(ny) * (0.48 * ayNorm + 0.52 * Math.pow(ayNorm, 2.0));

        // 横向全域温和拉伸 + 左右圆角端增强折射
        const axNorm = Math.abs(nx);
        dx = -Math.sign(nx) * (0.30 * axNorm + 0.70 * Math.pow(axNorm, 2.4));

        // 最外缘 1.5px 柔顺收边防杂色溢出
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
 * 2. 静止态液态玻璃泡：全域凸面水滴法线贴图（从中心到边缘连续拉伸泡背后的所有内容）
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
    const qy = ay;

    for (let x = 0; x < w; x++) {
      const px = x + 0.5 - halfW;
      const nx = px / halfW;
      const ax = Math.abs(px);
      const qx = ax - bx;

      const outX = Math.max(qx, 0);
      const sdf = Math.hypot(outX, qy) + Math.min(Math.max(qx, qy - r), 0) - r;

      let dx = 0;
      let dy = 0;

      if (sdf < 0) {
        const distFromEdge = -sdf;
        const radial = clamp(Math.hypot(nx, ny), 0, 1);
        // 连续凸透镜球面曲线：整颗水滴内部从中心到外缘都有明显拉伸放大感
        const domeGain = 0.58 + 0.42 * Math.pow(radial, 1.55);
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
 * 3. 交互态液态玻璃泡：仅拉边缘 Snell 弧壁贴图（中心 56% 严格 0 位移清澈，仅外缘弧壁强力弯曲拉伸）
 */
function buildRimOnlyOrbMap(cssW: number, cssH: number): string {
  const w = Math.max(24, Math.round(cssW));
  const h = Math.max(20, Math.round(cssH));
  const r = h * 0.5;
  const bezelPx = Math.min(22, h * 0.44);
  const ior = 1.58;

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
          let mag = clamp(Math.tan(thetaI - thetaT) * (1.32 - 0.5 * height) * 1.58, 0, 1);

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

  setFilterBox('mirage-orb-lens', w, h);
  setFeImage('mirage-orb-dome-map', w, h, buildFullDomeOrbMap(w, h));
  setFeImage('mirage-orb-rim-map', w, h, buildRimOnlyOrbMap(w, h));
}

export function initOpticalLensEngine() {
  if (typeof document === 'undefined') return;
  lastDockKey = '';
  lastOrbKey = '';
  updateDockLensGeometry(360, 64, false);
  updateOrbLensGeometry(108, 52);
}

/**
 * 连续驱动泡的光学表面变形：
 * - morphToRim = 0：静止态全域凸面拉伸（拉伸背后所有内容）
 * - morphToRim = 1：交互态仅拉边缘（中心清晰无畸变，外圈弧壁强力弯曲拉伸）
 */
export function setOrbMorphState(morphToRim: number, baseScale: number) {
  const t = clamp(morphToRim, 0, 1);
  const morphEl = document.getElementById('mirage-orb-morph');
  if (morphEl) {
    morphEl.setAttribute('k2', (1 - t).toFixed(3));
    morphEl.setAttribute('k3', t.toFixed(3));
  }
  setOrbRefractionScale(baseScale);
}

export function setOrbRefractionScale(baseScale: number) {
  const eff = Math.max(0, baseScale * refractionMultiplier);
  const disp = document.getElementById('mirage-orb-disp');
  if (disp) {
    disp.setAttribute('scale', eff.toFixed(1));
  }
}

export function setRefractionIntensity(level: RefractionLevel) {
  refractionMultiplier = level === 'off' ? 0 : level === 'vivid' ? 1.35 : 1.0;

  const dockDisp = document.getElementById('mirage-dock-disp');
  if (dockDisp) {
    dockDisp.setAttribute('scale', (26 * refractionMultiplier).toFixed(1));
  }
  // 静止默认处于全域凸面拉伸态 (morph = 0)
  setOrbMorphState(0, level === 'off' ? 0 : 18);
}

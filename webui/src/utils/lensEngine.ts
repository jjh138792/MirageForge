// SPDX-License-Identifier: GPL-3.0-only
// Copyright (c) 2025-2026 jjh138792
// MirageForge: https://github.com/jjh138792/MirageForge

import type { RefractionLevel } from '../store/types';

function clampByte(v: number): number {
  return Math.max(0, Math.min(255, Math.round(v)));
}

/**
 * 1. 生成高精度 Snell 定律球面液滴位移贴图 (256x128)
 * - 中心区 (r < 0.68)：温和凸透镜放大，底层文字/图标清晰放大不变糊
 * - 外缘液面弧面 (0.68 <= r < 1.0)：高曲率切向拉伸，产生真实水面边缘弯折与拉长效果
 */
function buildMeniscusOrbNormalMap(): string {
  const width = 256;
  const height = 128;
  const canvas = document.createElement('canvas');
  canvas.width = width;
  canvas.height = height;
  const ctx = canvas.getContext('2d');
  if (!ctx) return '';

  const imgData = ctx.createImageData(width, height);
  const data = imgData.data;
  const p = 3.2; // 超椭圆指数，贴合胶囊液滴轮廓

  for (let y = 0; y < height; y++) {
    const ny = (y / (height - 1)) * 2 - 1;
    const ay = Math.abs(ny);
    for (let x = 0; x < width; x++) {
      const nx = (x / (width - 1)) * 2 - 1;
      const ax = Math.abs(nx);

      const r = Math.pow(Math.pow(ax, p) + Math.pow(ay, p), 1 / p);
      let dx = 0;
      let dy = 0;

      if (r > 0.0001 && r < 1.0) {
        // 超椭圆外法线梯度方向
        const gx = Math.sign(nx) * Math.pow(ax, p - 1);
        const gy = Math.sign(ny) * Math.pow(ay, p - 1);
        const gLen = Math.hypot(gx, gy) || 1;
        const ux = gx / gLen;
        const uy = gy / gLen;

        // 分区物理折射斜率：中心凸透镜放大 + 边缘液面强拉伸
        let strength = 0;
        if (r < 0.68) {
          strength = 0.24 * (r / 0.68);
        } else {
          const t = Math.min(0.985, (r - 0.68) / 0.32);
          // 球面切线斜率 (1 - sqrt(1 - t^2)) 形成陡峭的边缘液面拉伸
          const rimCurve = 1 - Math.sqrt(Math.max(0.015, 1 - t * t));
          strength = 0.24 + 0.74 * rimCurve;
        }

        // 外缘最后 3% 平滑收敛，避免采样超出边框产生硬锯齿
        if (r > 0.96) {
          strength *= Math.max(0, (1.0 - r) / 0.04);
        }

        // 向内偏移采样源 -> 放大中心、拉伸边缘
        dx = -ux * strength;
        dy = -uy * strength;
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

/**
 * 2. 生成圆角倒角玻璃卡片位移贴图 (192x192)
 * - 中心 84% 区域完全平坦 (128, 128)，保证卡片正文锐利零变形
 * - 仅外缘 16% 倒角产生光学玻璃折边效果
 */
function buildCardBezelNormalMap(): string {
  const size = 192;
  const canvas = document.createElement('canvas');
  canvas.width = size;
  canvas.height = size;
  const ctx = canvas.getContext('2d');
  if (!ctx) return '';

  const imgData = ctx.createImageData(size, size);
  const data = imgData.data;
  const p = 6.0; // 圆角矩形轮廓

  for (let y = 0; y < size; y++) {
    const ny = (y / (size - 1)) * 2 - 1;
    const ay = Math.abs(ny);
    for (let x = 0; x < size; x++) {
      const nx = (x / (size - 1)) * 2 - 1;
      const ax = Math.abs(nx);

      const r = Math.pow(Math.pow(ax, p) + Math.pow(ay, p), 1 / p);
      let dx = 0;
      let dy = 0;

      if (r > 0.84 && r < 1.0) {
        const t = (r - 0.84) / 0.16;
        const bezel = Math.sin(t * Math.PI);
        const gx = Math.sign(nx) * Math.pow(ax, p - 1);
        const gy = Math.sign(ny) * Math.pow(ay, p - 1);
        const gLen = Math.hypot(gx, gy) || 1;
        dx = -(gx / gLen) * bezel * 0.65;
        dy = -(gy / gLen) * bezel * 0.65;
      }

      const idx = (y * size + x) * 4;
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

export function initOpticalLensEngine() {
  if (typeof document === 'undefined') return;
  setFeImageHref('mirage-spherical-map', buildMeniscusOrbNormalMap());
  setFeImageHref('mirage-card-map', buildCardBezelNormalMap());
}

export function setRefractionIntensity(level: RefractionLevel) {
  refractionMultiplier = level === 'off' ? 0 : level === 'vivid' ? 1.38 : 1.0;
  setOrbRefractionScale(36);
  const cardDisp = document.getElementById('mirage-card-disp');
  if (cardDisp) {
    cardDisp.setAttribute('scale', String(Math.round(12 * refractionMultiplier)));
  }
}

/**
 * 动态调节液态玻璃球三通道折射倍率与棱镜色散间距
 */
export function setOrbRefractionScale(baseScale: number) {
  const eff = baseScale * refractionMultiplier;
  const dispR = document.getElementById('mirage-orb-disp-r');
  const dispG = document.getElementById('mirage-orb-disp-g');
  const dispB = document.getElementById('mirage-orb-disp-b');
  if (dispR) dispR.setAttribute('scale', String(Math.round(eff * 1.13)));
  if (dispG) dispG.setAttribute('scale', String(Math.round(eff)));
  if (dispB) dispB.setAttribute('scale', String(Math.round(eff * 0.87)));
}

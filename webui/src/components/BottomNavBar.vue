<!--
SPDX-License-Identifier: GPL-3.0-only
Copyright (c) 2025-2026 jjh138792
MirageForge: https://github.com/jjh138792/MirageForge
-->

<script setup lang="ts">
import { ref, computed, watch, onMounted, onUnmounted } from 'vue';
import type { Component } from 'vue';
import { SpringSolver, triggerHaptic } from '../bridge/ksu-io';
import { setOrbRefractionScale, updateOrbLensGeometry } from '../utils/lensEngine';
import { settingsStore } from '../store/settingsStore';
import type { DockStyle } from '../store/types';

export interface NavBarItem {
  key: string;
  label: string;
  icon: Component;
}

const props = defineProps<{
  items: NavBarItem[];
  activeIndex: number;
  dockStyle: DockStyle;
}>();

const emit = defineEmits<{
  (e: 'select', index: number): void;
}>();

const dockRef = ref<HTMLElement | null>(null);
const orbRef = ref<HTMLElement | null>(null);
const orbX = ref(props.activeIndex);
const orbScaleX = ref(1);
const orbScaleY = ref(1);
const isOrbPressed = ref(false);
const isOrbDragging = ref(false);
const isOrbMoving = ref(false);

const springX = new SpringSolver(props.activeIndex, 260, 22);
const springScale = new SpringSolver(1, 340, 20);

let rafId = 0;
let lastFrameTime = performance.now();
let longPressTimer: any = null;
let pointerStartX = 0;
let resizeObserver: ResizeObserver | null = null;

// 按住、拖拽或滑行动画进行中时激活水晶液滴折射态；静止落位后回归干净温润药丸态
const isLiquidLensActive = computed(
  () =>
    isOrbPressed.value ||
    isOrbDragging.value ||
    isOrbMoving.value ||
    settingsStore.refractionLevel === 'vivid'
);

function syncOrbGeometry() {
  if (!orbRef.value) return;
  const w = orbRef.value.offsetWidth;
  const h = orbRef.value.offsetHeight;
  if (w > 10 && h > 10) {
    updateOrbLensGeometry(w, h);
  }
}

function startPhysicsLoop() {
  if (rafId) return;
  lastFrameTime = performance.now();
  isOrbMoving.value = true;

  const tick = (now: number) => {
    const dt = (now - lastFrameTime) / 1000;
    lastFrameTime = now;

    const curX = springX.step(dt);
    const baseScale = springScale.step(dt);
    const vx = springX.vel;

    // 速度越快，水滴沿水平方向自然拉伸、垂直微扁
    const speedStretch = Math.min(0.24, Math.abs(vx) * 0.042);
    const speedSquash = Math.min(0.11, Math.abs(vx) * 0.018);

    orbX.value = curX;
    orbScaleX.value = baseScale + speedStretch;
    orbScaleY.value = baseScale - speedSquash;

    // 根据按压膨胀与滑行速度动态调制液滴边缘折弯幅度 (16 ~ 28px，绝不越界撕裂)
    const activeBoost = isOrbPressed.value || isOrbDragging.value ? 1 : Math.min(1, Math.abs(vx) * 0.45);
    const baseRefraction = settingsStore.refractionLevel === 'vivid' ? 14 : 0;
    const dynamicScale = Math.max(
      baseRefraction,
      (19 + Math.min(9, Math.abs(vx) * 1.8 + (baseScale - 1) * 28)) * activeBoost
    );
    setOrbRefractionScale(dynamicScale);

    if (!springX.isSettled() || !springScale.isSettled() || isOrbPressed.value) {
      rafId = requestAnimationFrame(tick);
    } else {
      rafId = 0;
      isOrbMoving.value = false;
      setOrbRefractionScale(settingsStore.refractionLevel === 'vivid' ? 14 : 0);
    }
  };

  rafId = requestAnimationFrame(tick);
}

watch(
  () => props.activeIndex,
  (nextIdx) => {
    if (!isOrbDragging.value) {
      springX.stiffness = 240;
      springX.damping = 21;
      springX.target = nextIdx;
      startPhysicsLoop();
    }
  }
);

watch(
  () => props.dockStyle,
  () => {
    requestAnimationFrame(() => syncOrbGeometry());
  }
);

function onDockPointerDown(e: PointerEvent) {
  if (!dockRef.value) return;
  (e.currentTarget as HTMLElement).setPointerCapture(e.pointerId);
  syncOrbGeometry();

  isOrbPressed.value = true;
  isOrbDragging.value = false;
  pointerStartX = e.clientX;

  const count = props.items.length;
  const rect = dockRef.value.getBoundingClientRect();
  const pad = 6;
  const slotW = (rect.width - pad * 2) / count;
  const clickIdx = Math.max(0, Math.min(count - 1, (e.clientX - rect.left - pad) / slotW - 0.5));

  // 轻触即微幅膨胀并激活液态光学边缘；长按进一步放大成水晶放大镜
  springScale.target = 1.12;
  startPhysicsLoop();

  if (longPressTimer) clearTimeout(longPressTimer);
  longPressTimer = setTimeout(() => {
    if (isOrbPressed.value) {
      triggerHaptic('medium');
      springScale.target = 1.24;
      springX.target = clickIdx;
      startPhysicsLoop();
    }
  }, 130);
}

function onDockPointerMove(e: PointerEvent) {
  if (!isOrbPressed.value || !dockRef.value) return;

  const deltaX = e.clientX - pointerStartX;
  if (Math.abs(deltaX) > 5 && !isOrbDragging.value) {
    isOrbDragging.value = true;
    springScale.target = 1.22;
  }

  if (isOrbDragging.value) {
    const count = props.items.length;
    const maxIdx = count - 1;
    const rect = dockRef.value.getBoundingClientRect();
    const pad = 6;
    const slotW = (rect.width - pad * 2) / count;
    let rawIdx = (e.clientX - rect.left - pad) / slotW - 0.5;

    // 两端橡皮筋阻尼
    if (rawIdx < 0) rawIdx = rawIdx * 0.25;
    if (rawIdx > maxIdx) rawIdx = maxIdx + (rawIdx - maxIdx) * 0.25;

    springX.stiffness = 400;
    springX.damping = 28;
    springX.target = rawIdx;
    startPhysicsLoop();
  }
}

function onDockPointerUp(e: PointerEvent) {
  if (!isOrbPressed.value || !dockRef.value) return;
  if (longPressTimer) clearTimeout(longPressTimer);

  isOrbPressed.value = false;
  springScale.target = 1.0;

  const count = props.items.length;
  const rect = dockRef.value.getBoundingClientRect();
  const pad = 6;
  const slotW = (rect.width - pad * 2) / count;
  const releaseIdx = Math.round(
    Math.max(0, Math.min(count - 1, (e.clientX - rect.left - pad) / slotW - 0.5))
  );

  springX.stiffness = isOrbDragging.value ? 300 : 250;
  springX.damping = isOrbDragging.value ? 23 : 21;
  springX.target = releaseIdx;
  startPhysicsLoop();

  if (releaseIdx !== props.activeIndex) {
    triggerHaptic('light');
    emit('select', releaseIdx);
  }
  isOrbDragging.value = false;
}

onMounted(() => {
  requestAnimationFrame(() => syncOrbGeometry());
  if (typeof ResizeObserver !== 'undefined' && orbRef.value) {
    resizeObserver = new ResizeObserver(() => syncOrbGeometry());
    resizeObserver.observe(orbRef.value);
  }
});

onUnmounted(() => {
  if (rafId) cancelAnimationFrame(rafId);
  if (longPressTimer) clearTimeout(longPressTimer);
  if (resizeObserver) resizeObserver.disconnect();
});
</script>

<template>
  <nav class="nav-dock-wrap" :class="dockStyle">
    <div
      ref="dockRef"
      class="nav-dock-bar"
      :class="dockStyle"
      :style="{ gridTemplateColumns: `repeat(${items.length}, 1fr)` }"
      @pointerdown="onDockPointerDown"
      @pointermove="onDockPointerMove"
      @pointerup="onDockPointerUp"
      @pointercancel="onDockPointerUp"
    >
      <!-- 底层图标与文字 (z-index: 1)：清晰干净，被上方交互液滴球精确折射 -->
      <div
        v-for="(item, idx) in items"
        :key="item.key"
        class="dock-slot"
        :class="{ active: idx === activeIndex }"
      >
        <component :is="item.icon" :size="20" class="dock-icon" />
        <span class="dock-label">{{ item.label }}</span>
      </div>

      <!-- 顶层双态液态玻璃球 (z-index: 2)：静止=干净药丸，按住/滑动=晶莹曲面折射液滴 -->
      <div
        ref="orbRef"
        class="dock-liquid-orb liquid-orb-pill"
        :class="{ 'orb-liquid-active': isLiquidLensActive }"
        :style="{
          width: `calc((100% - 12px) / ${items.length})`,
          transform: `translate3d(${orbX * 100}%, 0, 0) scale(${orbScaleX}, ${orbScaleY})`,
        }"
      ></div>
    </div>
  </nav>
</template>

<style scoped>
.nav-dock-wrap {
  position: fixed;
  left: 0;
  right: 0;
  bottom: calc(var(--inset-bottom) + 12px);
  display: flex;
  justify-content: center;
  padding: 0 16px;
  z-index: 95;
  pointer-events: none;
  transition: bottom 0.36s var(--spring-easing), padding 0.36s var(--spring-easing);
}

.nav-dock-wrap.docked {
  bottom: 0;
  padding: 0;
}

/* 干净柔和的底栏底座（彻底虚化页面底部背景文字，避免杂乱底字透入玻璃球） */
.nav-dock-bar {
  pointer-events: auto;
  position: relative;
  display: grid;
  width: 100%;
  max-width: 460px;
  padding: 6px;
  border-radius: 999px;
  background: var(--dock-surface-bg);
  backdrop-filter: blur(28px) saturate(190%);
  -webkit-backdrop-filter: blur(28px) saturate(190%);
  border: 1px solid var(--glass-border);
  box-shadow:
    inset 0 1px 1px var(--glass-rim-top),
    0 12px 32px -8px rgba(0, 0, 0, 0.22);
  touch-action: none;
  cursor: grab;
  overflow: visible;
  transition:
    max-width 0.38s var(--spring-easing),
    border-radius 0.36s var(--spring-easing),
    padding-bottom 0.36s var(--spring-easing),
    var(--theme-transition);
}

.nav-dock-bar.docked {
  max-width: 100%;
  border-radius: 22px 22px 0 0;
  border-bottom: none;
  border-left: none;
  border-right: none;
  padding: 6px 6px calc(var(--inset-bottom) + 6px);
}

.nav-dock-bar:active {
  cursor: grabbing;
}

.dock-slot {
  position: relative;
  z-index: 1;
  color: var(--text-secondary);
  padding: 8px 4px;
  border-radius: 999px;
  display: flex;
  flex-direction: column;
  align-items: center;
  justify-content: center;
  gap: 3px;
  pointer-events: none;
  transition: color 0.22s ease, transform 0.24s var(--bounce-easing);
}

.dock-slot.active {
  color: var(--accent-primary);
}

.dock-slot.active .dock-icon {
  color: var(--accent-primary);
  transform: scale(1.05);
}

.dock-label {
  font-size: 11px;
  font-weight: 700;
  letter-spacing: 0.01em;
}

.dock-liquid-orb {
  position: absolute;
  z-index: 2;
  top: 5px;
  bottom: 5px;
  left: 6px;
  border-radius: 999px;
  pointer-events: none;
  transform-origin: center center;
}

.nav-dock-bar.docked .dock-liquid-orb {
  bottom: calc(var(--inset-bottom) + 5px);
}
</style>

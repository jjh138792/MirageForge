<!--
SPDX-License-Identifier: GPL-3.0-only
Copyright (c) 2025-2026 jjh138792
MirageForge: https://github.com/jjh138792/MirageForge
-->

<script setup lang="ts">
import { ref, watch, onUnmounted } from 'vue';
import type { Component } from 'vue';
import { SpringSolver, triggerHaptic } from '../bridge/ksu-io';
import { setOrbRefractionScale } from '../utils/lensEngine';
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
const orbX = ref(props.activeIndex);
const orbScaleX = ref(1);
const orbScaleY = ref(1);
const isOrbPressed = ref(false);
const isOrbDragging = ref(false);

const springX = new SpringSolver(props.activeIndex, 240, 20);
const springScale = new SpringSolver(1, 320, 18);

let rafId = 0;
let lastFrameTime = performance.now();
let longPressTimer: any = null;
let pointerStartX = 0;

function startPhysicsLoop() {
  if (rafId) return;
  lastFrameTime = performance.now();

  const tick = (now: number) => {
    const dt = (now - lastFrameTime) / 1000;
    lastFrameTime = now;

    const curX = springX.step(dt);
    const baseScale = springScale.step(dt);
    const vx = springX.vel;

    // 拖动速度越高，液滴水平拉伸越明显，呈现真实表面张力阻尼形变
    const speedStretch = Math.min(0.30, Math.abs(vx) * 0.052);
    const speedSquash = Math.min(0.15, Math.abs(vx) * 0.025);

    orbX.value = curX;
    orbScaleX.value = baseScale + speedStretch;
    orbScaleY.value = baseScale - speedSquash;

    // 实时联动 Snell 三通道折射强度：拖拽/长按放大时水面边缘拉伸感更强
    const dynamicRefraction = 36 + Math.min(26, Math.abs(vx) * 4.5 + (baseScale - 1) * 60);
    setOrbRefractionScale(dynamicRefraction);

    if (!springX.isSettled() || !springScale.isSettled() || isOrbPressed.value) {
      rafId = requestAnimationFrame(tick);
    } else {
      rafId = 0;
      setOrbRefractionScale(36);
    }
  };

  rafId = requestAnimationFrame(tick);
}

watch(
  () => props.activeIndex,
  (nextIdx) => {
    if (!isOrbDragging.value) {
      springX.stiffness = 230;
      springX.damping = 19;
      springX.target = nextIdx;
      startPhysicsLoop();
    }
  }
);

function onDockPointerDown(e: PointerEvent) {
  if (!dockRef.value) return;
  (e.currentTarget as HTMLElement).setPointerCapture(e.pointerId);

  isOrbPressed.value = true;
  isOrbDragging.value = false;
  pointerStartX = e.clientX;

  const count = props.items.length;
  const rect = dockRef.value.getBoundingClientRect();
  const pad = 6;
  const slotW = (rect.width - pad * 2) / count;
  const clickIdx = Math.max(0, Math.min(count - 1, (e.clientX - rect.left - pad) / slotW - 0.5));

  if (longPressTimer) clearTimeout(longPressTimer);
  longPressTimer = setTimeout(() => {
    if (isOrbPressed.value) {
      triggerHaptic('medium');
      springScale.target = 1.18;
      springX.target = clickIdx;
      startPhysicsLoop();
    }
  }, 150);
}

function onDockPointerMove(e: PointerEvent) {
  if (!isOrbPressed.value || !dockRef.value) return;

  const deltaX = e.clientX - pointerStartX;
  if (Math.abs(deltaX) > 6 && !isOrbDragging.value) {
    isOrbDragging.value = true;
    springScale.target = 1.15;
  }

  if (isOrbDragging.value) {
    const count = props.items.length;
    const maxIdx = count - 1;
    const rect = dockRef.value.getBoundingClientRect();
    const pad = 6;
    const slotW = (rect.width - pad * 2) / count;
    let rawIdx = (e.clientX - rect.left - pad) / slotW - 0.5;

    if (rawIdx < 0) rawIdx = rawIdx * 0.28;
    if (rawIdx > maxIdx) rawIdx = maxIdx + (rawIdx - maxIdx) * 0.28;

    springX.stiffness = 380;
    springX.damping = 26;
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

  springX.stiffness = isOrbDragging.value ? 290 : 230;
  springX.damping = isOrbDragging.value ? 22 : 19;
  springX.target = releaseIdx;
  startPhysicsLoop();

  if (releaseIdx !== props.activeIndex) {
    triggerHaptic('light');
    emit('select', releaseIdx);
  }
  isOrbDragging.value = false;
}

onUnmounted(() => {
  if (rafId) cancelAnimationFrame(rafId);
  if (longPressTimer) clearTimeout(longPressTimer);
});
</script>

<template>
  <nav class="nav-dock-wrap" :class="dockStyle">
    <div
      ref="dockRef"
      class="nav-dock-bar liquid-glass liquid-glass-elevated"
      :class="dockStyle"
      :style="{ gridTemplateColumns: `repeat(${items.length}, 1fr)` }"
      @pointerdown="onDockPointerDown"
      @pointermove="onDockPointerMove"
      @pointerup="onDockPointerUp"
      @pointercancel="onDockPointerUp"
    >
      <!-- 底层图标与文字 (z-index: 1)，直接透过上方零模糊玻璃球产生边缘水面拉伸折射 -->
      <div
        v-for="(item, idx) in items"
        :key="item.key"
        class="dock-slot"
        :class="{ active: idx === activeIndex }"
      >
        <component :is="item.icon" :size="20" class="dock-icon" />
        <span class="dock-label">{{ item.label }}</span>
      </div>

      <!-- 顶层光学折射液态玻璃球 (z-index: 2) -->
      <div
        class="dock-liquid-orb liquid-orb-pill"
        :class="{ 'orb-pressed': isOrbPressed }"
        :style="{
          width: `calc((100% - 12px) / ${items.length})`,
          transform: `translate3d(${orbX * 100}%, 0, 0) scale(${orbScaleX}, ${orbScaleY})`,
        }"
      >
        <div class="orb-specular-top"></div>
        <div class="orb-caustic-bottom"></div>
      </div>
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

/* 贴底常规栏形态 */
.nav-dock-wrap.docked {
  bottom: 0;
  padding: 0;
}

.nav-dock-bar {
  pointer-events: auto;
  position: relative;
  display: grid;
  width: 100%;
  max-width: 470px;
  padding: 6px;
  border-radius: 30px;
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
  border-radius: 22px;
  display: flex;
  flex-direction: column;
  align-items: center;
  justify-content: center;
  gap: 3px;
  pointer-events: none;
  transition: color 0.24s ease, transform 0.24s var(--bounce-easing);
}

.dock-slot.active {
  color: var(--text-primary);
}

.dock-slot.active .dock-icon {
  color: var(--accent-primary);
  transform: scale(1.06);
}

.dock-label {
  font-size: 11px;
  font-weight: 700;
  letter-spacing: 0.01em;
}

/* 零模糊光学液滴球体 */
.dock-liquid-orb {
  position: absolute;
  z-index: 2;
  top: 5px;
  bottom: 5px;
  left: 6px;
  border-radius: 24px;
  pointer-events: none;
  transform-origin: center center;
}

.nav-dock-bar.docked .dock-liquid-orb {
  bottom: calc(var(--inset-bottom) + 5px);
}

/* 水滴顶部镜面高光弧 */
.orb-specular-top {
  position: absolute;
  top: 2px;
  left: 12%;
  right: 12%;
  height: 30%;
  border-radius: 999px;
  background: linear-gradient(
    180deg,
    rgba(255, 255, 255, 0.72) 0%,
    rgba(255, 255, 255, 0.02) 100%
  );
  pointer-events: none;
}

/* 底部焦散内反射弧 */
.orb-caustic-bottom {
  position: absolute;
  bottom: 2px;
  left: 18%;
  right: 18%;
  height: 18%;
  border-radius: 999px;
  background: radial-gradient(
    ellipse at center bottom,
    rgba(var(--accent-secondary-rgb), 0.52) 0%,
    transparent 75%
  );
  pointer-events: none;
}
</style>

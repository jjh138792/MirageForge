<!--
SPDX-License-Identifier: GPL-3.0-only
Copyright (c) 2025-2026 jjh138792
MirageForge: https://github.com/jjh138792/MirageForge
-->

<script setup lang="ts">
import { ref, computed, watch, onMounted, onUnmounted } from 'vue';
import type { Component } from 'vue';
import { SpringSolver, triggerHaptic } from '../bridge/ksu-io';
import {
  setOrbMorphState,
  updateDockLensGeometry,
  updateOrbLensGeometry,
} from '../utils/lensEngine';
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

const isBlurOnly = computed(() => settingsStore.refractionLevel === 'off');

const orbX = ref(props.activeIndex);
const orbScaleX = ref(1);
const orbScaleY = ref(1);

const isOrbPressed = ref(false);
const isOrbDragging = ref(false);

const springX = new SpringSolver(props.activeIndex, 260, 22);
const springScale = new SpringSolver(1, 300, 22);
// 0 = 静止全域凸面拉伸，1 = 交互仅拉边缘
const springMorph = new SpringSolver(0, 200, 24);

let rafId = 0;
let lastFrameTime = performance.now();
let longPressTimer: any = null;
let pointerStartX = 0;
let resizeObserver: ResizeObserver | null = null;

function syncLensGeometries() {
  if (dockRef.value) {
    const dw = dockRef.value.offsetWidth;
    const dh = dockRef.value.offsetHeight;
    if (dw > 40 && dh > 24) {
      updateDockLensGeometry(dw, dh, props.dockStyle === 'docked');
    }
  }
  if (orbRef.value) {
    const ow = orbRef.value.offsetWidth;
    const oh = orbRef.value.offsetHeight;
    if (ow > 24 && oh > 20) {
      updateOrbLensGeometry(ow, oh);
    }
  }
}

function startPhysicsLoop() {
  if (rafId) return;
  lastFrameTime = performance.now();

  const tick = (now: number) => {
    const dt = (now - lastFrameTime) / 1000;
    lastFrameTime = now;

    const curX = springX.step(dt);
    const vx = springX.vel;
    orbX.value = curX;

    if (isBlurOnly.value) {
      // 仅毛玻璃模式：纯平滑水平滑移，无静态/活动态差异
      orbScaleX.value = 1;
      orbScaleY.value = 1;
      setOrbMorphState(0, 0);

      if (!springX.isSettled()) {
        rafId = requestAnimationFrame(tick);
      } else {
        rafId = 0;
      }
      return;
    }

    const baseScale = springScale.step(dt);
    const morphT = Math.max(0, Math.min(1, springMorph.step(dt)));

    const speedStretch = Math.min(0.22, Math.abs(vx) * 0.038);
    const speedSquash = Math.min(0.10, Math.abs(vx) * 0.016);

    orbScaleX.value = baseScale + speedStretch;
    orbScaleY.value = baseScale - speedSquash;

    // 静止 (morphT=0)：全域凸面拉伸 scale=18；交互 (morphT=1)：仅拉边缘 scale=28+速度微增
    const idleScale = 18;
    const rimScale = 28 + Math.min(8, Math.abs(vx) * 1.8);
    const currentScale = idleScale * (1 - morphT) + rimScale * morphT;
    setOrbMorphState(morphT, currentScale);

    if (
      !springX.isSettled() ||
      !springScale.isSettled() ||
      !springMorph.isSettled() ||
      isOrbPressed.value
    ) {
      rafId = requestAnimationFrame(tick);
    } else {
      rafId = 0;
      setOrbMorphState(0, 18);
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
  () => settingsStore.refractionLevel,
  () => {
    if (isBlurOnly.value) {
      springScale.pos = 1;
      springScale.target = 1;
      springScale.vel = 0;
      springMorph.pos = 0;
      springMorph.target = 0;
      springMorph.vel = 0;
      orbScaleX.value = 1;
      orbScaleY.value = 1;
      setOrbMorphState(0, 0);
    } else {
      syncLensGeometries();
      springMorph.target = 0;
      setOrbMorphState(0, 18);
    }
  }
);

watch(
  () => props.dockStyle,
  () => {
    setTimeout(() => syncLensGeometries(), 380);
  }
);

function onDockPointerDown(e: PointerEvent) {
  if (!dockRef.value) return;
  (e.currentTarget as HTMLElement).setPointerCapture(e.pointerId);

  isOrbPressed.value = true;
  isOrbDragging.value = false;
  pointerStartX = e.clientX;

  if (isBlurOnly.value) return;

  syncLensGeometries();

  const count = props.items.length;
  const rect = dockRef.value.getBoundingClientRect();
  const pad = 6;
  const slotW = (rect.width - pad * 2) / count;
  const clickIdx = Math.max(0, Math.min(count - 1, (e.clientX - rect.left - pad) / slotW - 0.5));

  // 按下交互态：平滑变形为「仅拉边缘」的交互水晶透镜
  springMorph.stiffness = 220;
  springMorph.damping = 24;
  springMorph.target = 1;
  springScale.target = 1.10;
  startPhysicsLoop();

  if (longPressTimer) clearTimeout(longPressTimer);
  longPressTimer = setTimeout(() => {
    if (isOrbPressed.value && !isBlurOnly.value) {
      triggerHaptic('medium');
      springScale.target = 1.22;
      springX.target = clickIdx;
      startPhysicsLoop();
    }
  }, 135);
}

function onDockPointerMove(e: PointerEvent) {
  if (!isOrbPressed.value || !dockRef.value) return;

  const deltaX = e.clientX - pointerStartX;
  if (Math.abs(deltaX) > 5 && !isOrbDragging.value) {
    isOrbDragging.value = true;
    if (!isBlurOnly.value) {
      springMorph.target = 1;
      springScale.target = 1.20;
    }
  }

  if (isOrbDragging.value) {
    const count = props.items.length;
    const maxIdx = count - 1;
    const rect = dockRef.value.getBoundingClientRect();
    const pad = 6;
    const slotW = (rect.width - pad * 2) / count;
    let rawIdx = (e.clientX - rect.left - pad) / slotW - 0.5;

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

  if (!isBlurOnly.value) {
    springScale.target = 1.0;
    // 松手后丝滑溶回「全域拉伸」静止态
    springMorph.stiffness = 150;
    springMorph.damping = 22;
    springMorph.target = 0;
  }

  startPhysicsLoop();

  if (releaseIdx !== props.activeIndex) {
    triggerHaptic('light');
    emit('select', releaseIdx);
  }
  isOrbDragging.value = false;
}

onMounted(() => {
  requestAnimationFrame(() => {
    syncLensGeometries();
    setOrbMorphState(0, isBlurOnly.value ? 0 : 18);
  });
  if (typeof ResizeObserver !== 'undefined') {
    resizeObserver = new ResizeObserver(() => syncLensGeometries());
    if (dockRef.value) resizeObserver.observe(dockRef.value);
    if (orbRef.value) resizeObserver.observe(orbRef.value);
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
      :class="[dockStyle, { 'blur-only': isBlurOnly }]"
      :style="{ gridTemplateColumns: `repeat(${items.length}, 1fr)` }"
      @pointerdown="onDockPointerDown"
      @pointermove="onDockPointerMove"
      @pointerup="onDockPointerUp"
      @pointercancel="onDockPointerUp"
    >
      <!-- Layer 0: 独立底栏玻璃层（液态=全域拉伸透视，毛玻璃=通透磨砂虚化） -->
      <div class="dock-glass-layer liquid-dock-glass" :class="dockStyle"></div>

      <!-- 图标与文字：液态模式下在泡下方被折射，毛玻璃模式下在磨砂泡上方保持锐利 -->
      <div
        v-for="(item, idx) in items"
        :key="item.key"
        class="dock-slot"
        :class="{ active: idx === activeIndex }"
      >
        <component :is="item.icon" :size="20" class="dock-icon" />
        <span class="dock-label">{{ item.label }}</span>
      </div>

      <!-- 滑块泡：液态模式静止全域拉伸 <-> 交互仅拉边缘；毛玻璃模式为统一真磨砂药丸 -->
      <div
        ref="orbRef"
        class="dock-liquid-orb liquid-orb-pill"
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

.nav-dock-bar {
  pointer-events: auto;
  position: relative;
  display: grid;
  width: 100%;
  max-width: 460px;
  padding: 6px;
  border-radius: 999px;
  touch-action: none;
  cursor: grab;
  overflow: visible;
  transition:
    max-width 0.38s var(--spring-easing),
    border-radius 0.36s var(--spring-easing),
    padding-bottom 0.36s var(--spring-easing);
}

.nav-dock-bar.docked {
  max-width: 100%;
  border-radius: 22px 22px 0 0;
  padding: 6px 6px calc(var(--inset-bottom) + 6px);
}

.nav-dock-bar:active {
  cursor: grabbing;
}

.dock-glass-layer {
  position: absolute;
  inset: 0;
  z-index: 0;
  border-radius: inherit;
  pointer-events: none;
  transition: border-radius 0.36s var(--spring-easing), var(--theme-transition);
}

.dock-glass-layer.docked {
  border-bottom: none;
  border-left: none;
  border-right: none;
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

/* 仅毛玻璃模式下：磨砂滑块置于图标下方，图标保持清晰锐利且磨砂质感完整可见 */
.nav-dock-bar.blur-only .dock-liquid-orb {
  z-index: 1;
}

.nav-dock-bar.blur-only .dock-slot {
  z-index: 2;
}

.nav-dock-bar.docked .dock-liquid-orb {
  bottom: calc(var(--inset-bottom) + 5px);
}
</style>

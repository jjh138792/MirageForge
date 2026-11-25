<!--
SPDX-License-Identifier: GPL-3.0-only
Copyright (c) 2025-2026 jjh138792
MirageForge: https://github.com/jjh138792/MirageForge
-->

<script setup lang="ts">
import { ref, computed, watch, onMounted, onUnmounted } from 'vue';
import type { Component } from 'vue';
import { triggerHaptic } from '../bridge/ksu-io';
import { JellySpring } from '../utils/jellyPhysics';
import {
  setOrbRefractionScale,
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
const idleOrbRef = ref<HTMLElement | null>(null);

const isBlurOnly = computed(() => settingsStore.refractionLevel === 'off');

const orbX = ref(props.activeIndex);
const orbScaleX = ref(1);
const orbScaleY = ref(1);
// 0 = 静止态（图标下层：全域拉伸+柔焦模糊）；1 = 交互态（图标上层：0 模糊仅拉边缘）
const interactiveT = ref(0);

const isOrbPressed = ref(false);
const isOrbDragging = ref(false);

// Q 弹果冻四维物理弹簧组：
// 1. springX：欠阻尼水平位移，切换时带轻微越界回弹
const springX = new JellySpring(props.activeIndex, 285, 16.5);
// 2. springScale：按压/松手整体缩放回弹
const springScale = new JellySpring(1, 320, 14);
// 3. springJelly：独立体积守恒果冻形变振荡器（低阻尼，产生 2~3 次真实 Q 弹晃动）
const springJelly = new JellySpring(0, 390, 10.5);
// 4. springInteractive：临界阻尼光学层切换（平滑无闪烁）
const springInteractive = new JellySpring(0, 230, 26);

let rafId = 0;
let lastFrameTime = performance.now();
let prevVelX = 0;
let longPressTimer: any = null;
let pointerStartX = 0;
let resizeObserver: ResizeObserver | null = null;

function clamp(v: number, lo: number, hi: number): number {
  return Math.max(lo, Math.min(hi, v));
}

function syncLensGeometries() {
  if (dockRef.value) {
    const dw = dockRef.value.offsetWidth;
    const dh = dockRef.value.offsetHeight;
    if (dw > 40 && dh > 24) {
      updateDockLensGeometry(dw, dh, props.dockStyle === 'docked');
    }
  }
  if (idleOrbRef.value) {
    const ow = idleOrbRef.value.offsetWidth;
    const oh = idleOrbRef.value.offsetHeight;
    if (ow > 24 && oh > 20) {
      updateOrbLensGeometry(ow, oh);
    }
  }
}

function startPhysicsLoop() {
  if (rafId) return;
  lastFrameTime = performance.now();

  const tick = (now: number) => {
    const dt = Math.min(Math.max((now - lastFrameTime) / 1000, 0.001), 0.032);
    lastFrameTime = now;

    const curX = springX.step(dt);
    const vx = springX.vel;
    const accelX = (vx - prevVelX) / dt;
    prevVelX = vx;

    orbX.value = curX;

    if (isBlurOnly.value) {
      // 仅毛玻璃模式：纯净平滑滑移，无静态/活动态变化
      orbScaleX.value = 1;
      orbScaleY.value = 1;
      interactiveT.value = 0;
      setOrbRefractionScale(0);

      if (!springX.isSettled()) {
        rafId = requestAnimationFrame(tick);
      } else {
        rafId = 0;
        prevVelX = 0;
      }
      return;
    }

    // 速度拉长 + 刹停减速时反向果冻压缩冲量（越界或落位刹停自动抖动！）
    const edgeOvershoot =
      curX < 0 ? -curX : curX > props.items.length - 1 ? curX - (props.items.length - 1) : 0;

    if (edgeOvershoot > 0.02) {
      // 顶到左右边界：像果冻撞玻璃壁一样横向压扁、纵向鼓起
      springJelly.target = -Math.min(0.24, edgeOvershoot * 0.45);
    } else {
      // 滑动中：随水平速度拉长成水滴形
      springJelly.target = Math.min(0.26, Math.abs(vx) * 0.052);
    }

    // 急加速/急刹停产生真实的惯性果冻冲量
    if (Math.abs(accelX) > 2.5) {
      const sign = vx * accelX >= 0 ? 1 : -1;
      const kick = clamp(Math.abs(accelX) * 0.012, 0, 0.45) * sign;
      springJelly.impulse(kick);
    }

    const baseScale = springScale.step(dt);
    const j = clamp(springJelly.step(dt), -0.30, 0.36);
    const inter = clamp(springInteractive.step(dt), 0, 1);

    // 严格体积守恒形变：横向拉长时纵向同步收窄，横向压扁时纵向同步鼓起
    orbScaleX.value = baseScale * (1 + j);
    orbScaleY.value = baseScale / (1 + j * 0.72);
    interactiveT.value = inter;

    // 交互态 0 模糊边缘拉伸强度随速度与交互权重连续变化
    const rimStrength = inter * (28 + Math.min(10, Math.abs(vx) * 2.0));
    setOrbRefractionScale(rimStrength);

    if (
      !springX.isSettled() ||
      !springScale.isSettled() ||
      !springJelly.isSettled() ||
      !springInteractive.isSettled() ||
      isOrbPressed.value
    ) {
      rafId = requestAnimationFrame(tick);
    } else {
      rafId = 0;
      prevVelX = 0;
      orbScaleX.value = 1;
      orbScaleY.value = 1;
      interactiveT.value = 0;
      setOrbRefractionScale(0);
    }
  };

  rafId = requestAnimationFrame(tick);
}

watch(
  () => props.activeIndex,
  (nextIdx) => {
    if (!isOrbDragging.value) {
      springX.stiffness = isBlurOnly.value ? 260 : 285;
      springX.damping = isBlurOnly.value ? 24 : 16.5;
      springX.target = nextIdx;
      if (!isBlurOnly.value) {
        // 外部切换页签也触发一次轻量 Q 弹果冻拉伸
        springJelly.impulse(1.8);
      }
      startPhysicsLoop();
    }
  }
);

watch(
  () => settingsStore.refractionLevel,
  () => {
    if (isBlurOnly.value) {
      springScale.snap(1);
      springJelly.snap(0);
      springInteractive.snap(0);
      orbScaleX.value = 1;
      orbScaleY.value = 1;
      interactiveT.value = 0;
      setOrbRefractionScale(0);
    } else {
      syncLensGeometries();
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
  const clickIdx = clamp((e.clientX - rect.left - pad) / slotW - 0.5, 0, count - 1);

  // 触碰瞬间：浮升至顶层 0 模糊交互态 + Q 弹按压脉冲
  springInteractive.stiffness = 260;
  springInteractive.damping = 26;
  springInteractive.target = 1;

  springScale.stiffness = 380;
  springScale.damping = 14;
  springScale.target = 1.13;
  springJelly.impulse(-1.4); // 瞬间微微竖向压弹一下，模拟按压果冻手感
  startPhysicsLoop();

  if (longPressTimer) clearTimeout(longPressTimer);
  longPressTimer = setTimeout(() => {
    if (isOrbPressed.value && !isBlurOnly.value) {
      triggerHaptic('medium');
      springScale.target = 1.24;
      springJelly.impulse(1.6);
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
    if (!isBlurOnly.value) {
      springInteractive.target = 1;
      springScale.target = 1.22;
      springJelly.impulse(1.8);
    }
  }

  if (isOrbDragging.value) {
    const count = props.items.length;
    const maxIdx = count - 1;
    const rect = dockRef.value.getBoundingClientRect();
    const pad = 6;
    const slotW = (rect.width - pad * 2) / count;
    let rawIdx = (e.clientX - rect.left - pad) / slotW - 0.5;

    // 橡皮筋阻尼越界（拉过头越拉越紧）
    if (rawIdx < 0) rawIdx = rawIdx * 0.28;
    if (rawIdx > maxIdx) rawIdx = maxIdx + (rawIdx - maxIdx) * 0.28;

    // 拖拽粘滞阻尼跟随：既有跟手度又有液滴惯性滞后感
    springX.stiffness = 430;
    springX.damping = 23;
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
    clamp((e.clientX - rect.left - pad) / slotW - 0.5, 0, count - 1)
  );

  // 松手归位：欠阻尼弹射落位 + 果冻回弹抖动
  springX.stiffness = isBlurOnly.value ? 260 : 300;
  springX.damping = isBlurOnly.value ? 24 : 15.5;
  springX.target = releaseIdx;

  if (!isBlurOnly.value) {
    springScale.stiffness = 300;
    springScale.damping = 12.5;
    springScale.target = 1.0;

    // 落位瞬间给果冻注入一次弹性回震
    springJelly.target = 0;
    springJelly.impulse(isOrbDragging.value ? -2.6 : 2.0);

    springInteractive.stiffness = 170;
    springInteractive.damping = 24;
    springInteractive.target = 0;
  }

  startPhysicsLoop();

  if (releaseIdx !== props.activeIndex) {
    triggerHaptic('light');
    emit('select', releaseIdx);
  }
  isOrbDragging.value = false;
}

const orbTransformStyle = computed(() => ({
  width: `calc((100% - 12px) / ${props.items.length})`,
  transform: `translate3d(${orbX.value * 100}%, 0, 0) scale(${orbScaleX.value.toFixed(4)}, ${orbScaleY.value.toFixed(4)})`,
}));

onMounted(() => {
  requestAnimationFrame(() => syncLensGeometries());
  if (typeof ResizeObserver !== 'undefined') {
    resizeObserver = new ResizeObserver(() => syncLensGeometries());
    if (dockRef.value) resizeObserver.observe(dockRef.value);
    if (idleOrbRef.value) resizeObserver.observe(idleOrbRef.value);
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
      <!-- Layer 0 (z:0): 整条底栏玻璃层（全域拉伸+柔和模糊；毛玻璃模式纯磨砂） -->
      <div class="dock-glass-layer liquid-dock-glass" :class="dockStyle"></div>

      <!-- Layer 1 (z:1): 静止态泡（位于图标下方！只拉伸+柔和模糊背后页面，绝不扭曲图标） -->
      <div
        ref="idleOrbRef"
        class="dock-orb orb-under orb-idle-glass"
        :style="{
          ...orbTransformStyle,
          opacity: isBlurOnly ? 1 : Math.max(0, 1 - interactiveT),
          visibility: !isBlurOnly && interactiveT > 0.99 ? 'hidden' : 'visible',
        }"
      ></div>

      <!-- Layer 2 (z:2): 导航图标与文字（静止时 100% 锐利端正不被拉伸） -->
      <div
        v-for="(item, idx) in items"
        :key="item.key"
        class="dock-slot"
        :class="{ active: idx === activeIndex }"
      >
        <component :is="item.icon" :size="20" class="dock-icon" />
        <span class="dock-label">{{ item.label }}</span>
      </div>

      <!-- Layer 3 (z:3): 交互态水晶泡（位于图标上方！0 模糊，仅拉边缘，松手无缝退回下层） -->
      <div
        v-if="!isBlurOnly"
        class="dock-orb orb-over orb-interactive-glass"
        :style="{
          ...orbTransformStyle,
          opacity: interactiveT,
          visibility: interactiveT < 0.01 ? 'hidden' : 'visible',
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
  z-index: 2;
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

.dock-orb {
  position: absolute;
  top: 5px;
  bottom: 5px;
  left: 6px;
  border-radius: 999px;
  pointer-events: none;
  transform-origin: center center;
}

.orb-under {
  z-index: 1;
}

.orb-over {
  z-index: 3;
}

.nav-dock-bar.docked .dock-orb {
  bottom: calc(var(--inset-bottom) + 5px);
}
</style>

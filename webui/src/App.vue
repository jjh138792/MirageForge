<script setup lang="ts">
import { ref, computed, onMounted, onUnmounted } from 'vue';
import {
  Cpu,
  Sparkles,
  Layers,
  Radar,
  Globe,
  Sun,
  Moon,
  Monitor,
  RefreshCw,
  ArrowUpCircle,
  ShieldCheck,
} from 'lucide-vue-next';
import MirageLogo from './components/MirageLogo.vue';
import TemplateForgeView from './components/TemplateForgeView.vue';
import PropAlchemyView from './components/PropAlchemyView.vue';
import AppMatrixView from './components/AppMatrixView.vue';
import ProbeRadarView from './components/ProbeRadarView.vue';

import { useI18n } from './i18n';
import {
  detectRuntimeEnv,
  triggerHaptic,
  SpringSolver,
  setOrbRefractionScale,
} from './bridge/ksu-io';
import {
  mirageStore,
  activeToastMsg,
  cycleThemeMode,
  checkOnlineUpdate,
  syncStateFromRustCli,
  reloadAppList,
  refreshAllProbeData,
  type DeviceTemplate,
} from './store/mirageStore';

type TabKey = 'templates' | 'alchemy' | 'apps' | 'probe';

const { locale, t, cycleLocale } = useI18n();
const currentTab = ref<TabKey>('templates');
const runtimeEnv = ref(detectRuntimeEnv());
const pendingParsedTemplate = ref<DeviceTemplate | null>(null);

const activeAppCount = computed(() => Object.keys(mirageStore.appRoutes).length);

const navItems = computed(() => [
  { key: 'templates' as TabKey, label: t('nav.templates'), icon: Cpu },
  { key: 'alchemy' as TabKey, label: t('nav.alchemy'), icon: Sparkles },
  { key: 'apps' as TabKey, label: t('nav.apps'), icon: Layers },
  { key: 'probe' as TabKey, label: t('nav.probe'), icon: Radar },
]);

const themeLabel = computed(() => {
  if (mirageStore.themeMode === 'auto') return t('app.themeAuto');
  return mirageStore.themeMode === 'light' ? t('app.themeLight') : t('app.themeDark');
});

// ============================================================================
// 二阶弹簧阻尼液态玻璃球物理引擎 (支持点击跳跃、长按放大透镜、快慢阻尼拖拽)
// ============================================================================
const dockRef = ref<HTMLElement | null>(null);
const orbX = ref(0);        // 归一化页签位置 0.0 ~ 3.0
const orbScaleX = ref(1);   // 水平水滴拉伸系数
const orbScaleY = ref(1);   // 垂直水滴压缩系数
const isOrbPressed = ref(false);
const isOrbDragging = ref(false);

const springX = new SpringSolver(0, 240, 20);
const springScale = new SpringSolver(1, 320, 18);

let rafId = 0;
let lastFrameTime = performance.now();
let longPressTimer: any = null;
let pointerStartX = 0;
let dragStartOrbX = 0;

function startPhysicsLoop() {
  if (rafId) return;
  lastFrameTime = performance.now();

  const tick = (now: number) => {
    const dt = (now - lastFrameTime) / 1000;
    lastFrameTime = now;

    const curX = springX.step(dt);
    const baseScale = springScale.step(dt);
    const vx = springX.vel; // 当前水平运动速度

    // 根据拖拽/滑动速度产生真实的流体阻尼拉伸形变 (拖得越快，水滴拉得越长)
    const speedStretch = Math.min(0.32, Math.abs(vx) * 0.055);
    const speedSquash = Math.min(0.16, Math.abs(vx) * 0.028);

    orbX.value = curX;
    orbScaleX.value = baseScale + speedStretch;
    orbScaleY.value = baseScale - speedSquash;

    // 动态调整 SVG 凸透镜折射强度：放大或高速移动时增强水波弯曲感
    const dynamicRefraction = 26 + Math.min(28, Math.abs(vx) * 4 + (baseScale - 1) * 55);
    setOrbRefractionScale(dynamicRefraction);

    if (!springX.isSettled() || !springScale.isSettled() || isOrbPressed.value) {
      rafId = requestAnimationFrame(tick);
    } else {
      rafId = 0;
      setOrbRefractionScale(26);
    }
  };

  rafId = requestAnimationFrame(tick);
}

function selectTabByIndex(idx: number, fromDrag = false) {
  const clamped = Math.max(0, Math.min(3, idx));
  const targetKey = navItems.value[clamped].key;

  // 根据跨越距离调整弹簧刚度，使长距离滑动更有流体惯性
  springX.stiffness = fromDrag ? 290 : 230;
  springX.damping = fromDrag ? 22 : 19;
  springX.target = clamped;
  startPhysicsLoop();

  if (currentTab.value !== targetKey) {
    triggerHaptic('light');
    currentTab.value = targetKey;
    if (targetKey === 'probe') {
      refreshAllProbeData();
    }
  }
}

function onDockPointerDown(e: PointerEvent) {
  if (!dockRef.value) return;
  (e.currentTarget as HTMLElement).setPointerCapture(e.pointerId);

  isOrbPressed.value = true;
  isOrbDragging.value = false;
  pointerStartX = e.clientX;

  const rect = dockRef.value.getBoundingClientRect();
  const pad = 6;
  const innerW = rect.width - pad * 2;
  const slotW = innerW / 4;
  const clickIdx = Math.max(0, Math.min(3, (e.clientX - rect.left - pad) / slotW - 0.5));

  dragStartOrbX = clickIdx;

  // 长按超过 150ms 触发液态玻璃球膨胀放大为凸透镜
  if (longPressTimer) clearTimeout(longPressTimer);
  longPressTimer = setTimeout(() => {
    if (isOrbPressed.value) {
      triggerHaptic('medium');
      springScale.target = 1.22;
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
    springScale.target = 1.18; // 拖拽态保持水滴饱满膨胀
  }

  if (isOrbDragging.value) {
    const rect = dockRef.value.getBoundingClientRect();
    const pad = 6;
    const slotW = (rect.width - pad * 2) / 4;
    let rawIdx = (e.clientX - rect.left - pad) / slotW - 0.5;

    // 左右边界橡皮筋阻尼效应 (Rubber-band damping)
    if (rawIdx < 0) rawIdx = rawIdx * 0.28;
    if (rawIdx > 3) rawIdx = 3 + (rawIdx - 3) * 0.28;

    // 拖拽时调低弹簧刚度、增大阻尼，让球呈现“水滴粘滞跟随”手感
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

  const rect = dockRef.value.getBoundingClientRect();
  const pad = 6;
  const slotW = (rect.width - pad * 2) / 4;
  const releaseIdx = Math.round(
    Math.max(0, Math.min(3, (e.clientX - rect.left - pad) / slotW - 0.5))
  );

  selectTabByIndex(releaseIdx, isOrbDragging.value);
  isOrbDragging.value = false;
}

function handleAlchemyParsed(tpl: DeviceTemplate) {
  pendingParsedTemplate.value = tpl;
  selectTabByIndex(0);
}

function handleUpdateClick() {
  if (mirageStore.updateInfo.hasUpdate && mirageStore.updateInfo.zipUrl) {
    window.open(mirageStore.updateInfo.zipUrl, '_blank');
  } else {
    checkOnlineUpdate(t);
  }
}

onMounted(async () => {
  await syncStateFromRustCli();
  await reloadAppList();
});

onUnmounted(() => {
  if (rafId) cancelAnimationFrame(rafId);
});
</script>

<template>
  <div class="mirage-shell">
    <!-- 1. 动态流体极光画布 (随日夜主题平滑过渡) -->
    <div class="ambient-canvas" aria-hidden="true">
      <div class="aurora-blob blob-1"></div>
      <div class="aurora-blob blob-2"></div>
      <div class="aurora-blob blob-3"></div>
    </div>

    <!-- 2. 顶部液态玻璃控制栏 -->
    <header class="top-header liquid-glass liquid-glass-elevated" v-touch-glass>
      <div class="brand-cluster">
        <MirageLogo :size="44" />
        <div class="brand-titles">
          <div class="title-line">
            <h1>{{ t('app.title') }}</h1>
            <span class="version-chip">v{{ mirageStore.version }}</span>
            <span class="env-chip">{{ runtimeEnv }}</span>
          </div>
          <p class="subtitle-line">{{ t('app.subtitle') }}</p>
        </div>
      </div>

      <div class="header-actions">
        <!-- 在线检查更新按钮 -->
        <button
          class="tool-btn tactile-press"
          :class="{ 'has-update-glow': mirageStore.updateInfo.hasUpdate }"
          @click="handleUpdateClick"
          :title="t('app.checkUpdate')"
        >
          <ArrowUpCircle v-if="mirageStore.updateInfo.hasUpdate" :size="15" />
          <RefreshCw
            v-else
            :size="14"
            :class="{ 'spin-anim': mirageStore.updateInfo.checking }"
          />
          <span>
            {{
              mirageStore.updateInfo.hasUpdate
                ? `v${mirageStore.updateInfo.latestVersion}`
                : t('app.checkUpdate')
            }}
          </span>
        </button>

        <!-- 日间 / 夜间 / 跟随系统切换按钮 -->
        <button
          class="tool-btn tactile-press"
          @click="cycleThemeMode()"
          :title="themeLabel"
        >
          <Monitor v-if="mirageStore.themeMode === 'auto'" :size="14" />
          <Sun v-else-if="mirageStore.themeMode === 'light'" :size="14" />
          <Moon v-else :size="14" />
          <span>{{ themeLabel }}</span>
        </button>

        <!-- 简 / 繁 / EN 语言切换按钮 -->
        <button class="tool-btn tactile-press" @click="cycleLocale()">
          <Globe :size="14" />
          <span>{{ locale === 'zh-CN' ? '简' : locale === 'zh-TW' ? '繁' : 'EN' }}</span>
        </button>

        <!-- 已伪装应用数徽章 -->
        <div class="active-pill">
          <ShieldCheck :size="14" />
          <span>{{ t('app.activeBadge', { count: activeAppCount }) }}</span>
        </div>
      </div>
    </header>

    <!-- 3. 主内容动态视图区 -->
    <main class="viewport-stage">
      <Transition name="view-morph" mode="out-in">
        <TemplateForgeView
          v-if="currentTab === 'templates'"
          :incoming-template="pendingParsedTemplate"
          @clear-incoming="pendingParsedTemplate = null"
        />
        <PropAlchemyView
          v-else-if="currentTab === 'alchemy'"
          @parsed="handleAlchemyParsed"
        />
        <AppMatrixView v-else-if="currentTab === 'apps'" />
        <ProbeRadarView v-else-if="currentTab === 'probe'" />
      </Transition>
    </main>

    <!-- 4. 液态玻璃浮动通知气泡 -->
    <Transition name="view-morph">
      <div v-if="activeToastMsg" class="liquid-toast liquid-glass liquid-glass-elevated">
        <span class="toast-dot"></span>
        <span>{{ activeToastMsg }}</span>
      </div>
    </Transition>

    <!-- 5. 底部悬浮灵动岛：支持点击回弹、长按透镜放大、快慢阻尼拖拽的真·液态玻璃球 -->
    <nav class="floating-dock-wrap">
      <div
        ref="dockRef"
        class="floating-dock liquid-glass liquid-glass-elevated"
        @pointerdown="onDockPointerDown"
        @pointermove="onDockPointerMove"
        @pointerup="onDockPointerUp"
        @pointercancel="onDockPointerUp"
      >
        <!-- 底层页签图标与文字 (处于 z-index: 1，会被上方的液态玻璃球实时光学折射！) -->
        <div
          v-for="(item, idx) in navItems"
          :key="item.key"
          class="dock-item"
          :class="{ active: currentTab === item.key }"
        >
          <component :is="item.icon" :size="19" class="dock-icon" />
          <span class="dock-label">{{ item.label }}</span>
        </div>

        <!-- 顶层光学折射液态玻璃球 (处于 z-index: 2，透过它直接折射下方的文字与背景) -->
        <div
          class="dock-liquid-orb liquid-orb-pill"
          :class="{ 'orb-magnified': isOrbPressed }"
          :style="{
            transform: `translate3d(${orbX * 100}%, 0, 0) scale(${orbScaleX}, ${orbScaleY})`,
          }"
        >
          <div class="orb-specular-glint"></div>
        </div>
      </div>
    </nav>
  </div>
</template>

<style scoped>
.mirage-shell {
  max-width: 860px;
  margin: 0 auto;
  padding: 12px 14px;
}

.top-header {
  display: flex;
  justify-content: space-between;
  align-items: center;
  padding: 14px 18px;
  border-radius: 26px;
  margin-bottom: 16px;
  gap: 12px;
  flex-wrap: wrap;
}

.brand-cluster {
  display: flex;
  align-items: center;
  gap: 13px;
}

.title-line {
  display: flex;
  align-items: center;
  gap: 7px;
  flex-wrap: wrap;
}

.title-line h1 {
  font-size: 17.5px;
  font-weight: 800;
}

.version-chip {
  font-size: 11px;
  font-weight: 700;
  padding: 2px 8px;
  border-radius: 8px;
  background: rgba(139, 92, 246, 0.18);
  color: var(--accent-primary);
  border: 1px solid rgba(139, 92, 246, 0.35);
}

.env-chip {
  font-size: 10.5px;
  font-weight: 700;
  padding: 2px 7px;
  border-radius: 8px;
  background: rgba(6, 182, 212, 0.14);
  color: var(--accent-secondary);
  border: 1px solid rgba(6, 182, 212, 0.32);
}

.subtitle-line {
  font-size: 11.5px;
  color: var(--text-secondary);
  margin-top: 3px;
}

.header-actions {
  display: flex;
  align-items: center;
  gap: 7px;
  flex-wrap: wrap;
}

.tool-btn {
  display: inline-flex;
  align-items: center;
  gap: 5px;
  padding: 6.5px 11px;
  border-radius: 14px;
  background: var(--subcard-bg);
  border: 1px solid var(--glass-border);
  color: var(--text-primary);
  font-size: 11.5px;
  font-weight: 700;
  transition: var(--theme-transition);
}

.has-update-glow {
  background: rgba(245, 158, 11, 0.22);
  color: #f59e0b;
  border-color: rgba(245, 158, 11, 0.5);
  box-shadow: 0 0 14px rgba(245, 158, 11, 0.35);
}

.spin-anim {
  animation: spin 1s linear infinite;
}

@keyframes spin {
  from { transform: rotate(0deg); }
  to { transform: rotate(360deg); }
}

.active-pill {
  display: inline-flex;
  align-items: center;
  gap: 5px;
  padding: 6.5px 11px;
  border-radius: 14px;
  background: rgba(16, 185, 129, 0.15);
  color: #10b981;
  border: 1px solid rgba(16, 185, 129, 0.35);
  font-size: 11.5px;
  font-weight: 700;
}

.viewport-stage {
  min-height: 62vh;
}

.liquid-toast {
  position: fixed;
  bottom: calc(var(--inset-bottom) + 90px);
  left: 50%;
  transform: translateX(-50%);
  padding: 10px 18px;
  border-radius: 20px;
  display: flex;
  align-items: center;
  gap: 9px;
  font-size: 13px;
  font-weight: 700;
  z-index: 200;
  white-space: nowrap;
}

.toast-dot {
  width: 8px;
  height: 8px;
  border-radius: 50%;
  background: #10b981;
  box-shadow: 0 0 10px #10b981;
}

/* ==========================================================================
   底部可拖拽光学折射液态玻璃球导航栏
   ========================================================================== */
.floating-dock-wrap {
  position: fixed;
  bottom: calc(var(--inset-bottom) + 12px);
  left: 0;
  right: 0;
  display: flex;
  justify-content: center;
  padding: 0 16px;
  z-index: 95;
  pointer-events: none;
}

.floating-dock {
  pointer-events: auto;
  position: relative;
  display: grid;
  grid-template-columns: repeat(4, 1fr);
  width: 100%;
  max-width: 480px;
  padding: 6px;
  border-radius: 30px;
  touch-action: none;
  cursor: grab;
  overflow: visible;
}

.floating-dock:active {
  cursor: grabbing;
}

/* 底层图标与文字：处于玻璃球下方，以便被液态玻璃球的 backdrop-filter 实时折射放大 */
.dock-item {
  position: relative;
  z-index: 1;
  color: var(--text-secondary);
  padding: 9px 4px;
  border-radius: 24px;
  display: flex;
  flex-direction: column;
  align-items: center;
  justify-content: center;
  gap: 3px;
  pointer-events: none;
  transition: color 0.25s ease, transform 0.25s var(--bounce-easing);
}

.dock-item.active {
  color: var(--text-primary);
  font-weight: 800;
}

.dock-item.active .dock-icon {
  transform: scale(1.08);
  color: var(--accent-primary);
}

.dock-label {
  font-size: 11px;
  font-weight: 700;
}

/* 顶层真·液态玻璃球：覆盖在 dock-item 之上，透过 backdrop-filter: url(#mirage-orb-lens) 产生水滴折射 */
.dock-liquid-orb {
  position: absolute;
  z-index: 2;
  top: 5px;
  bottom: 5px;
  left: 6px;
  width: calc((100% - 12px) / 4);
  border-radius: 24px;
  pointer-events: none;
  transform-origin: center center;
}

/* 水滴顶部高光镜面反射弧 */
.orb-specular-glint {
  position: absolute;
  top: 3px;
  left: 14%;
  right: 14%;
  height: 32%;
  border-radius: 999px;
  background: linear-gradient(
    180deg,
    rgba(255, 255, 255, 0.65) 0%,
    rgba(255, 255, 255, 0.05) 100%
  );
  pointer-events: none;
}
</style>

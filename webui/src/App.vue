<!--
SPDX-License-Identifier: GPL-3.0-only
Copyright (c) 2025-2026 jjh138792
MirageForge: https://github.com/jjh138792/MirageForge
-->

<script setup lang="ts">
import { ref, computed, onMounted } from 'vue';
import { Home, Cpu, Layers, Settings, ArrowLeft } from 'lucide-vue-next';

import HomeView from './views/HomeView.vue';
import TemplatesView from './views/TemplatesView.vue';
import AppsView from './views/AppsView.vue';
import SettingsView from './views/SettingsView.vue';
import ProbeRadarView from './components/ProbeRadarView.vue';
import BottomNavBar, { type NavBarItem } from './components/BottomNavBar.vue';
import OtaChangelogModal from './components/OtaChangelogModal.vue';
import OtaUpdateView from './components/OtaUpdateView.vue';

import { useI18n } from './i18n';
import { triggerHaptic } from './bridge/ksu-io';
import {
  mirageStore,
  settingsStore,
  activeToastMsg,
  checkOnlineUpdate,
  syncStateFromRustCli,
  reloadAppList,
  refreshAllProbeData,
  fetchHostDeviceInfo,
} from './store/mirageStore';

type TabKey = 'home' | 'templates' | 'apps' | 'settings';
type SubPageKey = null | 'ota' | 'probe';

const { t } = useI18n();
const tabOrder: TabKey[] = ['home', 'templates', 'apps', 'settings'];

const currentTab = ref<TabKey>('home');
const slideTransitionName = ref<'slide-left' | 'slide-right'>('slide-left');
const subPage = ref<SubPageKey>(null);
const showChangelogModal = ref(false);
const otaAutoStart = ref(false);

// 全屏横向滑动手势与底栏液态球实时联动状态
const isPageSwiping = ref(false);
const pageSwipeOrbIndex = ref<number | null>(null);
const pageDragOffsetPx = ref(0);

let touchStartX = 0;
let touchStartY = 0;
let touchLastX = 0;
let touchStartTs = 0;
let touchLockedAxis: 'x' | 'y' | null = null;
let touchIgnored = false;

const activeTabIndex = computed(() => tabOrder.indexOf(currentTab.value));

const navItems = computed<NavBarItem[]>(() => [
  { key: 'home', label: t('nav.home'), icon: Home },
  { key: 'templates', label: t('nav.templates'), icon: Cpu },
  { key: 'apps', label: t('nav.apps'), icon: Layers },
  { key: 'settings', label: t('nav.settings'), icon: Settings },
]);

const tabsStageStyle = computed(() => {
  if (!isPageSwiping.value && pageDragOffsetPx.value === 0) {
    return undefined;
  }
  return {
    transform: `translate3d(${pageDragOffsetPx.value.toFixed(1)}px, 0, 0)`,
    transition: isPageSwiping.value ? 'none' : 'transform 0.28s var(--spring-easing)',
  };
});

function switchTabByIndex(nextIdx: number) {
  const clamped = Math.max(0, Math.min(tabOrder.length - 1, nextIdx));
  const curIdx = activeTabIndex.value;
  if (clamped === curIdx) return;

  slideTransitionName.value = clamped > curIdx ? 'slide-left' : 'slide-right';
  currentTab.value = tabOrder[clamped];
}

function switchTabByKey(key: TabKey) {
  switchTabByIndex(tabOrder.indexOf(key));
}

function shouldIgnoreSwipeStart(target: EventTarget | null, clientX: number): boolean {
  if (subPage.value !== null || showChangelogModal.value) return true;
  // 留出左右 24px 边缘给 Android 系统侧滑返回手势
  if (typeof window !== 'undefined' && (clientX < 24 || clientX > window.innerWidth - 24)) {
    return true;
  }
  let el = target as HTMLElement | null;
  while (el && el !== document.body) {
    const tag = el.tagName;
    if (tag === 'INPUT' || tag === 'TEXTAREA' || tag === 'SELECT' || tag === 'OPTION') {
      return true;
    }
    if (el.hasAttribute('data-no-swipe')) {
      return true;
    }
    if (el.scrollWidth > el.clientWidth + 6) {
      const ovx = window.getComputedStyle(el).overflowX;
      if (ovx === 'auto' || ovx === 'scroll') {
        return true;
      }
    }
    el = el.parentElement;
  }
  return false;
}

function onTabsTouchStart(e: TouchEvent) {
  if (e.touches.length !== 1) {
    touchIgnored = true;
    return;
  }
  const touch = e.touches[0];
  touchIgnored = shouldIgnoreSwipeStart(e.target, touch.clientX);
  touchLockedAxis = null;
  if (touchIgnored) return;

  touchStartX = touch.clientX;
  touchStartY = touch.clientY;
  touchLastX = touch.clientX;
  touchStartTs = Date.now();
}

function onTabsTouchMove(e: TouchEvent) {
  if (touchIgnored) return;
  if (e.touches.length !== 1) {
    resetPageSwipe();
    return;
  }

  const touch = e.touches[0];
  const dx = touch.clientX - touchStartX;
  const dy = touch.clientY - touchStartY;
  touchLastX = touch.clientX;

  if (touchLockedAxis === null) {
    if (Math.hypot(dx, dy) < 10) return;
    // 严格水平偏向角校验，绝不与页面纵向滚动抢手势
    if (Math.abs(dx) > Math.abs(dy) * 1.4) {
      touchLockedAxis = 'x';
      isPageSwiping.value = true;
    } else {
      touchLockedAxis = 'y';
      return;
    }
  }

  if (touchLockedAxis !== 'x') return;

  const vw = typeof window !== 'undefined' ? window.innerWidth : 390;
  const span = Math.max(240, vw * 0.78);
  const deltaIdx = Math.max(-1.05, Math.min(1.05, -dx / span));
  const maxIdx = tabOrder.length - 1;

  let rawIdx = activeTabIndex.value + deltaIdx;
  if (rawIdx < 0) rawIdx *= 0.28;
  if (rawIdx > maxIdx) rawIdx = maxIdx + (rawIdx - maxIdx) * 0.28;

  pageSwipeOrbIndex.value = rawIdx;

  // 页面本体跟随做轻柔阻尼微移，边界处施加更强阻力
  const atLeftEdge = activeTabIndex.value === 0 && dx > 0;
  const atRightEdge = activeTabIndex.value === maxIdx && dx < 0;
  const damp = atLeftEdge || atRightEdge ? 0.08 : 0.17;
  pageDragOffsetPx.value = Math.max(-48, Math.min(48, dx * damp));
}

function resetPageSwipe() {
  touchLockedAxis = null;
  touchIgnored = false;
  isPageSwiping.value = false;
  pageSwipeOrbIndex.value = null;
  pageDragOffsetPx.value = 0;
}

function onTabsTouchEnd(e: TouchEvent) {
  if (touchIgnored) {
    touchIgnored = false;
    return;
  }

  if (touchLockedAxis === 'x' && e.type === 'touchend') {
    const dx = touchLastX - touchStartX;
    const dt = Math.max(16, Date.now() - touchStartTs);
    const vx = dx / dt; // px/ms
    const vw = typeof window !== 'undefined' ? window.innerWidth : 390;
    const distTrig = Math.abs(dx) > Math.min(82, vw * 0.22);
    const velTrig = Math.abs(vx) > 0.35 && Math.abs(dx) > 26;

    if (distTrig || velTrig) {
      if (dx < 0 && activeTabIndex.value < tabOrder.length - 1) {
        triggerHaptic('light');
        switchTabByIndex(activeTabIndex.value + 1);
      } else if (dx > 0 && activeTabIndex.value > 0) {
        triggerHaptic('light');
        switchTabByIndex(activeTabIndex.value - 1);
      }
    }
  }

  resetPageSwipe();
}

function openProbePage() {
  triggerHaptic('light');
  subPage.value = 'probe';
  refreshAllProbeData();
}

async function handleCheckOrOpenUpdate() {
  if (subPage.value === 'ota') return;
  if (mirageStore.updateInfo.hasUpdate) {
    triggerHaptic('light');
    showChangelogModal.value = true;
    return;
  }
  const hasNew = await checkOnlineUpdate(t, false);
  if (hasNew) {
    showChangelogModal.value = true;
  }
}

function enterOtaUpdatePage() {
  showChangelogModal.value = false;
  otaAutoStart.value = true;
  subPage.value = 'ota';
}

function leaveSubPage() {
  triggerHaptic('light');
  otaAutoStart.value = false;
  subPage.value = null;
}

onMounted(async () => {
  await syncStateFromRustCli();
  reloadAppList();
  fetchHostDeviceInfo(false);
  checkOnlineUpdate(t, true);
});
</script>

<template>
  <div class="mirage-shell">
    <!-- 1. 动态极光流体背景 -->
    <div class="ambient-canvas" aria-hidden="true">
      <div class="aurora-blob blob-1"></div>
      <div class="aurora-blob blob-2"></div>
      <div class="aurora-blob blob-3"></div>
    </div>

    <!-- 2. 主内容视图区（次级全屏页 + 4 主页全屏手势滑页联动） -->
    <main class="viewport-stage">
      <Transition name="page-push" mode="out-in">
        <!-- 次级全屏页 A：独立 OTA 在线更新页 -->
        <OtaUpdateView
          v-if="subPage === 'ota'"
          key="sub-ota"
          :auto-start="otaAutoStart"
          @back="leaveSubPage"
        />

        <!-- 次级全屏页 B：独立幻境探针与崩溃捕获页 -->
        <section v-else-if="subPage === 'probe'" key="sub-probe" class="subpage-wrap">
          <div class="subpage-topbar liquid-glass" v-touch-glass>
            <button class="btn-liquid-ghost tactile-press" @click="leaveSubPage">
              <ArrowLeft :size="15" />
              <span>{{ t('app.backBtn') }}</span>
            </button>
            <h2 class="subpage-title">{{ t('nav.probe') }}</h2>
          </div>
          <ProbeRadarView />
        </section>

        <!-- 4 主页签（全屏横向滑动实时驱动底栏液态球） -->
        <div
          v-else
          key="main-tabs"
          class="tabs-stage"
          :style="tabsStageStyle"
          @touchstart.passive="onTabsTouchStart"
          @touchmove.passive="onTabsTouchMove"
          @touchend.passive="onTabsTouchEnd"
          @touchcancel.passive="onTabsTouchEnd"
        >
          <Transition :name="slideTransitionName" mode="out-in">
            <HomeView
              v-if="currentTab === 'home'"
              key="tab-home"
              @navigate="switchTabByKey"
              @open-probe="openProbePage"
              @check-update="handleCheckOrOpenUpdate"
            />

            <TemplatesView
              v-else-if="currentTab === 'templates'"
              key="tab-templates"
              @jump-to-apps="switchTabByKey('apps')"
            />

            <AppsView
              v-else-if="currentTab === 'apps'"
              key="tab-apps"
            />

            <SettingsView
              v-else
              key="tab-settings"
              @open-probe="openProbePage"
              @check-update="handleCheckOrOpenUpdate"
            />
          </Transition>
        </div>
      </Transition>
    </main>

    <!-- 3. 更新日志弹窗 -->
    <OtaChangelogModal
      :visible="showChangelogModal"
      @close="showChangelogModal = false"
      @start-update="enterOtaUpdatePage"
    />

    <!-- 4. 液态玻璃浮动提示气泡 -->
    <Transition name="view-morph">
      <div v-if="activeToastMsg" class="liquid-toast liquid-glass liquid-glass-elevated">
        <span class="toast-dot"></span>
        <span>{{ activeToastMsg }}</span>
      </div>
    </Transition>

    <!-- 5. 底部双态光学液态玻璃导航栏（实时接收全屏滑页进度） -->
    <Transition name="view-morph">
      <BottomNavBar
        v-if="!subPage"
        :items="navItems"
        :active-index="activeTabIndex"
        :dock-style="settingsStore.dockStyle"
        :external-orb-index="pageSwipeOrbIndex"
        :external-swiping="isPageSwiping"
        @select="switchTabByIndex"
      />
    </Transition>
  </div>
</template>

<style scoped>
.mirage-shell {
  max-width: 860px;
  margin: 0 auto;
  padding: 10px 14px;
}

.viewport-stage {
  min-height: 76vh;
}

.tabs-stage {
  overflow-x: hidden;
  touch-action: pan-y;
}

.subpage-wrap {
  display: flex;
  flex-direction: column;
  gap: 13px;
}

.subpage-topbar {
  display: flex;
  align-items: center;
  justify-content: space-between;
  padding: 11px 14px;
  border-radius: 22px;
}

.subpage-title {
  font-size: 15px;
  font-weight: 800;
}

.liquid-toast {
  position: fixed;
  bottom: calc(var(--inset-bottom) + 92px);
  left: 50%;
  transform: translateX(-50%);
  padding: 10px 18px;
  border-radius: 20px;
  display: flex;
  align-items: center;
  gap: 9px;
  font-size: 13px;
  font-weight: 700;
  z-index: 220;
  white-space: nowrap;
}

.toast-dot {
  width: 8px;
  height: 8px;
  border-radius: 50%;
  background: #10b981;
  box-shadow: 0 0 10px #10b981;
}
</style>

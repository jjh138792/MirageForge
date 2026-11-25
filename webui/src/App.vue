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

const activeTabIndex = computed(() => tabOrder.indexOf(currentTab.value));

const navItems = computed<NavBarItem[]>(() => [
  { key: 'home', label: t('nav.home'), icon: Home },
  { key: 'templates', label: t('nav.templates'), icon: Cpu },
  { key: 'apps', label: t('nav.apps'), icon: Layers },
  { key: 'settings', label: t('nav.settings'), icon: Settings },
]);

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

    <!-- 2. 主内容视图区（次级全屏页 + 4 主页方向性水平滑动） -->
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

        <!-- 4 主页签（方向性左滑 / 右滑过渡） -->
        <div v-else key="main-tabs" class="tabs-stage">
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

    <!-- 5. 底部双态光学液态玻璃导航栏 -->
    <Transition name="view-morph">
      <BottomNavBar
        v-if="!subPage"
        :items="navItems"
        :active-index="activeTabIndex"
        :dock-style="settingsStore.dockStyle"
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

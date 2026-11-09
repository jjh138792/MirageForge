<script setup lang="ts">
import { ref, computed, onMounted } from 'vue';
import {
  Cpu,
  Sparkles,
  Layers,
  Radar,
  Globe,
  ShieldCheck,
} from 'lucide-vue-next';
import { useI18n } from './i18n';
import { detectRuntimeEnv, triggerHaptic } from './bridge/webui-x';
import {
  mirageStore,
  activeToastMsg,
  syncStateFromRustCli,
  reloadAppList,
  refreshAllProbeData,
  type DeviceTemplate,
} from './store/mirageStore';

import TemplateForgeView from './components/TemplateForgeView.vue';
import PropAlchemyView from './components/PropAlchemyView.vue';
import AppMatrixView from './components/AppMatrixView.vue';
import ProbeRadarView from './components/ProbeRadarView.vue';

type TabKey = 'templates' | 'alchemy' | 'apps' | 'probe';

const { locale, t, cycleLocale } = useI18n();
const currentTab = ref<TabKey>('templates');
const runtimeEnv = ref(detectRuntimeEnv());

// 当从「属性炼金」解析出新模板时，自动跳转到「全息熔炉」并打开编辑框
const pendingParsedTemplate = ref<DeviceTemplate | null>(null);

const activeAppCount = computed(() => Object.keys(mirageStore.appRoutes).length);

const navItems = computed(() => [
  { key: 'templates' as TabKey, label: t('nav.templates'), icon: Cpu },
  { key: 'alchemy' as TabKey, label: t('nav.alchemy'), icon: Sparkles },
  { key: 'apps' as TabKey, label: t('nav.apps'), icon: Layers },
  { key: 'probe' as TabKey, label: t('nav.probe'), icon: Radar },
]);

const activeTabIndex = computed(() =>
  navItems.value.findIndex((item) => item.key === currentTab.value)
);

function switchTab(tab: TabKey) {
  if (currentTab.value === tab) return;
  triggerHaptic('light');
  currentTab.value = tab;
  if (tab === 'probe') {
    refreshAllProbeData();
  }
}

function handleSwitchLocale() {
  triggerHaptic('medium');
  cycleLocale();
}

function handleAlchemyParsed(tpl: DeviceTemplate) {
  pendingParsedTemplate.value = tpl;
  currentTab.value = 'templates';
}

onMounted(async () => {
  await syncStateFromRustCli();
  await reloadAppList();
});
</script>

<template>
  <div class="mirage-shell">
    <!-- 1. 动态流体极光背景 -->
    <div class="ambient-canvas" aria-hidden="true">
      <div class="aurora-blob blob-1"></div>
      <div class="aurora-blob blob-2"></div>
      <div class="aurora-blob blob-3"></div>
    </div>

    <!-- 2. 顶部悬浮液态玻璃状态栏 -->
    <header class="top-header liquid-glass liquid-glass-elevated" v-touch-glass>
      <div class="brand-cluster">
        <div class="orb-logo">
          <span>蜃</span>
          <div class="orb-ring"></div>
        </div>
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
        <button
          class="locale-btn tactile-press"
          @click="handleSwitchLocale"
          :title="locale"
        >
          <Globe :size="15" />
          <span>{{ locale === 'zh-CN' ? '简' : locale === 'zh-TW' ? '繁' : 'EN' }}</span>
        </button>

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

    <!-- 4. 液态玻璃浮动通知气泡 (Liquid Toast) -->
    <Transition name="view-morph">
      <div v-if="activeToastMsg" class="liquid-toast liquid-glass-elevated">
        <span class="toast-dot"></span>
        <span>{{ activeToastMsg }}</span>
      </div>
    </Transition>

    <!-- 5. 底部悬浮灵动岛导航栏 (Floating Liquid Glass Dock) -->
    <nav class="floating-dock-wrap">
      <div class="floating-dock liquid-glass liquid-glass-elevated">
        <!-- 滑动的液态高光胶囊底座 -->
        <div
          class="dock-active-glider"
          :style="{ transform: `translate3d(${activeTabIndex * 100}%, 0, 0)` }"
        ></div>

        <button
          v-for="item in navItems"
          :key="item.key"
          class="dock-item tactile-press"
          :class="{ active: currentTab === item.key }"
          @click="switchTab(item.key)"
        >
          <component :is="item.icon" :size="19" class="dock-icon" />
          <span class="dock-label">{{ item.label }}</span>
        </button>
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

.orb-logo {
  position: relative;
  width: 46px;
  height: 46px;
  border-radius: 16px;
  background: linear-gradient(135deg, #8b5cf6 0%, #06b6d4 100%);
  display: flex;
  align-items: center;
  justify-content: center;
  font-size: 22px;
  font-weight: 900;
  color: #fff;
  box-shadow:
    inset 0 1.5px 1.5px rgba(255, 255, 255, 0.55),
    0 8px 22px -4px rgba(139, 92, 246, 0.6);
}

.orb-ring {
  position: absolute;
  inset: -2px;
  border-radius: 18px;
  border: 1px solid rgba(255, 255, 255, 0.28);
  pointer-events: none;
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
  letter-spacing: 0.2px;
}

.version-chip {
  font-size: 11px;
  font-weight: 700;
  padding: 2px 8px;
  border-radius: 8px;
  background: rgba(139, 92, 246, 0.22);
  color: #ddd6fe;
  border: 1px solid rgba(139, 92, 246, 0.42);
}

.env-chip {
  font-size: 10.5px;
  font-weight: 600;
  padding: 2px 7px;
  border-radius: 8px;
  background: rgba(6, 182, 212, 0.16);
  color: #67e8f9;
  border: 1px solid rgba(6, 182, 212, 0.35);
}

.subtitle-line {
  font-size: 11.5px;
  color: var(--text-secondary);
  margin-top: 3px;
}

.header-actions {
  display: flex;
  align-items: center;
  gap: 8px;
}

.locale-btn {
  display: inline-flex;
  align-items: center;
  gap: 5px;
  padding: 7px 11px;
  border-radius: 14px;
  background: rgba(255, 255, 255, 0.07);
  border: 1px solid rgba(255, 255, 255, 0.16);
  color: var(--text-primary);
  font-size: 12px;
  font-weight: 700;
}

.active-pill {
  display: inline-flex;
  align-items: center;
  gap: 6px;
  padding: 7px 12px;
  border-radius: 16px;
  background: rgba(16, 185, 129, 0.16);
  color: #6ee7b7;
  border: 1px solid rgba(16, 185, 129, 0.38);
  font-size: 12px;
  font-weight: 700;
}

.viewport-stage {
  min-height: 62vh;
}

/* 浮动通知气泡 */
.liquid-toast {
  position: fixed;
  bottom: calc(var(--inset-bottom) + 84px);
  left: 50%;
  transform: translateX(-50%);
  padding: 10px 18px;
  border-radius: 20px;
  display: flex;
  align-items: center;
  gap: 9px;
  font-size: 13px;
  font-weight: 600;
  color: #fff;
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

/* 底部悬浮液态玻璃导航栏 */
.floating-dock-wrap {
  position: fixed;
  bottom: calc(var(--inset-bottom) + 12px);
  left: 0;
  right: 0;
  display: flex;
  justify-content: center;
  padding: 0 16px;
  z-index: 90;
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
  border-radius: 26px;
}

.dock-active-glider {
  position: absolute;
  top: 6px;
  bottom: 6px;
  left: 6px;
  width: calc((100% - 12px) / 4);
  border-radius: 20px;
  background: linear-gradient(
    135deg,
    rgba(139, 92, 246, 0.65) 0%,
    rgba(6, 182, 212, 0.55) 100%
  );
  border: 1px solid rgba(255, 255, 255, 0.32);
  box-shadow:
    inset 0 1px 1px rgba(255, 255, 255, 0.45),
    0 6px 18px -3px rgba(139, 92, 246, 0.55);
  transition: transform 0.38s var(--spring-easing);
  pointer-events: none;
}

.dock-item {
  position: relative;
  z-index: 1;
  background: transparent;
  border: none;
  color: var(--text-secondary);
  padding: 8px 4px;
  border-radius: 20px;
  display: flex;
  flex-direction: column;
  align-items: center;
  justify-content: center;
  gap: 3px;
  transition: color 0.25s ease, transform 0.25s var(--bounce-easing);
}

.dock-item.active {
  color: #ffffff;
}

.dock-item.active .dock-icon {
  transform: translateY(-1px) scale(1.06);
}

.dock-label {
  font-size: 11px;
  font-weight: 700;
}
</style>

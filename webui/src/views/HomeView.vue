<!--
SPDX-License-Identifier: GPL-3.0-only
Copyright (c) 2025-2026 jjh138792
MirageForge: https://github.com/jjh138792/MirageForge
-->

<script setup lang="ts">
import { computed, onMounted } from 'vue';
import {
  Smartphone,
  Cpu,
  HardDrive,
  ShieldCheck,
  Terminal,
  RefreshCw,
  Layers,
  Radar,
  ArrowUpCircle,
  ChevronRight,
  Zap,
  Lock,
} from 'lucide-vue-next';
import HeroBanner from '../components/HeroBanner.vue';
import { useI18n } from '../i18n';
import { triggerHaptic } from '../bridge/ksu-io';
import {
  mirageStore,
  hostDeviceState,
  fetchHostDeviceInfo,
} from '../store/mirageStore';

const emit = defineEmits<{
  (e: 'navigate', tab: 'templates' | 'apps' | 'settings'): void;
  (e: 'open-probe'): void;
  (e: 'check-update'): void;
}>();

const { t } = useI18n();

const activeAppCount = computed(() => Object.keys(mirageStore.appRoutes).length);
const templateCount = computed(() => Object.keys(mirageStore.templates).length);

const mountNsCount = computed(
  () => Object.values(mirageStore.appRoutes).filter((r) => r.mountNs).length
);
const cowPropCount = computed(
  () => Object.values(mirageStore.appRoutes).filter((r) => r.cowProp).length
);

const hostDisplayName = computed(() => {
  const info = hostDeviceState.info;
  if (info.marketName && info.marketName !== info.model) {
    return `${info.marketName} (${info.model})`;
  }
  return `${info.brand} ${info.model}`.trim();
});

const deviceRows = computed(() => {
  const info = hostDeviceState.info;
  return [
    {
      label: t('home.devModel'),
      value: hostDisplayName.value || '...',
      sub: info.device ? `codename: ${info.device}` : '',
    },
    {
      label: t('home.devSystem'),
      value: `Android ${info.androidRelease} (API ${info.sdkInt})`,
      sub: info.securityPatch ? `Patch ${info.securityPatch}` : '',
    },
    {
      label: t('home.devKernel'),
      value: info.kernelRelease || '...',
      sub: info.arch,
    },
    {
      label: t('home.devCpu'),
      value: info.cpuHardware || info.socModel || '...',
      sub: `${info.cpuCores} Cores · ${info.ramGb > 0 ? `${info.ramGb}GB RAM` : ''}`,
    },
    {
      label: t('home.devGpu'),
      value: info.gpuRenderer || '...',
      sub: '',
    },
    {
      label: t('home.devRoot'),
      value: info.rootManager || 'Unknown',
      sub: info.rootVersion,
      accent: 'emerald',
    },
    {
      label: t('home.devZygisk'),
      value: info.zygiskImpl || 'Not Detected',
      sub: info.zygiskVersion,
      accent: 'cyan',
    },
  ];
});

function refreshDevice() {
  triggerHaptic('light');
  fetchHostDeviceInfo(true);
}

onMounted(() => {
  fetchHostDeviceInfo(false);
});
</script>

<template>
  <section class="home-view">
    <!-- 1. 顶部大宣传栏 -->
    <HeroBanner @open-update="emit('check-update')" />

    <!-- 2. 模块运行信息卡片 -->
    <div class="info-card liquid-glass" v-touch-glass>
      <div class="card-head">
        <div class="head-title">
          <Layers :size="16" class="accent-icon" />
          <h2>{{ t('home.moduleCardTitle') }}</h2>
        </div>
        <span class="ver-pill">v{{ mirageStore.version }}</span>
      </div>

      <div class="stats-grid">
        <button
          class="stat-tile tactile-press"
          @click="emit('navigate', 'apps')"
        >
          <span class="stat-num">{{ activeAppCount }}</span>
          <span class="stat-lbl">{{ t('home.modSpoofedApps') }}</span>
        </button>

        <button
          class="stat-tile tactile-press"
          @click="emit('navigate', 'templates')"
        >
          <span class="stat-num">{{ templateCount }}</span>
          <span class="stat-lbl">{{ t('home.modTemplates') }}</span>
        </button>

        <div class="stat-tile">
          <span class="stat-num accent-cyan">{{ mountNsCount }}</span>
          <span class="stat-lbl">
            <Lock :size="10" />
            {{ t('home.modMountNsActive') }}
          </span>
        </div>

        <div class="stat-tile">
          <span class="stat-num accent-primary">{{ cowPropCount }}</span>
          <span class="stat-lbl">
            <Zap :size="10" />
            {{ t('home.modCowActive') }}
          </span>
        </div>
      </div>

      <div class="quick-links">
        <button class="quick-row tactile-press" @click="emit('open-probe')">
          <div class="quick-icon probe">
            <Radar :size="16" />
          </div>
          <div class="quick-text">
            <div class="quick-title">{{ t('home.quickProbeTitle') }}</div>
            <div class="quick-sub">{{ t('home.quickProbeSub') }}</div>
          </div>
          <ChevronRight :size="16" class="quick-arrow" />
        </button>

        <button class="quick-row tactile-press" @click="emit('check-update')">
          <div class="quick-icon ota">
            <ArrowUpCircle :size="16" />
          </div>
          <div class="quick-text">
            <div class="quick-title">{{ t('home.quickOtaTitle') }}</div>
            <div class="quick-sub">{{ t('home.quickOtaSub') }}</div>
          </div>
          <ChevronRight :size="16" class="quick-arrow" />
        </button>
      </div>
    </div>

    <!-- 3. 宿主设备硬件与 Root/Zygisk 环境信息卡片 -->
    <div class="info-card liquid-glass" v-touch-glass>
      <div class="card-head">
        <div class="head-title">
          <Smartphone :size="16" class="accent-icon" />
          <h2>{{ t('home.deviceCardTitle') }}</h2>
        </div>
        <button
          class="mini-refresh tactile-press"
          :disabled="hostDeviceState.loading"
          @click="refreshDevice"
        >
          <RefreshCw :size="12" :class="{ 'spin-anim': hostDeviceState.loading }" />
          <span>{{ t('home.refreshInfo') }}</span>
        </button>
      </div>

      <div class="kv-list">
        <div v-for="(row, idx) in deviceRows" :key="idx" class="kv-row">
          <div class="kv-label">{{ row.label }}</div>
          <div class="kv-val-wrap">
            <div
              class="kv-val"
              :class="{
                'text-emerald': row.accent === 'emerald',
                'text-cyan': row.accent === 'cyan',
              }"
            >
              {{ row.value }}
            </div>
            <div v-if="row.sub" class="kv-sub">{{ row.sub }}</div>
          </div>
        </div>
      </div>
    </div>
  </section>
</template>

<style scoped>
.home-view {
  display: flex;
  flex-direction: column;
  gap: 14px;
}

.info-card {
  padding: 16px;
  display: flex;
  flex-direction: column;
  gap: 13px;
}

.card-head {
  display: flex;
  justify-content: space-between;
  align-items: center;
  gap: 8px;
}

.head-title {
  display: flex;
  align-items: center;
  gap: 8px;
}

.head-title h2 {
  font-size: 15px;
  font-weight: 800;
}

.accent-icon {
  color: var(--accent-primary);
}

.ver-pill {
  font-size: 11px;
  font-weight: 800;
  padding: 2.5px 9px;
  border-radius: 9px;
  background: rgba(var(--accent-primary-rgb), 0.16);
  color: var(--accent-primary);
  border: 1px solid rgba(var(--accent-primary-rgb), 0.34);
}

.mini-refresh {
  display: inline-flex;
  align-items: center;
  gap: 5px;
  padding: 5px 10px;
  border-radius: 10px;
  font-size: 11px;
  font-weight: 700;
  background: var(--subcard-bg);
  color: var(--text-secondary);
  border: 1px solid var(--glass-border);
}

.stats-grid {
  display: grid;
  grid-template-columns: repeat(4, 1fr);
  gap: 8px;
}

@media (max-width: 520px) {
  .stats-grid {
    grid-template-columns: repeat(2, 1fr);
  }
}

.stat-tile {
  display: flex;
  flex-direction: column;
  align-items: center;
  justify-content: center;
  gap: 4px;
  padding: 12px 8px;
  border-radius: 16px;
  background: var(--subcard-bg);
  border: 1px solid var(--glass-border);
  color: var(--text-primary);
}

.stat-num {
  font-size: 21px;
  font-weight: 900;
  line-height: 1.1;
}

.accent-cyan {
  color: var(--accent-secondary);
}

.accent-primary {
  color: var(--accent-primary);
}

.stat-lbl {
  display: inline-flex;
  align-items: center;
  gap: 3px;
  font-size: 11px;
  font-weight: 600;
  color: var(--text-secondary);
}

.quick-links {
  display: flex;
  flex-direction: column;
  gap: 8px;
}

.quick-row {
  display: flex;
  align-items: center;
  gap: 11px;
  width: 100%;
  padding: 11px 12px;
  border-radius: 16px;
  background: var(--subcard-bg);
  border: 1px solid var(--glass-border);
  text-align: left;
  color: var(--text-primary);
}

.quick-icon {
  width: 34px;
  height: 34px;
  border-radius: 11px;
  display: flex;
  align-items: center;
  justify-content: center;
  flex-shrink: 0;
}

.quick-icon.probe {
  background: rgba(244, 63, 94, 0.16);
  color: #f43f5e;
  border: 1px solid rgba(244, 63, 94, 0.32);
}

.quick-icon.ota {
  background: rgba(var(--accent-secondary-rgb), 0.16);
  color: var(--accent-secondary);
  border: 1px solid rgba(var(--accent-secondary-rgb), 0.34);
}

.quick-text {
  flex: 1;
  min-width: 0;
}

.quick-title {
  font-size: 13px;
  font-weight: 700;
}

.quick-sub {
  font-size: 11px;
  color: var(--text-secondary);
  margin-top: 2px;
}

.quick-arrow {
  color: var(--text-muted);
  flex-shrink: 0;
}

.kv-list {
  display: flex;
  flex-direction: column;
  gap: 2px;
}

.kv-row {
  display: flex;
  justify-content: space-between;
  align-items: flex-start;
  gap: 14px;
  padding: 9px 10px;
  border-radius: 12px;
  background: var(--subcard-bg);
  border: 1px solid transparent;
}

.kv-row + .kv-row {
  margin-top: 5px;
}

.kv-label {
  font-size: 12px;
  font-weight: 700;
  color: var(--text-secondary);
  white-space: nowrap;
  padding-top: 1px;
}

.kv-val-wrap {
  text-align: right;
  min-width: 0;
}

.kv-val {
  font-size: 12.5px;
  font-weight: 700;
  color: var(--text-primary);
  word-break: break-word;
}

.kv-sub {
  font-size: 10.5px;
  color: var(--text-muted);
  margin-top: 2px;
  word-break: break-all;
}

.text-emerald {
  color: #10b981;
}

.text-cyan {
  color: var(--accent-secondary);
}

.spin-anim {
  animation: spin 1s linear infinite;
}

@keyframes spin {
  from { transform: rotate(0deg); }
  to { transform: rotate(360deg); }
}
</style>

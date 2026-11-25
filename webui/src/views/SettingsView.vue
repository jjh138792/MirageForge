<!--
SPDX-License-Identifier: GPL-3.0-only
Copyright (c) 2025-2026 jjh138792
MirageForge: https://github.com/jjh138792/MirageForge
-->

<script setup lang="ts">
import {
  Sun,
  Moon,
  Monitor,
  Palette,
  Sliders,
  PanelBottom,
  Globe,
  Radar,
  FileArchive,
  ArrowUpCircle,
  ExternalLink,
  ChevronRight,
  RefreshCw,
} from 'lucide-vue-next';
import { useI18n, type LocaleCode } from '../i18n';
import { triggerHaptic } from '../bridge/ksu-io';
import {
  mirageStore,
  settingsStore,
  applyThemeMode,
  setAccentColor,
  setDockStyle,
  setRefractionLevel,
  setEngineSetting,
  applyGlobalEngineFlagToAllRoutes,
  exportDiagnosticBundleViaCli,
  type AccentTheme,
  type ThemeMode,
  type DockStyle,
  type RefractionLevel,
} from '../store/mirageStore';

const emit = defineEmits<{
  (e: 'open-probe'): void;
  (e: 'check-update'): void;
}>();

const { locale, t, setLocale } = useI18n();

const accentSwatches: Array<{ key: AccentTheme; color: string }> = [
  { key: 'violet', color: '#8b5cf6' },
  { key: 'cyan', color: '#0ea5e9' },
  { key: 'emerald', color: '#10b981' },
  { key: 'rose', color: '#f43f5e' },
  { key: 'amber', color: '#f59e0b' },
];

const themeOptions: Array<{ key: ThemeMode; labelKey: string; icon: any }> = [
  { key: 'auto', labelKey: 'app.themeAuto', icon: Monitor },
  { key: 'light', labelKey: 'app.themeLight', icon: Sun },
  { key: 'dark', labelKey: 'app.themeDark', icon: Moon },
];

const dockOptions: Array<{ key: DockStyle; labelKey: string }> = [
  { key: 'floating', labelKey: 'settings.dockFloating' },
  { key: 'docked', labelKey: 'settings.dockDocked' },
];

const refractionOptions: Array<{ key: RefractionLevel; labelKey: string }> = [
  { key: 'standard', labelKey: 'settings.refractionStd' },
  { key: 'vivid', labelKey: 'settings.refractionVivid' },
  { key: 'off', labelKey: 'settings.refractionOff' },
];

const localeOptions: Array<{ key: LocaleCode; label: string }> = [
  { key: 'zh-CN', label: '简体中文' },
  { key: 'zh-TW', label: '繁體中文' },
  { key: 'en-US', label: 'English' },
];

function selectLocale(code: LocaleCode) {
  triggerHaptic('light');
  setLocale(code);
}

function openRepo() {
  triggerHaptic('light');
  window.open('https://github.com/jjh138792/MirageForge', '_blank');
}
</script>

<template>
  <section class="settings-view">
    <div class="page-large-header">
      <div>
        <h1 class="page-large-title">{{ t('nav.settings') }}</h1>
        <p class="page-large-sub">{{ t('settings.subtitle') }}</p>
      </div>
    </div>

    <!-- 1. 外观与液态玻璃设置 -->
    <div class="settings-group liquid-glass" v-touch-glass>
      <div class="group-title">
        <Palette :size="15" />
        <span>{{ t('settings.secAppearance') }}</span>
      </div>

      <!-- 主题模式 -->
      <div class="setting-block">
        <div class="setting-label">{{ t('settings.themeLabel') }}</div>
        <div class="segmented-row">
          <button
            v-for="opt in themeOptions"
            :key="opt.key"
            class="seg-btn tactile-press"
            :class="{ active: settingsStore.themeMode === opt.key }"
            @click="applyThemeMode(opt.key)"
          >
            <component :is="opt.icon" :size="14" />
            <span>{{ t(opt.labelKey) }}</span>
          </button>
        </div>
      </div>

      <!-- 主题强调色 -->
      <div class="setting-block">
        <div class="setting-label">{{ t('settings.accentLabel') }}</div>
        <div class="swatch-row">
          <button
            v-for="sw in accentSwatches"
            :key="sw.key"
            class="swatch-dot tactile-press"
            :class="{ active: settingsStore.accentColor === sw.key }"
            :style="{ background: sw.color }"
            @click="setAccentColor(sw.key)"
          ></button>
        </div>
      </div>

      <!-- 底部导航栏样式：悬浮栏 / 贴底栏 -->
      <div class="setting-block">
        <div class="setting-label">
          <PanelBottom :size="13" />
          <span>{{ t('settings.dockStyleLabel') }}</span>
        </div>
        <div class="segmented-row two-col">
          <button
            v-for="opt in dockOptions"
            :key="opt.key"
            class="seg-btn tactile-press"
            :class="{ active: settingsStore.dockStyle === opt.key }"
            @click="setDockStyle(opt.key)"
          >
            <span>{{ t(opt.labelKey) }}</span>
          </button>
        </div>
      </div>

      <!-- 液态玻璃折射质感 -->
      <div class="setting-block">
        <div class="setting-label">{{ t('settings.refractionLabel') }}</div>
        <div class="segmented-row">
          <button
            v-for="opt in refractionOptions"
            :key="opt.key"
            class="seg-btn tactile-press"
            :class="{ active: settingsStore.refractionLevel === opt.key }"
            @click="setRefractionLevel(opt.key)"
          >
            <span>{{ t(opt.labelKey) }}</span>
          </button>
        </div>
      </div>

      <!-- 显示语言 -->
      <div class="setting-block">
        <div class="setting-label">
          <Globe :size="13" />
          <span>{{ t('settings.languageLabel') }}</span>
        </div>
        <div class="segmented-row">
          <button
            v-for="opt in localeOptions"
            :key="opt.key"
            class="seg-btn tactile-press"
            :class="{ active: locale === opt.key }"
            @click="selectLocale(opt.key)"
          >
            <span>{{ opt.label }}</span>
          </button>
        </div>
      </div>
    </div>

    <!-- 2. 底层引擎策略设置（含 MountNS 与 COW Prop 总控） -->
    <div class="settings-group liquid-glass" v-touch-glass>
      <div class="group-title">
        <Sliders :size="15" />
        <span>{{ t('settings.secEngine') }}</span>
      </div>

      <!-- CPU/GPU 文件节点是否使用挂载命名空间 -->
      <div class="switch-card">
        <div
          class="switch-row tactile-press"
          @click="setEngineSetting('defaultMountNs', !settingsStore.defaultMountNs)"
        >
          <div class="mf-switch" :class="{ on: settingsStore.defaultMountNs }">
            <div class="mf-switch-thumb"></div>
          </div>
          <div class="switch-copy">
            <div class="switch-title">{{ t('settings.mountNsTitle') }}</div>
            <div class="switch-desc">{{ t('settings.mountNsDesc') }}</div>
          </div>
        </div>
        <div class="switch-extra">
          <button
            class="sync-all-btn tactile-press"
            @click="applyGlobalEngineFlagToAllRoutes('mountNs', settingsStore.defaultMountNs)"
          >
            <RefreshCw :size="12" />
            <span>{{ t('settings.syncAllBtn') }}</span>
          </button>
        </div>
      </div>

      <!-- COW Prop Spoof 默认策略 -->
      <div class="switch-card">
        <div
          class="switch-row tactile-press"
          @click="setEngineSetting('defaultCowProp', !settingsStore.defaultCowProp)"
        >
          <div class="mf-switch" :class="{ on: settingsStore.defaultCowProp }">
            <div class="mf-switch-thumb"></div>
          </div>
          <div class="switch-copy">
            <div class="switch-title">{{ t('settings.cowPropTitle') }}</div>
            <div class="switch-desc">{{ t('settings.cowPropDesc') }}</div>
          </div>
        </div>
        <div class="switch-extra">
          <button
            class="sync-all-btn tactile-press"
            @click="applyGlobalEngineFlagToAllRoutes('cowProp', settingsStore.defaultCowProp)"
          >
            <RefreshCw :size="12" />
            <span>{{ t('settings.syncAllBtn') }}</span>
          </button>
        </div>
      </div>

      <!-- 保存配置自动重启目标应用 -->
      <div class="switch-card">
        <div
          class="switch-row tactile-press"
          @click="setEngineSetting('autoRestartOnSave', !settingsStore.autoRestartOnSave)"
        >
          <div class="mf-switch" :class="{ on: settingsStore.autoRestartOnSave }">
            <div class="mf-switch-thumb"></div>
          </div>
          <div class="switch-copy">
            <div class="switch-title">{{ t('settings.autoRestartTitle') }}</div>
            <div class="switch-desc">{{ t('settings.autoRestartDesc') }}</div>
          </div>
        </div>
      </div>
    </div>

    <!-- 3. 诊断工具、OTA 与开源关于 -->
    <div class="settings-group liquid-glass" v-touch-glass>
      <div class="group-title">
        <Radar :size="15" />
        <span>{{ t('settings.secTools') }}</span>
      </div>

      <button class="action-row tactile-press" @click="emit('open-probe')">
        <div class="row-ico probe">
          <Radar :size="16" />
        </div>
        <div class="row-copy">
          <div class="row-title">{{ t('settings.probeEntryTitle') }}</div>
          <div class="row-desc">{{ t('settings.probeEntryDesc') }}</div>
        </div>
        <ChevronRight :size="16" class="row-arrow" />
      </button>

      <button class="action-row tactile-press" @click="exportDiagnosticBundleViaCli()">
        <div class="row-ico bundle">
          <FileArchive :size="16" />
        </div>
        <div class="row-copy">
          <div class="row-title">{{ t('settings.diagBundleTitle') }}</div>
          <div class="row-desc">{{ t('settings.diagBundleDesc') }}</div>
        </div>
        <ChevronRight :size="16" class="row-arrow" />
      </button>

      <button class="action-row tactile-press" @click="emit('check-update')">
        <div class="row-ico ota">
          <ArrowUpCircle :size="16" />
        </div>
        <div class="row-copy">
          <div class="row-title">
            {{ t('settings.otaEntryTitle') }}
            <span v-if="mirageStore.updateInfo.hasUpdate" class="new-dot">
              v{{ mirageStore.updateInfo.latestVersion }}
            </span>
          </div>
          <div class="row-desc">
            {{ t('settings.otaEntryDesc', { ver: mirageStore.version, code: mirageStore.versionCode }) }}
          </div>
        </div>
        <ChevronRight :size="16" class="row-arrow" />
      </button>

      <button class="action-row tactile-press" @click="openRepo">
        <div class="row-ico repo">
          <ExternalLink :size="16" />
        </div>
        <div class="row-copy">
          <div class="row-title">{{ t('settings.repoEntryTitle') }}</div>
          <div class="row-desc">{{ t('settings.repoEntryDesc') }}</div>
        </div>
        <ChevronRight :size="16" class="row-arrow" />
      </button>
    </div>
  </section>
</template>

<style scoped>
.settings-view {
  display: flex;
  flex-direction: column;
  gap: 14px;
}

.settings-group {
  padding: 16px;
  display: flex;
  flex-direction: column;
  gap: 13px;
}

.group-title {
  display: flex;
  align-items: center;
  gap: 7px;
  font-size: 13.5px;
  font-weight: 800;
  color: var(--accent-secondary);
}

.setting-block {
  display: flex;
  flex-direction: column;
  gap: 8px;
}

.setting-label {
  display: flex;
  align-items: center;
  gap: 6px;
  font-size: 12px;
  font-weight: 700;
  color: var(--text-secondary);
}

.segmented-row {
  display: grid;
  grid-template-columns: repeat(3, 1fr);
  gap: 7px;
  padding: 4px;
  border-radius: 14px;
  background: var(--subcard-bg);
  border: 1px solid var(--glass-border);
}

.segmented-row.two-col {
  grid-template-columns: repeat(2, 1fr);
}

.seg-btn {
  display: inline-flex;
  align-items: center;
  justify-content: center;
  gap: 5px;
  padding: 8px 6px;
  border-radius: 10px;
  font-size: 12px;
  font-weight: 700;
  color: var(--text-secondary);
  background: transparent;
  border: 1px solid transparent;
}

.seg-btn.active {
  color: #ffffff;
  background: linear-gradient(135deg, var(--accent-primary), var(--accent-secondary));
  border-color: rgba(255, 255, 255, 0.28);
  box-shadow: 0 4px 12px -2px rgba(var(--accent-primary-rgb), 0.45);
}

.swatch-row {
  display: flex;
  align-items: center;
  gap: 12px;
  padding: 4px 2px;
}

.swatch-dot {
  width: 30px;
  height: 30px;
  border-radius: 50%;
  border: 2px solid transparent;
  box-shadow: 0 4px 12px rgba(0, 0, 0, 0.28);
}

.swatch-dot.active {
  border-color: var(--text-primary);
  transform: scale(1.14);
}

.switch-card {
  border-radius: 16px;
  background: var(--subcard-bg);
  border: 1px solid var(--glass-border);
  padding: 12px;
  display: flex;
  flex-direction: column;
  gap: 10px;
}

.switch-row {
  display: flex;
  align-items: center;
  gap: 12px;
}

.switch-copy {
  flex: 1;
  min-width: 0;
}

.switch-title {
  font-size: 13px;
  font-weight: 700;
  color: var(--text-primary);
}

.switch-desc {
  font-size: 11px;
  color: var(--text-secondary);
  margin-top: 3px;
  line-height: 1.45;
}

.switch-extra {
  display: flex;
  justify-content: flex-end;
  padding-top: 8px;
  border-top: 1px dashed var(--glass-border);
}

.sync-all-btn {
  display: inline-flex;
  align-items: center;
  gap: 5px;
  padding: 5px 10px;
  border-radius: 9px;
  font-size: 11px;
  font-weight: 700;
  background: rgba(var(--accent-primary-rgb), 0.14);
  color: var(--accent-primary);
  border: 1px solid rgba(var(--accent-primary-rgb), 0.34);
}

.action-row {
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

.row-ico {
  width: 34px;
  height: 34px;
  border-radius: 11px;
  display: flex;
  align-items: center;
  justify-content: center;
  flex-shrink: 0;
}

.row-ico.probe {
  background: rgba(244, 63, 94, 0.16);
  color: #f43f5e;
  border: 1px solid rgba(244, 63, 94, 0.32);
}

.row-ico.bundle {
  background: rgba(var(--accent-primary-rgb), 0.16);
  color: var(--accent-primary);
  border: 1px solid rgba(var(--accent-primary-rgb), 0.34);
}

.row-ico.ota {
  background: rgba(var(--accent-secondary-rgb), 0.16);
  color: var(--accent-secondary);
  border: 1px solid rgba(var(--accent-secondary-rgb), 0.34);
}

.row-ico.repo {
  background: rgba(16, 185, 129, 0.16);
  color: #10b981;
  border: 1px solid rgba(16, 185, 129, 0.34);
}

.row-copy {
  flex: 1;
  min-width: 0;
}

.row-title {
  display: flex;
  align-items: center;
  gap: 7px;
  font-size: 13px;
  font-weight: 700;
}

.row-desc {
  font-size: 11px;
  color: var(--text-secondary);
  margin-top: 2px;
}

.new-dot {
  font-size: 10px;
  font-weight: 800;
  padding: 1px 6px;
  border-radius: 6px;
  background: rgba(245, 158, 11, 0.2);
  color: #f59e0b;
  border: 1px solid rgba(245, 158, 11, 0.45);
}

.row-arrow {
  color: var(--text-muted);
  flex-shrink: 0;
}
</style>

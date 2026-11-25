<!--
SPDX-License-Identifier: GPL-3.0-only
Copyright (c) 2025-2026 jjh138792
MirageForge: https://github.com/jjh138792/MirageForge
-->

<script setup lang="ts">
import { ref, computed } from 'vue';
import {
  Search,
  RefreshCw,
  ChevronRight,
  Cpu,
  ShieldCheck,
  Sparkles,
} from 'lucide-vue-next';
import AppDetailEditor from '../components/AppDetailEditor.vue';
import { useI18n } from '../i18n';
import { triggerHaptic, type InstalledAppInfo } from '../bridge/ksu-io';
import { mirageStore, reloadAppList } from '../store/mirageStore';

const { t } = useI18n();
const searchQuery = ref('');
const filterMode = ref<'all' | 'configured'>('all');
const selectedApp = ref<InstalledAppInfo | null>(null);

const activeCount = computed(() => Object.keys(mirageStore.appRoutes).length);

const filteredApps = computed(() => {
  const q = searchQuery.value.trim().toLowerCase();
  return mirageStore.installedApps.filter((app) => {
    const route = mirageStore.appRoutes[app.packageName];
    if (filterMode.value === 'configured' && !route) return false;
    if (!q) return true;
    const tplName = route ? mirageStore.templates[route.tplId]?.name || '' : '';
    return (
      app.packageName.toLowerCase().includes(q) ||
      app.appLabel.toLowerCase().includes(q) ||
      tplName.toLowerCase().includes(q)
    );
  });
});

function getSpoofedTemplateName(pkg: string): string {
  const r = mirageStore.appRoutes[pkg];
  if (!r) return '';
  return mirageStore.templates[r.tplId]?.name || r.tplId;
}

function getRouteSummaryBadges(pkg: string): string[] {
  const r = mirageStore.appRoutes[pkg];
  if (!r) return [];
  const tags: string[] = [];
  if (r.build) tags.push('属性');
  if (r.cowProp) tags.push('COW');
  if (r.cpu) tags.push('CPU');
  if (r.gl) tags.push('GPU');
  if (r.mem) tags.push('内存');
  if (r.vfsNodes) tags.push(r.mountNs ? 'MountNS' : '节点');
  if (r.refreshRate >= 90) tags.push(`${r.refreshRate}Hz`);
  if (r.probe) tags.push('Trace');
  return tags;
}

function openAppEditor(app: InstalledAppInfo) {
  triggerHaptic('light');
  selectedApp.value = app;
}

async function toggleSystemApps() {
  triggerHaptic('light');
  mirageStore.includeSystemApps = !mirageStore.includeSystemApps;
  await reloadAppList();
}
</script>

<template>
  <section class="apps-view">
    <!-- 1. 顶部大标题：应用 -->
    <div class="page-large-header">
      <div>
        <h1 class="page-large-title">{{ t('nav.apps') }}</h1>
      </div>
      <span class="active-badge">
        <ShieldCheck :size="13" />
        <span>{{ t('app.activeBadge', { count: activeCount }) }}</span>
      </span>
    </div>

    <!-- 2. 搜索与过滤控制卡片 -->
    <div class="filter-panel liquid-glass" v-touch-glass>
      <div class="search-box">
        <Search :size="16" class="search-icon" />
        <input
          v-model="searchQuery"
          type="text"
          class="liquid-input search-input"
          :placeholder="t('apps.searchPlaceholder')"
        />
      </div>

      <div class="chip-row">
        <button
          class="filter-chip tactile-press"
          :class="{ active: filterMode === 'all' }"
          @click="filterMode = 'all'"
        >
          {{ t('apps.filterAll') }}
        </button>
        <button
          class="filter-chip tactile-press"
          :class="{ active: filterMode === 'configured' }"
          @click="filterMode = 'configured'"
        >
          {{ t('apps.filterConfigured') }} ({{ activeCount }})
        </button>
        <button
          class="filter-chip tactile-press"
          :class="{ active: mirageStore.includeSystemApps }"
          @click="toggleSystemApps"
        >
          {{ t('apps.sysApps') }}: {{ mirageStore.includeSystemApps ? 'ON' : 'OFF' }}
        </button>
        <button class="filter-chip tactile-press" @click="reloadAppList()">
          <RefreshCw :size="12" :class="{ spinning: mirageStore.loading }" />
          <span>{{ t('apps.refreshBtn') }}</span>
        </button>
      </div>
    </div>

    <!-- 3. 整齐应用长条列表（显示图标 + 下方显示所伪装的模板） -->
    <div class="app-list">
      <button
        v-for="app in filteredApps"
        :key="app.packageName"
        class="app-strip liquid-glass tactile-press"
        :class="{ spoofed: Boolean(mirageStore.appRoutes[app.packageName]) }"
        v-touch-glass
        @click="openAppEditor(app)"
      >
        <!-- 应用图标（附首字母兜底壳） -->
        <div class="icon-shell">
          <span class="icon-fallback">{{ (app.appLabel || app.packageName).slice(0, 1).toUpperCase() }}</span>
          <img
            :src="app.iconUrl"
            class="app-icon-img"
            loading="lazy"
            onerror="this.style.display='none'"
            alt=""
          />
        </div>

        <div class="strip-body">
          <div class="strip-top">
            <span class="app-name">{{ app.appLabel }}</span>
            <span v-if="app.versionName" class="app-ver">v{{ app.versionName }}</span>
          </div>

          <div class="app-pkg">{{ app.packageName }}</div>

          <!-- 下方清晰展示伪装模板名称与模块摘要 -->
          <div class="strip-template-line">
            <template v-if="mirageStore.appRoutes[app.packageName]">
              <span class="tpl-badge">
                <Sparkles :size="11" />
                <span class="tpl-badge-text">{{ getSpoofedTemplateName(app.packageName) }}</span>
              </span>
              <div class="mini-tags">
                <span
                  v-for="tag in getRouteSummaryBadges(app.packageName)"
                  :key="tag"
                  class="mini-tag"
                >
                  {{ tag }}
                </span>
              </div>
            </template>
            <span v-else class="native-label">
              <Cpu :size="11" />
              <span>{{ t('apps.nativeMode') }}</span>
            </span>
          </div>
        </div>

        <ChevronRight :size="17" class="strip-arrow" />
      </button>

      <div v-if="!mirageStore.loading && filteredApps.length === 0" class="empty-box liquid-glass">
        未找到匹配的应用，可尝试清空搜索词或开启「系统应用」。
      </div>
    </div>

    <!-- 4. 独立应用配置全屏页 -->
    <AppDetailEditor
      :app="selectedApp"
      @close="selectedApp = null"
    />
  </section>
</template>

<style scoped>
.apps-view {
  display: flex;
  flex-direction: column;
  gap: 12px;
}

.active-badge {
  display: inline-flex;
  align-items: center;
  gap: 5px;
  padding: 5.5px 11px;
  border-radius: 12px;
  font-size: 11.5px;
  font-weight: 700;
  background: rgba(16, 185, 129, 0.16);
  color: #10b981;
  border: 1px solid rgba(16, 185, 129, 0.35);
}

.filter-panel {
  padding: 13px 14px;
}

.search-box {
  position: relative;
}

.search-icon {
  position: absolute;
  left: 13px;
  top: 50%;
  transform: translateY(-50%);
  color: var(--text-secondary);
}

.search-input {
  padding-left: 36px;
}

.chip-row {
  display: flex;
  flex-wrap: wrap;
  gap: 7px;
  margin-top: 10px;
}

.filter-chip {
  display: inline-flex;
  align-items: center;
  gap: 5px;
  padding: 5.5px 12px;
  border-radius: 16px;
  font-size: 11.5px;
  font-weight: 700;
  background: var(--subcard-bg);
  border: 1px solid var(--glass-border);
  color: var(--text-secondary);
}

.filter-chip.active {
  background: rgba(var(--accent-primary-rgb), 0.22);
  color: var(--text-primary);
  border-color: var(--accent-primary);
}

.app-list {
  display: flex;
  flex-direction: column;
  gap: 9px;
}

.app-strip {
  width: 100%;
  padding: 12px 13px;
  border-radius: 20px;
  display: flex;
  align-items: center;
  gap: 12px;
  text-align: left;
  color: var(--text-primary);
}

.app-strip.spoofed {
  border-color: rgba(var(--accent-primary-rgb), 0.4);
}

.icon-shell {
  position: relative;
  width: 44px;
  height: 44px;
  border-radius: 13px;
  overflow: hidden;
  flex-shrink: 0;
  background: linear-gradient(
    135deg,
    rgba(var(--accent-primary-rgb), 0.24),
    rgba(var(--accent-secondary-rgb), 0.22)
  );
  border: 1px solid var(--glass-border);
  display: flex;
  align-items: center;
  justify-content: center;
}

.icon-fallback {
  font-size: 16px;
  font-weight: 800;
  color: var(--accent-primary);
}

.app-icon-img {
  position: absolute;
  inset: 0;
  width: 100%;
  height: 100%;
  object-fit: cover;
}

.strip-body {
  flex: 1;
  min-width: 0;
  display: flex;
  flex-direction: column;
  gap: 2.5px;
}

.strip-top {
  display: flex;
  align-items: baseline;
  gap: 6px;
}

.app-name {
  font-size: 14px;
  font-weight: 800;
  white-space: nowrap;
  overflow: hidden;
  text-overflow: ellipsis;
}

.app-ver {
  font-size: 10.5px;
  color: var(--text-muted);
  flex-shrink: 0;
}

.app-pkg {
  font-size: 11px;
  color: var(--text-secondary);
  white-space: nowrap;
  overflow: hidden;
  text-overflow: ellipsis;
}

.strip-template-line {
  display: flex;
  flex-direction: column;
  gap: 5px;
  margin-top: 4px;
}

.tpl-badge {
  display: inline-flex;
  align-items: center;
  gap: 5px;
  align-self: flex-start;
  max-width: 100%;
  padding: 3px 8px;
  border-radius: 8px;
  font-size: 11px;
  font-weight: 700;
  color: var(--accent-secondary);
  background: rgba(var(--accent-secondary-rgb), 0.14);
  border: 1px solid rgba(var(--accent-secondary-rgb), 0.34);
}

.tpl-badge-text {
  white-space: nowrap;
  overflow: hidden;
  text-overflow: ellipsis;
}

.mini-tags {
  display: flex;
  flex-wrap: wrap;
  gap: 4px;
}

.mini-tag {
  font-size: 9.5px;
  font-weight: 700;
  padding: 1px 6px;
  border-radius: 6px;
  background: rgba(var(--accent-primary-rgb), 0.14);
  color: var(--accent-primary);
}

.native-label {
  display: inline-flex;
  align-items: center;
  gap: 4px;
  font-size: 11px;
  font-weight: 600;
  color: var(--text-muted);
}

.strip-arrow {
  color: var(--text-muted);
  flex-shrink: 0;
}

.empty-box {
  padding: 26px 16px;
  text-align: center;
  font-size: 12.5px;
  color: var(--text-secondary);
}

.spinning {
  animation: spin 1s linear infinite;
}

@keyframes spin {
  from { transform: rotate(0deg); }
  to { transform: rotate(360deg); }
}
</style>

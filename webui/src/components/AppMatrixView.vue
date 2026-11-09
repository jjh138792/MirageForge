<script setup lang="ts">
import { ref, computed } from 'vue';
import { Search, RefreshCw, Zap, ShieldAlert } from 'lucide-vue-next';
import { useI18n } from '../i18n';
import { triggerHaptic } from '../bridge/webui-x';
import {
  mirageStore,
  reloadAppList,
  updateAppRouteViaCli,
  forceStopApp,
  type AppRoute,
} from '../store/mirageStore';

const { t } = useI18n();
const searchQuery = ref('');
const filterMode = ref<'all' | 'configured'>('all');

const filteredApps = computed(() => {
  const q = searchQuery.value.trim().toLowerCase();
  return mirageStore.installedApps.filter((app) => {
    const isConf = Boolean(mirageStore.appRoutes[app.packageName]);
    if (filterMode.value === 'configured' && !isConf) return false;
    if (!q) return true;
    return (
      app.packageName.toLowerCase().includes(q) ||
      app.appLabel.toLowerCase().includes(q)
    );
  });
});

async function handleTemplateChange(pkg: string, tplId: string) {
  triggerHaptic('medium');
  if (!tplId) {
    await updateAppRouteViaCli(pkg, null, false);
    return;
  }
  const existing = mirageStore.appRoutes[pkg];
  const nextRoute: AppRoute = {
    tplId,
    build: existing ? existing.build : true,
    gl: existing ? existing.gl : true,
    vfs: existing ? existing.vfs : true,
    probe: existing ? existing.probe : false,
    sdkSpoof: existing ? existing.sdkSpoof : false,
  };
  await updateAppRouteViaCli(pkg, nextRoute, false);
}

async function toggleModuleSwitch(pkg: string, field: keyof Omit<AppRoute, 'tplId'>) {
  triggerHaptic('light');
  const cur = mirageStore.appRoutes[pkg];
  if (!cur) return;
  const next: AppRoute = {
    ...cur,
    [field]: !cur[field],
  };
  await updateAppRouteViaCli(pkg, next, false);
}

async function toggleSystemApps() {
  triggerHaptic('light');
  mirageStore.includeSystemApps = !mirageStore.includeSystemApps;
  await reloadAppList();
}
</script>

<template>
  <section class="matrix-section">
    <!-- 顶部搜索与过滤液态栏 -->
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
          {{ t('apps.filterConfigured') }}
        </button>
        <button
          class="filter-chip tactile-press"
          :class="{ active: mirageStore.includeSystemApps }"
          @click="toggleSystemApps"
        >
          {{ t('apps.sysApps') }}: {{ mirageStore.includeSystemApps ? 'ON' : 'OFF' }}
        </button>
        <button class="filter-chip tactile-press" @click="reloadAppList()">
          <RefreshCw :size="13" />
          <span>{{ t('apps.refreshBtn') }}</span>
        </button>
      </div>
    </div>

    <!-- 应用矩阵列表 -->
    <div class="app-matrix-list">
      <div
        v-for="app in filteredApps"
        :key="app.packageName"
        class="app-card liquid-glass tactile-card"
        v-touch-glass
      >
        <div class="app-row-main">
          <div class="app-meta">
            <img
              :src="app.iconUrl"
              class="app-icon-img"
              onerror="this.style.display='none'"
              alt=""
            />
            <div class="app-titles">
              <div class="app-label-text">{{ app.appLabel }}</div>
              <div class="app-pkg-text">
                {{ app.packageName }}
                <span v-if="app.versionName">· v{{ app.versionName }}</span>
              </div>
            </div>
          </div>

          <div class="app-actions">
            <select
              class="liquid-select route-select"
              :value="mirageStore.appRoutes[app.packageName]?.tplId || ''"
              @change="(e) => handleTemplateChange(app.packageName, (e.target as HTMLSelectElement).value)"
            >
              <option value="">{{ t('apps.nativeMode') }}</option>
              <option
                v-for="tpl in Object.values(mirageStore.templates)"
                :key="tpl.id"
                :value="tpl.id"
              >
                {{ tpl.name }}
              </option>
            </select>

            <button
              class="btn-liquid-ghost tactile-press"
              :title="t('apps.restartBtn')"
              @click="forceStopApp(app.packageName, app.appLabel)"
            >
              <Zap :size="14" />
            </button>
          </div>
        </div>

        <!-- 五维微模块按需独立开关 (关闭的模块在底层 0 改动、0 Hook) -->
        <div v-if="mirageStore.appRoutes[app.packageName]" class="module-pills">
          <span
            class="mod-pill tactile-press"
            :class="{ on: mirageStore.appRoutes[app.packageName].build }"
            @click="toggleModuleSwitch(app.packageName, 'build')"
          >
            🧬 {{ t('apps.swBuild') }}
          </span>
          <span
            class="mod-pill tactile-press"
            :class="{ on: mirageStore.appRoutes[app.packageName].gl }"
            @click="toggleModuleSwitch(app.packageName, 'gl')"
          >
            🎮 {{ t('apps.swGl') }}
          </span>
          <span
            class="mod-pill tactile-press"
            :class="{ on: mirageStore.appRoutes[app.packageName].vfs }"
            @click="toggleModuleSwitch(app.packageName, 'vfs')"
          >
            🛡️ {{ t('apps.swVfs') }}
          </span>
          <span
            class="mod-pill probe tactile-press"
            :class="{ on: mirageStore.appRoutes[app.packageName].probe }"
            @click="toggleModuleSwitch(app.packageName, 'probe')"
          >
            📡 {{ t('apps.swProbe') }}
          </span>
          <span
            class="mod-pill sdk tactile-press"
            :class="{ on: mirageStore.appRoutes[app.packageName].sdkSpoof }"
            @click="toggleModuleSwitch(app.packageName, 'sdkSpoof')"
          >
            <ShieldAlert :size="12" />
            <span>{{ t('apps.swSdk') }}</span>
          </span>
        </div>
      </div>
    </div>
  </section>
</template>

<style scoped>
.filter-panel {
  padding: 15px;
  margin-bottom: 14px;
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
  gap: 8px;
  margin-top: 10px;
}

.filter-chip {
  display: inline-flex;
  align-items: center;
  gap: 5px;
  padding: 6px 13px;
  border-radius: 18px;
  font-size: 12px;
  font-weight: 600;
  background: rgba(255, 255, 255, 0.05);
  border: 1px solid rgba(255, 255, 255, 0.1);
  color: var(--text-secondary);
}

.filter-chip.active {
  background: rgba(139, 92, 246, 0.26);
  color: #fff;
  border-color: rgba(139, 92, 246, 0.55);
}

.app-matrix-list {
  display: flex;
  flex-direction: column;
  gap: 10px;
}

.app-card {
  padding: 13px 15px;
  display: flex;
  flex-direction: column;
  gap: 10px;
}

.app-row-main {
  display: flex;
  justify-content: space-between;
  align-items: center;
  gap: 10px;
}

.app-meta {
  display: flex;
  align-items: center;
  gap: 11px;
  min-width: 0;
}

.app-icon-img {
  width: 40px;
  height: 40px;
  border-radius: 11px;
  object-fit: cover;
  flex-shrink: 0;
}

.app-titles {
  min-width: 0;
}

.app-label-text {
  font-size: 14px;
  font-weight: 700;
  color: #fff;
  white-space: nowrap;
  overflow: hidden;
  text-overflow: ellipsis;
}

.app-pkg-text {
  font-size: 11px;
  color: var(--text-secondary);
  word-break: break-all;
}

.app-actions {
  display: flex;
  align-items: center;
  gap: 7px;
  flex-shrink: 0;
}

.route-select {
  width: 185px;
  padding: 8px 10px;
  font-size: 12px;
}

.module-pills {
  display: flex;
  flex-wrap: wrap;
  gap: 6px;
  padding-top: 8px;
  border-top: 1px dashed rgba(255, 255, 255, 0.1);
}

.mod-pill {
  display: inline-flex;
  align-items: center;
  gap: 4px;
  font-size: 11px;
  font-weight: 600;
  padding: 4px 10px;
  border-radius: 9px;
  background: rgba(255, 255, 255, 0.04);
  color: var(--text-muted);
  border: 1px solid rgba(255, 255, 255, 0.08);
}

.mod-pill.on {
  background: rgba(6, 182, 212, 0.16);
  color: #67e8f9;
  border-color: rgba(6, 182, 212, 0.42);
}

.mod-pill.probe.on {
  background: rgba(244, 63, 94, 0.18);
  color: #fda4af;
  border-color: rgba(244, 63, 94, 0.45);
}

.mod-pill.sdk.on {
  background: rgba(245, 158, 11, 0.18);
  color: #fcd34d;
  border-color: rgba(245, 158, 11, 0.45);
}
</style>

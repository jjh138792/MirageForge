<!--
SPDX-License-Identifier: GPL-3.0-only
Copyright (c) 2025-2026 jjh138792
MirageForge: https://github.com/jjh138792/MirageForge
-->

<script setup lang="ts">
import { computed } from 'vue';
import { X, Zap, Trash2, Layers, ShieldCheck, ExternalLink } from 'lucide-vue-next';
import { triggerHaptic, showToast } from '../bridge/ksu-io';
import {
  mirageStore,
  updateAppRouteViaCli,
  forceStopApp,
  type DeviceTemplate,
} from '../store/mirageStore';

const props = defineProps<{
  template: DeviceTemplate | null;
}>();

const emit = defineEmits<{
  (e: 'close'): void;
  (e: 'jump-to-apps'): void;
}>();

const boundApps = computed(() => {
  if (!props.template) return [];
  const tplId = props.template.id;
  const appInfoMap = new Map(
    mirageStore.installedApps.map((a) => [a.packageName, a])
  );

  return Object.entries(mirageStore.appRoutes)
    .filter(([, route]) => route.tplId === tplId)
    .map(([pkg, route]) => {
      const info = appInfoMap.get(pkg);
      const flags: string[] = [];
      if (route.build) flags.push('系统属性');
      if (route.cowProp) flags.push('COW');
      if (route.cpu) flags.push('CPU');
      if (route.gl) flags.push('GPU');
      if (route.mem) flags.push('内存');
      if (route.vfsNodes) flags.push(route.mountNs ? '节点·MountNS' : '节点');
      return {
        pkg,
        label: info?.appLabel || pkg,
        iconUrl: info?.iconUrl || `ksu://icon/${pkg}`,
        flags,
      };
    });
});

async function unbindApp(pkg: string, label: string) {
  triggerHaptic('medium');
  await updateAppRouteViaCli(pkg, null, false);
  showToast(`已恢复 ${label} 为原生环境 (0 改动)`);
}
</script>

<template>
  <Transition name="view-morph">
    <div v-if="template" class="sheet-overlay" @click.self="emit('close')">
      <div class="sheet-box liquid-glass liquid-glass-elevated">
        <div class="sheet-head">
          <div class="head-copy">
            <h3>生效应用 · {{ template.name }}</h3>
            <span class="head-sub">共 {{ boundApps.length }} 款应用正在使用此硬件模板</span>
          </div>
          <button class="icon-close tactile-press" @click="emit('close')">
            <X :size="18" />
          </button>
        </div>

        <div class="sheet-body">
          <div v-if="boundApps.length === 0" class="empty-state">
            <Layers :size="28" class="empty-ico" />
            <p>当前还没有应用绑定此模板</p>
            <button
              class="btn-liquid tactile-press"
              @click="emit('close'); emit('jump-to-apps')"
            >
              <ExternalLink :size="14" />
              <span>前往「应用」页绑定</span>
            </button>
          </div>

          <div v-else class="bound-list">
            <div v-for="item in boundApps" :key="item.pkg" class="bound-row">
              <img
                :src="item.iconUrl"
                class="app-ico"
                onerror="this.style.display='none'"
                alt=""
              />
              <div class="app-meta">
                <div class="app-name">{{ item.label }}</div>
                <div class="app-pkg">{{ item.pkg }}</div>
                <div class="flag-chips">
                  <span v-for="f in item.flags" :key="f" class="flag-chip">{{ f }}</span>
                </div>
              </div>
              <div class="row-actions">
                <button
                  class="mini-btn tactile-press"
                  title="重启应用"
                  @click="forceStopApp(item.pkg, item.label)"
                >
                  <Zap :size="14" />
                </button>
                <button
                  class="mini-btn danger tactile-press"
                  title="移出伪装 (恢复原生)"
                  @click="unbindApp(item.pkg, item.label)"
                >
                  <Trash2 :size="14" />
                </button>
              </div>
            </div>
          </div>
        </div>

        <div class="sheet-foot">
          <span class="foot-hint">
            <ShieldCheck :size="13" />
            未绑定的应用始终 0 Hook 并自动卸载
          </span>
          <button class="btn-liquid-ghost tactile-press" @click="emit('close')">关闭</button>
        </div>
      </div>
    </div>
  </Transition>
</template>

<style scoped>
.sheet-overlay {
  position: fixed;
  inset: 0;
  background: rgba(3, 5, 12, 0.68);
  backdrop-filter: blur(12px);
  display: flex;
  align-items: center;
  justify-content: center;
  padding: 16px;
  z-index: 210;
}

.sheet-box {
  width: 100%;
  max-width: 520px;
  max-height: 80vh;
  border-radius: 28px;
  padding: 18px;
  display: flex;
  flex-direction: column;
  gap: 12px;
}

.sheet-head {
  display: flex;
  justify-content: space-between;
  align-items: flex-start;
  gap: 10px;
  padding-bottom: 12px;
  border-bottom: 1px solid var(--glass-border);
}

.head-copy h3 {
  font-size: 15.5px;
  font-weight: 800;
}

.head-sub {
  font-size: 11.5px;
  color: var(--text-secondary);
}

.icon-close {
  background: var(--subcard-bg);
  border: 1px solid var(--glass-border);
  color: var(--text-primary);
  width: 32px;
  height: 32px;
  border-radius: 10px;
  display: flex;
  align-items: center;
  justify-content: center;
  flex-shrink: 0;
}

.sheet-body {
  flex: 1;
  overflow-y: auto;
  min-height: 140px;
}

.empty-state {
  display: flex;
  flex-direction: column;
  align-items: center;
  justify-content: center;
  gap: 11px;
  padding: 28px 12px;
  color: var(--text-secondary);
  font-size: 12.5px;
}

.empty-ico {
  color: var(--text-muted);
}

.bound-list {
  display: flex;
  flex-direction: column;
  gap: 9px;
}

.bound-row {
  display: flex;
  align-items: center;
  gap: 11px;
  padding: 10px 12px;
  border-radius: 16px;
  background: var(--subcard-bg);
  border: 1px solid var(--glass-border);
}

.app-ico {
  width: 38px;
  height: 38px;
  border-radius: 10px;
  object-fit: cover;
  flex-shrink: 0;
}

.app-meta {
  flex: 1;
  min-width: 0;
}

.app-name {
  font-size: 13.5px;
  font-weight: 700;
  white-space: nowrap;
  overflow: hidden;
  text-overflow: ellipsis;
}

.app-pkg {
  font-size: 10.5px;
  color: var(--text-secondary);
  word-break: break-all;
}

.flag-chips {
  display: flex;
  flex-wrap: wrap;
  gap: 4px;
  margin-top: 5px;
}

.flag-chip {
  font-size: 9.5px;
  font-weight: 700;
  padding: 1.5px 6px;
  border-radius: 6px;
  background: rgba(var(--accent-primary-rgb), 0.16);
  color: var(--accent-primary);
}

.row-actions {
  display: flex;
  gap: 6px;
  flex-shrink: 0;
}

.mini-btn {
  width: 32px;
  height: 32px;
  border-radius: 10px;
  display: flex;
  align-items: center;
  justify-content: center;
  background: var(--subcard-bg);
  border: 1px solid var(--glass-border);
  color: var(--text-primary);
}

.mini-btn.danger {
  color: #f43f5e;
  border-color: rgba(244, 63, 94, 0.32);
  background: rgba(244, 63, 94, 0.12);
}

.sheet-foot {
  display: flex;
  justify-content: space-between;
  align-items: center;
  gap: 8px;
  padding-top: 10px;
  border-top: 1px solid var(--glass-border);
}

.foot-hint {
  display: inline-flex;
  align-items: center;
  gap: 5px;
  font-size: 11px;
  color: var(--text-muted);
}
</style>

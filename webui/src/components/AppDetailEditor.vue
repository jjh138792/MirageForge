<!--
SPDX-License-Identifier: GPL-3.0-only
Copyright (c) 2025-2026 jjh138792
MirageForge: https://github.com/jjh138792/MirageForge
-->

<script setup lang="ts">
import { ref, computed, watch } from 'vue';
import {
  ArrowLeft,
  Zap,
  Layers,
  Gauge,
  ChevronDown,
  ChevronUp,
  ShieldCheck,
  RotateCcw,
} from 'lucide-vue-next';
import LeftToggleRow from './LeftToggleRow.vue';
import { useI18n } from '../i18n';
import { showToast, triggerHaptic, type InstalledAppInfo } from '../bridge/ksu-io';
import {
  mirageStore,
  settingsStore,
  normalizeAppRoute,
  isRouteAnyEnabled,
  sanitizeRefreshRate,
  updateAppRouteViaCli,
  forceStopApp,
  type AppRoute,
} from '../store/mirageStore';

const props = defineProps<{
  app: InstalledAppInfo | null;
}>();

const emit = defineEmits<{
  (e: 'close'): void;
}>();

const { t } = useI18n();
const showAdvanced = ref(false);
const fpsPresets = [0, 90, 120, 144, 165];

const currentRoute = computed<AppRoute | null>(() => {
  if (!props.app) return null;
  return mirageStore.appRoutes[props.app.packageName] || null;
});

const selectedTplId = computed(() => currentRoute.value?.tplId || '');

const hasAnyVfsEnabled = computed(() => {
  const r = currentRoute.value;
  return Boolean(r && (r.cpu || r.mem || r.vfsNodes));
});

const templateList = computed(() => Object.values(mirageStore.templates));

watch(
  () => props.app?.packageName,
  () => {
    const r = currentRoute.value;
    // 若该应用已开启高阶项（高刷 / SDK / 探针 / MountNS），进入时自动展开高阶区方便一眼看到
    showAdvanced.value = Boolean(
      r && (r.probe || r.sdkSpoof || r.refreshRate >= 90 || r.mountNs)
    );
  },
  { immediate: true }
);

async function handleTemplateSelect(tplId: string) {
  if (!props.app) return;
  triggerHaptic('medium');
  const pkg = props.app.packageName;

  if (!tplId) {
    await updateAppRouteViaCli(pkg, null, false);
    showToast(`已恢复 ${props.app.appLabel} 为原生环境 (0 改动)`);
    return;
  }

  const existing = mirageStore.appRoutes[pkg];
  const nextRoute: AppRoute = existing
    ? normalizeAppRoute({ ...existing, tplId })
    : normalizeAppRoute({
        tplId,
        build: true,
        cowProp: settingsStore.defaultCowProp,
        gl: true,
        vfs: true,
        cpu: true,
        mem: true,
        vfsNodes: true,
        probe: false,
        sdkSpoof: false,
        refreshRate: 0,
        mountNs: settingsStore.defaultMountNs,
      });

  await updateAppRouteViaCli(pkg, nextRoute, false);
}

async function updateSwitch(
  field: 'build' | 'cowProp' | 'cpu' | 'gl' | 'mem' | 'vfsNodes' | 'mountNs' | 'probe' | 'sdkSpoof',
  value: boolean
) {
  if (!props.app || !currentRoute.value) return;
  const pkg = props.app.packageName;
  const draft: AppRoute = { ...currentRoute.value, [field]: value };

  // 若开启 MountNS 但尚未开启任何文件节点类开关，自动点亮 CPU/GPU 文件节点
  if (field === 'mountNs' && value && !draft.cpu && !draft.mem && !draft.vfsNodes) {
    draft.vfsNodes = true;
  }
  // 若开启「改动 SDK 版本」，确保至少开启系统属性或 COW 属性使其可生效
  if (field === 'sdkSpoof' && value && !draft.build && !draft.cowProp) {
    draft.build = true;
  }

  const normalized = normalizeAppRoute(draft);
  if (!isRouteAnyEnabled(normalized)) {
    await updateAppRouteViaCli(pkg, null, false);
    showToast('全部开关已关闭，已自动解除伪装恢复 0 改动');
    return;
  }

  await updateAppRouteViaCli(pkg, normalized, false);
}

async function setRefreshRate(hz: number) {
  if (!props.app || !currentRoute.value) return;
  triggerHaptic('light');
  const cleanHz = sanitizeRefreshRate(hz);
  const draft: AppRoute = {
    ...currentRoute.value,
    refreshRate: cleanHz,
  };
  // 刷新率归属于系统/显示属性层，若两者均关闭则自动开启「伪装系统属性」
  if (cleanHz >= 90 && !draft.build && !draft.gl) {
    draft.build = true;
  }
  await updateAppRouteViaCli(props.app.packageName, normalizeAppRoute(draft), false);
}

async function resetToNative() {
  if (!props.app) return;
  triggerHaptic('medium');
  await updateAppRouteViaCli(props.app.packageName, null, false);
  showToast(`已恢复 ${props.app.appLabel} 为原生环境 (0 改动)`);
}
</script>

<template>
  <Transition name="page-push">
    <div v-if="app" class="app-editor-fullscreen">
      <!-- 1. 顶部返回与应用身份栏 -->
      <header class="editor-topbar liquid-glass liquid-glass-elevated">
        <button class="back-circle tactile-press" @click="emit('close')" aria-label="返回">
          <ArrowLeft :size="19" />
        </button>

        <div class="app-identity">
          <img
            :src="app.iconUrl"
            class="topbar-icon"
            onerror="this.style.display='none'"
            alt=""
          />
          <div class="topbar-titles">
            <h2>{{ app.appLabel }}</h2>
            <span class="topbar-pkg">{{ app.packageName }}</span>
          </div>
        </div>

        <button
          class="restart-pill tactile-press"
          :title="t('apps.restartBtn')"
          @click="forceStopApp(app.packageName, app.appLabel)"
        >
          <Zap :size="14" />
          <span>重启</span>
        </button>
      </header>

      <!-- 2. 主体滚动区 -->
      <div class="editor-scroll">
        <!-- 最上方：选择伪装模板卡片 -->
        <div class="section-card liquid-glass" v-touch-glass>
          <div class="sec-head">
            <div class="sec-title">
              <Layers :size="15" class="accent-ico" />
              <span>选择伪装模板</span>
            </div>
            <button
              v-if="currentRoute"
              class="reset-btn tactile-press"
              @click="resetToNative"
            >
              <RotateCcw :size="12" />
              <span>恢复原生</span>
            </button>
          </div>

          <select
            class="liquid-select tpl-select"
            :value="selectedTplId"
            @change="(e) => handleTemplateSelect((e.target as HTMLSelectElement).value)"
          >
            <option value="">{{ t('apps.nativeMode') }}</option>
            <option v-for="tpl in templateList" :key="tpl.id" :value="tpl.id">
              {{ tpl.name }}
            </option>
          </select>

          <p v-if="!currentRoute" class="native-notice">
            <ShieldCheck :size="14" />
            <span>当前应用处于原生模式：启动时将直接卸载模块，0 Hook、0 内存修改。</span>
          </p>
        </div>

        <!-- 六大核心细粒度开关（左侧独立开关：关灰 / 开主题色） -->
        <div v-if="currentRoute" class="section-card liquid-glass" v-touch-glass>
          <div class="sec-head">
            <span class="sec-title-plain">伪装能力开关</span>
            <span class="sec-hint">未开启的模块保持绝对 0 改动</span>
          </div>

          <div class="toggle-stack">
            <LeftToggleRow
              :model-value="currentRoute.build"
              :title="t('apps.swBuild')"
              description="覆写 Java android.os.Build 反射、系统属性读取与屏幕高刷声明"
              @update:model-value="(v) => updateSwitch('build', v)"
            />

            <LeftToggleRow
              :model-value="currentRoute.cpu"
              :title="t('apps.swCpu')"
              description="伪装 /proc/cpuinfo、处理器核数 (sysconf) 与 uname 架构信息"
              @update:model-value="(v) => updateSwitch('cpu', v)"
            />

            <LeftToggleRow
              :model-value="currentRoute.gl"
              :title="t('apps.swGl')"
              description="伪装 OpenGL ES 渲染器、Vulkan 设备与驱动属性及 MediaDrm"
              @update:model-value="(v) => updateSwitch('gl', v)"
            />

            <LeftToggleRow
              :model-value="currentRoute.cowProp"
              :title="t('apps.swCow')"
              badge="写时复制"
              description="通过 MAP_PRIVATE 原地覆写 /dev/__properties__/*，绕过属性 PLT Hook 检测"
              @update:model-value="(v) => updateSwitch('cowProp', v)"
            />

            <LeftToggleRow
              :model-value="currentRoute.mem"
              :title="t('apps.swMem')"
              description="伪装 /proc/meminfo、sysinfo 物理内存容量与 DDR 规格属性"
              @update:model-value="(v) => updateSwitch('mem', v)"
            />

            <LeftToggleRow
              :model-value="currentRoute.vfsNodes"
              :title="t('apps.swVfsNodes')"
              :badge="currentRoute.mountNs ? 'MountNS' : 'PLT'"
              description="伪装 /sys/devices/system/cpu/*、cpufreq、soc0 与 GPU kgsl 硬件节点"
              @update:model-value="(v) => updateSwitch('vfsNodes', v)"
            />
          </div>
        </div>

        <!-- 底部折叠区：屏幕刷新率与高阶排障开关 -->
        <div v-if="currentRoute" class="section-card liquid-glass" v-touch-glass>
          <button
            class="adv-header tactile-press"
            @click="showAdvanced = !showAdvanced"
          >
            <div class="sec-title">
              <Gauge :size="15" class="accent-ico" />
              <span>显示刷新率与高阶选项</span>
            </div>
            <component :is="showAdvanced ? ChevronUp : ChevronDown" :size="17" class="chev-ico" />
          </button>

          <Transition name="view-morph">
            <div v-if="showAdvanced" class="adv-body">
              <!-- 屏幕最高刷新率欺骗（归入系统/显示属性扩展，0 = 原生 0 改动） -->
              <div class="fps-card">
                <div class="fps-label">{{ t('apps.fpsLabel') }}</div>
                <div class="fps-chips">
                  <button
                    v-for="hz in fpsPresets"
                    :key="hz"
                    type="button"
                    class="fps-chip tactile-press"
                    :class="{ active: (currentRoute.refreshRate || 0) === hz }"
                    @click="setRefreshRate(hz)"
                  >
                    {{ hz === 0 ? '原生 (0 改动)' : `${hz}Hz` }}
                  </button>
                </div>
              </div>

              <div class="toggle-stack">
                <LeftToggleRow
                  :model-value="currentRoute.mountNs"
                  :disabled="!hasAnyVfsEnabled"
                  title="本应用使用命名空间挂载 (MountNS)"
                  badge="实验性"
                  description="挂载成功后自动关闭传统文件 I/O PLT Hook；可在「设置」页一键全局同步"
                  @update:model-value="(v) => updateSwitch('mountNs', v)"
                />

                <LeftToggleRow
                  :model-value="currentRoute.probe"
                  :title="t('apps.swProbe')"
                  description="记录属性查询、未映射属性与 Native 真实崩溃堆栈，排障完成后建议关闭"
                  @update:model-value="(v) => updateSwitch('probe', v)"
                />

                <LeftToggleRow
                  :model-value="currentRoute.sdkSpoof"
                  :title="t('apps.swSdk')"
                  badge="高风险"
                  danger
                  description="强制覆写 Build.VERSION.SDK_INT / RELEASE，跨大版本可能导致部分应用闪退"
                  @update:model-value="(v) => updateSwitch('sdkSpoof', v)"
                />
              </div>
            </div>
          </Transition>
        </div>
      </div>
    </div>
  </Transition>
</template>

<style scoped>
.app-editor-fullscreen {
  position: fixed;
  inset: 0;
  z-index: 165;
  background: var(--bg-gradient);
  display: flex;
  flex-direction: column;
  padding: calc(var(--inset-top) + 8px) 14px calc(var(--inset-bottom) + 12px);
  gap: 13px;
}

.editor-topbar {
  display: flex;
  align-items: center;
  gap: 10px;
  padding: 10px 12px;
  border-radius: 22px;
  flex-shrink: 0;
}

.back-circle {
  width: 36px;
  height: 36px;
  border-radius: 12px;
  background: var(--subcard-bg);
  border: 1px solid var(--glass-border);
  color: var(--text-primary);
  display: flex;
  align-items: center;
  justify-content: center;
  flex-shrink: 0;
}

.app-identity {
  flex: 1;
  min-width: 0;
  display: flex;
  align-items: center;
  gap: 10px;
}

.topbar-icon {
  width: 34px;
  height: 34px;
  border-radius: 9px;
  object-fit: cover;
  flex-shrink: 0;
}

.topbar-titles {
  min-width: 0;
}

.topbar-titles h2 {
  font-size: 14.5px;
  font-weight: 800;
  white-space: nowrap;
  overflow: hidden;
  text-overflow: ellipsis;
}

.topbar-pkg {
  display: block;
  font-size: 10.5px;
  color: var(--text-secondary);
  white-space: nowrap;
  overflow: hidden;
  text-overflow: ellipsis;
}

.restart-pill {
  display: inline-flex;
  align-items: center;
  gap: 4px;
  padding: 7px 11px;
  border-radius: 12px;
  font-size: 11.5px;
  font-weight: 700;
  color: var(--accent-secondary);
  background: rgba(var(--accent-secondary-rgb), 0.14);
  border: 1px solid rgba(var(--accent-secondary-rgb), 0.35);
  flex-shrink: 0;
}

.editor-scroll {
  flex: 1;
  overflow-y: auto;
  display: flex;
  flex-direction: column;
  gap: 13px;
  padding-bottom: 18px;
}

.section-card {
  padding: 15px;
  display: flex;
  flex-direction: column;
  gap: 12px;
}

.sec-head {
  display: flex;
  justify-content: space-between;
  align-items: center;
  gap: 8px;
}

.sec-title {
  display: flex;
  align-items: center;
  gap: 7px;
  font-size: 13.5px;
  font-weight: 800;
}

.sec-title-plain {
  font-size: 13.5px;
  font-weight: 800;
}

.sec-hint {
  font-size: 11px;
  color: var(--text-muted);
}

.accent-ico {
  color: var(--accent-primary);
}

.reset-btn {
  display: inline-flex;
  align-items: center;
  gap: 4px;
  padding: 4.5px 9px;
  border-radius: 9px;
  font-size: 11px;
  font-weight: 700;
  color: #f43f5e;
  background: rgba(244, 63, 94, 0.12);
  border: 1px solid rgba(244, 63, 94, 0.3);
}

.tpl-select {
  font-weight: 700;
  font-size: 13px;
  padding: 11px 13px;
}

.native-notice {
  display: flex;
  align-items: center;
  gap: 7px;
  padding: 10px 12px;
  border-radius: 13px;
  font-size: 11.5px;
  color: #10b981;
  background: rgba(16, 185, 129, 0.12);
  border: 1px solid rgba(16, 185, 129, 0.32);
  line-height: 1.45;
}

.toggle-stack {
  display: flex;
  flex-direction: column;
  gap: 9px;
}

.adv-header {
  width: 100%;
  display: flex;
  justify-content: space-between;
  align-items: center;
  background: transparent;
  border: none;
  color: var(--text-primary);
  padding: 2px 0;
}

.chev-ico {
  color: var(--text-secondary);
}

.adv-body {
  display: flex;
  flex-direction: column;
  gap: 11px;
  padding-top: 6px;
  border-top: 1px dashed var(--glass-border);
}

.fps-card {
  padding: 11px 12px;
  border-radius: 15px;
  background: var(--subcard-bg);
  border: 1px solid var(--glass-border);
  display: flex;
  flex-direction: column;
  gap: 8px;
}

.fps-label {
  font-size: 11.5px;
  font-weight: 700;
  color: var(--text-secondary);
}

.fps-chips {
  display: flex;
  flex-wrap: wrap;
  gap: 6px;
}

.fps-chip {
  padding: 5.5px 11px;
  border-radius: 9px;
  font-size: 11px;
  font-weight: 700;
  background: transparent;
  color: var(--text-secondary);
  border: 1px solid var(--glass-border);
}

.fps-chip.active {
  color: #fff;
  background: linear-gradient(135deg, var(--accent-primary), var(--accent-secondary));
  border-color: rgba(255, 255, 255, 0.28);
}
</style>

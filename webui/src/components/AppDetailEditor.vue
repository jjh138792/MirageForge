<!--
SPDX-License-Identifier: GPL-3.0-only
Copyright (c) 2025-2026 jjh138792
MirageForge: https://github.com/jjh138792/MirageForge
-->

<script setup lang="ts">
import { ref, computed, watch, onUnmounted } from 'vue';
import {
  ArrowLeft,
  Zap,
  Layers,
  Gauge,
  ChevronDown,
  ChevronUp,
  ShieldCheck,
  RotateCcw,
  Sparkles,
  SlidersHorizontal,
  Check,
  X,
  Activity,
  RefreshCw,
} from 'lucide-vue-next';
import LeftToggleRow from './LeftToggleRow.vue';
import { useI18n } from '../i18n';
import { showToast, triggerHaptic, type InstalledAppInfo } from '../bridge/ksu-io';
import {
  mirageStore,
  settingsStore,
  normalizeAppRoute,
  isRouteAnyEnabled,
  isAppOverridesEmpty,
  normalizeAppOverrides,
  sanitizeRefreshRate,
  updateAppRouteViaCli,
  refreshRuntimeStatus,
  forceStopApp,
  type AppRoute,
  type PropSpoofMode,
} from '../store/mirageStore';

const props = defineProps<{
  app: InstalledAppInfo | null;
}>();

const emit = defineEmits<{
  (e: 'close'): void;
}>();

const { t } = useI18n();
const showAdvanced = ref(false);
const showOverrides = ref(false);
const showPropModeModal = ref(false);
const fpsPresets = [0, 90, 120, 144, 165];

const overrideRamStr = ref('');
const overrideCoresStr = ref('');
const overrideCustomProps = ref('');

// 双击 / 长按手势状态（带滑动距离取消，避免上下滚动误触）
let propLongPressTimer: ReturnType<typeof setTimeout> | null = null;
let propLastTapTs = 0;
let propPressTriggered = false;
let propStartX = 0;
let propStartY = 0;

const currentRoute = computed<AppRoute | null>(() => {
  if (!props.app) return null;
  return mirageStore.appRoutes[props.app.packageName] || null;
});

const currentRuntime = computed(() => {
  if (!props.app) return null;
  return mirageStore.runtimeStatus[props.app.packageName] || null;
});

const selectedTplId = computed(() => currentRoute.value?.tplId || '');

const selectedTpl = computed(() =>
  selectedTplId.value ? mirageStore.templates[selectedTplId.value] || null : null
);

const hasAnyVfsEnabled = computed(() => {
  const r = currentRoute.value;
  return Boolean(r && (r.cpu || r.mem || r.vfsNodes));
});

const hasOverrides = computed(() => !isAppOverridesEmpty(currentRoute.value?.overrides));

const templateList = computed(() => Object.values(mirageStore.templates));

const propModeOptions = computed<
  Array<{ key: PropSpoofMode; label: string; short: string; desc: string; tag: string }>
>(() => [
  {
    key: 'hook',
    label: t('apps.propModeHook'),
    short: 'Hook',
    desc: t('apps.propModeHookDesc'),
    tag: '通用兜底',
  },
  {
    key: 'cow',
    label: t('apps.propModeCow'),
    short: '纯 COW',
    desc: t('apps.propModeCowDesc'),
    tag: '0 属性 Hook',
  },
  {
    key: 'mount_ns',
    label: t('apps.propModeMountNs'),
    short: 'MountNS',
    desc: t('apps.propModeMountNsDesc'),
    tag: 'r--s 零脏页',
  },
]);

function propModeShortLabel(mode?: PropSpoofMode): string {
  if (mode === 'mount_ns') return 'MountNS';
  if (mode === 'cow') return '纯 COW';
  return 'Hook';
}

function syncOverrideDrafts() {
  const ovr = currentRoute.value?.overrides;
  overrideRamStr.value = ovr?.ramGb ? String(ovr.ramGb) : '';
  overrideCoresStr.value = ovr?.cpuCores ? String(ovr.cpuCores) : '';
  overrideCustomProps.value = ovr?.customProps || '';
}

watch(
  () => props.app?.packageName,
  () => {
    const r = currentRoute.value;
    showAdvanced.value = Boolean(
      r && (r.probe || r.sdkSpoof || r.hdrSpoof || r.refreshRate >= 90 || r.mountNs)
    );
    showOverrides.value = Boolean(r && !isAppOverridesEmpty(r.overrides));
    showPropModeModal.value = false;
    syncOverrideDrafts();
    if (props.app) {
      refreshRuntimeStatus();
    }
  },
  { immediate: true }
);

watch(
  () => currentRoute.value?.overrides,
  () => syncOverrideDrafts(),
  { deep: true }
);

onUnmounted(() => {
  if (propLongPressTimer) {
    clearTimeout(propLongPressTimer);
    propLongPressTimer = null;
  }
});

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
        propMode: settingsStore.defaultPropMode,
        gl: true,
        vfs: true,
        cpu: true,
        mem: true,
        vfsNodes: true,
        probe: false,
        sdkSpoof: false,
        hdrSpoof: false,
        refreshRate: 0,
        mountNs: settingsStore.defaultMountNs,
      });

  await updateAppRouteViaCli(pkg, nextRoute, false);
}

async function applyScenePreset(preset: 'stealth' | 'gaming' | 'full') {
  if (!props.app || !currentRoute.value) return;
  triggerHaptic('medium');
  const base = currentRoute.value;
  let draft: AppRoute;

  if (preset === 'stealth') {
    draft = {
      ...base,
      build: true,
      propMode: 'mount_ns',
      gl: false,
      cpu: true,
      mem: true,
      vfsNodes: true,
      mountNs: true,
      probe: false,
      sdkSpoof: false,
      hdrSpoof: false,
      refreshRate: 0,
    };
    showToast('已应用「极致隐匿」预设（MountNS 属性 + MountNS 节点）');
  } else if (preset === 'gaming') {
    const targetHz = sanitizeRefreshRate(selectedTpl.value?.MAX_REFRESH_RATE || 120) || 120;
    draft = {
      ...base,
      build: true,
      propMode: 'cow',
      gl: true,
      cpu: true,
      mem: true,
      vfsNodes: true,
      mountNs: settingsStore.defaultMountNs,
      probe: false,
      sdkSpoof: false,
      hdrSpoof: true,
      refreshRate: targetHz,
    };
    showAdvanced.value = true;
    showToast(`已应用「游戏高刷」预设（GPU + ${targetHz}Hz + HDR + 纯 COW）`);
  } else {
    draft = {
      ...base,
      build: true,
      propMode: settingsStore.defaultPropMode,
      gl: true,
      cpu: true,
      mem: true,
      vfsNodes: true,
      mountNs: settingsStore.defaultMountNs,
      probe: false,
      sdkSpoof: false,
      hdrSpoof: false,
      refreshRate: 0,
    };
    showToast('已应用「全量伪装」预设');
  }

  await updateAppRouteViaCli(props.app.packageName, normalizeAppRoute(draft), false);
}

async function updateSwitch(
  field: 'build' | 'cpu' | 'gl' | 'mem' | 'vfsNodes' | 'mountNs' | 'hdrSpoof' | 'probe' | 'sdkSpoof',
  value: boolean
) {
  if (!props.app || !currentRoute.value) return;
  const pkg = props.app.packageName;
  const draft: AppRoute = { ...currentRoute.value, [field]: value };

  if (field === 'build') {
    if (value) {
      draft.propMode =
        currentRoute.value.propMode !== 'hook'
          ? currentRoute.value.propMode
          : settingsStore.defaultPropMode;
    } else {
      draft.sdkSpoof = false;
    }
  } else if (field === 'sdkSpoof' && value) {
    draft.build = true;
  } else if (field === 'hdrSpoof' && value && !draft.build && !draft.gl) {
    draft.gl = true;
  } else if (field === 'mountNs' && value && !draft.cpu && !draft.mem && !draft.vfsNodes) {
    draft.vfsNodes = true;
  }

  const normalized = normalizeAppRoute(draft);
  if (!isRouteAnyEnabled(normalized)) {
    await updateAppRouteViaCli(pkg, null, false);
    showToast('全部开关已关闭，已自动解除伪装恢复 0 改动');
    return;
  }

  await updateAppRouteViaCli(pkg, normalized, false);
}

async function selectPropMode(mode: PropSpoofMode) {
  if (!props.app || !currentRoute.value) return;
  triggerHaptic('medium');
  const draft: AppRoute = {
    ...currentRoute.value,
    build: true,
    propMode: mode,
  };
  showPropModeModal.value = false;
  await updateAppRouteViaCli(props.app.packageName, normalizeAppRoute(draft), false);
  showToast(`系统属性引擎已切换为 ${propModeShortLabel(mode)}`);
}

function clearPropLongPress() {
  if (propLongPressTimer) {
    clearTimeout(propLongPressTimer);
    propLongPressTimer = null;
  }
}

function onBuildRowPointerDown(e: PointerEvent) {
  propPressTriggered = false;
  propStartX = e.clientX;
  propStartY = e.clientY;
  clearPropLongPress();
  propLongPressTimer = setTimeout(() => {
    propPressTriggered = true;
    propLongPressTimer = null;
    triggerHaptic('medium');
    showPropModeModal.value = true;
  }, 420);
}

function onBuildRowPointerMove(e: PointerEvent) {
  if (!propLongPressTimer) return;
  if (Math.hypot(e.clientX - propStartX, e.clientY - propStartY) > 10) {
    clearPropLongPress();
  }
}

function onBuildRowPointerUp() {
  clearPropLongPress();
  if (propPressTriggered) {
    propPressTriggered = false;
    propLastTapTs = 0;
    return;
  }
  const now = Date.now();
  if (now - propLastTapTs < 320) {
    propLastTapTs = 0;
    triggerHaptic('medium');
    showPropModeModal.value = true;
  } else {
    propLastTapTs = now;
  }
}

function onBuildRowPointerCancel() {
  clearPropLongPress();
  propPressTriggered = false;
}

async function setRefreshRate(hz: number) {
  if (!props.app || !currentRoute.value) return;
  triggerHaptic('light');
  const cleanHz = sanitizeRefreshRate(hz);
  const draft: AppRoute = {
    ...currentRoute.value,
    refreshRate: cleanHz,
  };
  if (cleanHz >= 90 && !draft.build && !draft.gl) {
    draft.build = true;
  }
  await updateAppRouteViaCli(props.app.packageName, normalizeAppRoute(draft), false);
}

async function saveOverrides() {
  if (!props.app || !currentRoute.value) return;
  triggerHaptic('medium');
  const parsed = normalizeAppOverrides({
    ramGb: overrideRamStr.value ? parseInt(overrideRamStr.value, 10) : undefined,
    cpuCores: overrideCoresStr.value ? parseInt(overrideCoresStr.value, 10) : undefined,
    customProps: overrideCustomProps.value,
  });
  const draft: AppRoute = {
    ...currentRoute.value,
    overrides: isAppOverridesEmpty(parsed) ? undefined : parsed,
  };
  await updateAppRouteViaCli(props.app.packageName, normalizeAppRoute(draft), false);
  showToast(isAppOverridesEmpty(parsed) ? '已清空本应用局部覆写' : '已保存本应用专属局部覆写');
}

async function clearOverrides() {
  overrideRamStr.value = '';
  overrideCoresStr.value = '';
  overrideCustomProps.value = '';
  await saveOverrides();
}

async function resetToNative() {
  if (!props.app) return;
  triggerHaptic('medium');
  await updateAppRouteViaCli(props.app.packageName, null, false);
  showToast(`已恢复 ${props.app.appLabel} 为原生环境 (0 改动)`);
}

function formatRuntimeTime(ts: number): string {
  if (!ts) return '';
  const d = new Date(ts * 1000);
  const pad = (n: number) => String(n).padStart(2, '0');
  return `${pad(d.getHours())}:${pad(d.getMinutes())}:${pad(d.getSeconds())}`;
}
</script>

<template>
  <Teleport to="body">
    <Transition name="page-push">
      <div v-if="app" class="app-editor-fullscreen">
        <!-- 1. 顶部返回与应用身份栏 -->
        <header class="editor-topbar liquid-glass liquid-glass-elevated">
          <button class="back-circle tactile-press" @click="emit('close')" aria-label="返回">
            <ArrowLeft :size="19" />
          </button>

          <div class="app-identity">
            <div class="topbar-icon-shell">
              <span class="topbar-icon-fallback">
                {{ (app.appLabel || app.packageName).slice(0, 1).toUpperCase() }}
              </span>
              <img
                :src="app.iconUrl"
                class="topbar-icon"
                onerror="this.style.display='none'"
                alt=""
              />
            </div>
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

        <!-- 2. 主滚动区 -->
        <div class="editor-scroll">
          <!-- 选择伪装模板 + 场景预设 -->
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

            <!-- 一键场景快捷预设 -->
            <div v-if="currentRoute" class="preset-bar">
              <span class="preset-label">{{ t('apps.presetTitle') }}</span>
              <div class="preset-chips">
                <button class="preset-chip stealth tactile-press" @click="applyScenePreset('stealth')">
                  <ShieldCheck :size="12" />
                  <span>{{ t('apps.presetStealth') }}</span>
                </button>
                <button class="preset-chip gaming tactile-press" @click="applyScenePreset('gaming')">
                  <Zap :size="12" />
                  <span>{{ t('apps.presetGaming') }}</span>
                </button>
                <button class="preset-chip full tactile-press" @click="applyScenePreset('full')">
                  <Sparkles :size="12" />
                  <span>{{ t('apps.presetFull') }}</span>
                </button>
              </div>
            </div>

            <p v-else class="native-notice">
              <ShieldCheck :size="14" />
              <span>当前应用处于原生模式：启动时将直接卸载模块，0 Hook、0 内存修改。</span>
            </p>
          </div>

          <!-- 最近一次真实生效状态卡（Companion 实测落盘） -->
          <div v-if="currentRoute && currentRuntime" class="section-card runtime-card liquid-glass">
            <div class="sec-head">
              <div class="sec-title">
                <Activity :size="14" class="emerald-ico" />
                <span>{{ t('apps.runtimeCardTitle') }}</span>
                <span class="rt-time">PID {{ currentRuntime.pid }} · {{ formatRuntimeTime(currentRuntime.timestamp) }}</span>
              </div>
              <button class="rt-refresh tactile-press" @click="refreshRuntimeStatus()">
                <RefreshCw :size="12" />
              </button>
            </div>

            <div class="rt-pills">
              <span
                class="rt-pill"
                :class="{
                  warn: currentRuntime.propReq !== currentRuntime.propEff && currentRuntime.propReq !== 'off',
                  ok: currentRuntime.propEff === 'mount_ns' || currentRuntime.propEff === 'cow',
                }"
              >
                属性: {{ currentRuntime.propEff.toUpperCase() }}
                <template v-if="currentRuntime.propVerified > 0">
                  ({{ currentRuntime.propVerified }}<template v-if="currentRuntime.propInserted > 0">,+{{ currentRuntime.propInserted }}</template>)
                </template>
              </span>

              <span v-if="currentRuntime.vfsRequested > 0" class="rt-pill ok">
                MountNS 节点: {{ currentRuntime.vfsMounted }}/{{ currentRuntime.vfsRequested }}
              </span>

              <span v-if="currentRuntime.liveNodes > 0" class="rt-pill cyan">
                活体波动: {{ currentRuntime.liveNodes }} 项
              </span>

              <span v-if="currentRuntime.zeroResidence" class="rt-pill emerald">
                {{ t('apps.runtimeZeroRes') }}
              </span>
            </div>
          </div>

          <!-- 5 大核心细粒度开关（伪装系统属性内置三引擎模式徽章 + 双击/长按切换） -->
          <div v-if="currentRoute" class="section-card liquid-glass" v-touch-glass>
            <div class="sec-head">
              <span class="sec-title-plain">伪装能力开关</span>
              <span class="sec-hint">未开启的模块保持绝对 0 改动</span>
            </div>

            <div class="toggle-stack">
              <!-- 定制行：伪装系统属性（左开关控制启停，条目双击/长按切换 Hook / 纯 COW / MountNS） -->
              <div
                class="prop-engine-row"
                :class="{ active: currentRoute.build }"
                @pointerdown="onBuildRowPointerDown"
                @pointermove="onBuildRowPointerMove"
                @pointerup="onBuildRowPointerUp"
                @pointercancel="onBuildRowPointerCancel"
                @contextmenu.prevent
              >
                <div
                  class="switch-hitbox tactile-press"
                  @pointerdown.stop
                  @pointerup.stop
                  @click.stop="updateSwitch('build', !currentRoute.build)"
                >
                  <div class="mf-switch" :class="{ on: currentRoute.build }">
                    <div class="mf-switch-thumb"></div>
                  </div>
                </div>

                <div class="prop-row-copy">
                  <div class="prop-row-head">
                    <span class="prop-row-title">{{ t('apps.swBuild') }}</span>
                    <span
                      class="mode-pill"
                      :class="[
                        currentRoute.build ? currentRoute.propMode : 'muted',
                      ]"
                    >
                      {{ currentRoute.build ? propModeShortLabel(currentRoute.propMode) : '未启用' }}
                    </span>
                  </div>
                  <div class="prop-row-desc">{{ t('apps.swBuildHint') }}</div>
                </div>
              </div>

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

          <!-- 折叠区 A：显示刷新率、HDR 与高阶选项 -->
          <div v-if="currentRoute" class="section-card liquid-glass" v-touch-glass>
            <button
              class="adv-header tactile-press"
              @click="showAdvanced = !showAdvanced"
            >
              <div class="sec-title">
                <Gauge :size="15" class="accent-ico" />
                <span>显示刷新率、HDR 与高阶选项</span>
              </div>
              <component :is="showAdvanced ? ChevronUp : ChevronDown" :size="17" class="chev-ico" />
            </button>

            <Transition name="view-morph">
              <div v-if="showAdvanced" class="adv-body">
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
                    :model-value="currentRoute.hdrSpoof"
                    :title="t('apps.swHdr')"
                    badge="杜比视界/HDR10+"
                    description="在 DisplayInfo 中补齐 Dolby Vision / HDR10 / HLG / HDR10+ 能力声明，用于解锁流媒体与游戏高画质"
                    @update:model-value="(v) => updateSwitch('hdrSpoof', v)"
                  />

                  <LeftToggleRow
                    :model-value="currentRoute.mountNs"
                    :disabled="!hasAnyVfsEnabled"
                    title="本应用使用命名空间挂载 (MountNS)"
                    badge="活体 RAM"
                    description="挂载成功后自动关闭传统文件 I/O PLT Hook，并支持内存与核心频率后台无截断活体波动"
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

          <!-- 折叠区 B：单应用专属局部覆写 -->
          <div v-if="currentRoute" class="section-card liquid-glass" v-touch-glass>
            <button
              class="adv-header tactile-press"
              @click="showOverrides = !showOverrides"
            >
              <div class="sec-title">
                <SlidersHorizontal :size="15" class="accent-ico" />
                <span>{{ t('apps.overridesTitle') }}</span>
                <span v-if="hasOverrides" class="ovr-active-dot">已覆写</span>
              </div>
              <component :is="showOverrides ? ChevronUp : ChevronDown" :size="17" class="chev-ico" />
            </button>

            <Transition name="view-morph">
              <div v-if="showOverrides" class="adv-body">
                <p class="ovr-desc">{{ t('apps.overridesDesc') }}</p>

                <div class="ovr-grid">
                  <label class="ovr-field">
                    <span>独立 RAM (GB)</span>
                    <input
                      v-model="overrideRamStr"
                      type="number"
                      min="1"
                      max="128"
                      class="liquid-input"
                      :placeholder="selectedTpl ? `跟随模板 (${selectedTpl.RAM_GB}GB)` : '留空跟随模板'"
                    />
                  </label>
                  <label class="ovr-field">
                    <span>独立 CPU 核数</span>
                    <input
                      v-model="overrideCoresStr"
                      type="number"
                      min="1"
                      max="32"
                      class="liquid-input"
                      :placeholder="selectedTpl ? `跟随模板 (${selectedTpl.CPU_CORES}核)` : '留空跟随模板'"
                    />
                  </label>
                </div>

                <label class="ovr-field">
                  <span>本应用追加自定义属性 (每行一条 key=value)</span>
                  <textarea
                    v-model="overrideCustomProps"
                    rows="3"
                    class="liquid-input ovr-textarea"
                    placeholder="ro.product.marketname=Xiaomi 15 Ultra&#10;persist.vendor.camera.foo=1"
                  ></textarea>
                </label>

                <div class="ovr-actions">
                  <button
                    v-if="hasOverrides"
                    class="btn-ovr-clear tactile-press"
                    @click="clearOverrides"
                  >
                    {{ t('apps.overridesClearBtn') }}
                  </button>
                  <button class="btn-ovr-save tactile-press" @click="saveOverrides">
                    {{ t('apps.overridesSaveBtn') }}
                  </button>
                </div>
              </div>
            </Transition>
          </div>
        </div>

        <!-- 3. 系统属性三引擎模式选择器（双击 / 长按触发） -->
        <Transition name="view-morph">
          <div
            v-if="showPropModeModal && currentRoute"
            class="mode-sheet-backdrop"
            @click.self="showPropModeModal = false"
          >
            <div class="mode-sheet liquid-glass liquid-glass-elevated">
              <div class="sheet-head">
                <div>
                  <h3>{{ t('apps.propModeTitle') }}</h3>
                  <p>三模式均同步 Java Build 反射；若底层受限将按 MountNS → COW → Hook 自动安全降级</p>
                </div>
                <button class="sheet-close tactile-press" @click="showPropModeModal = false">
                  <X :size="16" />
                </button>
              </div>

              <div class="mode-option-list">
                <button
                  v-for="opt in propModeOptions"
                  :key="opt.key"
                  class="mode-option tactile-press"
                  :class="{ selected: currentRoute.build && currentRoute.propMode === opt.key }"
                  @click="selectPropMode(opt.key)"
                >
                  <div class="mode-opt-main">
                    <div class="mode-opt-title">
                      <span>{{ opt.label }}</span>
                      <span class="mode-opt-tag">{{ opt.tag }}</span>
                    </div>
                    <p class="mode-opt-desc">{{ opt.desc }}</p>
                  </div>
                  <div
                    class="mode-opt-check"
                    :class="{ on: currentRoute.build && currentRoute.propMode === opt.key }"
                  >
                    <Check v-if="currentRoute.build && currentRoute.propMode === opt.key" :size="14" />
                  </div>
                </button>
              </div>
            </div>
          </div>
        </Transition>
      </div>
    </Transition>
  </Teleport>
</template>

<style scoped>
.app-editor-fullscreen {
  position: fixed;
  inset: 0;
  width: 100%;
  height: 100%;
  z-index: 165;
  background: var(--bg-gradient);
  display: flex;
  flex-direction: column;
  overflow: hidden;
  padding: calc(var(--inset-top) + 6px) 14px 0;
  gap: 12px;
}

.editor-topbar {
  flex: 0 0 auto;
  display: flex;
  align-items: center;
  gap: 10px;
  padding: 10px 12px;
  border-radius: 22px;
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

.topbar-icon-shell {
  position: relative;
  width: 34px;
  height: 34px;
  border-radius: 10px;
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

.topbar-icon-fallback {
  font-size: 13px;
  font-weight: 800;
  color: var(--accent-primary);
}

.topbar-icon {
  position: absolute;
  inset: 0;
  width: 100%;
  height: 100%;
  object-fit: cover;
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
  flex: 1 1 0%;
  min-height: 0;
  overflow-y: auto;
  overflow-x: hidden;
  -webkit-overflow-scrolling: touch;
  overscroll-behavior: contain;
  display: flex;
  flex-direction: column;
  gap: 12px;
  padding-bottom: calc(var(--inset-bottom) + 28px);
}

.section-card {
  flex: 0 0 auto;
  padding: 14px;
  display: flex;
  flex-direction: column;
  gap: 11px;
}

.sec-head {
  display: flex;
  justify-content: space-between;
  align-items: center;
  flex-wrap: wrap;
  gap: 6px 10px;
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

.emerald-ico {
  color: #10b981;
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
  padding: 10.5px 12px;
}

.preset-bar {
  display: flex;
  flex-direction: column;
  gap: 7px;
  padding-top: 4px;
}

.preset-label {
  font-size: 11px;
  font-weight: 700;
  color: var(--text-muted);
}

.preset-chips {
  display: grid;
  grid-template-columns: repeat(3, minmax(0, 1fr));
  gap: 7px;
}

.preset-chip {
  display: inline-flex;
  align-items: center;
  justify-content: center;
  gap: 5px;
  padding: 7.5px 6px;
  border-radius: 12px;
  font-size: 11.5px;
  font-weight: 700;
  background: var(--subcard-bg);
  border: 1px solid var(--glass-border);
  color: var(--text-primary);
}

.preset-chip.stealth {
  color: #10b981;
  background: rgba(16, 185, 129, 0.12);
  border-color: rgba(16, 185, 129, 0.32);
}

.preset-chip.gaming {
  color: var(--accent-secondary);
  background: rgba(var(--accent-secondary-rgb), 0.12);
  border-color: rgba(var(--accent-secondary-rgb), 0.34);
}

.preset-chip.full {
  color: var(--accent-primary);
  background: rgba(var(--accent-primary-rgb), 0.12);
  border-color: rgba(var(--accent-primary-rgb), 0.34);
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

.runtime-card {
  padding: 12px 14px;
  border-color: rgba(16, 185, 129, 0.32);
}

.rt-time {
  font-size: 10.5px;
  font-weight: 600;
  color: var(--text-muted);
}

.rt-refresh {
  width: 26px;
  height: 26px;
  border-radius: 8px;
  background: var(--subcard-bg);
  border: 1px solid var(--glass-border);
  color: var(--text-secondary);
  display: flex;
  align-items: center;
  justify-content: center;
}

.rt-pills {
  display: flex;
  flex-wrap: wrap;
  gap: 6px;
}

.rt-pill {
  font-size: 10.5px;
  font-weight: 700;
  padding: 3px 8px;
  border-radius: 8px;
  background: var(--subcard-bg);
  border: 1px solid var(--glass-border);
  color: var(--text-secondary);
}

.rt-pill.ok {
  color: var(--accent-primary);
  background: rgba(var(--accent-primary-rgb), 0.14);
  border-color: rgba(var(--accent-primary-rgb), 0.34);
}

.rt-pill.cyan {
  color: var(--accent-secondary);
  background: rgba(var(--accent-secondary-rgb), 0.14);
  border-color: rgba(var(--accent-secondary-rgb), 0.34);
}

.rt-pill.emerald {
  color: #10b981;
  background: rgba(16, 185, 129, 0.14);
  border-color: rgba(16, 185, 129, 0.36);
}

.rt-pill.warn {
  color: #f59e0b;
  background: rgba(245, 158, 11, 0.14);
  border-color: rgba(245, 158, 11, 0.38);
}

.toggle-stack {
  display: flex;
  flex-direction: column;
  gap: 9px;
}

/* 定制的「伪装系统属性」行（左开关 + 右侧模式徽章 + 双击/长按交互） */
.prop-engine-row {
  display: flex;
  align-items: center;
  gap: 12px;
  padding: 11px 12px;
  border-radius: 16px;
  background: var(--subcard-bg);
  border: 1px solid var(--glass-border);
  user-select: none;
  -webkit-user-select: none;
  touch-action: manipulation;
  transition: border-color 0.2s ease, background-color 0.2s ease;
}

.prop-engine-row.active {
  border-color: rgba(var(--accent-primary-rgb), 0.36);
}

.switch-hitbox {
  padding: 4px 2px;
  display: flex;
  align-items: center;
  flex-shrink: 0;
}

.prop-row-copy {
  flex: 1;
  min-width: 0;
}

.prop-row-head {
  display: flex;
  align-items: center;
  justify-content: space-between;
  gap: 8px;
}

.prop-row-title {
  font-size: 13px;
  font-weight: 700;
  color: var(--text-primary);
}

.mode-pill {
  font-size: 10.5px;
  font-weight: 800;
  padding: 2.5px 9px;
  border-radius: 999px;
  flex-shrink: 0;
  border: 1px solid var(--glass-border);
  letter-spacing: 0.01em;
}

.mode-pill.hook {
  color: var(--text-secondary);
  background: rgba(148, 163, 184, 0.16);
  border-color: rgba(148, 163, 184, 0.35);
}

.mode-pill.cow {
  color: var(--accent-primary);
  background: rgba(var(--accent-primary-rgb), 0.16);
  border-color: rgba(var(--accent-primary-rgb), 0.42);
}

.mode-pill.mount_ns {
  color: #10b981;
  background: rgba(16, 185, 129, 0.16);
  border-color: rgba(16, 185, 129, 0.44);
}

.mode-pill.muted {
  color: var(--text-muted);
  background: transparent;
}

.prop-row-desc {
  font-size: 11px;
  color: var(--text-secondary);
  margin-top: 3px;
  line-height: 1.45;
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
  padding-top: 8px;
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

.ovr-active-dot {
  font-size: 10px;
  font-weight: 800;
  padding: 1.5px 7px;
  border-radius: 999px;
  color: var(--accent-secondary);
  background: rgba(var(--accent-secondary-rgb), 0.16);
  border: 1px solid rgba(var(--accent-secondary-rgb), 0.38);
}

.ovr-desc {
  font-size: 11.5px;
  color: var(--text-secondary);
  line-height: 1.45;
}

.ovr-grid {
  display: grid;
  grid-template-columns: repeat(2, minmax(0, 1fr));
  gap: 9px;
}

.ovr-field {
  display: flex;
  flex-direction: column;
  gap: 5px;
  font-size: 11.5px;
  font-weight: 700;
  color: var(--text-secondary);
}

.ovr-textarea {
  resize: vertical;
  font-family: ui-monospace, SFMono-Regular, Menlo, monospace;
  font-size: 11.5px;
  line-height: 1.45;
}

.ovr-actions {
  display: flex;
  justify-content: flex-end;
  gap: 8px;
}

.btn-ovr-clear {
  padding: 8px 13px;
  border-radius: 11px;
  font-size: 12px;
  font-weight: 700;
  background: var(--subcard-bg);
  border: 1px solid var(--glass-border);
  color: var(--text-secondary);
}

.btn-ovr-save {
  padding: 8px 15px;
  border-radius: 11px;
  font-size: 12px;
  font-weight: 700;
  color: #fff;
  background: linear-gradient(135deg, var(--accent-primary), var(--accent-secondary));
  border: 1px solid rgba(255, 255, 255, 0.24);
}

/* 三引擎模式选择弹层 */
.mode-sheet-backdrop {
  position: fixed;
  inset: 0;
  z-index: 210;
  background: rgba(6, 9, 19, 0.56);
  backdrop-filter: blur(8px);
  -webkit-backdrop-filter: blur(8px);
  display: flex;
  align-items: flex-end;
  justify-content: center;
  padding: 16px 14px calc(var(--inset-bottom) + 16px);
}

.mode-sheet {
  width: 100%;
  max-width: 460px;
  padding: 16px;
  border-radius: 26px;
  display: flex;
  flex-direction: column;
  gap: 13px;
}

.sheet-head {
  display: flex;
  align-items: flex-start;
  justify-content: space-between;
  gap: 10px;
}

.sheet-head h3 {
  font-size: 15px;
  font-weight: 800;
}

.sheet-head p {
  font-size: 11px;
  color: var(--text-secondary);
  margin-top: 3px;
  line-height: 1.45;
}

.sheet-close {
  width: 30px;
  height: 30px;
  border-radius: 10px;
  background: var(--subcard-bg);
  border: 1px solid var(--glass-border);
  color: var(--text-secondary);
  display: flex;
  align-items: center;
  justify-content: center;
  flex-shrink: 0;
}

.mode-option-list {
  display: flex;
  flex-direction: column;
  gap: 9px;
}

.mode-option {
  width: 100%;
  text-align: left;
  padding: 12px 13px;
  border-radius: 16px;
  background: var(--subcard-bg);
  border: 1px solid var(--glass-border);
  color: var(--text-primary);
  display: flex;
  align-items: center;
  gap: 10px;
}

.mode-option.selected {
  border-color: var(--accent-primary);
  background: rgba(var(--accent-primary-rgb), 0.14);
}

.mode-opt-main {
  flex: 1;
  min-width: 0;
}

.mode-opt-title {
  display: flex;
  align-items: center;
  flex-wrap: wrap;
  gap: 6px;
  font-size: 13px;
  font-weight: 800;
}

.mode-opt-tag {
  font-size: 10px;
  font-weight: 700;
  padding: 1.5px 6px;
  border-radius: 6px;
  color: var(--accent-secondary);
  background: rgba(var(--accent-secondary-rgb), 0.14);
  border: 1px solid rgba(var(--accent-secondary-rgb), 0.34);
}

.mode-opt-desc {
  font-size: 11px;
  color: var(--text-secondary);
  margin-top: 4px;
  line-height: 1.45;
}

.mode-opt-check {
  width: 24px;
  height: 24px;
  border-radius: 50%;
  border: 1.5px solid var(--glass-border);
  display: flex;
  align-items: center;
  justify-content: center;
  flex-shrink: 0;
}

.mode-opt-check.on {
  background: linear-gradient(135deg, var(--accent-primary), var(--accent-secondary));
  border-color: transparent;
  color: #fff;
}

@media (max-width: 360px) {
  .app-editor-fullscreen {
    padding-left: 10px;
    padding-right: 10px;
  }
  .section-card {
    padding: 12px;
  }
  .ovr-grid {
    grid-template-columns: 1fr;
  }
}
</style>

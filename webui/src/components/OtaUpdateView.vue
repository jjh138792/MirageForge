<!--
SPDX-License-Identifier: GPL-3.0-only
Copyright (c) 2025-2026 jjh138792
MirageForge: https://github.com/jjh138792/MirageForge
-->

<script setup lang="ts">
import { ref, computed, watch, nextTick, onMounted, onUnmounted } from 'vue';
import {
  ArrowLeft,
  Terminal,
  Download,
  RotateCcw,
  Power,
  Copy,
  CheckCircle2,
  XCircle,
  Loader2,
  Wrench,
  Globe,
} from 'lucide-vue-next';
import { useI18n } from '../i18n';
import { showToast, triggerHaptic } from '../bridge/ksu-io';
import {
  mirageStore,
  detectRootManagerForOta,
  setOtaMirrorMode,
  startOtaInstall,
  stopOtaPolling,
  rebootDeviceNow,
  type RootManagerKind,
  type OtaMirrorMode,
  type OtaStage,
} from '../store/mirageStore';

const props = defineProps<{
  autoStart: boolean;
}>();

const emit = defineEmits<{
  (e: 'back'): void;
}>();

const { t } = useI18n();
const consoleRef = ref<HTMLElement | null>(null);
const confirmReboot = ref(false);

const stages = computed(() => [
  { key: 'detecting' as OtaStage, label: t('ota.stepDetect') },
  { key: 'downloading' as OtaStage, label: t('ota.stepDownload') },
  { key: 'verifying' as OtaStage, label: t('ota.stepVerify') },
  { key: 'installing' as OtaStage, label: t('ota.stepInstall') },
  { key: 'success' as OtaStage, label: t('ota.stepDone') },
]);

const stageOrder: Record<OtaStage, number> = {
  idle: -1,
  detecting: 0,
  downloading: 1,
  verifying: 2,
  installing: 3,
  success: 4,
  error: -2,
};

const activeStageIndex = computed(() => stageOrder[mirageStore.ota.stage] ?? -1);

const managerOptions: Array<{ key: RootManagerKind; label: string }> = [
  { key: 'auto', label: 'Auto' },
  { key: 'ksu', label: 'KernelSU (ksud)' },
  { key: 'apatch', label: 'APatch (apd)' },
  { key: 'magisk', label: 'Magisk (magiskd)' },
];

const mirrorOptions = computed<Array<{ key: OtaMirrorMode; label: string }>>(() => [
  { key: 'auto', label: t('ota.mirrorAuto') },
  { key: 'ghfast', label: t('ota.mirrorGhfast') },
  { key: 'ghproxy', label: t('ota.mirrorGhproxy') },
  { key: 'direct', label: t('ota.mirrorDirect') },
]);

const detectedMgrLabel = computed(() => {
  const m = mirageStore.ota.detectedManager;
  const map: Record<string, string> = {
    ksu: 'KernelSU (ksud)',
    apatch: 'APatch (apd)',
    magisk: 'Magisk (magiskd)',
    none: t('ota.managerNone'),
  };
  const base = map[m] || m;
  return mirageStore.ota.managerVersion ? `${base} · ${mirageStore.ota.managerVersion}` : base;
});

function classifyLogLine(line: string): string {
  const s = line.trim();
  if (s.startsWith('[!]') || /\b(error|failed|fatal)\b/i.test(s)) return 'log-err';
  if (s.startsWith('[+]') || s.includes('成功') || s.includes('Done')) return 'log-ok';
  if (s.startsWith('[*]') || s.startsWith('===') || s.startsWith('---')) return 'log-accent';
  if (s.startsWith('-') || s.startsWith('!')) return 'log-script';
  return 'log-normal';
}

watch(
  () => mirageStore.ota.logs.length,
  async () => {
    await nextTick();
    if (consoleRef.value) {
      consoleRef.value.scrollTop = consoleRef.value.scrollHeight;
    }
  }
);

function handleBack() {
  if (mirageStore.ota.running) {
    showToast(t('ota.runningGuard'));
    return;
  }
  triggerHaptic('light');
  confirmReboot.value = false;
  emit('back');
}

function selectManager(mgr: RootManagerKind) {
  if (mirageStore.ota.running) return;
  triggerHaptic('light');
  mirageStore.ota.selectedManager = mgr;
}

function selectMirror(mode: OtaMirrorMode) {
  if (mirageStore.ota.running) return;
  triggerHaptic('light');
  setOtaMirrorMode(mode);
}

async function handleRebootClick() {
  if (!confirmReboot.value) {
    triggerHaptic('medium');
    confirmReboot.value = true;
    setTimeout(() => {
      confirmReboot.value = false;
    }, 4500);
    return;
  }
  await rebootDeviceNow();
}

async function copyConsoleLogs() {
  triggerHaptic('light');
  const text = mirageStore.ota.logs.join('\n');
  if (!text) return;
  try {
    if (navigator.clipboard && navigator.clipboard.writeText) {
      await navigator.clipboard.writeText(text);
    } else {
      const ta = document.createElement('textarea');
      ta.value = text;
      document.body.appendChild(ta);
      ta.select();
      document.execCommand('copy');
      document.body.removeChild(ta);
    }
    showToast(t('ota.logCopied'));
  } catch (_) {}
}

onMounted(async () => {
  await detectRootManagerForOta();
  if (props.autoStart && !mirageStore.ota.running && mirageStore.ota.stage !== 'success') {
    startOtaInstall();
  }
});

onUnmounted(() => {
  if (!mirageStore.ota.running) {
    stopOtaPolling();
  }
});
</script>

<template>
  <section class="ota-page">
    <!-- 1. 顶部返回与版本信息栏 -->
    <div class="ota-topbar liquid-glass" v-touch-glass>
      <button
        class="btn-liquid-ghost tactile-press"
        :class="{ disabled: mirageStore.ota.running }"
        @click="handleBack"
      >
        <ArrowLeft :size="15" />
        <span>{{ t('ota.backBtn') }}</span>
      </button>

      <div class="ota-title-group">
        <h2>{{ t('ota.pageTitle') }}</h2>
        <span class="ver-badge">
          v{{ mirageStore.version }} → v{{ mirageStore.updateInfo.latestVersion || '?' }}
        </span>
      </div>
    </div>

    <!-- 2. 刷入控制与五阶段进度卡片 -->
    <div class="ota-control-card liquid-glass" v-touch-glass>
      <!-- 五阶段进度条 -->
      <div class="stage-stepper">
        <div
          v-for="(st, idx) in stages"
          :key="st.key"
          class="stage-node"
          :class="{
            done: activeStageIndex > idx || mirageStore.ota.stage === 'success',
            active: activeStageIndex === idx && mirageStore.ota.running,
            failed: mirageStore.ota.stage === 'error' && idx === Math.max(0, activeStageIndex),
          }"
        >
          <div class="stage-dot">
            <CheckCircle2 v-if="activeStageIndex > idx || mirageStore.ota.stage === 'success'" :size="13" />
            <Loader2 v-else-if="activeStageIndex === idx && mirageStore.ota.running" :size="13" class="spin-anim" />
            <span v-else>{{ idx + 1 }}</span>
          </div>
          <span class="stage-label">{{ st.label }}</span>
        </div>
      </div>

      <!-- Root 管理器后端选择 -->
      <div class="config-block">
        <div class="config-head">
          <div class="config-title">
            <Wrench :size="13" />
            <span>{{ t('ota.managerLabel') }}</span>
          </div>
          <span class="detected-chip" :class="{ warn: mirageStore.ota.detectedManager === 'none' }">
            {{ detectedMgrLabel }}
          </span>
        </div>
        <div class="pill-row">
          <button
            v-for="opt in managerOptions"
            :key="opt.key"
            type="button"
            class="opt-pill tactile-press"
            :class="{ active: mirageStore.ota.selectedManager === opt.key }"
            :disabled="mirageStore.ota.running"
            @click="selectManager(opt.key)"
          >
            {{ opt.key === 'auto' ? t('ota.managerAuto') : opt.label }}
          </button>
        </div>
      </div>

      <!-- 下载镜像线路选择 -->
      <div class="config-block">
        <div class="config-head">
          <div class="config-title">
            <Globe :size="13" />
            <span>{{ t('ota.mirrorLabel') }}</span>
          </div>
        </div>
        <div class="pill-row">
          <button
            v-for="opt in mirrorOptions"
            :key="opt.key"
            type="button"
            class="opt-pill tactile-press"
            :class="{ active: mirageStore.ota.mirrorMode === opt.key }"
            :disabled="mirageStore.ota.running"
            @click="selectMirror(opt.key)"
          >
            {{ opt.label }}
          </button>
        </div>
      </div>

      <!-- 主操作按钮区 -->
      <div class="ota-action-bar">
        <div v-if="mirageStore.ota.stage === 'error'" class="status-banner err">
          <XCircle :size="15" />
          <span>{{ mirageStore.ota.errorMsg }}</span>
        </div>
        <div v-else-if="mirageStore.ota.stage === 'success'" class="status-banner ok">
          <CheckCircle2 :size="15" />
          <span>v{{ mirageStore.updateInfo.latestVersion }} 刷入完成，重启设备生效</span>
        </div>

        <div class="action-buttons">
          <button
            v-if="mirageStore.ota.stage === 'success'"
            class="btn-reboot tactile-press"
            :class="{ armed: confirmReboot }"
            @click="handleRebootClick"
          >
            <Power :size="15" />
            <span>{{ confirmReboot ? t('ota.rebootConfirmBtn') : t('ota.rebootBtn') }}</span>
          </button>

          <button
            v-else
            class="btn-liquid tactile-press full-action-btn"
            :disabled="mirageStore.ota.running"
            @click="startOtaInstall()"
          >
            <Loader2 v-if="mirageStore.ota.running" :size="15" class="spin-anim" />
            <RotateCcw v-else-if="mirageStore.ota.stage === 'error'" :size="15" />
            <Download v-else :size="15" />
            <span>
              {{
                mirageStore.ota.running
                  ? t('ota.installingBtn')
                  : mirageStore.ota.stage === 'error'
                  ? t('ota.retryInstallBtn')
                  : t('ota.startInstallBtn')
              }}
            </span>
          </button>
        </div>
      </div>
    </div>

    <!-- 3. 下方半透明液态毛玻璃实时终端屏幕 -->
    <div class="transparent-terminal liquid-glass">
      <div class="terminal-head">
        <div class="term-left">
          <span
            class="term-led"
            :class="{
              running: mirageStore.ota.running,
              ok: mirageStore.ota.stage === 'success',
              err: mirageStore.ota.stage === 'error',
            }"
          ></span>
          <Terminal :size="14" class="term-icon" />
          <span>{{ t('ota.consoleTitle') }}</span>
        </div>

        <button class="term-copy-btn tactile-press" @click="copyConsoleLogs">
          <Copy :size="12" />
          <span>{{ t('ota.copyLogBtn') }}</span>
        </button>
      </div>

      <div ref="consoleRef" class="terminal-screen">
        <div v-if="mirageStore.ota.logs.length === 0" class="term-empty">
          {{ t('ota.consoleWaiting') }}
        </div>
        <div
          v-for="(line, idx) in mirageStore.ota.logs"
          :key="idx"
          class="term-line"
          :class="classifyLogLine(line)"
        >
          <span class="line-no">{{ String(idx + 1).padStart(2, '0') }}</span>
          <span class="line-content">{{ line }}</span>
        </div>
      </div>
    </div>
  </section>
</template>

<style scoped>
.ota-page {
  display: flex;
  flex-direction: column;
  gap: 13px;
  padding-bottom: 18px;
}

.ota-topbar {
  display: flex;
  justify-content: space-between;
  align-items: center;
  padding: 12px 15px;
  border-radius: 22px;
  gap: 10px;
}

.ota-topbar .disabled {
  opacity: 0.45;
}

.ota-title-group {
  display: flex;
  align-items: center;
  gap: 8px;
  flex-wrap: wrap;
}

.ota-title-group h2 {
  font-size: 15px;
  font-weight: 800;
}

.ver-badge {
  font-size: 11px;
  font-weight: 700;
  padding: 2px 9px;
  border-radius: 9px;
  background: rgba(6, 182, 212, 0.16);
  color: var(--accent-secondary);
  border: 1px solid rgba(6, 182, 212, 0.36);
}

.ota-control-card {
  padding: 16px;
  display: flex;
  flex-direction: column;
  gap: 14px;
}

.stage-stepper {
  display: grid;
  grid-template-columns: repeat(5, 1fr);
  gap: 6px;
  padding: 10px 8px;
  border-radius: 16px;
  background: var(--subcard-bg);
  border: 1px solid var(--glass-border);
}

.stage-node {
  display: flex;
  flex-direction: column;
  align-items: center;
  gap: 5px;
  font-size: 10.5px;
  font-weight: 700;
  color: var(--text-muted);
  text-align: center;
}

.stage-dot {
  width: 24px;
  height: 24px;
  border-radius: 50%;
  display: flex;
  align-items: center;
  justify-content: center;
  font-size: 11px;
  background: rgba(148, 163, 184, 0.16);
  border: 1px solid var(--glass-border);
}

.stage-node.active {
  color: var(--accent-secondary);
}

.stage-node.active .stage-dot {
  background: rgba(6, 182, 212, 0.24);
  border-color: var(--accent-secondary);
  box-shadow: 0 0 12px rgba(6, 182, 212, 0.45);
}

.stage-node.done {
  color: #10b981;
}

.stage-node.done .stage-dot {
  background: rgba(16, 185, 129, 0.22);
  border-color: #10b981;
}

.stage-node.failed {
  color: #f43f5e;
}

.stage-node.failed .stage-dot {
  background: rgba(244, 63, 94, 0.22);
  border-color: #f43f5e;
}

.config-block {
  display: flex;
  flex-direction: column;
  gap: 7px;
}

.config-head {
  display: flex;
  justify-content: space-between;
  align-items: center;
  flex-wrap: wrap;
  gap: 6px;
}

.config-title {
  display: flex;
  align-items: center;
  gap: 6px;
  font-size: 11.5px;
  font-weight: 700;
  color: var(--text-secondary);
}

.detected-chip {
  font-size: 10.5px;
  font-weight: 700;
  padding: 2px 8px;
  border-radius: 8px;
  background: rgba(16, 185, 129, 0.16);
  color: #10b981;
  border: 1px solid rgba(16, 185, 129, 0.35);
}

.detected-chip.warn {
  background: rgba(245, 158, 11, 0.16);
  color: #f59e0b;
  border-color: rgba(245, 158, 11, 0.35);
}

.pill-row {
  display: flex;
  flex-wrap: wrap;
  gap: 6px;
}

.opt-pill {
  padding: 5px 11px;
  border-radius: 10px;
  font-size: 11px;
  font-weight: 700;
  background: var(--subcard-bg);
  color: var(--text-secondary);
  border: 1px solid var(--glass-border);
}

.opt-pill.active {
  background: rgba(139, 92, 246, 0.24);
  color: var(--text-primary);
  border-color: var(--accent-primary);
}

.opt-pill:disabled {
  opacity: 0.55;
  pointer-events: none;
}

.ota-action-bar {
  display: flex;
  flex-direction: column;
  gap: 9px;
}

.status-banner {
  display: flex;
  align-items: center;
  gap: 7px;
  padding: 9px 12px;
  border-radius: 12px;
  font-size: 12px;
  font-weight: 700;
}

.status-banner.ok {
  background: rgba(16, 185, 129, 0.15);
  color: #10b981;
  border: 1px solid rgba(16, 185, 129, 0.38);
}

.status-banner.err {
  background: rgba(244, 63, 94, 0.15);
  color: #f43f5e;
  border: 1px solid rgba(244, 63, 94, 0.38);
}

.action-buttons {
  display: flex;
  gap: 8px;
}

.full-action-btn {
  width: 100%;
  padding: 10.5px 16px;
}

.btn-reboot {
  width: 100%;
  display: inline-flex;
  align-items: center;
  justify-content: center;
  gap: 7px;
  padding: 11px 16px;
  border-radius: 14px;
  font-size: 13px;
  font-weight: 800;
  color: #fff;
  background: linear-gradient(135deg, #10b981, #06b6d4);
  border: 1px solid rgba(255, 255, 255, 0.35);
  box-shadow: 0 8px 20px -4px rgba(16, 185, 129, 0.45);
}

.btn-reboot.armed {
  background: linear-gradient(135deg, #f59e0b, #ef4444);
  box-shadow: 0 8px 20px -4px rgba(239, 68, 68, 0.5);
}

/* ==========================================================================
   半透明液态毛玻璃实时终端屏幕（透出底层极光流体背景）
   ========================================================================== */
.transparent-terminal {
  background: rgba(6, 10, 20, 0.34);
  backdrop-filter: blur(22px) saturate(185%);
  -webkit-backdrop-filter: blur(22px) saturate(185%);
  border: 1px solid rgba(56, 189, 248, 0.28);
  border-radius: 22px;
  display: flex;
  flex-direction: column;
  overflow: hidden;
}

:root[data-theme="light"] .transparent-terminal {
  background: rgba(15, 23, 42, 0.62);
  border-color: rgba(99, 102, 241, 0.38);
}

.terminal-head {
  display: flex;
  justify-content: space-between;
  align-items: center;
  padding: 10px 14px;
  background: rgba(255, 255, 255, 0.05);
  border-bottom: 1px solid rgba(255, 255, 255, 0.1);
  color: #e2e8f0;
  font-size: 11.5px;
  font-weight: 700;
}

.term-left {
  display: flex;
  align-items: center;
  gap: 7px;
}

.term-icon {
  color: #38bdf8;
}

.term-led {
  width: 8px;
  height: 8px;
  border-radius: 50%;
  background: #64748b;
}

.term-led.running {
  background: #38bdf8;
  box-shadow: 0 0 10px #38bdf8;
  animation: pulseLed 1s infinite ease-in-out;
}

.term-led.ok {
  background: #10b981;
  box-shadow: 0 0 10px #10b981;
}

.term-led.err {
  background: #f43f5e;
  box-shadow: 0 0 10px #f43f5e;
}

@keyframes pulseLed {
  0%, 100% { opacity: 0.45; }
  50% { opacity: 1; }
}

.term-copy-btn {
  display: inline-flex;
  align-items: center;
  gap: 5px;
  padding: 4px 9px;
  border-radius: 8px;
  background: rgba(255, 255, 255, 0.08);
  border: 1px solid rgba(255, 255, 255, 0.16);
  color: #e2e8f0;
  font-size: 10.5px;
  font-weight: 700;
}

.terminal-screen {
  height: 310px;
  overflow-y: auto;
  padding: 12px 14px;
  font-family: ui-monospace, SFMono-Regular, Menlo, Consolas, monospace;
  font-size: 11.5px;
  line-height: 1.55;
  display: flex;
  flex-direction: column;
  gap: 3px;
  user-select: text;
}

.term-empty {
  color: rgba(148, 163, 184, 0.8);
  text-align: center;
  margin: auto 0;
}

.term-line {
  display: flex;
  align-items: flex-start;
  gap: 9px;
  word-break: break-all;
}

.line-no {
  color: rgba(148, 163, 184, 0.45);
  font-size: 10px;
  user-select: none;
  flex-shrink: 0;
  padding-top: 1px;
}

.line-content {
  white-space: pre-wrap;
}

.log-normal {
  color: #e2e8f0;
}

.log-script {
  color: #cbd5e1;
}

.log-accent {
  color: #38bdf8;
  font-weight: 700;
}

.log-ok {
  color: #34d399;
  font-weight: 700;
}

.log-err {
  color: #fb7185;
  font-weight: 700;
}

.spin-anim {
  animation: spin 1s linear infinite;
}

@keyframes spin {
  from { transform: rotate(0deg); }
  to { transform: rotate(360deg); }
}
</style>

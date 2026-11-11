<script setup lang="ts">
import { ref, onMounted } from 'vue';
import { RefreshCw, Trash2, PlusCircle, AlertTriangle, Activity } from 'lucide-vue-next';
import { useI18n } from '../i18n';
import { execRootShell, showToast, triggerHaptic } from '../bridge/ksu-io';
import {
  CLI_BIN,
  mirageStore,
  refreshAllProbeData,
  captureUnmappedPropViaCli,
} from '../store/mirageStore';

const { t } = useI18n();
const filterQuery = ref('');
const unmappedOnly = ref(false);
const captureTarget = ref<{ pkg: string; key: string; val: string } | null>(null);

async function handleRefresh() {
  triggerHaptic('light');
  await refreshAllProbeData(filterQuery.value, unmappedOnly.value);
}

async function handleClearProbe() {
  triggerHaptic('medium');
  await execRootShell(`"${CLI_BIN}" probe clear`);
  await handleRefresh();
  showToast('已清空探针与崩溃日志');
}

async function handleClearTelemetry() {
  triggerHaptic('medium');
  await execRootShell(`"${CLI_BIN}" probe clear-telemetry`);
  await handleRefresh();
  showToast('已清空注入遥测');
}

function openCaptureDialog(pkg: string, key: string, currentVal: string) {
  triggerHaptic('light');
  captureTarget.value = {
    pkg,
    key,
    val: currentVal && !currentVal.startsWith('<') ? currentVal : '1',
  };
}

async function confirmCapture() {
  if (!captureTarget.value) return;
  const { pkg, key, val } = captureTarget.value;
  await captureUnmappedPropViaCli(pkg, key, val);
  captureTarget.value = null;
  await handleRefresh();
}

onMounted(() => {
  handleRefresh();
});
</script>

<template>
  <section class="probe-stage">
    <!-- 1. Native 崩溃信号现场诊断卡片 -->
    <div class="probe-card crash-border liquid-glass" v-touch-glass>
      <div class="card-head">
        <div class="head-title">
          <AlertTriangle :size="16" class="text-danger" />
          <h2>{{ t('probe.crashTitle') }}</h2>
        </div>
      </div>

      <div v-if="mirageStore.crashEvents.length === 0" class="empty-box">
        {{ t('probe.crashEmpty') }}
      </div>
      <div v-else class="event-scroll short-scroll">
        <div v-for="(c, i) in mirageStore.crashEvents" :key="i" class="event-item crash-item">
          <div class="event-top">
            <span class="badge-tag crash">{{ c.signal }}</span>
            <b class="pkg-highlight">{{ c.pkg }}</b>
            <span class="val-text">{{ c.detail }}</span>
          </div>
          <div v-if="c.stack" class="stack-box crash-stack">
            💥 {{ c.stack }}
          </div>
        </div>
      </div>
    </div>

    <!-- 2. Zygisk 实时注入遥测卡片 -->
    <div class="probe-card liquid-glass" v-touch-glass>
      <div class="card-head">
        <div class="head-title">
          <Activity :size="16" class="text-cyan" />
          <h2>{{ t('probe.teleTitle') }}</h2>
        </div>
        <button class="btn-liquid-ghost tactile-press" @click="handleClearTelemetry">
          <Trash2 :size="13" />
          <span>{{ t('probe.clearTele') }}</span>
        </button>
      </div>

      <div v-if="mirageStore.telemetryEvents.length === 0" class="empty-box">
        {{ t('probe.teleEmpty') }}
      </div>
      <div v-else class="event-scroll short-scroll">
        <div v-for="(tm, idx) in mirageStore.telemetryEvents" :key="idx" class="event-item">
          <div class="event-top">
            <div>
              <span class="badge-tag tele">INJECTED</span>
              <b class="pkg-highlight">{{ tm.pkg }}</b>
              <span class="pid-tag">(PID: {{ tm.pid }})</span>
              <span class="tele-summary">➔ {{ tm.model }} · {{ tm.gpu }} · {{ tm.ram }}</span>
            </div>
            <span class="time-stamp">
              {{ tm.timestamp ? new Date(tm.timestamp * 1000).toLocaleTimeString() : '' }}
            </span>
          </div>
        </div>
      </div>
    </div>

    <!-- 3. 硬件嗅探与调用栈回溯雷达 -->
    <div class="probe-card liquid-glass" v-touch-glass>
      <div class="card-head">
        <div>
          <h2>{{ t('probe.radarTitle') }}</h2>
          <p class="radar-desc">{{ t('probe.radarDesc') }}</p>
        </div>
      </div>

      <div class="radar-toolbar">
        <input
          v-model="filterQuery"
          type="text"
          class="liquid-input"
          :placeholder="t('probe.filterPlaceholder')"
          @input="handleRefresh"
        />
        <div class="toolbar-btns">
          <button
            class="btn-liquid-ghost tactile-press"
            :class="{ 'active-unmapped': unmappedOnly }"
            @click="unmappedOnly = !unmappedOnly; handleRefresh()"
          >
            {{ t('probe.unmappedOnly') }}
          </button>
          <button class="btn-liquid-danger tactile-press" @click="handleClearProbe">
            <Trash2 :size="14" />
          </button>
          <button class="btn-liquid tactile-press" @click="handleRefresh">
            <RefreshCw :size="14" />
          </button>
        </div>
      </div>

      <div v-if="mirageStore.probeEvents.length === 0" class="empty-box">
        暂无匹配的探针记录。请先在「分流矩阵」中为目标应用开启 [📡 探针与调用栈] 并运行应用。
      </div>
      <div v-else class="event-scroll">
        <div v-for="(ev, idx) in mirageStore.probeEvents" :key="idx" class="event-item">
          <div class="event-top">
            <div class="event-line">
              <span class="badge-tag" :class="{ unmapped: ev.unmapped }">{{ ev.domain }}</span>
              <b class="pkg-highlight">{{ ev.pkg }}</b>
              <span class="key-text">{{ ev.key }}</span>
              <span class="val-text">➔ {{ ev.value }}</span>
            </div>

            <button
              v-if="ev.unmapped"
              class="capture-btn tactile-press"
              @click="openCaptureDialog(ev.pkg, ev.key, ev.value)"
            >
              <PlusCircle :size="13" />
              <span>{{ t('probe.captureBtn') }}</span>
            </button>
          </div>

          <div v-if="ev.stack" class="stack-box">
            ↳ {{ t('probe.stackPrefix') }} {{ ev.stack }}
          </div>
        </div>
      </div>
    </div>

    <!-- 液态玻璃属性捕获弹窗 -->
    <Transition name="view-morph">
      <div v-if="captureTarget" class="modal-overlay" @click.self="captureTarget = null">
        <div class="capture-dialog liquid-glass liquid-glass-elevated">
          <h3>捕获未映射属性入库</h3>
          <p class="cap-sub">目标应用: <b>{{ captureTarget.pkg }}</b></p>
          <p class="cap-sub">属性键名: <code>{{ captureTarget.key }}</code></p>
          <input v-model="captureTarget.val" class="liquid-input" placeholder="输入伪装值..." />
          <div class="cap-actions">
            <button class="btn-liquid-ghost tactile-press" @click="captureTarget = null">取消</button>
            <button class="btn-liquid tactile-press" @click="confirmCapture">写入模板并重编译</button>
          </div>
        </div>
      </div>
    </Transition>
  </section>
</template>

<style scoped>
.probe-stage {
  display: flex;
  flex-direction: column;
  gap: 14px;
}

.probe-card {
  padding: 16px;
}

.crash-border {
  border-color: rgba(244, 63, 94, 0.35);
}

.card-head {
  display: flex;
  justify-content: space-between;
  align-items: center;
  margin-bottom: 10px;
}

.head-title {
  display: flex;
  align-items: center;
  gap: 8px;
}

.card-head h2 {
  font-size: 15px;
  font-weight: 800;
}

.text-danger { color: #f43f5e; }
.text-cyan { color: var(--accent-secondary); }

.radar-desc {
  font-size: 11.5px;
  color: var(--text-secondary);
  margin-top: 4px;
}

.radar-toolbar {
  display: flex;
  gap: 8px;
  margin-bottom: 12px;
  flex-wrap: wrap;
}

.radar-toolbar .liquid-input {
  flex: 1;
  min-width: 200px;
}

.toolbar-btns {
  display: flex;
  gap: 6px;
}

.active-unmapped {
  background: rgba(245, 158, 11, 0.22);
  color: #f59e0b;
  border-color: rgba(245, 158, 11, 0.5);
}

.event-scroll {
  display: flex;
  flex-direction: column;
  gap: 8px;
  max-height: 460px;
  overflow-y: auto;
}

.short-scroll {
  max-height: 170px;
}

.event-item {
  background: var(--subcard-bg);
  border: 1px solid var(--glass-border);
  border-radius: 12px;
  padding: 9px 12px;
  font-family: ui-monospace, SFMono-Regular, Menlo, Consolas, monospace;
  font-size: 11px;
  display: flex;
  flex-direction: column;
  gap: 6px;
}

.event-top {
  display: flex;
  justify-content: space-between;
  align-items: center;
  gap: 8px;
}

.event-line {
  word-break: break-all;
}

.badge-tag {
  padding: 2px 6px;
  border-radius: 6px;
  font-size: 10px;
  font-weight: 800;
  background: rgba(139, 92, 246, 0.22);
  color: var(--accent-primary);
}

.badge-tag.unmapped {
  background: rgba(245, 158, 11, 0.2);
  color: #f59e0b;
  border: 1px solid rgba(245, 158, 11, 0.45);
}

.badge-tag.crash {
  background: rgba(244, 63, 94, 0.22);
  color: #f43f5e;
}

.badge-tag.tele {
  background: rgba(16, 185, 129, 0.2);
  color: #10b981;
}

.pkg-highlight {
  color: var(--accent-secondary);
  margin-left: 6px;
}

.key-text {
  color: var(--text-primary);
  margin-left: 8px;
  font-weight: 600;
}

.val-text {
  color: var(--text-secondary);
  margin-left: 6px;
}

.pid-tag {
  color: var(--text-muted);
  margin-left: 5px;
}

.tele-summary {
  color: #10b981;
  margin-left: 8px;
}

.time-stamp {
  font-size: 10px;
  color: var(--text-muted);
  white-space: nowrap;
}

.stack-box {
  font-size: 10.5px;
  color: var(--accent-secondary);
  background: rgba(6, 182, 212, 0.09);
  border-left: 2.5px solid var(--accent-secondary);
  padding: 5px 9px;
  border-radius: 0 8px 8px 0;
  word-break: break-all;
  line-height: 1.45;
}

.crash-stack {
  color: #f43f5e;
  background: rgba(244, 63, 94, 0.1);
  border-left-color: #f43f5e;
}

.capture-btn {
  display: inline-flex;
  align-items: center;
  gap: 4px;
  background: rgba(245, 158, 11, 0.18);
  color: #f59e0b;
  border: 1px solid rgba(245, 158, 11, 0.4);
  padding: 4px 9px;
  border-radius: 8px;
  font-size: 11px;
  font-weight: 700;
  white-space: nowrap;
}

.empty-box {
  text-align: center;
  padding: 20px;
  font-size: 12px;
  color: var(--text-secondary);
}

.modal-overlay {
  position: fixed;
  inset: 0;
  background: rgba(3, 5, 12, 0.65);
  backdrop-filter: blur(8px);
  display: flex;
  align-items: center;
  justify-content: center;
  padding: 16px;
  z-index: 160;
}

.capture-dialog {
  width: 100%;
  max-width: 400px;
  padding: 18px;
  border-radius: 24px;
  display: flex;
  flex-direction: column;
  gap: 10px;
}

.cap-sub {
  font-size: 12px;
  color: var(--text-secondary);
  word-break: break-all;
}

.cap-actions {
  display: flex;
  justify-content: flex-end;
  gap: 8px;
  margin-top: 6px;
}
</style>

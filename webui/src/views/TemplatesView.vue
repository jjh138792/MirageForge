<!--
SPDX-License-Identifier: GPL-3.0-only
Copyright (c) 2025-2026 jjh138792
MirageForge: https://github.com/jjh138792/MirageForge
-->

<script setup lang="ts">
import { ref } from 'vue';
import {
  CloudDownload,
  Plus,
  Share2,
  Edit3,
  Trash2,
  Layers,
  Cpu,
  HardDrive,
  Gauge,
  X,
  Check,
  RefreshCw,
  Globe,
  Sparkles,
  Smartphone,
  Download,
  Upload,
  FileUp,
} from 'lucide-vue-next';
import TemplateCategoryEditor from '../components/TemplateCategoryEditor.vue';
import TemplateActiveAppsSheet from '../components/TemplateActiveAppsSheet.vue';
import { useI18n } from '../i18n';
import { execRootShell, showToast, triggerHaptic } from '../bridge/ksu-io';
import {
  CLI_BIN,
  mirageStore,
  parseClusterBars,
  saveTemplateViaCli,
  deleteTemplateViaCli,
  encodeTemplateShareCode,
  decodeTemplateShareCode,
  fetchOnlineTemplateHub,
  saveCustomHubUrl,
  parsePropTextViaCli,
  cloneHostDeviceViaCli,
  normalizeTemplateVulkanFields,
  type DeviceTemplate,
} from '../store/mirageStore';

const emit = defineEmits<{
  (e: 'jump-to-apps'): void;
}>();

const { t } = useI18n();

// 分类全屏编辑器状态
const editingTemplate = ref<DeviceTemplate | null>(null);
const editingIsNew = ref(false);

// 生效应用弹窗状态
const activeAppsTemplate = ref<DeviceTemplate | null>(null);

// 在线模板库弹窗状态
const showOnlineHubModal = ref(false);
const customHubInput = ref(mirageStore.hubStatus.customUrl);

// 统一导入/导出中心弹窗（mf:// 口令、build.prop 属性炼金、一键克隆本机、备份导出）
const showTransferModal = ref(false);
const transferTab = ref<'share' | 'alchemy' | 'backup'>('share');
const shareCodeInput = ref('');
const rawPropText = ref('');
const propFileInputRef = ref<HTMLInputElement | null>(null);
const cloningHost = ref(false);

function createEmptyTemplate(): DeviceTemplate {
  return normalizeTemplateVulkanFields({
    id: `tpl_${Date.now()}`,
    name: '',
    MODEL: '',
    BRAND: 'Xiaomi',
    MANUFACTURER: 'Xiaomi',
    DEVICE: '',
    PRODUCT: '',
    BOARD: 'sun',
    HARDWARE: 'qcom',
    BOOTLOADER: 'unknown',
    SOC_MANUFACTURER: 'Qualcomm',
    SOC_MODEL: 'SM8750-AB',
    SUPPORTED_ABIS: 'arm64-v8a,armeabi-v7a,armeabi',
    BUILD_ID: 'AP3A.240905.015.A2',
    DISPLAY_ID: 'AP3A.240905.015.A2 release-keys',
    INCREMENTAL: 'OS2.0.102.0.VOBCNXM',
    BUILD_HOST: 'pangu-build-component-system',
    BUILD_USER: 'builder',
    BUILD_TIME: '1736899200000',
    RELEASE: '15',
    SDK_INT: '35',
    SECURITY_PATCH: '2025-02-01',
    FINGERPRINT: '',
    GL_VENDOR: 'Qualcomm',
    GL_RENDERER: 'Adreno (TM) 830',
    GL_VERSION: 'OpenGL ES 3.2 V@0800.0',
    MAX_REFRESH_RATE: 120,
    CPU_HARDWARE: 'Qualcomm Technologies, Inc SM8750',
    CPU_CORES: 8,
    CPU_MAX_FREQ: 4320000,
    CPU_CLUSTERS: '0-5:614400:3532800;6-7:806400:4320000',
    RAM_GB: 16,
    DDR_TYPE: 'LPDDR5X',
    DDR_VENDOR: 'SKhynix',
    SOC_ID: '618',
    SOC_MACHINE: 'SM8750',
    KERNEL_RELEASE: '6.6.30-android15-8-g94b8f12ab4c1',
    KERNEL_VERSION: '#1 SMP PREEMPT Fri Nov 15 12:00:00 UTC 2025',
    CUSTOM_PROPS: '',
  });
}

function getBoundAppCount(tplId: string): number {
  return Object.values(mirageStore.appRoutes).filter((r) => r.tplId === tplId).length;
}

function openNewTemplate() {
  triggerHaptic('light');
  editingIsNew.value = true;
  editingTemplate.value = createEmptyTemplate();
}

function openEditTemplate(tpl: DeviceTemplate) {
  triggerHaptic('light');
  editingIsNew.value = false;
  editingTemplate.value = normalizeTemplateVulkanFields({ ...tpl });
}

function openActiveApps(tpl: DeviceTemplate) {
  triggerHaptic('light');
  activeAppsTemplate.value = tpl;
}

async function copyShareCode(tpl: DeviceTemplate) {
  triggerHaptic('medium');
  const code = encodeTemplateShareCode(tpl);
  try {
    if (navigator.clipboard && navigator.clipboard.writeText) {
      await navigator.clipboard.writeText(code);
    } else {
      const ta = document.createElement('textarea');
      ta.value = code;
      document.body.appendChild(ta);
      ta.select();
      document.execCommand('copy');
      document.body.removeChild(ta);
    }
    showToast(t('templates.shareCopied'));
  } catch (_) {
    shareCodeInput.value = code;
    transferTab.value = 'share';
    showTransferModal.value = true;
  }
}

async function openOnlineHub() {
  triggerHaptic('light');
  customHubInput.value = mirageStore.hubStatus.customUrl;
  showOnlineHubModal.value = true;
  await fetchOnlineTemplateHub(false);
}

async function applyCustomHubSource() {
  triggerHaptic('medium');
  saveCustomHubUrl(customHubInput.value);
  await fetchOnlineTemplateHub(true);
}

async function importFromHub(tpl: DeviceTemplate) {
  await saveTemplateViaCli({ ...tpl });
}

function openTransferCenter() {
  triggerHaptic('light');
  showTransferModal.value = true;
}

function handleConfirmShareImport() {
  const decoded = decodeTemplateShareCode(shareCodeInput.value);
  if (!decoded) {
    showToast('分享口令格式无效，请检查是否以 mf:// 开头');
    return;
  }
  showTransferModal.value = false;
  shareCodeInput.value = '';
  editingIsNew.value = true;
  editingTemplate.value = normalizeTemplateVulkanFields({ ...createEmptyTemplate(), ...decoded });
}

function handlePropFileChange(e: Event) {
  const target = e.target as HTMLInputElement;
  const file = target.files?.[0];
  if (!file) return;
  const reader = new FileReader();
  reader.onload = async (ev) => {
    rawPropText.value = String(ev.target?.result || '');
    await runPropAlchemy();
  };
  reader.readAsText(file);
}

async function runPropAlchemy() {
  if (!rawPropText.value.trim()) {
    showToast('请先导入或粘贴 build.prop 内容');
    return;
  }
  const tpl = await parsePropTextViaCli(rawPropText.value);
  if (tpl) {
    showTransferModal.value = false;
    rawPropText.value = '';
    editingIsNew.value = true;
    editingTemplate.value = tpl;
  }
}

async function handleCloneHostDevice() {
  cloningHost.value = true;
  const tpl = await cloneHostDeviceViaCli();
  cloningHost.value = false;
  if (tpl) {
    showTransferModal.value = false;
    editingIsNew.value = true;
    editingTemplate.value = tpl;
    showToast('已提取本机全部硬件拓扑，请确认后保存');
  }
}

async function handleExportBackup() {
  triggerHaptic('medium');
  const outPath = `/sdcard/Download/MirageForge_v${mirageStore.version}_Backup.json`;
  await execRootShell(`"${CLI_BIN}" export "${outPath}"`);
  showToast(`已导出完整备份至 ${outPath}`);
}
</script>

<template>
  <section class="templates-view">
    <!-- 1. 顶部大标题：模板 + 3 大核心快捷按钮 -->
    <div class="page-large-header">
      <h1 class="page-large-title">{{ t('nav.templates') }}</h1>
    </div>

    <div class="top-action-bar liquid-glass" v-touch-glass>
      <button class="bar-action tactile-press" @click="openOnlineHub">
        <CloudDownload :size="15" class="ico-cyan" />
        <span>{{ t('templates.hubBtn') }}</span>
      </button>
      <button class="bar-action primary tactile-press" @click="openNewTemplate">
        <Plus :size="15" />
        <span>{{ t('templates.newBtn') }}</span>
      </button>
      <button class="bar-action tactile-press" @click="openTransferCenter">
        <Upload :size="15" class="ico-violet" />
        <span>{{ t('templates.importShareBtn') }}</span>
      </button>
    </div>

    <!-- 2. 模板卡片网格 -->
    <div class="template-grid">
      <article
        v-for="tpl in Object.values(mirageStore.templates)"
        :key="tpl.id"
        class="tpl-card liquid-glass"
        v-touch-glass
      >
        <div class="tpl-card-top">
          <div class="tpl-title-wrap">
            <h3 class="tpl-name">{{ tpl.name }}</h3>
            <span class="tpl-sub">
              {{ tpl.BRAND }} · {{ tpl.MODEL }} ({{ tpl.BOARD }} / {{ tpl.SOC_MODEL }})
            </span>
          </div>
          <span class="gpu-pill">{{ tpl.VK_DEVICE_NAME || tpl.GL_RENDERER }}</span>
        </div>

        <div class="spec-chips">
          <div class="spec-item">
            <Cpu :size="13" class="spec-icon" />
            <span>VK {{ tpl.VK_API_VERSION || '1.3.295' }} · {{ tpl.CPU_CORES }} 核</span>
          </div>
          <div class="spec-item">
            <HardDrive :size="13" class="spec-icon" />
            <span>{{ tpl.RAM_GB }}GB {{ tpl.DDR_TYPE || 'LPDDR5X' }} · {{ tpl.MAX_REFRESH_RATE || 120 }}Hz</span>
          </div>
        </div>

        <div v-if="parseClusterBars(tpl.CPU_CLUSTERS).length > 0" class="cluster-visual-box">
          <div class="cluster-visual-title">
            <Gauge :size="12" />
            <span>{{ t('templates.clusterLabel') }}</span>
          </div>
          <div class="cluster-bars">
            <div
              v-for="(bar, idx) in parseClusterBars(tpl.CPU_CLUSTERS)"
              :key="idx"
              class="cluster-bar-item"
            >
              <div class="bar-label">
                <span>Core {{ bar.range }} ({{ bar.cores }}x)</span>
                <b>{{ bar.minGhz }} ~ {{ bar.maxGhz }} GHz</b>
              </div>
              <div class="bar-track">
                <div
                  class="bar-fill"
                  :style="{ width: `${Math.min(100, Math.round((bar.rawMax / 4500000) * 100))}%` }"
                ></div>
              </div>
            </div>
          </div>
        </div>

        <!-- 卡片底部操作区：新增「生效应用」按钮 -->
        <div class="tpl-card-footer">
          <button class="foot-pill active-apps tactile-press" @click="openActiveApps(tpl)">
            <Layers :size="13" />
            <span>{{ t('templates.activeAppsBtn') }}</span>
            <b class="count-dot">{{ getBoundAppCount(tpl.id) }}</b>
          </button>

          <div class="foot-right-btns">
            <button class="foot-pill tactile-press" @click="copyShareCode(tpl)" title="复制 mf:// 分享码">
              <Share2 :size="13" />
            </button>
            <button class="foot-pill edit tactile-press" @click="openEditTemplate(tpl)">
              <Edit3 :size="13" />
              <span>{{ t('templates.editBtn') }}</span>
            </button>
            <button class="foot-pill danger tactile-press" @click="deleteTemplateViaCli(tpl.id)" title="删除模板">
              <Trash2 :size="13" />
            </button>
          </div>
        </div>
      </article>
    </div>

    <!-- 3. 分类全屏展开编辑器 -->
    <TemplateCategoryEditor
      :template="editingTemplate"
      :is-new="editingIsNew"
      @close="editingTemplate = null"
      @open-active-apps="(tpl) => (activeAppsTemplate = tpl)"
    />

    <!-- 4. 生效应用管理面板 -->
    <TemplateActiveAppsSheet
      :template="activeAppsTemplate"
      @close="activeAppsTemplate = null"
      @jump-to-apps="emit('jump-to-apps')"
    />

    <!-- 5. 统一导入 / 导出中心（mf:// 分享码 + 属性炼金 + 克隆本机 + 备份） -->
    <Transition name="view-morph">
      <div v-if="showTransferModal" class="modal-overlay" @click.self="showTransferModal = false">
        <div class="modal-sheet liquid-glass liquid-glass-elevated">
          <div class="modal-header">
            <h3>导入 / 导出与真机提取</h3>
            <button class="icon-close tactile-press" @click="showTransferModal = false">
              <X :size="18" />
            </button>
          </div>

          <div class="transfer-tabs">
            <button
              class="t-tab tactile-press"
              :class="{ active: transferTab === 'share' }"
              @click="transferTab = 'share'"
            >
              mf:// 口令
            </button>
            <button
              class="t-tab tactile-press"
              :class="{ active: transferTab === 'alchemy' }"
              @click="transferTab = 'alchemy'"
            >
              属性炼金
            </button>
            <button
              class="t-tab tactile-press"
              :class="{ active: transferTab === 'backup' }"
              @click="transferTab = 'backup'"
            >
              克隆与备份
            </button>
          </div>

          <div class="modal-body">
            <!-- Tab 1: mf:// 分享码导入 -->
            <div v-if="transferTab === 'share'" class="transfer-pane">
              <p class="pane-desc">粘贴 <code>mf://</code> 开头的模板分享码或完整模板 JSON，解析后可二次精细微调。</p>
              <textarea
                v-model="shareCodeInput"
                rows="6"
                class="liquid-textarea mono-area"
                :placeholder="t('templates.sharePlaceholder')"
              ></textarea>
              <button class="btn-liquid tactile-press full-w" @click="handleConfirmShareImport">
                <Check :size="15" />
                <span>{{ t('templates.shareConfirm') }}</span>
              </button>
            </div>

            <!-- Tab 2: build.prop 属性炼金 -->
            <div v-else-if="transferTab === 'alchemy'" class="transfer-pane">
              <input
                ref="propFileInputRef"
                type="file"
                accept=".prop,.txt,.conf,*/*"
                hidden
                @change="handlePropFileChange"
              />
              <button class="drop-btn tactile-press" @click="propFileInputRef?.click()">
                <FileUp :size="18" />
                <span>点击导入本地 build.prop 文件</span>
              </button>
              <textarea
                v-model="rawPropText"
                rows="6"
                class="liquid-textarea mono-area"
                placeholder="或直接粘贴 ro.product.model=... / ro.soc.model=... 属性内容"
              ></textarea>
              <button class="btn-liquid tactile-press full-w" @click="runPropAlchemy">
                <Sparkles :size="15" />
                <span>{{ t('alchemy.parseBtn') }}</span>
              </button>
            </div>

            <!-- Tab 3: 一键克隆本机 & 导出备份 -->
            <div v-else class="transfer-pane">
              <div class="tool-card">
                <div class="tool-copy">
                  <div class="tool-title">一键克隆当前真机为模板</div>
                  <div class="tool-desc">
                    直接采集本机真实 getprop、每簇 cpufreq 频点表、内存规格与 GPU 渲染器，一键铸造完整模板并生成分享码。
                  </div>
                </div>
                <button
                  class="btn-liquid tactile-press"
                  :disabled="cloningHost"
                  @click="handleCloneHostDevice"
                >
                  <Smartphone :size="15" />
                  <span>{{ cloningHost ? '正在提取...' : '立即克隆' }}</span>
                </button>
              </div>

              <div class="tool-card">
                <div class="tool-copy">
                  <div class="tool-title">导出全部模板与路由备份</div>
                  <div class="tool-desc">
                    导出完整 JSON 配置至 /sdcard/Download 目录，便于刷机换机后快速恢复。
                  </div>
                </div>
                <button class="btn-liquid-ghost tactile-press" @click="handleExportBackup">
                  <Download :size="15" />
                  <span>导出 JSON</span>
                </button>
              </div>
            </div>
          </div>
        </div>
      </div>
    </Transition>

    <!-- 6. 在线模板库弹窗 -->
    <Transition name="view-morph">
      <div v-if="showOnlineHubModal" class="modal-overlay" @click.self="showOnlineHubModal = false">
        <div class="modal-sheet liquid-glass liquid-glass-elevated">
          <div class="modal-header">
            <h3>🌐 {{ t('templates.hubModalTitle') }}</h3>
            <div class="hub-head-actions">
              <button
                class="btn-liquid-ghost tactile-press"
                :disabled="mirageStore.hubLoading"
                @click="fetchOnlineTemplateHub(true)"
              >
                <RefreshCw :size="14" :class="{ spinning: mirageStore.hubLoading }" />
                <span>{{ t('templates.hubRefreshBtn') }}</span>
              </button>
              <button class="icon-close tactile-press" @click="showOnlineHubModal = false">
                <X :size="18" />
              </button>
            </div>
          </div>

          <div class="hub-status-bar">
            <div class="hub-status-line">
              <span class="status-dot" :class="mirageStore.hubStatus.state"></span>
              <span v-if="mirageStore.hubStatus.state === 'loading'">{{ t('templates.hubSyncing') }}</span>
              <span v-else-if="mirageStore.hubStatus.state === 'online'">
                {{ t('templates.hubOnline', { src: mirageStore.hubStatus.source, count: mirageStore.hubStatus.remoteCount }) }}
              </span>
              <span v-else>
                {{ t('templates.hubOffline', { err: mirageStore.hubStatus.error || '离线模式' }) }}
              </span>
            </div>

            <div class="hub-custom-row">
              <Globe :size="14" class="spec-icon" />
              <input
                v-model="customHubInput"
                class="liquid-input hub-url-input"
                :placeholder="t('templates.hubCustomPlaceholder')"
              />
              <button class="btn-liquid-ghost tactile-press" @click="applyCustomHubSource">
                {{ t('templates.hubApplyUrl') }}
              </button>
            </div>
          </div>

          <div class="modal-body hub-list">
            <div v-for="item in mirageStore.hubTemplates" :key="item.id" class="hub-item">
              <div class="hub-info">
                <div class="hub-name">{{ item.name }}</div>
                <div class="hub-meta">
                  {{ item.BRAND }} {{ item.MODEL }} · {{ item.VK_DEVICE_NAME || item.GL_RENDERER }} · {{ item.RAM_GB }}GB {{ item.DDR_TYPE }} · {{ item.MAX_REFRESH_RATE || 120 }}Hz
                </div>
              </div>
              <button class="btn-liquid tactile-press" @click="importFromHub(item)">
                <Check v-if="mirageStore.templates[item.id]" :size="14" />
                <CloudDownload v-else :size="14" />
                <span>{{ mirageStore.templates[item.id] ? '已入库' : t('templates.hubImportBtn') }}</span>
              </button>
            </div>
          </div>
        </div>
      </div>
    </Transition>
  </section>
</template>

<style scoped>
.templates-view {
  display: flex;
  flex-direction: column;
  gap: 13px;
}

.top-action-bar {
  display: grid;
  grid-template-columns: repeat(3, 1fr);
  gap: 8px;
  padding: 8px;
  border-radius: 20px;
}

.bar-action {
  display: inline-flex;
  align-items: center;
  justify-content: center;
  gap: 6px;
  padding: 9px 8px;
  border-radius: 13px;
  font-size: 12px;
  font-weight: 700;
  color: var(--text-primary);
  background: var(--subcard-bg);
  border: 1px solid var(--glass-border);
}

.bar-action.primary {
  color: #fff;
  background: linear-gradient(135deg, var(--accent-primary), var(--accent-secondary));
  border-color: rgba(255, 255, 255, 0.3);
  box-shadow: 0 6px 16px -4px rgba(var(--accent-primary-rgb), 0.45);
}

.ico-cyan { color: var(--accent-secondary); }
.ico-violet { color: var(--accent-primary); }

.template-grid {
  display: grid;
  grid-template-columns: repeat(auto-fill, minmax(320px, 1fr));
  gap: 13px;
}

.tpl-card {
  padding: 15px;
  display: flex;
  flex-direction: column;
  gap: 11px;
}

.tpl-card-top {
  display: flex;
  justify-content: space-between;
  align-items: flex-start;
  gap: 8px;
}

.tpl-title-wrap {
  min-width: 0;
}

.tpl-name {
  font-size: 15px;
  font-weight: 800;
}

.tpl-sub {
  font-size: 11px;
  color: var(--text-secondary);
}

.gpu-pill {
  font-size: 10.5px;
  font-weight: 700;
  padding: 3px 8px;
  border-radius: 9px;
  background: rgba(var(--accent-secondary-rgb), 0.16);
  color: var(--accent-secondary);
  border: 1px solid rgba(var(--accent-secondary-rgb), 0.34);
  white-space: nowrap;
}

.spec-chips {
  display: grid;
  grid-template-columns: 1fr 1fr;
  gap: 7px;
}

.spec-item {
  display: flex;
  align-items: center;
  gap: 6px;
  font-size: 11px;
  padding: 6.5px 9px;
  border-radius: 11px;
  background: var(--subcard-bg);
  border: 1px solid var(--glass-border);
  white-space: nowrap;
  overflow: hidden;
  text-overflow: ellipsis;
}

.spec-icon {
  color: var(--accent-primary);
  flex-shrink: 0;
}

.cluster-visual-box {
  background: var(--subcard-bg);
  border: 1px solid var(--glass-border);
  border-radius: 13px;
  padding: 9px 11px;
}

.cluster-visual-title {
  display: flex;
  align-items: center;
  gap: 5px;
  font-size: 10.5px;
  font-weight: 700;
  color: var(--accent-secondary);
  margin-bottom: 6px;
}

.cluster-bars {
  display: flex;
  flex-direction: column;
  gap: 5px;
}

.bar-label {
  display: flex;
  justify-content: space-between;
  font-size: 10.5px;
  color: var(--text-secondary);
  margin-bottom: 2px;
}

.bar-label b {
  color: var(--text-primary);
}

.bar-track {
  height: 4.5px;
  border-radius: 4px;
  background: rgba(148, 163, 184, 0.2);
  overflow: hidden;
}

.bar-fill {
  height: 100%;
  border-radius: 4px;
  background: linear-gradient(90deg, var(--accent-primary), var(--accent-secondary));
}

.tpl-card-footer {
  display: flex;
  justify-content: space-between;
  align-items: center;
  gap: 8px;
  padding-top: 6px;
  border-top: 1px dashed var(--glass-border);
}

.foot-right-btns {
  display: flex;
  align-items: center;
  gap: 6px;
}

.foot-pill {
  display: inline-flex;
  align-items: center;
  gap: 5px;
  padding: 6.5px 10px;
  border-radius: 11px;
  font-size: 11.5px;
  font-weight: 700;
  color: var(--text-primary);
  background: var(--subcard-bg);
  border: 1px solid var(--glass-border);
}

.foot-pill.active-apps {
  color: var(--accent-secondary);
  background: rgba(var(--accent-secondary-rgb), 0.12);
  border-color: rgba(var(--accent-secondary-rgb), 0.32);
}

.count-dot {
  padding: 0 5px;
  border-radius: 6px;
  font-size: 10.5px;
  background: rgba(var(--accent-secondary-rgb), 0.22);
}

.foot-pill.edit {
  color: var(--accent-primary);
  background: rgba(var(--accent-primary-rgb), 0.14);
  border-color: rgba(var(--accent-primary-rgb), 0.34);
}

.foot-pill.danger {
  color: #f43f5e;
  background: rgba(244, 63, 94, 0.12);
  border-color: rgba(244, 63, 94, 0.3);
}

/* 弹窗通用样式 */
.modal-overlay {
  position: fixed;
  inset: 0;
  background: rgba(3, 5, 12, 0.68);
  backdrop-filter: blur(10px);
  display: flex;
  align-items: center;
  justify-content: center;
  padding: 14px;
  z-index: 185;
}

.modal-sheet {
  width: 100%;
  max-width: 580px;
  max-height: 85vh;
  display: flex;
  flex-direction: column;
  padding: 18px;
  border-radius: 28px;
  gap: 12px;
}

.modal-header {
  display: flex;
  justify-content: space-between;
  align-items: center;
  gap: 8px;
  padding-bottom: 10px;
  border-bottom: 1px solid var(--glass-border);
}

.hub-head-actions {
  display: flex;
  align-items: center;
  gap: 8px;
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
}

.transfer-tabs {
  display: grid;
  grid-template-columns: repeat(3, 1fr);
  gap: 6px;
  padding: 4px;
  border-radius: 14px;
  background: var(--subcard-bg);
  border: 1px solid var(--glass-border);
}

.t-tab {
  padding: 7px 6px;
  border-radius: 10px;
  font-size: 12px;
  font-weight: 700;
  color: var(--text-secondary);
  background: transparent;
  border: none;
}

.t-tab.active {
  color: #fff;
  background: linear-gradient(135deg, var(--accent-primary), var(--accent-secondary));
}

.modal-body {
  flex: 1;
  overflow-y: auto;
}

.transfer-pane {
  display: flex;
  flex-direction: column;
  gap: 11px;
}

.pane-desc {
  font-size: 12px;
  color: var(--text-secondary);
  line-height: 1.5;
}

.mono-area {
  font-family: ui-monospace, SFMono-Regular, Menlo, Consolas, monospace;
  font-size: 11.5px;
}

.full-w {
  width: 100%;
}

.drop-btn {
  display: flex;
  align-items: center;
  justify-content: center;
  gap: 8px;
  padding: 13px;
  border-radius: 14px;
  border: 1.5px dashed rgba(var(--accent-primary-rgb), 0.5);
  background: rgba(var(--accent-primary-rgb), 0.08);
  color: var(--accent-primary);
  font-size: 12.5px;
  font-weight: 700;
}

.tool-card {
  display: flex;
  justify-content: space-between;
  align-items: center;
  gap: 12px;
  padding: 13px;
  border-radius: 16px;
  background: var(--subcard-bg);
  border: 1px solid var(--glass-border);
}

.tool-copy {
  flex: 1;
  min-width: 0;
}

.tool-title {
  font-size: 13.5px;
  font-weight: 800;
}

.tool-desc {
  font-size: 11px;
  color: var(--text-secondary);
  margin-top: 3px;
  line-height: 1.45;
}

.hub-status-bar {
  padding: 10px 12px;
  border-radius: 14px;
  background: var(--subcard-bg);
  border: 1px solid var(--glass-border);
  display: flex;
  flex-direction: column;
  gap: 8px;
}

.hub-status-line {
  display: flex;
  align-items: center;
  gap: 8px;
  font-size: 11.5px;
  font-weight: 700;
  color: var(--text-secondary);
}

.status-dot {
  width: 8px;
  height: 8px;
  border-radius: 50%;
  background: #94a3b8;
  flex-shrink: 0;
}

.status-dot.online { background: #10b981; box-shadow: 0 0 8px rgba(16, 185, 129, 0.65); }
.status-dot.offline { background: #f59e0b; }
.status-dot.loading { background: var(--accent-secondary); }

.hub-custom-row {
  display: flex;
  align-items: center;
  gap: 7px;
}

.hub-url-input {
  flex: 1;
  padding: 6px 10px;
  font-size: 11.5px;
}

.hub-list {
  display: flex;
  flex-direction: column;
  gap: 9px;
}

.hub-item {
  display: flex;
  justify-content: space-between;
  align-items: center;
  gap: 12px;
  padding: 11px 13px;
  border-radius: 15px;
  background: var(--subcard-bg);
  border: 1px solid var(--glass-border);
}

.hub-name {
  font-size: 13px;
  font-weight: 800;
}

.hub-meta {
  font-size: 11px;
  color: var(--text-secondary);
  margin-top: 2px;
}

.spinning {
  animation: spin 1s linear infinite;
}

@keyframes spin {
  from { transform: rotate(0deg); }
  to { transform: rotate(360deg); }
}
</style>

<script setup lang="ts">
import { ref, watch } from 'vue';
import {
  Plus,
  Download,
  Edit3,
  Trash2,
  Cpu,
  HardDrive,
  Gauge,
  X,
  Check,
} from 'lucide-vue-next';
import { useI18n } from '../i18n';
import { execRootShell, showToast, triggerHaptic } from '../bridge/ksu-io';
import {
  CLI_BIN,
  mirageStore,
  parseClusterBars,
  saveTemplateViaCli,
  deleteTemplateViaCli,
  type DeviceTemplate,
} from '../store/mirageStore';

const props = defineProps<{
  incomingTemplate: DeviceTemplate | null;
}>();

const emit = defineEmits<{
  (e: 'clear-incoming'): void;
}>();

const { t } = useI18n();
const showModal = ref(false);
const isEditing = ref(false);

const emptyTemplate = (): DeviceTemplate => ({
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
  DEFAULT_DPI: 0,
  CUSTOM_PROPS: '',
});

const form = ref<DeviceTemplate>(emptyTemplate());

watch(
  () => props.incomingTemplate,
  (tpl) => {
    if (tpl) {
      form.value = { ...emptyTemplate(), ...tpl };
      isEditing.value = false;
      showModal.value = true;
      emit('clear-incoming');
    }
  },
  { immediate: true }
);

function openNewModal() {
  triggerHaptic('light');
  form.value = emptyTemplate();
  isEditing.value = false;
  showModal.value = true;
}

function openEditModal(tpl: DeviceTemplate) {
  triggerHaptic('light');
  form.value = { ...emptyTemplate(), ...tpl };
  isEditing.value = true;
  showModal.value = true;
}

function applyClusterPreset(
  clusters: string,
  maxFreq: number,
  socMachine: string,
  renderer: string,
  vendor: string
) {
  triggerHaptic('medium');
  form.value.CPU_CLUSTERS = clusters;
  form.value.CPU_MAX_FREQ = maxFreq;
  form.value.SOC_MACHINE = socMachine;
  form.value.SOC_MODEL = socMachine;
  form.value.GL_RENDERER = renderer;
  form.value.GL_VENDOR = vendor;
}

async function handleSave() {
  if (!form.value.name.trim() || !form.value.MODEL.trim()) {
    showToast('请填写模板名称与手机型号 (MODEL)');
    return;
  }
  await saveTemplateViaCli(form.value);
  showModal.value = false;
}

async function handleExportBackup() {
  triggerHaptic('medium');
  const outPath = `/sdcard/Download/MirageForge_v${mirageStore.version}_Backup.json`;
  await execRootShell(`"${CLI_BIN}" export "${outPath}"`);
  showToast(`已导出至 ${outPath}`);
}
</script>

<template>
  <section class="forge-section">
    <div class="section-bar">
      <h2>{{ t('templates.header') }}</h2>
      <div class="bar-btns">
        <button class="btn-liquid-ghost tactile-press" @click="handleExportBackup">
          <Download :size="15" />
          <span>{{ t('templates.exportBtn') }}</span>
        </button>
        <button class="btn-liquid tactile-press" @click="openNewModal">
          <Plus :size="16" />
          <span>{{ t('templates.newBtn') }}</span>
        </button>
      </div>
    </div>

    <!-- 模板液态玻璃网格 -->
    <div class="template-grid">
      <article
        v-for="tpl in Object.values(mirageStore.templates)"
        :key="tpl.id"
        class="tpl-card liquid-glass"
        v-touch-glass
      >
        <div class="tpl-card-top">
          <div>
            <h3 class="tpl-name">{{ tpl.name }}</h3>
            <span class="tpl-sub">
              {{ tpl.BRAND }} · {{ tpl.MODEL }} ({{ tpl.BOARD }} / {{ tpl.SOC_MODEL }})
            </span>
          </div>
          <span class="gpu-pill">{{ tpl.GL_RENDERER }}</span>
        </div>

        <div class="spec-chips">
          <div class="spec-item">
            <Cpu :size="14" class="spec-icon" />
            <span>{{ tpl.SOC_MACHINE || tpl.SOC_MODEL }} · {{ tpl.SECURITY_PATCH }}</span>
          </div>
          <div class="spec-item">
            <HardDrive :size="14" class="spec-icon" />
            <span>
              {{ tpl.RAM_GB }}GB {{ tpl.DDR_TYPE || 'LPDDR5X' }}
              <template v-if="tpl.DEFAULT_DPI > 0">· {{ tpl.DEFAULT_DPI }} DPI</template>
            </span>
          </div>
        </div>

        <div class="cluster-visual-box">
          <div class="cluster-visual-title">
            <Gauge :size="13" />
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

        <div class="tpl-card-footer">
          <button class="btn-liquid-ghost tactile-press" @click="openEditModal(tpl)">
            <Edit3 :size="14" />
            <span>{{ t('templates.editBtn') }}</span>
          </button>
          <button class="btn-liquid-danger tactile-press" @click="deleteTemplateViaCli(tpl.id)">
            <Trash2 :size="14" />
            <span>{{ t('templates.deleteBtn') }}</span>
          </button>
        </div>
      </article>
    </div>

    <!-- 液态玻璃模态编辑窗 (含新增的深度 Build 字段与默认 DPI) -->
    <Transition name="view-morph">
      <div v-if="showModal" class="modal-overlay" @click.self="showModal = false">
        <div class="modal-sheet liquid-glass liquid-glass-elevated">
          <div class="modal-header">
            <h3>{{ isEditing ? t('templates.modalTitleEdit') : t('templates.modalTitleNew') }}</h3>
            <button class="icon-close tactile-press" @click="showModal = false">
              <X :size="18" />
            </button>
          </div>

          <div class="modal-body">
            <div class="sec-divider">{{ t('templates.secIdentity') }}</div>
            <div class="form-grid">
              <label class="span-2">
                <span>模板显示名称 *</span>
                <input v-model="form.name" class="liquid-input" placeholder="如：Xiaomi 15 Pro (16GB LPDDR5X)" />
              </label>
              <label>
                <span>MODEL (手机型号) *</span>
                <input v-model="form.MODEL" class="liquid-input" placeholder="2410DPN6CC" />
              </label>
              <label>
                <span>BRAND (品牌) *</span>
                <input v-model="form.BRAND" class="liquid-input" placeholder="Xiaomi" />
              </label>
              <label>
                <span>MANUFACTURER (制造商)</span>
                <input v-model="form.MANUFACTURER" class="liquid-input" placeholder="Xiaomi" />
              </label>
              <label>
                <span>DEVICE (设备代号/SKU)</span>
                <input v-model="form.DEVICE" class="liquid-input" placeholder="haotian" />
              </label>
              <label>
                <span>PRODUCT (产品名)</span>
                <input v-model="form.PRODUCT" class="liquid-input" placeholder="haotian" />
              </label>
              <label>
                <span>Build.BOARD (主板代号)</span>
                <input v-model="form.BOARD" class="liquid-input" placeholder="sun" />
              </label>
              <label>
                <span>Build.SOC_MODEL (SoC 型号)</span>
                <input v-model="form.SOC_MODEL" class="liquid-input" placeholder="SM8750-AB" />
              </label>
              <label>
                <span>SOC_MANUFACTURER</span>
                <input v-model="form.SOC_MANUFACTURER" class="liquid-input" placeholder="Qualcomm" />
              </label>
              <label>
                <span>BUILD_ID (构建号)</span>
                <input v-model="form.BUILD_ID" class="liquid-input" placeholder="AP3A.240905.015.A2" />
              </label>
              <label>
                <span>SECURITY_PATCH (安全补丁日期)</span>
                <input v-model="form.SECURITY_PATCH" class="liquid-input" placeholder="2025-02-01" />
              </label>
              <label>
                <span>DISPLAY_ID (Build.DISPLAY 版本显示号)</span>
                <input v-model="form.DISPLAY_ID" class="liquid-input" placeholder="OS2.0.102.0.VOBCNXM release-keys" />
              </label>
              <label>
                <span>INCREMENTAL (VERSION.INCREMENTAL)</span>
                <input v-model="form.INCREMENTAL" class="liquid-input" placeholder="OS2.0.102.0.VOBCNXM" />
              </label>
              <label>
                <span>BUILD_HOST (编译主机名)</span>
                <input v-model="form.BUILD_HOST" class="liquid-input" placeholder="pangu-build-component-system" />
              </label>
              <label>
                <span>BUILD_USER (编译用户名)</span>
                <input v-model="form.BUILD_USER" class="liquid-input" placeholder="builder" />
              </label>
              <label>
                <span>RELEASE (Android 版本)</span>
                <input v-model="form.RELEASE" class="liquid-input" placeholder="15" />
              </label>
              <label>
                <span>SDK_INT (API 级别)</span>
                <input v-model="form.SDK_INT" class="liquid-input" placeholder="35" />
              </label>
              <label class="span-2">
                <span>FINGERPRINT (完整设备指纹)</span>
                <input v-model="form.FINGERPRINT" class="liquid-input" placeholder="Xiaomi/haotian/..." />
              </label>
            </div>

            <div class="sec-divider">{{ t('templates.secGraphics') }}</div>
            <div class="form-grid">
              <label>
                <span>GL_VENDOR / PCI Vendor</span>
                <input v-model="form.GL_VENDOR" class="liquid-input" placeholder="Qualcomm 或 ARM" />
              </label>
              <label>
                <span>GL_RENDERER / Vulkan DeviceName</span>
                <input v-model="form.GL_RENDERER" class="liquid-input" placeholder="Adreno (TM) 830" />
              </label>
              <label class="span-2">
                <span>GL_VERSION</span>
                <input v-model="form.GL_VERSION" class="liquid-input" placeholder="OpenGL ES 3.2 V@0800.0" />
              </label>
            </div>

            <div class="sec-divider">{{ t('templates.secVfs') }}</div>
            <div class="form-grid">
              <div class="span-2 preset-panel">
                <div class="preset-panel-head">
                  <span>{{ t('templates.clusterPresetTitle') }}</span>
                  <div class="preset-pills">
                    <button
                      type="button"
                      class="preset-pill tactile-press"
                      @click="applyClusterPreset('0-5:614400:3532800;6-7:806400:4320000', 4320000, 'SM8750', 'Adreno (TM) 830', 'Qualcomm')"
                    >
                      骁龙8至尊版 (6+2)
                    </button>
                    <button
                      type="button"
                      class="preset-pill tactile-press"
                      @click="applyClusterPreset('0-1:556800:2265600;2-6:691200:3148800;7-7:806400:3302400', 3302400, 'SM8650', 'Adreno (TM) 750', 'Qualcomm')"
                    >
                      骁龙8 Gen3 (2+5+1)
                    </button>
                    <button
                      type="button"
                      class="preset-pill tactile-press"
                      @click="applyClusterPreset('0-3:800000:2400000;4-6:1000000:3300000;7-7:1200000:3620000', 3620000, 'MT6991', 'Immortalis-G925 MC12', 'ARM')"
                    >
                      天玑9400 (4+3+1)
                    </button>
                  </div>
                </div>
                <input v-model="form.CPU_CLUSTERS" class="liquid-input" />
              </div>

              <label class="span-2">
                <span>/proc/cpuinfo Hardware</span>
                <input v-model="form.CPU_HARDWARE" class="liquid-input" />
              </label>
              <label>
                <span>RAM_GB (对称物理内存 GB)</span>
                <input v-model.number="form.RAM_GB" type="number" class="liquid-input" />
              </label>
              <label>
                <span>DDR_TYPE (内存规格)</span>
                <input v-model="form.DDR_TYPE" class="liquid-input" placeholder="LPDDR5X / LPDDR5T" />
              </label>
              <label>
                <span>DDR_VENDOR (颗粒厂商)</span>
                <input v-model="form.DDR_VENDOR" class="liquid-input" placeholder="SKhynix / Samsung" />
              </label>
              <label>
                <span>DEFAULT_DPI (模板默认 DPI, 0=不改)</span>
                <input v-model.number="form.DEFAULT_DPI" type="number" class="liquid-input" placeholder="0 或 480" />
              </label>
              <label class="span-2">
                <span>附加跨品牌与受限属性池 (KEY=VALUE 每行一条)</span>
                <textarea v-model="form.CUSTOM_PROPS" rows="4" class="liquid-textarea"></textarea>
              </label>
            </div>
          </div>

          <div class="modal-footer">
            <button class="btn-liquid-ghost tactile-press" @click="showModal = false">
              {{ t('templates.cancelBtn') }}
            </button>
            <button class="btn-liquid tactile-press" @click="handleSave">
              <Check :size="16" />
              <span>{{ t('templates.saveBtn') }}</span>
            </button>
          </div>
        </div>
      </div>
    </Transition>
  </section>
</template>

<style scoped>
.section-bar {
  display: flex;
  justify-content: space-between;
  align-items: center;
  margin-bottom: 14px;
  gap: 10px;
}

.section-bar h2 {
  font-size: 16px;
  font-weight: 800;
}

.bar-btns {
  display: flex;
  gap: 8px;
}

.template-grid {
  display: grid;
  grid-template-columns: repeat(auto-fill, minmax(330px, 1fr));
  gap: 14px;
}

.tpl-card {
  padding: 16px;
  display: flex;
  flex-direction: column;
  gap: 12px;
}

.tpl-card-top {
  display: flex;
  justify-content: space-between;
  align-items: flex-start;
  gap: 8px;
}

.tpl-name {
  font-size: 15.5px;
  font-weight: 800;
}

.tpl-sub {
  font-size: 11.5px;
  color: var(--text-secondary);
}

.gpu-pill {
  font-size: 11px;
  font-weight: 700;
  padding: 3px 9px;
  border-radius: 10px;
  background: rgba(6, 182, 212, 0.16);
  color: var(--accent-secondary);
  border: 1px solid rgba(6, 182, 212, 0.35);
  white-space: nowrap;
}

.spec-chips {
  display: grid;
  grid-template-columns: 1fr 1fr;
  gap: 8px;
}

.spec-item {
  display: flex;
  align-items: center;
  gap: 6px;
  font-size: 11.5px;
  padding: 7px 10px;
  border-radius: 12px;
  background: var(--subcard-bg);
  border: 1px solid var(--glass-border);
}

.spec-icon {
  color: var(--accent-primary);
  flex-shrink: 0;
}

.cluster-visual-box {
  background: var(--subcard-bg);
  border: 1px solid var(--glass-border);
  border-radius: 14px;
  padding: 10px 12px;
}

.cluster-visual-title {
  display: flex;
  align-items: center;
  gap: 6px;
  font-size: 11px;
  font-weight: 700;
  color: var(--accent-secondary);
  margin-bottom: 7px;
}

.cluster-bars {
  display: flex;
  flex-direction: column;
  gap: 6px;
}

.bar-label {
  display: flex;
  justify-content: space-between;
  font-size: 11px;
  color: var(--text-secondary);
  margin-bottom: 3px;
}

.bar-label b {
  color: var(--text-primary);
}

.bar-track {
  height: 5px;
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
  justify-content: flex-end;
  gap: 8px;
}

.modal-overlay {
  position: fixed;
  inset: 0;
  background: rgba(3, 5, 12, 0.65);
  backdrop-filter: blur(10px);
  display: flex;
  align-items: center;
  justify-content: center;
  padding: 14px;
  z-index: 150;
}

.modal-sheet {
  width: 100%;
  max-width: 640px;
  max-height: 88vh;
  display: flex;
  flex-direction: column;
  padding: 18px;
  border-radius: 28px;
}

.modal-header {
  display: flex;
  justify-content: space-between;
  align-items: center;
  padding-bottom: 12px;
  border-bottom: 1px solid var(--glass-border);
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

.modal-body {
  overflow-y: auto;
  padding: 10px 2px;
  flex: 1;
}

.sec-divider {
  font-size: 12px;
  font-weight: 800;
  color: var(--accent-secondary);
  margin: 14px 0 8px;
}

.form-grid {
  display: grid;
  grid-template-columns: 1fr 1fr;
  gap: 9px;
}

.form-grid label {
  display: flex;
  flex-direction: column;
  gap: 4px;
  font-size: 11px;
  color: var(--text-secondary);
}

.span-2 {
  grid-column: 1 / -1;
}

.preset-panel {
  background: rgba(139, 92, 246, 0.08);
  border: 1px solid rgba(139, 92, 246, 0.28);
  border-radius: 14px;
  padding: 10px;
  display: flex;
  flex-direction: column;
  gap: 7px;
}

.preset-panel-head {
  display: flex;
  justify-content: space-between;
  align-items: center;
  flex-wrap: wrap;
  gap: 6px;
  font-size: 11px;
  color: var(--accent-primary);
  font-weight: 700;
}

.preset-pills {
  display: flex;
  gap: 6px;
  flex-wrap: wrap;
}

.preset-pill {
  background: rgba(6, 182, 212, 0.16);
  color: var(--accent-secondary);
  border: 1px solid rgba(6, 182, 212, 0.35);
  border-radius: 8px;
  padding: 3px 8px;
  font-size: 10.5px;
  font-weight: 700;
}

.modal-footer {
  display: flex;
  justify-content: flex-end;
  gap: 10px;
  padding-top: 12px;
  border-top: 1px solid var(--glass-border);
}
</style>

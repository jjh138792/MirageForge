<!--
SPDX-License-Identifier: GPL-3.0-only
Copyright (c) 2025-2026 jjh138792
MirageForge: https://github.com/jjh138792/MirageForge
-->

<script setup lang="ts">
import { ref, computed, watch, nextTick } from 'vue';
import {
  ArrowLeft,
  Check,
  Smartphone,
  Cpu,
  Gauge,
  HardDrive,
  Code2,
  ChevronRight,
  AlertTriangle,
  Layers,
} from 'lucide-vue-next';
import { triggerHaptic, showToast } from '../bridge/ksu-io';
import {
  mirageStore,
  normalizeTemplateVulkanFields,
  saveTemplateViaCli,
  type DeviceTemplate,
} from '../store/mirageStore';
import { auditTemplateConsistency } from '../utils/templateAudit';

type CategoryKey = 'system' | 'cpu' | 'gpu' | 'memory' | 'custom';

const props = defineProps<{
  template: DeviceTemplate | null;
  isNew: boolean;
}>();

const emit = defineEmits<{
  (e: 'close'): void;
  (e: 'open-active-apps', tpl: DeviceTemplate): void;
}>();

const form = ref<DeviceTemplate | null>(null);

const activeCategory = ref<CategoryKey | null>(null);
const originRect = ref<{ top: number; left: number; width: number; height: number } | null>(null);
const flipPhase = ref<'from' | 'expanded' | 'collapsing'>('from');
const contentVisible = ref(false);
const cardElMap = new Map<CategoryKey, HTMLElement>();

watch(
  () => props.template,
  (tpl) => {
    if (tpl) {
      form.value = normalizeTemplateVulkanFields({ ...tpl });
      activeCategory.value = null;
      flipPhase.value = 'from';
      contentVisible.value = false;
    } else {
      form.value = null;
    }
  },
  { immediate: true }
);

const auditIssues = computed(() => (form.value ? auditTemplateConsistency(form.value) : []));

const boundAppCount = computed(() => {
  if (!form.value) return 0;
  return Object.values(mirageStore.appRoutes).filter((r) => r.tplId === form.value!.id).length;
});

const categoryCards = computed(() => {
  const f = form.value;
  if (!f) return [];
  const customLines = (f.CUSTOM_PROPS || '')
    .split(/\r?\n/)
    .map((s) => s.trim())
    .filter((s) => s && !s.startsWith('#')).length;

  return [
    {
      key: 'system' as CategoryKey,
      title: '系统属性与 Build',
      summary: `${f.BRAND || '-'} · ${f.MODEL || '-'} · Android ${f.RELEASE} (SDK ${f.SDK_INT})`,
      icon: Smartphone,
      accent: 'violet',
    },
    {
      key: 'cpu' as CategoryKey,
      title: 'CPU 拓扑与频率',
      summary: `${f.SOC_MODEL || f.BOARD || '-'} · ${f.CPU_CORES} 核 · 最高 ${(f.CPU_MAX_FREQ / 1e6).toFixed(2)} GHz`,
      icon: Cpu,
      accent: 'cyan',
    },
    {
      key: 'gpu' as CategoryKey,
      title: 'GPU 与 Vulkan 驱动',
      summary: `${f.VK_DEVICE_NAME || f.GL_RENDERER} · VK ${f.VK_API_VERSION} · ${f.MAX_REFRESH_RATE || 120}Hz`,
      icon: Gauge,
      accent: 'emerald',
    },
    {
      key: 'memory' as CategoryKey,
      title: '内存与内核信息',
      summary: `${f.RAM_GB}GB ${f.DDR_TYPE} (${f.DDR_VENDOR}) · ${f.KERNEL_RELEASE.split('-')[0] || 'Linux'}`,
      icon: HardDrive,
      accent: 'amber',
    },
    {
      key: 'custom' as CategoryKey,
      title: '自定义属性池',
      summary: customLines > 0 ? `已配置 ${customLines} 条附加 OEM / 受限属性` : '点击追加跨品牌属性 (KEY=VALUE)',
      icon: Code2,
      accent: 'rose',
    },
  ];
});

const activeCategoryTitle = computed(() => {
  const found = categoryCards.value.find((c) => c.key === activeCategory.value);
  return found ? found.title : '';
});

function setCardRef(key: CategoryKey, el: any) {
  if (el instanceof HTMLElement) {
    cardElMap.set(key, el);
  }
}

async function openCategoryWithFlip(key: CategoryKey, ev: MouseEvent) {
  triggerHaptic('medium');
  const el = (ev.currentTarget as HTMLElement) || cardElMap.get(key);
  if (el) {
    const rect = el.getBoundingClientRect();
    originRect.value = {
      top: rect.top,
      left: rect.left,
      width: rect.width,
      height: rect.height,
    };
  } else {
    originRect.value = {
      top: window.innerHeight * 0.35,
      left: 16,
      width: window.innerWidth - 32,
      height: 84,
    };
  }

  flipPhase.value = 'from';
  contentVisible.value = false;
  activeCategory.value = key;

  await nextTick();
  requestAnimationFrame(() => {
    requestAnimationFrame(() => {
      flipPhase.value = 'expanded';
      setTimeout(() => {
        if (flipPhase.value === 'expanded') contentVisible.value = true;
      }, 95);
    });
  });
}

function closeCategoryWithFlip() {
  if (!activeCategory.value || flipPhase.value === 'collapsing') return;
  triggerHaptic('light');

  const targetEl = cardElMap.get(activeCategory.value);
  if (targetEl) {
    const rect = targetEl.getBoundingClientRect();
    originRect.value = {
      top: rect.top,
      left: rect.left,
      width: rect.width,
      height: rect.height,
    };
  }

  contentVisible.value = false;
  flipPhase.value = 'collapsing';

  setTimeout(() => {
    activeCategory.value = null;
    flipPhase.value = 'from';
  }, 330);
}

const flipPanelStyle = computed(() => {
  const r = originRect.value;
  if (!r || flipPhase.value === 'expanded') {
    return {
      top: '0px',
      left: '0px',
      width: '100%',
      height: '100%',
      borderRadius: '0px',
      opacity: 1,
    };
  }
  return {
    top: `${r.top}px`,
    left: `${r.left}px`,
    width: `${r.width}px`,
    height: `${r.height}px`,
    borderRadius: '22px',
    opacity: flipPhase.value === 'collapsing' ? 0.15 : 0.96,
  };
});

function applyVulkanPreset(kind: 'adreno830' | 'adreno750' | 'g925' | 'xclipse940') {
  if (!form.value) return;
  triggerHaptic('medium');
  if (kind === 'adreno830') {
    form.value.GL_VENDOR = 'Qualcomm';
    form.value.GL_RENDERER = 'Adreno (TM) 830';
    form.value.GL_VERSION = 'OpenGL ES 3.2 V@0800.0';
    form.value.VK_DEVICE_NAME = 'Adreno (TM) 830';
    form.value.VK_VENDOR_ID = '0x5143';
    form.value.VK_DEVICE_ID = '0x43050A01';
    form.value.VK_API_VERSION = '1.3.295';
    form.value.VK_DRIVER_VERSION = '512.800.0';
    form.value.VK_DRIVER_NAME = 'Qualcomm Technologies Inc. Adreno Vulkan Driver';
    form.value.VK_DRIVER_INFO = 'Driver Build: 0800.0';
  } else if (kind === 'adreno750') {
    form.value.GL_VENDOR = 'Qualcomm';
    form.value.GL_RENDERER = 'Adreno (TM) 750';
    form.value.GL_VERSION = 'OpenGL ES 3.2 V@0762.0';
    form.value.VK_DEVICE_NAME = 'Adreno (TM) 750';
    form.value.VK_VENDOR_ID = '0x5143';
    form.value.VK_DEVICE_ID = '0x43050000';
    form.value.VK_API_VERSION = '1.3.279';
    form.value.VK_DRIVER_VERSION = '512.762.0';
    form.value.VK_DRIVER_NAME = 'Qualcomm Technologies Inc. Adreno Vulkan Driver';
    form.value.VK_DRIVER_INFO = 'Driver Build: 0762.0';
  } else if (kind === 'g925') {
    form.value.GL_VENDOR = 'ARM';
    form.value.GL_RENDERER = 'Immortalis-G925 MC12';
    form.value.GL_VERSION = 'OpenGL ES 3.2 v1.r49p0-01eac0';
    form.value.VK_DEVICE_NAME = 'Immortalis-G925 MC12';
    form.value.VK_VENDOR_ID = '0x13B5';
    form.value.VK_DEVICE_ID = '0x12870000';
    form.value.VK_API_VERSION = '1.3.295';
    form.value.VK_DRIVER_VERSION = '49.0.0';
    form.value.VK_DRIVER_NAME = 'ARM';
    form.value.VK_DRIVER_INFO = 'r49p0-01eac0';
  } else {
    form.value.GL_VENDOR = 'Samsung Electronics';
    form.value.GL_RENDERER = 'Samsung Xclipse 940';
    form.value.GL_VERSION = 'OpenGL ES 3.2 ANGLE';
    form.value.VK_DEVICE_NAME = 'Samsung Xclipse 940';
    form.value.VK_VENDOR_ID = '0x144D';
    form.value.VK_DEVICE_ID = '0x73A00000';
    form.value.VK_API_VERSION = '1.3.283';
    form.value.VK_DRIVER_VERSION = '24.1.0';
    form.value.VK_DRIVER_NAME = 'Samsung Electronics';
    form.value.VK_DRIVER_INFO = 'Xclipse Vulkan Driver 1.3';
  }
}

function applyClusterPreset(
  clusters: string,
  maxFreq: number,
  socMachine: string,
  gpuKind: 'adreno830' | 'adreno750' | 'g925'
) {
  if (!form.value) return;
  triggerHaptic('medium');
  form.value.CPU_CLUSTERS = clusters;
  form.value.CPU_MAX_FREQ = maxFreq;
  form.value.CPU_CORES = 8;
  form.value.SOC_MACHINE = socMachine;
  form.value.SOC_MODEL = socMachine;
  applyVulkanPreset(gpuKind);
}

async function handleSaveTemplate() {
  if (!form.value) return;
  if (!form.value.name.trim() || !form.value.MODEL.trim()) {
    showToast('请先填写模板名称与手机型号 (MODEL)');
    return;
  }
  await saveTemplateViaCli(form.value);
  emit('close');
}
</script>

<template>
  <Teleport to="body">
    <Transition name="page-push">
      <div v-if="form" class="editor-fullscreen">
        <!-- 1. 分类编辑总览页顶部栏 -->
        <header class="editor-topbar liquid-glass liquid-glass-elevated">
          <button class="back-circle tactile-press" @click="emit('close')" aria-label="返回">
            <ArrowLeft :size="19" />
          </button>

          <div class="topbar-title-box">
            <h2>{{ isNew ? '新建硬件模板' : '编辑硬件模板' }}</h2>
            <span class="topbar-sub">点击下方分类展开全屏精细编辑</span>
          </div>

          <button class="btn-liquid tactile-press" @click="handleSaveTemplate">
            <Check :size="15" />
            <span>保存</span>
          </button>
        </header>

        <!-- 2. 分类总览流畅滚动主体 -->
        <div class="editor-hub-scroll">
          <div class="name-card liquid-glass" v-touch-glass>
            <label class="name-label">
              <span>模板显示名称 *</span>
              <input
                v-model="form.name"
                class="liquid-input"
                placeholder="例如：Xiaomi 15 Pro (骁龙8至尊版 / 16GB)"
              />
            </label>
            <div class="quick-id-row">
              <label>
                <span>模板 ID</span>
                <input v-model="form.id" class="liquid-input" :disabled="!isNew" />
              </label>
              <label>
                <span>MODEL (型号) *</span>
                <input v-model="form.MODEL" class="liquid-input" placeholder="2410DPN6CC" />
              </label>
            </div>
          </div>

          <div v-if="auditIssues.length > 0" class="audit-card liquid-glass">
            <div class="audit-head">
              <AlertTriangle :size="14" />
              <span>硬件一致性体检发现 {{ auditIssues.length }} 项提示</span>
            </div>
            <ul class="audit-list">
              <li v-for="(iss, idx) in auditIssues" :key="idx" :class="iss.level">
                {{ iss.message }}
              </li>
            </ul>
          </div>

          <div class="category-grid">
            <button
              v-for="cat in categoryCards"
              :key="cat.key"
              :ref="(el) => setCardRef(cat.key, el)"
              class="cat-card liquid-glass tactile-press"
              v-touch-glass
              @click="openCategoryWithFlip(cat.key, $event)"
            >
              <div class="cat-icon-box" :class="cat.accent">
                <component :is="cat.icon" :size="20" />
              </div>
              <div class="cat-copy">
                <div class="cat-title">{{ cat.title }}</div>
                <div class="cat-summary">{{ cat.summary }}</div>
              </div>
              <ChevronRight :size="17" class="cat-arrow" />
            </button>

            <button
              v-if="!isNew"
              class="cat-card liquid-glass tactile-press"
              v-touch-glass
              @click="emit('open-active-apps', form)"
            >
              <div class="cat-icon-box teal">
                <Layers :size="20" />
              </div>
              <div class="cat-copy">
                <div class="cat-title">生效应用</div>
                <div class="cat-summary">
                  {{ boundAppCount > 0 ? `当前有 ${boundAppCount} 款应用使用此模板` : '暂无应用绑定此模板' }}
                </div>
              </div>
              <ChevronRight :size="17" class="cat-arrow" />
            </button>
          </div>
        </div>

        <!-- 3. FLIP 全屏张开动画子页面 -->
        <div
          v-if="activeCategory"
          class="flip-expand-sheet"
          :style="flipPanelStyle"
        >
          <div class="flip-inner" :class="{ visible: contentVisible }">
            <header class="flip-topbar liquid-glass liquid-glass-elevated">
              <button class="back-circle tactile-press" @click="closeCategoryWithFlip" aria-label="返回分类">
                <ArrowLeft :size="19" />
              </button>
              <div class="topbar-title-box">
                <h2>{{ activeCategoryTitle }}</h2>
                <span class="topbar-sub">{{ form.name || form.MODEL || '未命名模板' }}</span>
              </div>
              <button class="btn-liquid-ghost tactile-press" @click="closeCategoryWithFlip">
                <Check :size="15" />
                <span>完成</span>
              </button>
            </header>

            <div class="flip-form-scroll">
              <div v-if="activeCategory === 'system'" class="form-card liquid-glass">
                <div class="form-grid">
                  <label>
                    <span>MODEL (手机型号) *</span>
                    <input v-model="form.MODEL" class="liquid-input" placeholder="2410DPN6CC" />
                  </label>
                  <label>
                    <span>BRAND (品牌)</span>
                    <input v-model="form.BRAND" class="liquid-input" placeholder="Xiaomi" />
                  </label>
                  <label>
                    <span>MANUFACTURER (制造商)</span>
                    <input v-model="form.MANUFACTURER" class="liquid-input" placeholder="Xiaomi" />
                  </label>
                  <label>
                    <span>DEVICE (设备代号 / SKU)</span>
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
                    <span>HARDWARE (硬件平台)</span>
                    <input v-model="form.HARDWARE" class="liquid-input" placeholder="qcom" />
                  </label>
                  <label>
                    <span>BOOTLOADER</span>
                    <input v-model="form.BOOTLOADER" class="liquid-input" placeholder="unknown" />
                  </label>
                  <label>
                    <span>BUILD_ID (构建号)</span>
                    <input v-model="form.BUILD_ID" class="liquid-input" placeholder="AP3A.240905.015.A2" />
                  </label>
                  <label>
                    <span>SECURITY_PATCH (安全补丁)</span>
                    <input v-model="form.SECURITY_PATCH" class="liquid-input" placeholder="2025-02-01" />
                  </label>
                  <label>
                    <span>DISPLAY_ID (显示版本)</span>
                    <input v-model="form.DISPLAY_ID" class="liquid-input" />
                  </label>
                  <label>
                    <span>INCREMENTAL (增量版本)</span>
                    <input v-model="form.INCREMENTAL" class="liquid-input" />
                  </label>
                  <label>
                    <span>BUILD_HOST (编译主机)</span>
                    <input v-model="form.BUILD_HOST" class="liquid-input" />
                  </label>
                  <label>
                    <span>BUILD_USER (编译用户)</span>
                    <input v-model="form.BUILD_USER" class="liquid-input" />
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
                    <span>SUPPORTED_ABIS (指令集架构)</span>
                    <input v-model="form.SUPPORTED_ABIS" class="liquid-input" />
                  </label>
                  <label class="span-2">
                    <span>FINGERPRINT (完整设备指纹)</span>
                    <input v-model="form.FINGERPRINT" class="liquid-input" />
                  </label>
                </div>
              </div>

              <div v-else-if="activeCategory === 'cpu'" class="form-card liquid-glass">
                <div class="preset-box">
                  <div class="preset-title">一键套用旗舰 SoC 分簇与 GPU 联动配置</div>
                  <div class="preset-pills">
                    <button
                      type="button"
                      class="preset-pill tactile-press"
                      @click="applyClusterPreset('0-5:614400:3532800;6-7:806400:4320000', 4320000, 'SM8750', 'adreno830')"
                    >
                      骁龙 8 至尊版 (6+2)
                    </button>
                    <button
                      type="button"
                      class="preset-pill tactile-press"
                      @click="applyClusterPreset('0-1:556800:2265600;2-6:691200:3148800;7-7:806400:3302400', 3302400, 'SM8650', 'adreno750')"
                    >
                      骁龙 8 Gen3 (2+5+1)
                    </button>
                    <button
                      type="button"
                      class="preset-pill tactile-press"
                      @click="applyClusterPreset('0-3:800000:2400000;4-6:1000000:3300000;7-7:1200000:3620000', 3620000, 'MT6991', 'g925')"
                    >
                      天玑 9400 (4+3+1)
                    </button>
                  </div>
                </div>

                <div class="form-grid">
                  <label>
                    <span>SOC_MANUFACTURER (SoC 厂商)</span>
                    <input v-model="form.SOC_MANUFACTURER" class="liquid-input" placeholder="Qualcomm" />
                  </label>
                  <label>
                    <span>SOC_MODEL (Build.SOC_MODEL)</span>
                    <input v-model="form.SOC_MODEL" class="liquid-input" placeholder="SM8750-AB" />
                  </label>
                  <label>
                    <span>SOC_MACHINE (/sys/soc0/machine)</span>
                    <input v-model="form.SOC_MACHINE" class="liquid-input" placeholder="SM8750" />
                  </label>
                  <label>
                    <span>SOC_ID (/sys/soc0/soc_id)</span>
                    <input v-model="form.SOC_ID" class="liquid-input" placeholder="618" />
                  </label>
                  <label class="span-2">
                    <span>CPU_HARDWARE (/proc/cpuinfo Hardware)</span>
                    <input v-model="form.CPU_HARDWARE" class="liquid-input" />
                  </label>
                  <label>
                    <span>CPU_CORES (物理核心数)</span>
                    <input v-model.number="form.CPU_CORES" type="number" class="liquid-input" />
                  </label>
                  <label>
                    <span>CPU_MAX_FREQ (峰值主频 kHz)</span>
                    <input v-model.number="form.CPU_MAX_FREQ" type="number" class="liquid-input" />
                  </label>
                  <label class="span-2">
                    <span>CPU_CLUSTERS (异构分簇：起始-结束:最小kHz:最大kHz)</span>
                    <input v-model="form.CPU_CLUSTERS" class="liquid-input" />
                  </label>
                </div>
              </div>

              <div v-else-if="activeCategory === 'gpu'" class="form-card liquid-glass">
                <div class="preset-box">
                  <div class="preset-title">一键填充旗舰 OpenGL ES + Vulkan 独立驱动配置</div>
                  <div class="preset-pills">
                    <button type="button" class="preset-pill tactile-press" @click="applyVulkanPreset('adreno830')">
                      Adreno 830
                    </button>
                    <button type="button" class="preset-pill tactile-press" @click="applyVulkanPreset('adreno750')">
                      Adreno 750
                    </button>
                    <button type="button" class="preset-pill tactile-press" @click="applyVulkanPreset('g925')">
                      Immortalis-G925
                    </button>
                    <button type="button" class="preset-pill tactile-press" @click="applyVulkanPreset('xclipse940')">
                      Xclipse 940
                    </button>
                  </div>
                </div>

                <div class="form-grid">
                  <label>
                    <span>GL_VENDOR (OpenGL 厂商)</span>
                    <input v-model="form.GL_VENDOR" class="liquid-input" />
                  </label>
                  <label>
                    <span>GL_RENDERER (OpenGL 渲染器)</span>
                    <input v-model="form.GL_RENDERER" class="liquid-input" />
                  </label>
                  <label>
                    <span>GL_VERSION (OpenGL 版本)</span>
                    <input v-model="form.GL_VERSION" class="liquid-input" />
                  </label>
                  <label>
                    <span>MAX_REFRESH_RATE (屏幕默认高刷 Hz)</span>
                    <input v-model.number="form.MAX_REFRESH_RATE" type="number" class="liquid-input" />
                  </label>
                  <label>
                    <span>VK_DEVICE_NAME (Vulkan 设备名)</span>
                    <input v-model="form.VK_DEVICE_NAME" class="liquid-input" />
                  </label>
                  <label>
                    <span>VK_API_VERSION (Vulkan API)</span>
                    <input v-model="form.VK_API_VERSION" class="liquid-input" />
                  </label>
                  <label>
                    <span>VK_VENDOR_ID (16 进制)</span>
                    <input v-model="form.VK_VENDOR_ID" class="liquid-input" />
                  </label>
                  <label>
                    <span>VK_DEVICE_ID (16 进制)</span>
                    <input v-model="form.VK_DEVICE_ID" class="liquid-input" />
                  </label>
                  <label>
                    <span>VK_DRIVER_VERSION (驱动版本)</span>
                    <input v-model="form.VK_DRIVER_VERSION" class="liquid-input" />
                  </label>
                  <label>
                    <span>VK_DRIVER_INFO (构建信息)</span>
                    <input v-model="form.VK_DRIVER_INFO" class="liquid-input" />
                  </label>
                  <label class="span-2">
                    <span>VK_DRIVER_NAME (Vulkan 驱动全名)</span>
                    <input v-model="form.VK_DRIVER_NAME" class="liquid-input" />
                  </label>
                </div>
              </div>

              <div v-else-if="activeCategory === 'memory'" class="form-card liquid-glass">
                <div class="form-grid">
                  <label>
                    <span>RAM_GB (对称物理内存 GB)</span>
                    <input v-model.number="form.RAM_GB" type="number" class="liquid-input" />
                  </label>
                  <label>
                    <span>DDR_TYPE (内存规格)</span>
                    <input v-model="form.DDR_TYPE" class="liquid-input" placeholder="LPDDR5X / LPDDR5T" />
                  </label>
                  <label class="span-2">
                    <span>DDR_VENDOR (颗粒厂商)</span>
                    <input v-model="form.DDR_VENDOR" class="liquid-input" placeholder="SKhynix / Samsung / Micron" />
                  </label>
                  <label class="span-2">
                    <span>KERNEL_RELEASE (uname -r / /proc/version)</span>
                    <input v-model="form.KERNEL_RELEASE" class="liquid-input" />
                  </label>
                  <label class="span-2">
                    <span>KERNEL_VERSION (uname -v 编译签名)</span>
                    <input v-model="form.KERNEL_VERSION" class="liquid-input" />
                  </label>
                </div>
              </div>

              <div v-else-if="activeCategory === 'custom'" class="form-card liquid-glass">
                <label class="custom-area-label">
                  <span>附加 OEM / 受限属性池 (每行一条 KEY=VALUE，支持 # 注释)</span>
                  <textarea
                    v-model="form.CUSTOM_PROPS"
                    rows="13"
                    class="liquid-textarea mono-area"
                    placeholder="ro.product.marketname=Xiaomi 15 Pro&#10;ro.miui.ui.version.name=V816"
                  ></textarea>
                </label>
              </div>
            </div>
          </div>
        </div>
      </div>
    </Transition>
  </Teleport>
</template>

<style scoped>
.editor-fullscreen {
  position: fixed;
  inset: 0;
  width: 100%;
  height: 100%;
  z-index: 160;
  background: var(--bg-gradient);
  display: flex;
  flex-direction: column;
  overflow: hidden;
  padding: calc(var(--inset-top) + 6px) 14px 0;
  gap: 12px;
}

.editor-topbar,
.flip-topbar {
  flex: 0 0 auto;
  display: flex;
  align-items: center;
  gap: 11px;
  padding: 10px 13px;
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

.topbar-title-box {
  flex: 1;
  min-width: 0;
}

.topbar-title-box h2 {
  font-size: 15.5px;
  font-weight: 800;
  white-space: nowrap;
  overflow: hidden;
  text-overflow: ellipsis;
}

.topbar-sub {
  font-size: 11px;
  color: var(--text-secondary);
}

.editor-hub-scroll {
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

.name-card {
  flex: 0 0 auto;
  padding: 14px;
  display: flex;
  flex-direction: column;
  gap: 10px;
}

.name-label,
.quick-id-row label {
  display: flex;
  flex-direction: column;
  gap: 4px;
  font-size: 11px;
  font-weight: 700;
  color: var(--text-secondary);
}

.quick-id-row {
  display: grid;
  grid-template-columns: 1fr 1fr;
  gap: 9px;
}

.audit-card {
  flex: 0 0 auto;
  padding: 12px 14px;
  border-color: rgba(245, 158, 11, 0.42);
  background: rgba(245, 158, 11, 0.08);
}

.audit-head {
  display: flex;
  align-items: center;
  gap: 6px;
  font-size: 12px;
  font-weight: 800;
  color: #f59e0b;
  margin-bottom: 6px;
}

.audit-list {
  padding-left: 16px;
  font-size: 11.5px;
  color: var(--text-secondary);
  display: flex;
  flex-direction: column;
  gap: 4px;
}

.audit-list li.error {
  color: #f43f5e;
  font-weight: 700;
}

.category-grid {
  flex: 0 0 auto;
  display: flex;
  flex-direction: column;
  gap: 10px;
}

.cat-card {
  flex: 0 0 auto;
  display: flex;
  align-items: center;
  gap: 13px;
  width: 100%;
  padding: 14px 15px;
  border-radius: 22px;
  text-align: left;
  color: var(--text-primary);
}

.cat-icon-box {
  width: 42px;
  height: 42px;
  border-radius: 14px;
  display: flex;
  align-items: center;
  justify-content: center;
  flex-shrink: 0;
}

.cat-icon-box.violet {
  background: rgba(139, 92, 246, 0.18);
  color: #a78bfa;
  border: 1px solid rgba(139, 92, 246, 0.35);
}

.cat-icon-box.cyan {
  background: rgba(6, 182, 212, 0.18);
  color: #22d3ee;
  border: 1px solid rgba(6, 182, 212, 0.35);
}

.cat-icon-box.emerald {
  background: rgba(16, 185, 129, 0.18);
  color: #34d399;
  border: 1px solid rgba(16, 185, 129, 0.35);
}

.cat-icon-box.amber {
  background: rgba(245, 158, 11, 0.18);
  color: #fbbf24;
  border: 1px solid rgba(245, 158, 11, 0.35);
}

.cat-icon-box.rose {
  background: rgba(244, 63, 94, 0.18);
  color: #fb7185;
  border: 1px solid rgba(244, 63, 94, 0.35);
}

.cat-icon-box.teal {
  background: rgba(20, 184, 166, 0.18);
  color: #2dd4bf;
  border: 1px solid rgba(20, 184, 166, 0.35);
}

.cat-copy {
  flex: 1;
  min-width: 0;
}

.cat-title {
  font-size: 14.5px;
  font-weight: 800;
}

.cat-summary {
  font-size: 11.5px;
  color: var(--text-secondary);
  margin-top: 3px;
  white-space: nowrap;
  overflow: hidden;
  text-overflow: ellipsis;
}

.cat-arrow {
  color: var(--text-muted);
  flex-shrink: 0;
}

.flip-expand-sheet {
  position: fixed;
  z-index: 200;
  background: var(--bg-gradient);
  overflow: hidden;
  box-shadow: 0 24px 60px rgba(0, 0, 0, 0.55);
  transition:
    top 0.36s cubic-bezier(0.22, 1, 0.36, 1),
    left 0.36s cubic-bezier(0.22, 1, 0.36, 1),
    width 0.36s cubic-bezier(0.22, 1, 0.36, 1),
    height 0.36s cubic-bezier(0.22, 1, 0.36, 1),
    border-radius 0.34s cubic-bezier(0.22, 1, 0.36, 1),
    opacity 0.26s ease;
  will-change: top, left, width, height, border-radius, opacity;
}

.flip-inner {
  width: 100%;
  height: 100%;
  display: flex;
  flex-direction: column;
  overflow: hidden;
  gap: 12px;
  padding: calc(var(--inset-top) + 6px) 14px 0;
  opacity: 0;
  transform: translateY(8px);
  transition: opacity 0.22s ease, transform 0.26s var(--spring-easing);
}

.flip-inner.visible {
  opacity: 1;
  transform: translateY(0);
}

.flip-form-scroll {
  flex: 1 1 0%;
  min-height: 0;
  overflow-y: auto;
  overflow-x: hidden;
  -webkit-overflow-scrolling: touch;
  overscroll-behavior: contain;
  display: flex;
  flex-direction: column;
  padding-bottom: calc(var(--inset-bottom) + 28px);
}

.form-card {
  flex: 0 0 auto;
  padding: 15px;
  display: flex;
  flex-direction: column;
  gap: 13px;
}

.preset-box {
  padding: 11px 12px;
  border-radius: 15px;
  background: rgba(var(--accent-primary-rgb), 0.1);
  border: 1px solid rgba(var(--accent-primary-rgb), 0.28);
  display: flex;
  flex-direction: column;
  gap: 8px;
}

.preset-title {
  font-size: 11.5px;
  font-weight: 700;
  color: var(--accent-primary);
}

.preset-pills {
  display: flex;
  flex-wrap: wrap;
  gap: 6px;
}

.preset-pill {
  padding: 5px 10px;
  border-radius: 9px;
  font-size: 11px;
  font-weight: 700;
  background: rgba(var(--accent-secondary-rgb), 0.16);
  color: var(--accent-secondary);
  border: 1px solid rgba(var(--accent-secondary-rgb), 0.34);
}

.form-grid {
  display: grid;
  grid-template-columns: 1fr 1fr;
  gap: 10px;
}

.form-grid label,
.custom-area-label {
  display: flex;
  flex-direction: column;
  gap: 4px;
  font-size: 11px;
  font-weight: 700;
  color: var(--text-secondary);
  min-width: 0;
}

.span-2 {
  grid-column: 1 / -1;
}

.mono-area {
  font-family: ui-monospace, SFMono-Regular, Menlo, Consolas, monospace;
  font-size: 12px;
  line-height: 1.5;
}

@media (max-width: 440px) {
  .form-grid,
  .quick-id-row {
    grid-template-columns: 1fr;
  }
}
</style>

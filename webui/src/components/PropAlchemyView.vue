<script setup lang="ts">
import { ref } from 'vue';
import { FileUp, Sparkles, Eraser } from 'lucide-vue-next';
import { useI18n } from '../i18n';
import { showToast, triggerHaptic } from '../bridge/webui-x';
import { parsePropTextViaCli, type DeviceTemplate } from '../store/mirageStore';

const emit = defineEmits<{
  (e: 'parsed', tpl: DeviceTemplate): void;
}>();

const { t } = useI18n();
const rawPropText = ref('');
const fileInputRef = ref<HTMLInputElement | null>(null);

function triggerFileSelect() {
  triggerHaptic('light');
  fileInputRef.value?.click();
}

function handleFileChange(e: Event) {
  const target = e.target as HTMLInputElement;
  const file = target.files?.[0];
  if (!file) return;

  const reader = new FileReader();
  reader.onload = async (ev) => {
    const content = String(ev.target?.result || '');
    rawPropText.value = content;
    await runAlchemy();
  };
  reader.readAsText(file);
}

async function runAlchemy() {
  if (!rawPropText.value.trim()) {
    showToast('请先导入或粘贴 build.prop 内容');
    return;
  }
  const tpl = await parsePropTextViaCli(rawPropText.value);
  if (tpl) {
    emit('parsed', tpl);
  }
}
</script>

<template>
  <section class="alchemy-card liquid-glass" v-touch-glass>
    <h2>{{ t('alchemy.title') }}</h2>
    <p class="alchemy-desc">{{ t('alchemy.desc') }}</p>

    <div class="drop-portal tactile-press" @click="triggerFileSelect">
      <input
        ref="fileInputRef"
        type="file"
        accept=".prop,.txt,.conf,*/*"
        hidden
        @change="handleFileChange"
      />
      <div class="portal-orb">
        <FileUp :size="26" />
      </div>
      <div class="portal-title">{{ t('alchemy.dropTitle') }}</div>
      <div class="portal-sub">{{ t('alchemy.dropSub') }}</div>
    </div>

    <textarea
      v-model="rawPropText"
      rows="8"
      class="liquid-textarea mono-area"
      placeholder="ro.product.brand=Xiaomi&#10;ro.product.model=2410DPN6CC&#10;ro.board.platform=sun&#10;ro.soc.model=SM8750-AB&#10;ro.boot.hardware.ddr=16GB,SKhynix,LPDDR5X"
    ></textarea>

    <div class="alchemy-actions">
      <button class="btn-liquid-ghost tactile-press" @click="rawPropText = ''">
        <Eraser :size="15" />
        <span>{{ t('alchemy.clearBtn') }}</span>
      </button>
      <button class="btn-liquid tactile-press" @click="runAlchemy">
        <Sparkles :size="16" />
        <span>{{ t('alchemy.parseBtn') }}</span>
      </button>
    </div>
  </section>
</template>

<style scoped>
.alchemy-card {
  padding: 20px;
}

.alchemy-card h2 {
  font-size: 16px;
  font-weight: 800;
}

.alchemy-desc {
  font-size: 12.5px;
  color: var(--text-secondary);
  margin: 8px 0 16px;
  line-height: 1.55;
}

.drop-portal {
  border: 1.5px dashed rgba(139, 92, 246, 0.5);
  border-radius: 20px;
  padding: 24px 16px;
  text-align: center;
  background: radial-gradient(circle at center, rgba(139, 92, 246, 0.12), rgba(6, 182, 212, 0.04));
  margin-bottom: 14px;
}

.portal-orb {
  width: 52px;
  height: 52px;
  border-radius: 18px;
  margin: 0 auto 10px;
  background: rgba(139, 92, 246, 0.22);
  border: 1px solid rgba(255, 255, 255, 0.25);
  color: #c4b5fd;
  display: flex;
  align-items: center;
  justify-content: center;
}

.portal-title {
  font-size: 14px;
  font-weight: 700;
  color: #fff;
}

.portal-sub {
  font-size: 11.5px;
  color: var(--text-secondary);
  margin-top: 4px;
}

.mono-area {
  font-family: ui-monospace, SFMono-Regular, Menlo, Consolas, monospace;
  font-size: 12px;
}

.alchemy-actions {
  display: flex;
  justify-content: flex-end;
  gap: 10px;
  margin-top: 14px;
}
</style>

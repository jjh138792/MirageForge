<!--
SPDX-License-Identifier: GPL-3.0-only
Copyright (c) 2025-2026 jjh138792
MirageForge: https://github.com/jjh138792/MirageForge
-->

<script setup lang="ts">
import { computed, watch } from 'vue';
import {
  ArrowUpCircle,
  X,
  RefreshCw,
  ExternalLink,
  Download,
  AlertTriangle,
} from 'lucide-vue-next';
import { useI18n } from '../i18n';
import { triggerHaptic } from '../bridge/ksu-io';
import { mirageStore, fetchChangelogContent } from '../store/mirageStore';

const props = defineProps<{
  visible: boolean;
}>();

const emit = defineEmits<{
  (e: 'close'): void;
  (e: 'start-update'): void;
}>();

const { t } = useI18n();

interface ParsedLogBlock {
  type: 'h1' | 'h2' | 'h3' | 'li' | 'hr' | 'p';
  text: string;
}

const parsedBlocks = computed<ParsedLogBlock[]>(() => {
  const raw = mirageStore.updateInfo.changelogText || '';
  if (!raw.trim()) return [];

  const blocks: ParsedLogBlock[] = [];
  for (const rawLine of raw.split(/\r?\n/)) {
    const line = rawLine.trim();
    if (!line) continue;
    if (/^---+$/.test(line) || /^\*\*\*+$/.test(line)) {
      blocks.push({ type: 'hr', text: '' });
    } else if (line.startsWith('### ')) {
      blocks.push({ type: 'h3', text: line.slice(4).trim() });
    } else if (line.startsWith('## ')) {
      blocks.push({ type: 'h2', text: line.slice(3).trim() });
    } else if (line.startsWith('# ')) {
      blocks.push({ type: 'h1', text: line.slice(2).trim() });
    } else if (/^[-*+]\s+/.test(line)) {
      blocks.push({ type: 'li', text: line.replace(/^[-*+]\s+/, '').trim() });
    } else {
      blocks.push({ type: 'p', text: line });
    }
  }
  return blocks;
});

watch(
  () => props.visible,
  (isOpen) => {
    if (isOpen && mirageStore.updateInfo.changelogUrl && !mirageStore.updateInfo.changelogText) {
      fetchChangelogContent(false);
    }
  }
);

function handleOpenExternal() {
  triggerHaptic('light');
  const url = mirageStore.updateInfo.changelogUrl || mirageStore.updateInfo.zipUrl;
  if (url) window.open(url, '_blank');
}

function handleStartUpdate() {
  triggerHaptic('medium');
  emit('start-update');
}
</script>

<template>
  <Transition name="view-morph">
    <div v-if="visible" class="ota-modal-overlay" @click.self="emit('close')">
      <div class="ota-modal-sheet liquid-glass liquid-glass-elevated">
        <div class="ota-modal-head">
          <div class="head-left">
            <div class="update-orb-icon">
              <ArrowUpCircle :size="20" />
            </div>
            <div>
              <h3>{{ t('ota.modalTitle') }}</h3>
              <div class="ver-transition">
                <span class="ver-old">v{{ mirageStore.version }}</span>
                <span class="ver-arrow">→</span>
                <span class="ver-new">v{{ mirageStore.updateInfo.latestVersion }}</span>
                <span v-if="mirageStore.updateInfo.latestCode" class="ver-code">
                  ({{ mirageStore.updateInfo.latestCode }})
                </span>
              </div>
            </div>
          </div>

          <button class="icon-close tactile-press" @click="emit('close')">
            <X :size="18" />
          </button>
        </div>

        <div class="ota-modal-body">
          <div v-if="mirageStore.updateInfo.changelogLoading" class="changelog-state">
            <RefreshCw :size="20" class="spin-anim" />
            <span>{{ t('ota.changelogLoading') }}</span>
          </div>

          <div v-else-if="mirageStore.updateInfo.changelogError" class="changelog-state error-state">
            <AlertTriangle :size="20" />
            <span>{{ mirageStore.updateInfo.changelogError }}</span>
            <button class="btn-liquid-ghost tactile-press" @click="fetchChangelogContent(true)">
              <RefreshCw :size="13" />
              <span>{{ t('ota.changelogRetry') }}</span>
            </button>
          </div>

          <div v-else-if="parsedBlocks.length === 0" class="changelog-state">
            <span>{{ t('ota.changelogEmpty') }}</span>
          </div>

          <div v-else class="changelog-markdown">
            <template v-for="(blk, idx) in parsedBlocks" :key="idx">
              <h4 v-if="blk.type === 'h1'" class="md-h1">{{ blk.text }}</h4>
              <h5 v-else-if="blk.type === 'h2'" class="md-h2">{{ blk.text }}</h5>
              <h6 v-else-if="blk.type === 'h3'" class="md-h3">{{ blk.text }}</h6>
              <hr v-else-if="blk.type === 'hr'" class="md-hr" />
              <div v-else-if="blk.type === 'li'" class="md-li">
                <span class="md-bullet"></span>
                <span>{{ blk.text }}</span>
              </div>
              <p v-else class="md-p">{{ blk.text }}</p>
            </template>
          </div>
        </div>

        <div class="ota-modal-foot">
          <button class="btn-liquid-ghost tactile-press" @click="handleOpenExternal">
            <ExternalLink :size="14" />
            <span>{{ t('ota.openExternal') }}</span>
          </button>

          <div class="foot-right">
            <button class="btn-liquid-ghost tactile-press" @click="emit('close')">
              {{ t('ota.laterBtn') }}
            </button>
            <button class="btn-liquid tactile-press" @click="handleStartUpdate">
              <Download :size="15" />
              <span>{{ t('ota.updateNowBtn') }}</span>
            </button>
          </div>
        </div>
      </div>
    </div>
  </Transition>
</template>

<style scoped>
.ota-modal-overlay {
  position: fixed;
  inset: 0;
  background: rgba(3, 5, 12, 0.68);
  backdrop-filter: blur(12px);
  display: flex;
  align-items: center;
  justify-content: center;
  padding: 16px;
  z-index: 180;
}

.ota-modal-sheet {
  width: 100%;
  max-width: 560px;
  max-height: 82vh;
  display: flex;
  flex-direction: column;
  padding: 18px;
  border-radius: 28px;
}

.ota-modal-head {
  display: flex;
  justify-content: space-between;
  align-items: center;
  gap: 10px;
  padding-bottom: 13px;
  border-bottom: 1px solid var(--glass-border);
}

.head-left {
  display: flex;
  align-items: center;
  gap: 11px;
}

.update-orb-icon {
  width: 40px;
  height: 40px;
  border-radius: 14px;
  background: linear-gradient(135deg, rgba(139, 92, 246, 0.28), rgba(6, 182, 212, 0.28));
  border: 1px solid rgba(139, 92, 246, 0.45);
  color: var(--accent-secondary);
  display: flex;
  align-items: center;
  justify-content: center;
  flex-shrink: 0;
}

.ota-modal-head h3 {
  font-size: 16px;
  font-weight: 800;
}

.ver-transition {
  display: flex;
  align-items: center;
  gap: 6px;
  margin-top: 3px;
  font-size: 11.5px;
  font-weight: 700;
}

.ver-old {
  color: var(--text-secondary);
}

.ver-arrow {
  color: var(--accent-secondary);
}

.ver-new {
  padding: 1px 7px;
  border-radius: 7px;
  background: rgba(16, 185, 129, 0.18);
  color: #10b981;
  border: 1px solid rgba(16, 185, 129, 0.35);
}

.ver-code {
  color: var(--text-muted);
  font-weight: 600;
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

.ota-modal-body {
  flex: 1;
  overflow-y: auto;
  padding: 14px 4px;
  min-height: 150px;
}

.changelog-state {
  display: flex;
  flex-direction: column;
  align-items: center;
  justify-content: center;
  gap: 10px;
  padding: 32px 12px;
  font-size: 12.5px;
  color: var(--text-secondary);
  text-align: center;
}

.error-state {
  color: #f59e0b;
}

.spin-anim {
  animation: spin 1s linear infinite;
}

@keyframes spin {
  from { transform: rotate(0deg); }
  to { transform: rotate(360deg); }
}

.changelog-markdown {
  background: var(--subcard-bg);
  border: 1px solid var(--glass-border);
  border-radius: 16px;
  padding: 14px 16px;
  display: flex;
  flex-direction: column;
  gap: 7px;
  line-height: 1.6;
}

.md-h1 {
  font-size: 15px;
  font-weight: 800;
  color: var(--text-primary);
  margin-top: 4px;
}

.md-h2 {
  font-size: 13.5px;
  font-weight: 800;
  color: var(--accent-secondary);
  margin-top: 6px;
}

.md-h3 {
  font-size: 12.5px;
  font-weight: 700;
  color: var(--accent-primary);
  margin-top: 4px;
}

.md-hr {
  border: none;
  border-top: 1px dashed var(--glass-border);
  margin: 4px 0;
}

.md-li {
  display: flex;
  align-items: flex-start;
  gap: 8px;
  font-size: 12.5px;
  color: var(--text-primary);
  word-break: break-word;
}

.md-bullet {
  width: 5px;
  height: 5px;
  border-radius: 50%;
  background: var(--accent-secondary);
  margin-top: 8px;
  flex-shrink: 0;
}

.md-p {
  font-size: 12.5px;
  color: var(--text-secondary);
  word-break: break-word;
}

.ota-modal-foot {
  display: flex;
  justify-content: space-between;
  align-items: center;
  gap: 8px;
  padding-top: 13px;
  border-top: 1px solid var(--glass-border);
  flex-wrap: wrap;
}

.foot-right {
  display: flex;
  align-items: center;
  gap: 8px;
}
</style>

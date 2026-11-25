<!--
SPDX-License-Identifier: GPL-3.0-only
Copyright (c) 2025-2026 jjh138792
MirageForge: https://github.com/jjh138792/MirageForge
-->

<script setup lang="ts">
import { triggerHaptic } from '../bridge/ksu-io';

const props = defineProps<{
  modelValue: boolean;
  title: string;
  description?: string;
  badge?: string;
  danger?: boolean;
  disabled?: boolean;
}>();

const emit = defineEmits<{
  (e: 'update:modelValue', value: boolean): void;
}>();

function toggle() {
  if (props.disabled) return;
  triggerHaptic('light');
  emit('update:modelValue', !props.modelValue);
}
</script>

<template>
  <div
    class="left-toggle-row tactile-press"
    :class="{ active: modelValue, disabled, danger }"
    role="switch"
    :aria-checked="modelValue"
    @click="toggle"
  >
    <!-- 左侧独立开关：关为灰色，开为主题色 -->
    <div class="mf-switch" :class="{ on: modelValue, 'switch-danger': danger && modelValue }">
      <div class="mf-switch-thumb"></div>
    </div>

    <div class="row-copy">
      <div class="row-title-line">
        <span class="row-title">{{ title }}</span>
        <span v-if="badge" class="row-badge" :class="{ warn: danger }">{{ badge }}</span>
      </div>
      <p v-if="description" class="row-desc">{{ description }}</p>
    </div>
  </div>
</template>

<style scoped>
.left-toggle-row {
  display: flex;
  align-items: center;
  gap: 13px;
  padding: 12px 14px;
  border-radius: 16px;
  background: var(--subcard-bg);
  border: 1px solid var(--glass-border);
  transition:
    border-color 0.25s ease,
    background-color 0.25s ease,
    transform 0.22s var(--bounce-easing);
}

.left-toggle-row.active {
  border-color: rgba(var(--accent-primary-rgb), 0.38);
  background: rgba(var(--accent-primary-rgb), 0.08);
}

.left-toggle-row.danger.active {
  border-color: rgba(245, 158, 11, 0.45);
  background: rgba(245, 158, 11, 0.09);
}

.left-toggle-row.disabled {
  opacity: 0.48;
  pointer-events: none;
}

.switch-danger {
  background: linear-gradient(135deg, #f59e0b, #f43f5e) !important;
  box-shadow: 0 0 12px rgba(245, 158, 11, 0.45) !important;
}

.row-copy {
  flex: 1;
  min-width: 0;
}

.row-title-line {
  display: flex;
  align-items: center;
  gap: 7px;
  flex-wrap: wrap;
}

.row-title {
  font-size: 13.5px;
  font-weight: 700;
  color: var(--text-primary);
}

.row-badge {
  font-size: 10px;
  font-weight: 800;
  padding: 1.5px 7px;
  border-radius: 7px;
  background: rgba(var(--accent-secondary-rgb), 0.16);
  color: var(--accent-secondary);
  border: 1px solid rgba(var(--accent-secondary-rgb), 0.34);
}

.row-badge.warn {
  background: rgba(245, 158, 11, 0.16);
  color: #f59e0b;
  border-color: rgba(245, 158, 11, 0.38);
}

.row-desc {
  font-size: 11.5px;
  color: var(--text-secondary);
  margin-top: 3px;
  line-height: 1.45;
}
</style>

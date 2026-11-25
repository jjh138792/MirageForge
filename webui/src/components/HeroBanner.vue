<!--
SPDX-License-Identifier: GPL-3.0-only
Copyright (c) 2025-2026 jjh138792
MirageForge: https://github.com/jjh138792/MirageForge
-->

<script setup lang="ts">
import { ArrowUpCircle, ShieldCheck, Sparkles } from 'lucide-vue-next';
import MirageLogo from './MirageLogo.vue';
import { useI18n } from '../i18n';
import { detectRuntimeEnv } from '../bridge/ksu-io';
import { mirageStore } from '../store/mirageStore';

const emit = defineEmits<{
  (e: 'open-update'): void;
}>();

const { t } = useI18n();
const runtimeEnv = detectRuntimeEnv();
</script>

<template>
  <section class="hero-banner liquid-glass liquid-glass-elevated" v-touch-glass>
    <div class="hero-glow glow-a" aria-hidden="true"></div>
    <div class="hero-glow glow-b" aria-hidden="true"></div>

    <div class="hero-main">
      <div class="hero-top-row">
        <span class="hero-status-pill">
          <ShieldCheck :size="13" />
          <span>{{ t('home.heroBadge') }}</span>
        </span>
        <span class="hero-env-pill">{{ runtimeEnv }}</span>
      </div>

      <div class="hero-brand-row">
        <div class="hero-logo-shell">
          <MirageLogo :size="58" />
        </div>
        <div class="hero-brand-text">
          <div class="hero-title-wrap">
            <h1>{{ t('app.title') }}</h1>
            <span class="hero-ver-tag">v{{ mirageStore.version }}</span>
          </div>
          <p class="hero-tagline">{{ t('app.subtitle') }}</p>
        </div>
      </div>

      <div class="hero-feature-line">
        <Sparkles :size="13" class="feat-icon" />
        <span>{{ t('home.heroDesc') }}</span>
      </div>

      <!-- 检测到新版本时直接亮起 OTA 快捷更新条 -->
      <button
        v-if="mirageStore.updateInfo.hasUpdate"
        class="hero-ota-strip tactile-press"
        @click="emit('open-update')"
      >
        <ArrowUpCircle :size="16" />
        <span>{{ t('home.newVerAvailable', { ver: mirageStore.updateInfo.latestVersion }) }}</span>
      </button>
    </div>
  </section>
</template>

<style scoped>
.hero-banner {
  padding: 20px 18px 18px;
  border-radius: 28px;
  position: relative;
  overflow: hidden;
}

.hero-glow {
  position: absolute;
  border-radius: 50%;
  filter: blur(46px);
  pointer-events: none;
  z-index: 0;
}

.glow-a {
  width: 210px;
  height: 210px;
  top: -70px;
  right: -35px;
  background: radial-gradient(circle, rgba(var(--accent-primary-rgb), 0.55), transparent 70%);
}

.glow-b {
  width: 180px;
  height: 180px;
  bottom: -75px;
  left: -30px;
  background: radial-gradient(circle, rgba(var(--accent-secondary-rgb), 0.45), transparent 70%);
}

.hero-main {
  position: relative;
  z-index: 1;
  display: flex;
  flex-direction: column;
  gap: 14px;
}

.hero-top-row {
  display: flex;
  justify-content: space-between;
  align-items: center;
  gap: 8px;
  flex-wrap: wrap;
}

.hero-status-pill {
  display: inline-flex;
  align-items: center;
  gap: 5px;
  padding: 4px 10px;
  border-radius: 999px;
  font-size: 10.5px;
  font-weight: 800;
  letter-spacing: 0.03em;
  background: rgba(16, 185, 129, 0.16);
  color: #10b981;
  border: 1px solid rgba(16, 185, 129, 0.36);
}

.hero-env-pill {
  font-size: 10.5px;
  font-weight: 700;
  padding: 3px 9px;
  border-radius: 999px;
  background: var(--subcard-bg);
  color: var(--text-secondary);
  border: 1px solid var(--glass-border);
}

.hero-brand-row {
  display: flex;
  align-items: center;
  gap: 15px;
}

.hero-logo-shell {
  padding: 4px;
  border-radius: 20px;
  background: rgba(255, 255, 255, 0.06);
  border: 1px solid var(--glass-border);
  box-shadow: 0 10px 24px -6px rgba(var(--accent-primary-rgb), 0.38);
  flex-shrink: 0;
}

.hero-title-wrap {
  display: flex;
  align-items: center;
  gap: 8px;
  flex-wrap: wrap;
}

.hero-title-wrap h1 {
  font-size: 24px;
  font-weight: 900;
  letter-spacing: -0.02em;
}

.hero-ver-tag {
  font-size: 11.5px;
  font-weight: 800;
  padding: 2.5px 9px;
  border-radius: 9px;
  background: linear-gradient(
    135deg,
    rgba(var(--accent-primary-rgb), 0.24),
    rgba(var(--accent-secondary-rgb), 0.24)
  );
  color: var(--accent-primary);
  border: 1px solid rgba(var(--accent-primary-rgb), 0.42);
}

.hero-tagline {
  font-size: 12.5px;
  color: var(--text-secondary);
  margin-top: 4px;
}

.hero-feature-line {
  display: flex;
  align-items: center;
  gap: 7px;
  padding: 8px 12px;
  border-radius: 13px;
  background: var(--subcard-bg);
  border: 1px solid var(--glass-border);
  font-size: 11.5px;
  font-weight: 600;
  color: var(--text-secondary);
}

.feat-icon {
  color: var(--accent-secondary);
  flex-shrink: 0;
}

.hero-ota-strip {
  display: flex;
  align-items: center;
  gap: 8px;
  width: 100%;
  padding: 10px 13px;
  border-radius: 14px;
  font-size: 12px;
  font-weight: 700;
  text-align: left;
  color: #f59e0b;
  background: rgba(245, 158, 11, 0.16);
  border: 1px solid rgba(245, 158, 11, 0.45);
  box-shadow: 0 0 16px rgba(245, 158, 11, 0.2);
}
</style>

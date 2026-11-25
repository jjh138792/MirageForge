// SPDX-License-Identifier: GPL-3.0-only
// Copyright (c) 2025-2026 jjh138792
// MirageForge: https://github.com/jjh138792/MirageForge

import { createApp } from 'vue';
import App from './App.vue';
import './styles/liquid-glass.css';
import { registerToastHandler, vTouchGlass } from './bridge/ksu-io';
import { initOpticalLensEngine } from './utils/lensEngine';
import { applyAllVisualSettings, triggerLiquidToast } from './store/mirageStore';

applyAllVisualSettings();

registerToastHandler((msg: string) => {
  triggerLiquidToast(msg);
});

const app = createApp(App);
app.directive('touch-glass', vTouchGlass);
app.mount('#app');

requestAnimationFrame(() => {
  initOpticalLensEngine();
  applyAllVisualSettings();
});

// SPDX-License-Identifier: GPL-3.0-only
// Copyright (c) 2025-2026 jjh138792
// MirageForge: https://github.com/jjh138792/MirageForge

import { reactive } from 'vue';
import { syncSystemBarsTheme, triggerHaptic } from '../bridge/ksu-io';
import { setRefractionIntensity } from '../utils/lensEngine';
import type {
  AccentTheme,
  AppSettings,
  DockStyle,
  RefractionLevel,
  ThemeMode,
} from './types';

function readStr<T extends string>(key: string, valid: readonly T[], def: T): T {
  if (typeof localStorage === 'undefined') return def;
  const v = localStorage.getItem(key) as T | null;
  return v && valid.includes(v) ? v : def;
}

function readBool(key: string, def: boolean): boolean {
  if (typeof localStorage === 'undefined') return def;
  const v = localStorage.getItem(key);
  if (v === null) return def;
  return v === '1' || v === 'true';
}

export const settingsStore = reactive<AppSettings & { resolvedTheme: 'light' | 'dark' }>({
  themeMode: readStr('mirage_theme', ['auto', 'light', 'dark'] as const, 'auto'),
  resolvedTheme: 'dark',
  accentColor: readStr('mirage_accent', ['violet', 'cyan', 'emerald', 'rose', 'amber'] as const, 'violet'),
  dockStyle: readStr('mirage_dock_style', ['floating', 'docked'] as const, 'floating'),
  refractionLevel: readStr('mirage_refraction', ['standard', 'vivid', 'off'] as const, 'standard'),
  defaultMountNs: readBool('mirage_default_mount_ns', false),
  defaultCowProp: readBool('mirage_default_cow_prop', false),
  autoRestartOnSave: readBool('mirage_auto_restart', false),
});

export function applyThemeMode(mode?: ThemeMode) {
  if (mode) {
    settingsStore.themeMode = mode;
    localStorage.setItem('mirage_theme', mode);
  }
  const sysLight =
    typeof window !== 'undefined' &&
    window.matchMedia &&
    window.matchMedia('(prefers-color-scheme: light)').matches;

  const target: 'light' | 'dark' =
    settingsStore.themeMode === 'auto'
      ? sysLight
        ? 'light'
        : 'dark'
      : settingsStore.themeMode;

  settingsStore.resolvedTheme = target;
  if (typeof document !== 'undefined') {
    document.documentElement.setAttribute('data-theme', target);
    const metaTheme = document.querySelector('meta[name="theme-color"]');
    if (metaTheme) {
      metaTheme.setAttribute('content', target === 'light' ? '#eef2ff' : '#060913');
    }
  }
  syncSystemBarsTheme(target === 'light');
}

export function setAccentColor(accent: AccentTheme) {
  settingsStore.accentColor = accent;
  localStorage.setItem('mirage_accent', accent);
  if (typeof document !== 'undefined') {
    document.documentElement.setAttribute('data-accent', accent);
  }
  triggerHaptic('light');
}

export function setDockStyle(style: DockStyle) {
  settingsStore.dockStyle = style;
  localStorage.setItem('mirage_dock_style', style);
  triggerHaptic('medium');
}

export function setRefractionLevel(level: RefractionLevel) {
  settingsStore.refractionLevel = level;
  localStorage.setItem('mirage_refraction', level);
  if (typeof document !== 'undefined') {
    document.documentElement.setAttribute('data-refraction', level);
  }
  setRefractionIntensity(level);
  triggerHaptic('light');
}

export function setEngineSetting(
  key: 'defaultMountNs' | 'defaultCowProp' | 'autoRestartOnSave',
  val: boolean
) {
  settingsStore[key] = val;
  const map = {
    defaultMountNs: 'mirage_default_mount_ns',
    defaultCowProp: 'mirage_default_cow_prop',
    autoRestartOnSave: 'mirage_auto_restart',
  };
  localStorage.setItem(map[key], val ? '1' : '0');
  triggerHaptic('light');
}

export function cycleThemeMode(): ThemeMode {
  const order: ThemeMode[] = ['auto', 'light', 'dark'];
  const next = order[(order.indexOf(settingsStore.themeMode) + 1) % order.length];
  triggerHaptic('medium');
  applyThemeMode(next);
  return next;
}

export function applyAllVisualSettings() {
  applyThemeMode();
  if (typeof document !== 'undefined') {
    document.documentElement.setAttribute('data-accent', settingsStore.accentColor);
    document.documentElement.setAttribute('data-refraction', settingsStore.refractionLevel);
  }
  setRefractionIntensity(settingsStore.refractionLevel);
}

if (typeof window !== 'undefined' && window.matchMedia) {
  window.matchMedia('(prefers-color-scheme: light)').addEventListener('change', () => {
    if (settingsStore.themeMode === 'auto') applyThemeMode();
  });
}

import type { Directive } from 'vue';

declare global {
  interface Window {
    ksu?: {
      exec: (cmd: string, options: string, callbackFuncName: string) => void;
      toast: (msg: string) => void;
      fullScreen?: (enable: boolean) => void;
      listPackages?: (type: 'user' | 'system' | 'all') => string;
      getPackagesInfo?: (pkgNamesJson: string) => string;
    };
    $packageManager?: {
      getApplicationInfo?: (pkg: string, flags: number, userId: number) => any;
      getInstalledPackages?: (flags: number, userId: number) => string;
    };
    webui?: {
      setLightStatusBars?: (light: boolean) => void;
      setLightNavigationBars?: (light: boolean) => void;
      vibrate?: (durationMs: number) => void;
    };
    [key: string]: any;
  }
}

export interface ExecResult {
  errno: number;
  stdout: string;
  stderr: string;
}

export interface InstalledAppInfo {
  packageName: string;
  appLabel: string;
  versionName: string;
  iconUrl: string;
}

export function safeStorageGet(key: string, fallback: string): string {
  try {
    if (typeof window !== 'undefined' && window.localStorage) {
      return window.localStorage.getItem(key) || fallback;
    }
  } catch (_) {}
  return fallback;
}

export function safeStorageSet(key: string, val: string) {
  try {
    if (typeof window !== 'undefined' && window.localStorage) {
      window.localStorage.setItem(key, val);
    }
  } catch (_) {}
}

/**
 * 1. 生成球面凸透镜法线位移贴图 (Spherical Lens Displacement Map)
 */
export function initLiquidLensNormalMap() {
  try {
    if (typeof document === 'undefined') return;
    const width = 160;
    const height = 80;
    const canvas = document.createElement('canvas');
    canvas.width = width;
    canvas.height = height;
    const ctx = canvas.getContext('2d');
    if (!ctx) return;

    const imgData = ctx.createImageData(width, height);
    const data = imgData.data;

    for (let y = 0; y < height; y++) {
      const ny = (y / (height - 1)) * 2 - 1;
      for (let x = 0; x < width; x++) {
        const nx = (x / (width - 1)) * 2 - 1;
        const dist = Math.pow(Math.abs(nx), 2.4) + Math.pow(Math.abs(ny), 2.4);

        let dx = 0;
        let dy = 0;
        if (dist < 1.0) {
          const edgeFactor = Math.pow(dist, 1.35);
          dx = -nx * edgeFactor * 0.85;
          dy = -ny * edgeFactor * 0.85;
        }

        const idx = (y * width + x) * 4;
        data[idx] = Math.max(0, Math.min(255, Math.round(128 + dx * 127)));
        data[idx + 1] = Math.max(0, Math.min(255, Math.round(128 + dy * 127)));
        data[idx + 2] = 128;
        data[idx + 3] = 255;
      }
    }

    ctx.putImageData(imgData, 0, 0);
    const dataUrl = canvas.toDataURL('image/png');
    const feImg = document.getElementById('mirage-spherical-map');
    if (feImg) {
      feImg.setAttribute('href', dataUrl);
      feImg.setAttributeNS('http://www.w3.org/1999/xlink', 'xlink:href', dataUrl);
    }
  } catch (_) {}
}

export function setOrbRefractionScale(scale: number) {
  try {
    const disp = document.getElementById('mirage-orb-disp');
    if (disp) {
      disp.setAttribute('scale', String(Math.round(scale)));
    }
  } catch (_) {}
}

/**
 * 2. 二阶胡克定律弹簧阻尼物理求解器
 */
export class SpringSolver {
  pos: number;
  target: number;
  vel = 0;
  stiffness: number;
  damping: number;

  constructor(initial = 0, stiffness = 260, damping = 22) {
    this.pos = initial;
    this.target = initial;
    this.stiffness = stiffness;
    this.damping = damping;
  }

  step(dt: number): number {
    const safeDt = Math.min(Math.max(dt, 0.001), 0.032);
    const force = -this.stiffness * (this.pos - this.target) - this.damping * this.vel;
    this.vel += force * safeDt;
    this.pos += this.vel * safeDt;

    if (Math.abs(this.vel) < 0.05 && Math.abs(this.pos - this.target) < 0.08) {
      this.pos = this.target;
      this.vel = 0;
    }
    return this.pos;
  }

  isSettled(): boolean {
    return this.vel === 0 && Math.abs(this.pos - this.target) < 0.08;
  }
}

export function detectRuntimeEnv(): 'WebUI-X' | 'KernelSU' | 'BrowserMock' {
  if (typeof window !== 'undefined') {
    if (window.$packageManager || window.webui) return 'WebUI-X';
    if (window.ksu && typeof window.ksu.exec === 'function') return 'KernelSU';
  }
  return 'BrowserMock';
}

export function syncSystemBarsTheme(isLight: boolean) {
  try {
    if (window.ksu?.fullScreen) window.ksu.fullScreen(true);
    if (window.webui?.setLightStatusBars) window.webui.setLightStatusBars(isLight);
    if (window.webui?.setLightNavigationBars) window.webui.setLightNavigationBars(isLight);
  } catch (_) {}
}

export function triggerHaptic(level: 'light' | 'medium' | 'heavy' | 'success' = 'light') {
  const durationMap = { light: 9, medium: 20, heavy: 38, success: 15 };
  const ms = durationMap[level];
  try {
    if (window.webui?.vibrate) {
      window.webui.vibrate(ms);
      return;
    }
    if (typeof navigator !== 'undefined' && typeof navigator.vibrate === 'function') {
      navigator.vibrate(level === 'success' ? [10, 30, 16] : ms);
    }
  } catch (_) {}
}

/**
 * 3. Root Shell 异步桥接器 (带 6 秒防挂起超时保护)
 */
let cbSeq = 0;
export function execRootShell(command: string): Promise<ExecResult> {
  return new Promise((resolve) => {
    if (typeof window !== 'undefined' && window.ksu && typeof window.ksu.exec === 'function') {
      const cbName = `__mirage_io_cb_${Date.now()}_${++cbSeq}`;
      let settled = false;

      const timer = setTimeout(() => {
        if (!settled) {
          settled = true;
          try { delete window[cbName]; } catch (_) {}
          resolve({ errno: -1, stdout: '', stderr: 'timeout' });
        }
      }, 6000);

      window[cbName] = (errno: number, stdout: string, stderr: string) => {
        if (settled) return;
        settled = true;
        clearTimeout(timer);
        try { delete window[cbName]; } catch (_) {}
        resolve({
          errno: Number(errno ?? 0),
          stdout: typeof stdout === 'string' ? stdout : String(stdout ?? ''),
          stderr: typeof stderr === 'string' ? stderr : String(stderr ?? ''),
        });
      };

      try {
        window.ksu.exec(command, '{}', cbName);
      } catch (e) {
        if (!settled) {
          settled = true;
          clearTimeout(timer);
          resolve({ errno: -1, stdout: '', stderr: String(e) });
        }
      }
    } else {
      resolve(mockShellFallback(command));
    }
  });
}

/**
 * 4. 零 Shell Fork 包枚举器 (使用 KernelSU pm API: listPackages & 分批 getPackagesInfo)
 */
export async function fetchInstalledAppsZeroFork(
  includeSystem: boolean,
  configuredPkgs: string[]
): Promise<InstalledAppInfo[]> {
  let pkgList: string[] = [];
  const mode = includeSystem ? 'all' : 'user';

  if (typeof window !== 'undefined' && window.ksu?.listPackages) {
    try {
      const raw = window.ksu.listPackages(mode);
      const parsed = typeof raw === 'string' ? JSON.parse(raw) : raw;
      if (Array.isArray(parsed)) pkgList = parsed;
    } catch (_) {}
  }

  if (pkgList.length === 0) {
    const flag = includeSystem ? '' : '-3';
    const res = await execRootShell(`pm list packages ${flag}`);
    if (res.stdout) {
      pkgList = res.stdout
        .split(/\r?\n/)
        .map((s) => s.replace(/^package:/, '').trim())
        .filter(Boolean);
    }
  }

  const mergedPkgs = Array.from(new Set([...configuredPkgs, ...pkgList]));
  const infoMap: Record<string, { appLabel?: string; versionName?: string }> = {};

  if (typeof window !== 'undefined' && window.ksu?.getPackagesInfo && mergedPkgs.length > 0) {
    // 分批查询（每批 40 个），避免一次性传入几百个包名导致 WebView Bridge 溢出失败
    const chunkSize = 40;
    for (let i = 0; i < mergedPkgs.length; i += chunkSize) {
      const chunk = mergedPkgs.slice(i, i + chunkSize);
      try {
        const raw = window.ksu.getPackagesInfo(JSON.stringify(chunk));
        const rawInfos = typeof raw === 'string' ? JSON.parse(raw) : raw;
        if (Array.isArray(rawInfos)) {
          for (const item of rawInfos) {
            if (item?.packageName) infoMap[item.packageName] = item;
          }
        }
      } catch (_) {}
    }
  } else if (typeof window !== 'undefined' && window.$packageManager?.getApplicationInfo) {
    for (const pkg of mergedPkgs) {
      try {
        const info = window.$packageManager.getApplicationInfo(pkg, 0, 0);
        if (info) {
          infoMap[pkg] = {
            appLabel: String(info.label || info.nonLocalizedLabel || pkg),
            versionName: String(info.versionName || ''),
          };
        }
      } catch (_) {}
    }
  }

  return mergedPkgs.map((pkg) => ({
    packageName: pkg,
    appLabel: infoMap[pkg]?.appLabel || pkg,
    versionName: infoMap[pkg]?.versionName || '',
    iconUrl: `ksu://icon/${pkg}`,
  }));
}

let customToastHandler: ((msg: string) => void) | null = null;
export function registerToastHandler(fn: (msg: string) => void) {
  customToastHandler = fn;
}

export function showToast(msg: string) {
  triggerHaptic('light');
  if (customToastHandler) customToastHandler(msg);
  else if (typeof window !== 'undefined' && window.ksu?.toast) window.ksu.toast(msg);
}

export const vTouchGlass: Directive<HTMLElement, 'light' | 'medium' | undefined> = {
  mounted(el, binding) {
    const updateSpot = (x: number, y: number) => {
      const rect = el.getBoundingClientRect();
      el.style.setProperty('--px', `${x - rect.left}px`);
      el.style.setProperty('--py', `${y - rect.top}px`);
      el.style.setProperty('--spot-opacity', '1');
    };
    const hideSpot = () => el.style.setProperty('--spot-opacity', '0');

    el.addEventListener('pointerdown', (e) => {
      updateSpot(e.clientX, e.clientY);
      triggerHaptic(binding.value || 'light');
    }, { passive: true });
    el.addEventListener('pointermove', (e) => updateSpot(e.clientX, e.clientY), { passive: true });
    el.addEventListener('pointerup', hideSpot, { passive: true });
    el.addEventListener('pointerleave', hideSpot, { passive: true });
    el.addEventListener('pointercancel', hideSpot, { passive: true });
  },
};

function mockShellFallback(cmd: string): ExecResult {
  if (cmd.includes('module.prop')) {
    return {
      errno: 0,
      stderr: '',
      stdout: 'id=mirage_forge\nname=蜃楼工坊 · MirageForge\nversion=v1.2.0\nversionCode=120\n',
    };
  }
  if (cmd.includes('state --json') || cmd.includes('config.json')) {
    return {
      errno: 0,
      stderr: '',
      stdout: JSON.stringify({
        version: '1.2.0',
        versionCode: 120,
        templates: {
          xiaomi_15_pro: {
            id: 'xiaomi_15_pro',
            name: 'Xiaomi 15 Pro (骁龙8至尊版 / 16GB LPDDR5X)',
            MODEL: '2410DPN6CC',
            BRAND: 'Xiaomi',
            MANUFACTURER: 'Xiaomi',
            DEVICE: 'haotian',
            PRODUCT: 'haotian',
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
            FINGERPRINT: 'Xiaomi/haotian/haotian:15/AP3A.240905.015.A2/OS2.0.102.0.VOBCNXM:user/release-keys',
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
            KERNEL_RELEASE: '',
            KERNEL_VERSION: '',
            CUSTOM_PROPS: 'ro.miui.ui.version.name=V816\nro.product.marketname=Xiaomi 15 Pro',
          },
        },
        appRoutes: {
          'flar2.devcheck': {
            tplId: 'xiaomi_15_pro',
            build: true,
            gl: true,
            vfs: true,
            probe: true,
            sdkSpoof: false,
          },
        },
      }),
    };
  }
  if (cmd.includes('pm list packages')) {
    return {
      errno: 0,
      stderr: '',
      stdout: 'package:flar2.devcheck\npackage:com.finalwire.aida64\npackage:com.tencent.tmgp.sgame\n',
    };
  }
  return { errno: 0, stdout: '[]', stderr: '' };
}

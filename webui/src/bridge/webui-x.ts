import type { Directive } from 'vue';

// 声明 KernelSU 与 MMRL WebUI-X 全局接口
declare global {
  interface Window {
    ksu?: {
      exec: (cmd: string, options: string, callbackFuncName: string) => void;
      toast: (msg: string) => void;
      fullScreen?: (enable: boolean) => void;
      listPackages?: (type: 'user' | 'system' | 'all') => string;
      getPackagesInfo?: (pkgNamesJson: string) => string;
    };
    // MMRL WebUI-X API 扩展接口
    $packageManager?: {
      getApplicationInfo?: (pkg: string, flags: number, userId: number) => any;
      getInstalledPackages?: (flags: number, userId: number) => string;
    };
    webui?: {
      setLightStatusBars?: (light: boolean) => void;
      setLightNavigationBars?: (light: boolean) => void;
      vibrate?: (durationMs: number) => void;
    };
    $mirage_forge?: any;
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

let cbSeq = 0;

/**
 * 判断当前所处的 WebUI 容器环境
 */
export function detectRuntimeEnv(): 'WebUI-X' | 'KernelSU' | 'BrowserMock' {
  if (typeof window !== 'undefined') {
    if (window.$packageManager || window.webui) return 'WebUI-X';
    if (window.ksu && typeof window.ksu.exec === 'function') return 'KernelSU';
  }
  return 'BrowserMock';
}

/**
 * 初始化沉浸式全面屏与状态栏样式 (兼容 WebUI-X 与 KSU)
 */
export function initImmersionMode() {
  try {
    if (window.ksu?.fullScreen) {
      window.ksu.fullScreen(true);
    }
    if (window.webui?.setLightStatusBars) {
      window.webui.setLightStatusBars(false);
    }
    if (window.webui?.setLightNavigationBars) {
      window.webui.setLightNavigationBars(false);
    }
  } catch (_) {}
}

/**
 * 多级物理触觉振动反馈 (Tactile Haptic Engine)
 * 优先使用 WebUI-X 原生振动与 Web Vibration API
 */
export function triggerHaptic(level: 'light' | 'medium' | 'heavy' | 'success' = 'light') {
  const durationMap = { light: 10, medium: 22, heavy: 40, success: 16 };
  const ms = durationMap[level];

  try {
    if (window.webui?.vibrate) {
      window.webui.vibrate(ms);
      return;
    }
    if (typeof navigator !== 'undefined' && typeof navigator.vibrate === 'function') {
      if (level === 'success') {
        navigator.vibrate([12, 35, 18]);
      } else {
        navigator.vibrate(ms);
      }
    }
  } catch (_) {}
}

/**
 * 统一 Root Shell 执行器 (带 PC 浏览器 Mock 调试支持)
 */
export function execRootShell(command: string): Promise<ExecResult> {
  return new Promise((resolve) => {
    if (window.ksu && typeof window.ksu.exec === 'function') {
      const cbName = `__mirage_wx_cb_${Date.now()}_${++cbSeq}`;
      window[cbName] = (errno: number, stdout: string, stderr: string) => {
        delete window[cbName];
        resolve({ errno, stdout: stdout || '', stderr: stderr || '' });
      };
      window.ksu.exec(command, '{}', cbName);
    } else {
      // PC 浏览器 npm run dev 预览模式下的模拟响应
      resolve(mockCliResponse(command));
    }
  });
}

/**
 * 原生 Toast 提示或页面内液态通知回调
 */
let customToastHandler: ((msg: string) => void) | null = null;
export function registerToastHandler(fn: (msg: string) => void) {
  customToastHandler = fn;
}

export function showToast(msg: string) {
  triggerHaptic('light');
  if (customToastHandler) {
    customToastHandler(msg);
  } else if (window.ksu?.toast) {
    window.ksu.toast(msg);
  }
}

/**
 * UTF-8 安全 Base64 编码
 */
export function encodeUtf8Base64(str: string): string {
  return btoa(unescape(encodeURIComponent(str)));
}

/**
 * 智能读取已安装应用列表与图标 (融合 WebUI-X $packageManager 与 KSU 接口)
 */
export async function fetchInstalledApps(
  includeSystem: boolean,
  configuredPkgs: string[]
): Promise<InstalledAppInfo[]> {
  let pkgList: string[] = [];
  const mode = includeSystem ? 'all' : 'user';

  // 1. 优先尝试 KSU / WebUI-X 注入的 listPackages
  if (window.ksu?.listPackages) {
    try {
      pkgList = JSON.parse(window.ksu.listPackages(mode));
    } catch (_) {}
  }

  // 2. Shell 回退查询
  if (!pkgList || pkgList.length === 0) {
    const flag = includeSystem ? '' : '-3';
    const res = await execRootShell(`pm list packages ${flag} | sed 's/package://g'`);
    if (res.stdout) {
      pkgList = res.stdout
        .split(/\r?\n/)
        .map((s) => s.trim())
        .filter(Boolean);
    }
  }

  const mergedPkgs = Array.from(new Set([...configuredPkgs, ...pkgList]));
  const infoMap: Record<string, { appLabel?: string; versionName?: string }> = {};

  // 3. 批量拉取应用真实名称与版本号
  if (window.ksu?.getPackagesInfo && mergedPkgs.length > 0) {
    try {
      const rawInfos = JSON.parse(window.ksu.getPackagesInfo(JSON.stringify(mergedPkgs)));
      if (Array.isArray(rawInfos)) {
        for (const item of rawInfos) {
          if (item?.packageName) {
            infoMap[item.packageName] = item;
          }
        }
      }
    } catch (_) {}
  } else if (window.$packageManager?.getApplicationInfo) {
    for (const pkg of mergedPkgs) {
      try {
        const info = window.$packageManager.getApplicationInfo(pkg, 0, 0);
        if (info) {
          infoMap[pkg] = {
            appLabel: info.label || info.nonLocalizedLabel || pkg,
            versionName: info.versionName || '',
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

/**
 * Vue 自定义指令 v-touch-glass：
 * 为液态玻璃卡片/按钮注入实时手指追光折射 (--px, --py) 与触觉按压反馈
 */
export const vTouchGlass: Directive<HTMLElement, 'light' | 'medium' | undefined> = {
  mounted(el, binding) {
    const updateSpot = (clientX: number, clientY: number) => {
      const rect = el.getBoundingClientRect();
      el.style.setProperty('--px', `${clientX - rect.left}px`);
      el.style.setProperty('--py', `${clientY - rect.top}px`);
      el.style.setProperty('--spot-opacity', '1');
    };

    const hideSpot = () => {
      el.style.setProperty('--spot-opacity', '0');
    };

    el.addEventListener(
      'pointerdown',
      (e: PointerEvent) => {
        updateSpot(e.clientX, e.clientY);
        triggerHaptic(binding.value || 'light');
      },
      { passive: true }
    );

    el.addEventListener(
      'pointermove',
      (e: PointerEvent) => {
        updateSpot(e.clientX, e.clientY);
      },
      { passive: true }
    );

    el.addEventListener('pointerup', hideSpot, { passive: true });
    el.addEventListener('pointerleave', hideSpot, { passive: true });
    el.addEventListener('pointercancel', hideSpot, { passive: true });
  },
};

/**
 * PC 浏览器开发预览 Mock 数据（当你在电脑运行 npm run dev 时提供逼真的展示数据）
 */
function mockCliResponse(cmd: string): ExecResult {
  if (cmd.includes('state --json')) {
    return {
      errno: 0,
      stderr: '',
      stdout: JSON.stringify({
        version: '1.1.0',
        versionCode: 110,
        activeCount: 2,
        templateCount: 2,
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
            SOC_MANUFACTURER: 'Qualcomm',
            SOC_MODEL: 'SM8750-AB',
            SUPPORTED_ABIS: 'arm64-v8a,armeabi-v7a,armeabi',
            BUILD_ID: 'AP3A.240905.015.A2',
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
            KERNEL_RELEASE: '6.6.30-android15-8-g94b8f12ab4c1',
            KERNEL_VERSION: '#1 SMP PREEMPT Fri Nov 15 12:00:00 UTC 2025',
            CUSTOM_PROPS: 'ro.miui.ui.version.name=V816\nro.product.marketname=Xiaomi 15 Pro',
          },
          rog_9_pro: {
            id: 'rog_9_pro',
            name: 'ROG Phone 9 Pro (24GB LPDDR5X / 165Hz)',
            MODEL: 'ASUS_AI2501_A',
            BRAND: 'asus',
            MANUFACTURER: 'asus',
            DEVICE: 'AI2501',
            PRODUCT: 'CN_AI2501',
            BOARD: 'sun',
            HARDWARE: 'qcom',
            SOC_MANUFACTURER: 'Qualcomm',
            SOC_MODEL: 'SM8750-AB',
            SUPPORTED_ABIS: 'arm64-v8a,armeabi-v7a,armeabi',
            BUILD_ID: 'AQ3A.240812.002',
            BUILD_TIME: '1737504000000',
            RELEASE: '15',
            SDK_INT: '35',
            SECURITY_PATCH: '2025-02-05',
            FINGERPRINT: 'asus/CN_AI2501/AI2501:15/AQ3A.240812.002/35.1810.1810.88:user/release-keys',
            GL_VENDOR: 'Qualcomm',
            GL_RENDERER: 'Adreno (TM) 830',
            GL_VERSION: 'OpenGL ES 3.2 V@0800.0',
            CPU_HARDWARE: 'Qualcomm Technologies, Inc SM8750',
            CPU_CORES: 8,
            CPU_MAX_FREQ: 4320000,
            CPU_CLUSTERS: '0-5:614400:3532800;6-7:806400:4320000',
            RAM_GB: 24,
            DDR_TYPE: 'LPDDR5X',
            DDR_VENDOR: 'SKhynix',
            SOC_ID: '618',
            SOC_MACHINE: 'SM8750',
            KERNEL_RELEASE: '6.6.30-android15-8-rog9',
            KERNEL_VERSION: '#1 SMP PREEMPT Mon Jan 20 08:30:00 UTC 2025',
            CUSTOM_PROPS: 'ro.product.asus.series=ROG Phone 9',
          },
        },
        appRoutes: {
          'com.tencent.tmgp.sgame': {
            tplId: 'rog_9_pro',
            build: true,
            gl: true,
            vfs: true,
            probe: true,
            sdkSpoof: false,
          },
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
  if (cmd.includes('probe list')) {
    return {
      errno: 0,
      stderr: '',
      stdout: JSON.stringify([
        {
          pkg: 'flar2.devcheck',
          domain: 'VFS_OPEN',
          key: '/sys/devices/system/cpu/cpu7/cpufreq/scaling_cur_freq',
          value: 'LiveFreq:4320000',
          stack: 'libjavacore.so!Linux_open+0x44 <- libart.so+0x219a0',
          unmapped: false,
        },
        {
          pkg: 'com.tencent.tmgp.sgame',
          domain: 'PROP_UNMAPPED',
          key: 'ro.vendor.qti.soc_model',
          value: 'SM8650',
          stack: 'libtersafe2.so+0x8f410 <- libc.so!__system_property_read_callback+0x28',
          unmapped: true,
        },
      ]),
    };
  }
  if (cmd.includes('probe telemetry')) {
    return {
      errno: 0,
      stderr: '',
      stdout: JSON.stringify([
        {
          timestamp: Math.floor(Date.now() / 1000) - 18,
          pkg: 'flar2.devcheck',
          pid: 18492,
          model: '2410DPN6CC',
          gpu: 'Adreno (TM) 830',
          ram: '16GB',
        },
      ]),
    };
  }
  if (cmd.includes('pm list packages')) {
    return {
      errno: 0,
      stderr: '',
      stdout: 'com.tencent.tmgp.sgame\nflar2.devcheck\ncom.miHoYo.Yuanshen\nru.andr7e.deviceinfohw\n',
    };
  }
  return { errno: 0, stdout: '{"ok":true}', stderr: '' };
}

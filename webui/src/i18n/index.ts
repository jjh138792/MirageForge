import { ref, computed } from 'vue';

export type LocaleCode = 'zh-CN' | 'zh-TW' | 'en-US';

const messages = {
  'zh-CN': {
    app: {
      title: '蜃楼工坊',
      subtitle: 'Rust CLI · 液态全息虚拟化引擎',
      activeBadge: '已幻化 {count} 款应用',
    },
    nav: {
      templates: '全息熔炉',
      alchemy: '属性炼金',
      apps: '分流矩阵',
      probe: '幻境探针',
    },
    templates: {
      header: '全息硬件拓扑模板库',
      exportBtn: '导出备份',
      newBtn: '铸造新机型',
      editBtn: '编辑拓扑',
      deleteBtn: '删除',
      ramLabel: '对称内存',
      clusterLabel: '异构单核分簇',
      modalTitleEdit: '编辑全息硬件模板',
      modalTitleNew: '铸造全息硬件模板',
      secIdentity: '一、身份标识与 Bionic/ART 反射层',
      secGraphics: '二、Vulkan + OpenGL ES + MediaDrm 渲染层',
      secVfs: '三、异构单核频率 (cpufreq) 与物理 DDR 对称层',
      clusterPresetTitle: '异构每核心频率分簇拓扑 (起始核-结束核:最小kHz:最大kHz)',
      saveBtn: '提交至 Rust CLI 原子编译',
      cancelBtn: '取消',
    },
    alchemy: {
      title: 'Rust 原生 build.prop 炼金解析器',
      desc: '上传任意 ROM 提取的 build.prop 或直接粘贴文本，Rust CLI 将自动提取指纹并根据 SoC 平台推导异构单核频率表、GPU 渲染器与 LPDDR 规格。',
      dropTitle: '点击导入本地 .prop / .txt 属性文件',
      dropSub: '支持 system / vendor / odm / product 全分区提取文件',
      clearBtn: '清空内容',
      parseBtn: '立即炼成全息模板',
    },
    apps: {
      searchPlaceholder: '搜索应用名称或包名 (如 DevCheck / 王者荣耀)...',
      filterAll: '全部应用',
      filterConfigured: '仅已伪装',
      sysApps: '系统应用',
      refreshBtn: '刷新',
      nativeMode: '原生环境 (0 改动卸载)',
      restartBtn: '重启生效',
      swBuild: 'Bionic/ART 属性',
      swGl: 'Vulkan/GLES/DRM',
      swVfs: '单核频率/对称内存/VFS',
      swProbe: '探针与调用栈',
      swSdk: '强改 SDK 版本',
    },
    probe: {
      crashTitle: 'Native 崩溃信号现场捕获 (crash_events.log)',
      crashEmpty: '运行稳定，未捕获到任何目标应用 Native 崩溃信号。',
      teleTitle: 'Zygisk 实时注入遥测 (launch_history.log)',
      teleEmpty: '暂无启动遥测记录，运行已绑定的应用后自动生成。',
      clearTele: '清空遥测',
      radarTitle: '幻境探针 · 硬件嗅探与调用栈回溯 (SCM_RIGHTS 直写)',
      radarDesc: '基于 SCM_RIGHTS 跨进程文件描述符直写磁盘，无视商业 ROM 日志限制，实时展示 _Unwind_Backtrace 调用栈。',
      filterPlaceholder: '过滤包名、属性名或调用库 (如 cpufreq / libtersafe)...',
      unmappedOnly: '仅看未映射',
      clearProbe: '清空探针',
      captureBtn: '+ 捕获入库',
      stackPrefix: '调用栈回溯:',
    },
  },
  'zh-TW': {
    app: {
      title: '蜃樓工坊',
      subtitle: 'Rust CLI · 液態全息虛擬化引擎',
      activeBadge: '已幻化 {count} 款應用',
    },
    nav: {
      templates: '全息熔爐',
      alchemy: '屬性煉金',
      apps: '分流矩陣',
      probe: '幻境探針',
    },
    templates: {
      header: '全息硬體拓撲模板庫',
      exportBtn: '匯出備份',
      newBtn: '鑄造新機型',
      editBtn: '編輯拓撲',
      deleteBtn: '刪除',
      ramLabel: '對稱記憶體',
      clusterLabel: '異構單核分簇',
      modalTitleEdit: '編輯全息硬體模板',
      modalTitleNew: '鑄造全息硬體模板',
      secIdentity: '一、身份標識與 Bionic/ART 反射層',
      secGraphics: '二、Vulkan + OpenGL ES + MediaDrm 渲染層',
      secVfs: '三、異構單核頻率 (cpufreq) 與物理 DDR 對稱層',
      clusterPresetTitle: '異構每核心頻率分簇拓撲 (起始核-結束核:最小kHz:最大kHz)',
      saveBtn: '提交至 Rust CLI 原子編譯',
      cancelBtn: '取消',
    },
    alchemy: {
      title: 'Rust 原生 build.prop 煉金解析器',
      desc: '上傳任意 ROM 提取的 build.prop 或直接貼上文本，Rust CLI 將自動推導異構單核頻率表、GPU 渲染器與 LPDDR 規格。',
      dropTitle: '點擊匯入本機 .prop / .txt 屬性檔案',
      dropSub: '支援 system / vendor / odm / product 全分區提取檔案',
      clearBtn: '清空內容',
      parseBtn: '立即煉成全息模板',
    },
    apps: {
      searchPlaceholder: '搜尋應用名稱或包名 (如 DevCheck / 王者榮耀)...',
      filterAll: '全部應用',
      filterConfigured: '僅已偽裝',
      sysApps: '系統應用',
      refreshBtn: '重新整理',
      nativeMode: '原生環境 (0 改動卸載)',
      restartBtn: '重啟生效',
      swBuild: 'Bionic/ART 屬性',
      swGl: 'Vulkan/GLES/DRM',
      swVfs: '單核頻率/對稱記憶體/VFS',
      swProbe: '探針與調用棧',
      swSdk: '強改 SDK 版本',
    },
    probe: {
      crashTitle: 'Native 崩潰信號現場捕獲 (crash_events.log)',
      crashEmpty: '運行穩定，未捕獲到任何目標應用 Native 崩潰信號。',
      teleTitle: 'Zygisk 實時注入遙測 (launch_history.log)',
      teleEmpty: '暫無啟動遙測記錄，運行已綁定的應用後自動生成。',
      clearTele: '清空遙測',
      radarTitle: '幻境探針 · 硬體嗅探與調用棧回溯 (SCM_RIGHTS 直寫)',
      radarDesc: '基於 SCM_RIGHTS 跨進程檔案描述符直寫磁碟，無視商業 ROM 日誌限制，實時展示 _Unwind_Backtrace 調用棧。',
      filterPlaceholder: '過濾包名、屬性名或調用庫 (如 cpufreq / libtersafe)...',
      unmappedOnly: '僅看未映射',
      clearProbe: '清空探針',
      captureBtn: '+ 捕獲入庫',
      stackPrefix: '調用棧回溯:',
    },
  },
  'en-US': {
    app: {
      title: 'MirageForge',
      subtitle: 'Rust CLI · Liquid Glass Hypervisor',
      activeBadge: '{count} Apps Spoofed',
    },
    nav: {
      templates: 'Templates',
      alchemy: 'Prop Alchemy',
      apps: 'App Matrix',
      probe: 'Probe Radar',
    },
    templates: {
      header: 'Holographic Hardware Templates',
      exportBtn: 'Export Backup',
      newBtn: 'Forge Device',
      editBtn: 'Edit Topology',
      deleteBtn: 'Delete',
      ramLabel: 'Symmetric RAM',
      clusterLabel: 'Per-Core Clusters',
      modalTitleEdit: 'Edit Hardware Template',
      modalTitleNew: 'Forge Hardware Template',
      secIdentity: '1. Identity & Bionic/ART Reflection Layer',
      secGraphics: '2. Vulkan + OpenGL ES + MediaDrm Pipeline',
      secVfs: '3. Per-Core Cpufreq & Symmetric DDR VFS Layer',
      clusterPresetTitle: 'Heterogeneous CPU Clusters (first-last:minKhz:maxKhz)',
      saveBtn: 'Compile via Rust CLI',
      cancelBtn: 'Cancel',
    },
    alchemy: {
      title: 'Rust Native build.prop Alchemy Engine',
      desc: 'Import any ROM build.prop file or paste raw properties. Rust CLI automatically infers CPU cluster frequencies, Vulkan/GLES GPU renderer, and LPDDR specs.',
      dropTitle: 'Tap to import local .prop / .txt file',
      dropSub: 'Supports system / vendor / odm / product partition build.prop',
      clearBtn: 'Clear',
      parseBtn: 'Synthesize Template',
    },
    apps: {
      searchPlaceholder: 'Search app label or package name...',
      filterAll: 'All Apps',
      filterConfigured: 'Spoofed Only',
      sysApps: 'System Apps',
      refreshBtn: 'Refresh',
      nativeMode: 'Native Environment (0-Touch DLCLOSE)',
      restartBtn: 'Force Stop',
      swBuild: 'Bionic/ART Props',
      swGl: 'Vulkan/GLES/DRM',
      swVfs: 'Per-Core Freq/DDR/VFS',
      swProbe: 'Probe & Backtrace',
      swSdk: 'Force Spoof SDK',
    },
    probe: {
      crashTitle: 'Native Crash Signal Inspector (crash_events.log)',
      crashEmpty: 'All target processes healthy. No SIGSEGV/SIGABRT captured.',
      teleTitle: 'Live Zygisk Injection Telemetry (launch_history.log)',
      teleEmpty: 'No recent app launches recorded yet.',
      clearTele: 'Clear',
      radarTitle: 'Mirage Probe · Hardware Access & Native Stacktrace',
      radarDesc: 'Powered by SCM_RIGHTS direct file descriptors, bypassing ROM logd restrictions with live _Unwind_Backtrace symbol resolution.',
      filterPlaceholder: 'Filter package, property, or caller .so...',
      unmappedOnly: 'Unmapped Only',
      clearProbe: 'Clear Logs',
      captureBtn: '+ Capture Prop',
      stackPrefix: 'Backtrace:',
    },
  },
};

const savedLocale = (localStorage.getItem('mirage_locale') as LocaleCode) || 'zh-CN';
export const currentLocale = ref<LocaleCode>(
  messages[savedLocale] ? savedLocale : 'zh-CN'
);

export function setLocale(loc: LocaleCode) {
  currentLocale.value = loc;
  localStorage.setItem('mirage_locale', loc);
}

export function cycleLocale(): LocaleCode {
  const order: LocaleCode[] = ['zh-CN', 'zh-TW', 'en-US'];
  const next = order[(order.indexOf(currentLocale.value) + 1) % order.length];
  setLocale(next);
  return next;
}

export function useI18n() {
  const dict = computed(() => messages[currentLocale.value]);

  function t(path: string, params?: Record<string, string | number>): string {
    const keys = path.split('.');
    let cur: any = dict.value;
    for (const k of keys) {
      cur = cur?.[k];
    }
    if (typeof cur !== 'string') return path;
    if (!params) return cur;
    return cur.replace(/\{(\w+)\}/g, (_, k) => String(params[k] ?? ''));
  }

  return { locale: currentLocale, t, setLocale, cycleLocale };
}

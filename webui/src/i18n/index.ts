import { ref, computed } from 'vue';

export type LocaleCode = 'zh-CN' | 'zh-TW' | 'en-US';

const messages = {
  'zh-CN': {
    app: {
      title: '蜃楼工坊',
      subtitle: '光学液态玻璃 · 零 Fork 硬件虚拟化中枢',
      activeBadge: '已幻化 {count} 款',
      themeAuto: '跟随系统',
      themeLight: '浅色护眼',
      themeDark: '深空黑曜',
      checkUpdate: '检查更新',
      upToDate: '当前已是最新版本 (v{ver})',
      hasUpdate: '发现新版本 v{ver}！点击前往下载',
      updateError: '检查更新失败，请检查网络连接',
      orbHint: '💡 提示：底部液态玻璃球支持直接按住拖动与长按透镜放大',
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
      clusterLabel: '异构每核心频率拓扑',
      modalTitleEdit: '编辑全息硬件模板',
      modalTitleNew: '铸造全息硬件模板',
      secIdentity: '一、身份标识、深度 Build 与受限属性区',
      secGraphics: '二、Vulkan + OpenGL ES + MediaDrm 渲染层',
      secVfs: '三、异构单核频率 (cpufreq)、对称 DDR 与默认 DPI',
      clusterPresetTitle: '异构每核心频率分簇拓扑 (起始核-结束核:最小kHz:最大kHz)',
      saveBtn: '保存并零延迟编译',
      cancelBtn: '取消',
    },
    alchemy: {
      title: 'build.prop 全分区炼金解析器',
      desc: '导入或粘贴 ROM 的 build.prop，自动提取 MODEL、BOARD、SOC_MODEL、DISPLAY_ID、INCREMENTAL、安全补丁、DPI，并推导异构单核频率与 LPDDR 规格。',
      dropTitle: '点击导入本地 .prop / .txt 属性文件',
      dropSub: '支持 system / vendor / odm / product 全分区文件',
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
      restartBtn: '重启应用',
      swBuild: 'Bionic/ART/受限属性',
      swGl: 'Vulkan/GLES/DRM',
      swVfs: '单核频率/对称内存/VFS',
      swProbe: '探针与调用栈',
      swSdk: '强改 SDK 版本',
      dpiLabel: '独立 DPI 缩放 (0=默认):',
    },
    probe: {
      crashTitle: 'Native 崩溃信号现场捕获 (crash_events.log)',
      crashEmpty: '运行稳定，未捕获到任何目标应用 Native 崩溃信号。',
      teleTitle: 'Zygisk 实时注入遥测 (launch_history.log)',
      teleEmpty: '暂无启动遥测记录，运行已绑定的应用后自动生成。',
      clearTele: '清空遥测',
      radarTitle: '幻境探针 · 硬件嗅探与调用栈回溯',
      radarDesc: '由 Root 守护线程实时落盘，精准还原属性读取、VFS 访问与 .so!函数+偏移量 调用栈。',
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
      subtitle: '光學液態玻璃 · 零 Fork 硬體虛擬化中樞',
      activeBadge: '已幻化 {count} 款',
      themeAuto: '跟隨系統',
      themeLight: '淺色護眼',
      themeDark: '深空黑曜',
      checkUpdate: '檢查更新',
      upToDate: '目前為最新版本 (v{ver})',
      hasUpdate: '發現新版本 v{ver}！點擊前往下載',
      updateError: '檢查更新失敗，請檢查網路連線',
      orbHint: '💡 提示：底部液態玻璃球支援直接按住拖動與長按透鏡放大',
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
      clusterLabel: '異構每核心頻率拓撲',
      modalTitleEdit: '編輯全息硬體模板',
      modalTitleNew: '鑄造全息硬體模板',
      secIdentity: '一、身份標識、深度 Build 與受限屬性區',
      secGraphics: '二、Vulkan + OpenGL ES + MediaDrm 渲染層',
      secVfs: '三、異構單核頻率 (cpufreq)、對稱 DDR 與預設 DPI',
      clusterPresetTitle: '異構每核心頻率分簇拓撲 (起始核-結束核:最小kHz:最大kHz)',
      saveBtn: '儲存並零延遲編譯',
      cancelBtn: '取消',
    },
    alchemy: {
      title: 'build.prop 全分區煉金解析器',
      desc: '匯入或貼上 ROM 的 build.prop，自動提取 MODEL、BOARD、SOC_MODEL、DISPLAY_ID、INCREMENTAL、安全修補程式、DPI，並推導異構單核頻率。',
      dropTitle: '點擊匯入本機 .prop / .txt 屬性檔案',
      dropSub: '支援 system / vendor / odm / product 全分區檔案',
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
      restartBtn: '重啟應用',
      swBuild: 'Bionic/ART/受限屬性',
      swGl: 'Vulkan/GLES/DRM',
      swVfs: '單核頻率/對稱記憶體/VFS',
      swProbe: '探針與調用棧',
      swSdk: '強改 SDK 版本',
      dpiLabel: '獨立 DPI 縮放 (0=預設):',
    },
    probe: {
      crashTitle: 'Native 崩潰信號現場捕獲 (crash_events.log)',
      crashEmpty: '運行穩定，未捕獲到任何目標應用 Native 崩潰信號。',
      teleTitle: 'Zygisk 實時注入遙測 (launch_history.log)',
      teleEmpty: '暫無啟動遙測記錄，運行已綁定的應用後自動生成。',
      clearTele: '清空遙測',
      radarTitle: '幻境探針 · 硬體嗅探與調用棧回溯',
      radarDesc: '由 Root 守護線程實時落盤，精準還原屬性讀取、VFS 訪問與 .so!函數+偏移量 調用棧。',
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
      subtitle: 'Optical Liquid Glass · Zero-Fork Hypervisor',
      activeBadge: '{count} Spoofed',
      themeAuto: 'System Auto',
      themeLight: 'Daylight',
      themeDark: 'Obsidian',
      checkUpdate: 'Update',
      upToDate: 'Already up to date (v{ver})',
      hasUpdate: 'New version v{ver} available!',
      updateError: 'Failed to check updates',
      orbHint: '💡 Tip: Hold & drag the liquid glass orb below, or long-press to magnify',
    },
    nav: {
      templates: 'Templates',
      alchemy: 'Prop Alchemy',
      apps: 'App Matrix',
      probe: 'Probe Radar',
    },
    templates: {
      header: 'Holographic Hardware Templates',
      exportBtn: 'Export',
      newBtn: 'Forge Device',
      editBtn: 'Edit Topology',
      deleteBtn: 'Delete',
      clusterLabel: 'Per-Core Cluster Topology',
      modalTitleEdit: 'Edit Hardware Template',
      modalTitleNew: 'Forge Hardware Template',
      secIdentity: '1. Identity, Deep Build & Restricted SELinux Props',
      secGraphics: '2. Vulkan + OpenGL ES + MediaDrm Pipeline',
      secVfs: '3. Per-Core Cpufreq, Symmetric DDR & Default DPI',
      clusterPresetTitle: 'Heterogeneous CPU Clusters (first-last:minKhz:maxKhz)',
      saveBtn: 'Save & Zero-Fork Compile',
      cancelBtn: 'Cancel',
    },
    alchemy: {
      title: 'build.prop Alchemy Engine',
      desc: 'Import or paste any ROM build.prop to extract MODEL, BOARD, SOC_MODEL, DISPLAY_ID, INCREMENTAL, SECURITY_PATCH, DPI, and infer CPU clusters.',
      dropTitle: 'Tap to import local .prop / .txt file',
      dropSub: 'Supports system / vendor / odm / product build.prop',
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
      swBuild: 'Bionic/ART/Restricted',
      swGl: 'Vulkan/GLES/DRM',
      swVfs: 'Per-Core Freq/DDR/VFS',
      swProbe: 'Probe & Backtrace',
      swSdk: 'Force Spoof SDK',
      dpiLabel: 'Per-App DPI (0=Default):',
    },
    probe: {
      crashTitle: 'Native Crash Signal Inspector (crash_events.log)',
      crashEmpty: 'All target processes healthy. No SIGSEGV/SIGABRT captured.',
      teleTitle: 'Live Zygisk Injection Telemetry (launch_history.log)',
      teleEmpty: 'No recent app launches recorded yet.',
      clearTele: 'Clear',
      radarTitle: 'Mirage Probe · Hardware Access & Native Stacktrace',
      radarDesc: 'Logged directly by Root Companion sink thread with live _Unwind_Backtrace symbol resolution.',
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

export function cycleLocale(): LocaleCode {
  const order: LocaleCode[] = ['zh-CN', 'zh-TW', 'en-US'];
  const next = order[(order.indexOf(currentLocale.value) + 1) % order.length];
  currentLocale.value = next;
  localStorage.setItem('mirage_locale', next);
  return next;
}

export function useI18n() {
  const dict = computed(() => messages[currentLocale.value]);
  function t(path: string, params?: Record<string, string | number>): string {
    const keys = path.split('.');
    let cur: any = dict.value;
    for (const k of keys) cur = cur?.[k];
    if (typeof cur !== 'string') return path;
    if (!params) return cur;
    return cur.replace(/\{(\w+)\}/g, (_, k) => String(params[k] ?? ''));
  }
  return { locale: currentLocale, t, cycleLocale };
}

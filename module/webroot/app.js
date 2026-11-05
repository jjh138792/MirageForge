const DATA_DIR = "/data/adb/mirage_forge";
const CONFIG_FILE = `${DATA_DIR}/config.json`;
const APPS_DIR = `${DATA_DIR}/apps`;

let appState = {
  templates: {},
  // appRoutes: { "com.pkg": { tplId, build, gl, vfs, probe } }
  appRoutes: {},
  // installedApps: [{ packageName, appLabel, versionName, isSystem, uid }]
  installedApps: [],
  includeSystemApps: false,
  filterMode: "all",
  searchQuery: ""
};

let cbCounter = 0;
function ksuExec(command) {
  return new Promise((resolve) => {
    if (typeof ksu !== "undefined" && typeof ksu.exec === "function") {
      const cbName = `mirage_cb_${Date.now()}_${cbCounter++}`;
      window[cbName] = (errno, stdout, stderr) => {
        delete window[cbName];
        resolve({ errno, stdout: stdout || "", stderr: stderr || "" });
      };
      ksu.exec(command, "{}", cbName);
    } else {
      console.log("[Mock Exec]", command);
      resolve({ errno: 0, stdout: "", stderr: "" });
    }
  });
}

function toast(msg) {
  if (typeof ksu !== "undefined" && typeof ksu.toast === "function") {
    ksu.toast(msg);
  } else {
    document.getElementById("statusPill").textContent = msg;
  }
}

function toBase64(str) {
  return btoa(unescape(encodeURIComponent(str)));
}

function normalizeRoute(routeVal) {
  if (!routeVal) return null;
  if (typeof routeVal === "string") {
    return { tplId: routeVal, build: true, gl: true, vfs: true, probe: false };
  }
  if (typeof routeVal === "object" && routeVal.tplId) {
    return {
      tplId: routeVal.tplId,
      build: routeVal.build !== false,
      gl: routeVal.gl !== false,
      vfs: routeVal.vfs !== false,
      probe: Boolean(routeVal.probe)
    };
  }
  return null;
}

// 编译单应用 v4.0 全息运行态配置文件 (.conf)，严格对齐 C++ parse_config_kv 协议
function buildRuntimeConf(tpl, route) {
  const abis = tpl.SUPPORTED_ABIS || "arm64-v8a,armeabi-v7a,armeabi";
  const lines = [
    `META:ENABLE_BUILD=${route.build ? "1" : "0"}`,
    `META:ENABLE_GL=${route.gl ? "1" : "0"}`,
    `META:ENABLE_VFS=${route.vfs ? "1" : "0"}`,
    `META:ENABLE_PROBE=${route.probe ? "1" : "0"}`,
    // OpenGL ES & Vulkan 双管线参数
    `GLES:VENDOR=${tpl.GL_VENDOR || "Qualcomm"}`,
    `GLES:RENDERER=${tpl.GL_RENDERER || "Adreno (TM) 830"}`,
    `GLES:VERSION=${tpl.GL_VERSION || "OpenGL ES 3.2 V@0800.0"}`,
    // VFS 隐身节点与 Syscall (sysinfo/sysconf/uname) 参数
    `VFS:CPU_HARDWARE=${tpl.CPU_HARDWARE || "Qualcomm Technologies, Inc SM8750"}`,
    `VFS:CPU_ARCH=aarch64`,
    `VFS:CPU_CORES=${tpl.CPU_CORES || 8}`,
    `VFS:CPU_MAX_FREQ=${tpl.CPU_MAX_FREQ || 4320000}`,
    `VFS:RAM_GB=${tpl.RAM_GB || 16}`,
    `VFS:SOC_ID=${tpl.SOC_ID || "618"}`,
    `VFS:SOC_MACHINE=${tpl.SOC_MACHINE || tpl.SOC_MODEL || "SM8750"}`,
    `VFS:KERNEL_RELEASE=${tpl.KERNEL_RELEASE || "6.6.30-android15-8-g94b8f12ab4c1"}`,
    `VFS:KERNEL_VERSION=${tpl.KERNEL_VERSION || "#1 SMP PREEMPT Fri Nov 15 12:00:00 UTC 2025"}`,
    // Java android.os.Build 全类型反射池
    `BUILD:MODEL=${tpl.MODEL || ""}`,
    `BUILD:BRAND=${tpl.BRAND || ""}`,
    `BUILD:MANUFACTURER=${tpl.MANUFACTURER || ""}`,
    `BUILD:DEVICE=${tpl.DEVICE || ""}`,
    `BUILD:PRODUCT=${tpl.PRODUCT || ""}`,
    `BUILD:HARDWARE=${tpl.HARDWARE || "qcom"}`,
    `BUILD:BOARD=${tpl.BOARD || ""}`,
    `BUILD:BOOTLOADER=${tpl.BOOTLOADER || "unknown"}`,
    `BUILD:SOC_MANUFACTURER=${tpl.SOC_MANUFACTURER || "Qualcomm"}`,
    `BUILD:SOC_MODEL=${tpl.SOC_MODEL || ""}`,
    `BUILD:ID=${tpl.BUILD_ID || ""}`,
    `BUILD:FINGERPRINT=${tpl.FINGERPRINT || ""}`,
    `BUILD:SUPPORTED_ABIS=${abis}`,
    `BUILD:SUPPORTED_64_BIT_ABIS=arm64-v8a`,
    `BUILD:SUPPORTED_32_BIT_ABIS=armeabi-v7a,armeabi`,
    `BUILD:TAGS=release-keys`,
    `BUILD:TYPE=user`,
    `BUILD:TIME=${tpl.BUILD_TIME || "1736899200000"}`,
    // Java android.os.Build$VERSION 反射池
    `VERSION:RELEASE=${tpl.RELEASE || "15"}`,
    `VERSION:SDK_INT=${tpl.SDK_INT || "35"}`,
    `VERSION:DEVICE_INITIAL_SDK_INT=${tpl.SDK_INT || "35"}`,
    `VERSION:PREVIEW_SDK_INT=0`,
    `VERSION:SECURITY_PATCH=${tpl.SECURITY_PATCH || "2025-02-01"}`,
    // Bionic 1:1 内存池多分区 SystemProperties 映射
    `ro.product.model=${tpl.MODEL || ""}`,
    `ro.product.system.model=${tpl.MODEL || ""}`,
    `ro.product.vendor.model=${tpl.MODEL || ""}`,
    `ro.product.odm.model=${tpl.MODEL || ""}`,
    `ro.product.product.model=${tpl.MODEL || ""}`,
    `ro.product.brand=${tpl.BRAND || ""}`,
    `ro.product.system.brand=${tpl.BRAND || ""}`,
    `ro.product.vendor.brand=${tpl.BRAND || ""}`,
    `ro.product.odm.brand=${tpl.BRAND || ""}`,
    `ro.product.manufacturer=${tpl.MANUFACTURER || ""}`,
    `ro.product.system.manufacturer=${tpl.MANUFACTURER || ""}`,
    `ro.product.vendor.manufacturer=${tpl.MANUFACTURER || ""}`,
    `ro.product.odm.manufacturer=${tpl.MANUFACTURER || ""}`,
    `ro.product.device=${tpl.DEVICE || ""}`,
    `ro.product.system.device=${tpl.DEVICE || ""}`,
    `ro.product.vendor.device=${tpl.DEVICE || ""}`,
    `ro.product.odm.device=${tpl.DEVICE || ""}`,
    `ro.build.product=${tpl.DEVICE || ""}`,
    `ro.product.name=${tpl.PRODUCT || ""}`,
    `ro.product.system.name=${tpl.PRODUCT || ""}`,
    `ro.product.vendor.name=${tpl.PRODUCT || ""}`,
    `ro.product.board=${tpl.BOARD || ""}`,
    `ro.board.platform=${tpl.BOARD || ""}`,
    `ro.hardware=${tpl.HARDWARE || "qcom"}`,
    `ro.boot.hardware=${tpl.HARDWARE || "qcom"}`,
    `ro.soc.manufacturer=${tpl.SOC_MANUFACTURER || ""}`,
    `ro.soc.model=${tpl.SOC_MODEL || ""}`,
    `ro.product.cpu.abi=arm64-v8a`,
    `ro.product.cpu.abilist=${abis}`,
    `ro.product.cpu.abilist64=arm64-v8a`,
    `ro.product.cpu.abilist32=armeabi-v7a,armeabi`,
    `ro.build.id=${tpl.BUILD_ID || ""}`,
    `ro.build.date.utc=${Math.floor(Number(tpl.BUILD_TIME || 1736899200000) / 1000)}`,
    `ro.build.version.release=${tpl.RELEASE || "15"}`,
    `ro.build.version.sdk=${tpl.SDK_INT || "35"}`,
    `ro.build.version.security_patch=${tpl.SECURITY_PATCH || ""}`,
    `ro.build.fingerprint=${tpl.FINGERPRINT || ""}`,
    `ro.product.build.fingerprint=${tpl.FINGERPRINT || ""}`,
    `ro.system.build.fingerprint=${tpl.FINGERPRINT || ""}`,
    `ro.vendor.build.fingerprint=${tpl.FINGERPRINT || ""}`,
    `ro.odm.build.fingerprint=${tpl.FINGERPRINT || ""}`,
    `ro.build.tags=release-keys`,
    `ro.build.type=user`,
    `ro.boot.vbmeta.device_state=locked`,
    `ro.boot.verifiedbootstate=green`,
    `ro.boot.flash.locked=1`,
    `ro.boot.veritymode=enforcing`,
    `ro.debuggable=0`,
    `ro.secure=1`
  ];

  if (tpl.CUSTOM_PROPS) {
    tpl.CUSTOM_PROPS.split(/\r?\n/).forEach(raw => {
      const line = raw.trim();
      if (line && !line.startsWith("#") && line.includes("=")) {
        lines.push(line);
      }
    });
  }

  return lines.filter(l => !l.endsWith("=")).join("\n") + "\n";
}

async function saveAndSyncConfig() {
  const payload = JSON.stringify({
    templates: appState.templates,
    appRoutes: appState.appRoutes
  }, null, 2);

  const b64Config = toBase64(payload);
  let script = `mkdir -p "${APPS_DIR}" && echo "${b64Config}" | base64 -d > "${CONFIG_FILE}" && rm -f "${APPS_DIR}"/*.conf\n`;

  let activeCount = 0;
  for (const [pkg, rawRoute] of Object.entries(appState.appRoutes)) {
    const route = normalizeRoute(rawRoute);
    if (!route) continue;
    const tpl = appState.templates[route.tplId];
    if (tpl && /^[a-zA-Z0-9._]+$/.test(pkg)) {
      activeCount++;
      const confB64 = toBase64(buildRuntimeConf(tpl, route));
      script += `echo "${confB64}" | base64 -d > "${APPS_DIR}/${pkg}.conf"\n`;
    }
  }
  script += `chmod 0700 "${DATA_DIR}" "${APPS_DIR}" && chmod 0600 "${CONFIG_FILE}" "${APPS_DIR}"/*.conf 2>/dev/null\n`;

  await ksuExec(script);
  document.getElementById("statusPill").textContent = `已伪装 ${activeCount} 款应用`;
}

async function loadConfig() {
  const res = await ksuExec(`cat "${CONFIG_FILE}"`);
  if (res.errno === 0 && res.stdout.trim()) {
    try {
      const parsed = JSON.parse(res.stdout);
      appState.templates = parsed.templates || {};
      const rawRoutes = parsed.appRoutes || {};
      appState.appRoutes = {};
      for (const [k, v] of Object.entries(rawRoutes)) {
        const norm = normalizeRoute(v);
        if (norm) appState.appRoutes[k] = norm;
      }
    } catch (e) {
      console.error("Config parse error:", e);
    }
  }
  renderTemplates();
  await saveAndSyncConfig();
}

// 调用 KernelSU 原生 listPackages + getPackagesInfo 接口读取真实应用名与图标
async function loadInstalledPackages() {
  let pkgNames = [];
  const pkgType = appState.includeSystemApps ? "all" : "user";

  if (typeof ksu !== "undefined" && typeof ksu.listPackages === "function") {
    try {
      pkgNames = JSON.parse(ksu.listPackages(pkgType));
    } catch (e) {}
  }

  if (!pkgNames || pkgNames.length === 0) {
    const flag = appState.includeSystemApps ? "" : "-3";
    const res = await ksuExec(`pm list packages ${flag} | sed 's/package://g'`);
    if (res.stdout) {
      pkgNames = res.stdout.split(/\r?\n/).map(s => s.trim()).filter(Boolean);
    }
  }

  const allSet = new Set([...Object.keys(appState.appRoutes), ...(pkgNames || [])]);
  const mergedList = Array.from(allSet);

  let infoMap = {};
  if (typeof ksu !== "undefined" && typeof ksu.getPackagesInfo === "function" && mergedList.length > 0) {
    try {
      const rawInfos = JSON.parse(ksu.getPackagesInfo(JSON.stringify(mergedList)));
      if (Array.isArray(rawInfos)) {
        rawInfos.forEach(item => {
          if (item && item.packageName) infoMap[item.packageName] = item;
        });
      }
    } catch (e) {}
  }

  appState.installedApps = mergedList.map(pkg => {
    const meta = infoMap[pkg] || {};
    return {
      packageName: pkg,
      appLabel: meta.appLabel || pkg,
      versionName: meta.versionName || "",
      uid: meta.uid || ""
    };
  }).sort((a, b) => {
    const aConf = Boolean(appState.appRoutes[a.packageName]);
    const bConf = Boolean(appState.appRoutes[b.packageName]);
    if (aConf !== bConf) return aConf ? -1 : 1;
    return a.appLabel.localeCompare(b.appLabel, "zh-CN");
  });

  renderAppList();
}

function renderTemplates() {
  const container = document.getElementById("templateList");
  container.innerHTML = "";

  Object.values(appState.templates).forEach(tpl => {
    const card = document.createElement("div");
    card.className = "tpl-card";
    card.innerHTML = `
      <div class="tpl-head">
        <div class="tpl-title">${tpl.name}</div>
        <div class="tpl-badge">${tpl.GL_RENDERER || "Adreno GPU"}</div>
      </div>
      <div class="tpl-meta">
        <span>型号: <b>${tpl.MODEL || "-"}</b></span>
        <span>SoC: <b>${tpl.SOC_MACHINE || tpl.SOC_MODEL || "-"} (#${tpl.SOC_ID || "618"})</b></span>
        <span>VFS 运存: <b>${tpl.RAM_GB || 16} GB</b></span>
        <span>CPU 拓扑: <b>${tpl.CPU_CORES || 8}核 @ ${((tpl.CPU_MAX_FREQ || 4320000) / 1e6).toFixed(2)}GHz</b></span>
      </div>
      <div class="tpl-actions">
        <button class="btn-secondary btn-edit" data-id="${tpl.id}">编辑全息拓扑</button>
        <button class="btn-danger btn-del" data-id="${tpl.id}">删除</button>
      </div>
    `;
    container.appendChild(card);
  });

  container.querySelectorAll(".btn-edit").forEach(btn => {
    btn.onclick = () => openTemplateModal(appState.templates[btn.dataset.id]);
  });

  container.querySelectorAll(".btn-del").forEach(btn => {
    btn.onclick = async () => {
      const id = btn.dataset.id;
      delete appState.templates[id];
      for (const [pkg, route] of Object.entries(appState.appRoutes)) {
        if (route.tplId === id) delete appState.appRoutes[pkg];
      }
      await saveAndSyncConfig();
      renderTemplates();
      renderAppList();
      toast("模板已删除");
    };
  });
}

// v4.0 智能 SoC / GPU / VFS 参数推导矩阵
function inferHardwareTopology(board, socModel) {
  const token = `${board} ${socModel}`.toLowerCase();
  if (token.includes("sun") || token.includes("sm8750")) {
    return {
      glVendor: "Qualcomm", glRenderer: "Adreno (TM) 830",
      cpuHw: "Qualcomm Technologies, Inc SM8750", socId: "618", socMachine: "SM8750", maxFreq: 4320000
    };
  }
  if (token.includes("pineapple") || token.includes("sm8650")) {
    return {
      glVendor: "Qualcomm", glRenderer: "Adreno (TM) 750",
      cpuHw: "Qualcomm Technologies, Inc SM8650", socId: "557", socMachine: "SM8650", maxFreq: 3300000
    };
  }
  if (token.includes("kalama") || token.includes("sm8550")) {
    return {
      glVendor: "Qualcomm", glRenderer: "Adreno (TM) 740",
      cpuHw: "Qualcomm Technologies, Inc SM8550", socId: "519", socMachine: "SM8550", maxFreq: 3190000
    };
  }
  if (token.includes("mt6991")) {
    return {
      glVendor: "ARM", glRenderer: "Immortalis-G925 MC12",
      cpuHw: "MT6991", socId: "6991", socMachine: "MT6991", maxFreq: 3620000
    };
  }
  if (token.includes("mt6989")) {
    return {
      glVendor: "ARM", glRenderer: "Immortalis-G720 MC12",
      cpuHw: "MT6989", socId: "6989", socMachine: "MT6989", maxFreq: 3250000
    };
  }
  return {
    glVendor: "Qualcomm", glRenderer: "Adreno (TM) 830",
    cpuHw: `Qualcomm Technologies, Inc ${socModel || board || "SM8750"}`,
    socId: "618", socMachine: socModel || "SM8750", maxFreq: 4320000
  };
}

function parseBuildPropText(rawText) {
  const map = {};
  rawText.split(/\r?\n/).forEach(line => {
    const trimmed = line.trim();
    if (!trimmed || trimmed.startsWith("#")) return;
    const eqIdx = trimmed.indexOf("=");
    if (eqIdx === -1) return;
    const key = trimmed.slice(0, eqIdx).trim();
    const val = trimmed.slice(eqIdx + 1).trim();
    if (key && val) map[key] = val;
  });

  const consumed = new Set();
  const pick = (...keys) => {
    for (const k of keys) {
      consumed.add(k);
      if (map[k]) return map[k];
    }
    return "";
  };

  const model = pick("ro.product.model", "ro.product.vendor.model", "ro.product.odm.model", "ro.product.system.model");
  const brand = pick("ro.product.brand", "ro.product.vendor.brand", "ro.product.odm.brand", "ro.product.system.brand");
  const manufacturer = pick("ro.product.manufacturer", "ro.product.vendor.manufacturer", "ro.product.odm.manufacturer", "ro.product.system.manufacturer", brand);
  const device = pick("ro.product.device", "ro.product.vendor.device", "ro.product.odm.device", "ro.product.system.device", "ro.build.product");
  const product = pick("ro.product.name", "ro.product.vendor.name", "ro.product.odm.name", "ro.product.system.name");
  const board = pick("ro.board.platform", "ro.product.board");
  const hardware = pick("ro.hardware", "ro.boot.hardware");
  const socMan = pick("ro.soc.manufacturer");
  const socModel = pick("ro.soc.model", "ro.mediatek.platform");
  const abis = pick("ro.product.cpu.abilist", "ro.system.product.cpu.abilist") || "arm64-v8a,armeabi-v7a,armeabi";
  const buildId = pick("ro.build.id", "ro.product.build.id");
  const utcSec = pick("ro.build.date.utc");
  const release = pick("ro.build.version.release", "ro.product.build.version.release");
  const sdk = pick("ro.build.version.sdk", "ro.product.build.version.sdk");
  const patch = pick("ro.build.version.security_patch");
  const fingerprint = pick("ro.build.fingerprint", "ro.product.build.fingerprint", "ro.vendor.build.fingerprint", "ro.system.build.fingerprint");

  if (!model && !brand && !fingerprint) {
    toast("未识别到有效的 build.prop 属性！");
    return null;
  }

  const hw = inferHardwareTopology(board, socModel);
  const extraLines = [];
  for (const [k, v] of Object.entries(map)) {
    if (!consumed.has(k) && (k.startsWith("ro.") || k.startsWith("persist."))) {
      extraLines.push(`${k}=${v}`);
    }
  }

  return {
    id: `tpl_${Date.now()}`,
    name: `${brand ? brand + " " : ""}${map["ro.product.marketname"] || model || device}`,
    MODEL: model,
    BRAND: brand,
    MANUFACTURER: manufacturer || brand,
    DEVICE: device,
    PRODUCT: product || device,
    BOARD: board,
    HARDWARE: hardware || "qcom",
    BOOTLOADER: "unknown",
    SOC_MANUFACTURER: socMan || (hw.glVendor === "ARM" ? "Mediatek" : "Qualcomm"),
    SOC_MODEL: socModel || hw.socMachine,
    SUPPORTED_ABIS: abis,
    BUILD_ID: buildId || "AP3A.240905.015.A2",
    BUILD_TIME: utcSec ? String(Number(utcSec) * 1000) : "1736899200000",
    RELEASE: release || "15",
    SDK_INT: sdk || "35",
    SECURITY_PATCH: patch || "2025-02-01",
    FINGERPRINT: fingerprint,
    GL_VENDOR: hw.glVendor,
    GL_RENDERER: hw.glRenderer,
    GL_VERSION: "OpenGL ES 3.2 V@0800.0",
    CPU_HARDWARE: hw.cpuHw,
    CPU_CORES: 8,
    CPU_MAX_FREQ: hw.maxFreq,
    RAM_GB: 16,
    SOC_ID: hw.socId,
    SOC_MACHINE: hw.socMachine,
    KERNEL_RELEASE: "6.6.30-android15-8-g94b8f12ab4c1",
    KERNEL_VERSION: "#1 SMP PREEMPT Fri Nov 15 12:00:00 UTC 2025",
    CUSTOM_PROPS: extraLines.slice(0, 100).join("\n")
  };
}

function renderAppList() {
  const container = document.getElementById("appList");
  container.innerHTML = "";

  const tplOptions = [`<option value="">-- 原生环境 (DLCLOSE 零残留) --</option>`]
    .concat(
      Object.values(appState.templates).map(
        t => `<option value="${t.id}">${t.name}</option>`
      )
    )
    .join("");

  const filtered = appState.installedApps.filter(app => {
    const pkg = app.packageName;
    if (appState.filterMode === "configured" && !appState.appRoutes[pkg]) return false;
    if (appState.searchQuery) {
      const q = appState.searchQuery;
      const matchPkg = pkg.toLowerCase().includes(q);
      const matchLabel = (app.appLabel || "").toLowerCase().includes(q);
      if (!matchPkg && !matchLabel) return false;
    }
    return true;
  });

  filtered.forEach(app => {
    const pkg = app.packageName;
    const route = normalizeRoute(appState.appRoutes[pkg]);
    const currentTpl = route ? route.tplId : "";
    const row = document.createElement("div");
    row.className = "app-item";

    let switchesHtml = "";
    if (route) {
      switchesHtml = `
        <div class="app-switches">
          <span class="sw-pill ${route.build ? "on" : ""}" data-sw="build">🧬 Bionic/ART属性: ${route.build ? "开" : "关"}</span>
          <span class="sw-pill ${route.gl ? "on" : ""}" data-sw="gl">🎮 Vulkan/GLES: ${route.gl ? "开" : "关"}</span>
          <span class="sw-pill ${route.vfs ? "on" : ""}" data-sw="vfs">🛡️ 隐身VFS/Syscall: ${route.vfs ? "开" : "关"}</span>
          <span class="sw-pill ${route.probe ? "probe-on" : ""}" data-sw="probe">📡 幻境探针: ${route.probe ? "嗅探中" : "关"}</span>
        </div>
      `;
    }

    const verBadge = app.versionName ? ` · v${app.versionName}` : "";
    row.innerHTML = `
      <div class="app-item-top">
        <div class="app-info-wrap">
          <img class="app-icon" src="ksu://icon/${pkg}" onerror="this.style.display='none'" alt="" />
          <div class="app-title-box">
            <div class="app-label">${app.appLabel}</div>
            <div class="app-pkg">${pkg}${verBadge}</div>
          </div>
        </div>
        <div class="app-controls">
          <select class="app-select">${tplOptions}</select>
          <button class="btn-secondary btn-kill" title="强行停止并重注入">⚡重启</button>
        </div>
      </div>
      ${switchesHtml}
    `;

    const selectEl = row.querySelector("select");
    selectEl.value = currentTpl;
    selectEl.onchange = async (e) => {
      const val = e.target.value;
      if (val) {
        appState.appRoutes[pkg] = {
          tplId: val,
          build: route ? route.build : true,
          gl: route ? route.gl : true,
          vfs: route ? route.vfs : true,
          probe: route ? route.probe : false
        };
      } else {
        delete appState.appRoutes[pkg];
      }
      await saveAndSyncConfig();
      renderAppList();
      toast(val ? `已为 ${app.appLabel} 挂载虚拟引擎` : `已恢复 ${app.appLabel} 原生环境`);
    };

    row.querySelector(".btn-kill").onclick = async () => {
      await ksuExec(`am force-stop "${pkg}"`);
      toast(`已强行停止 ${app.appLabel}，重新打开即可生效`);
    };

    row.querySelectorAll(".sw-pill").forEach(pill => {
      pill.onclick = async () => {
        const key = pill.dataset.sw;
        if (appState.appRoutes[pkg]) {
          appState.appRoutes[pkg][key] = !appState.appRoutes[pkg][key];
          await saveAndSyncConfig();
          renderAppList();
        }
      };
    });

    container.appendChild(row);
  });
}

// 扫描幻境探针日志，并提供「+ 捕获入库」一键写入模板能力
async function refreshProbeLogs() {
  const res = await ksuExec(`logcat -d -s MirageForge:I | grep "\\[PROBE\\]" | tail -n 150`);
  const listEl = document.getElementById("probeLogList");
  listEl.innerHTML = "";

  const filterText = document.getElementById("probeFilterInput").value.trim().toLowerCase();
  const lines = (res.stdout || "").split(/\r?\n/).filter(Boolean).reverse();

  const matched = lines.filter(l => !filterText || l.toLowerCase().includes(filterText));
  if (matched.length === 0) {
    listEl.innerHTML = `<div class="probe-empty">暂无匹配的探针日志。请在「应用分流矩阵」中点亮目标应用的 [📡 幻境探针]，运行目标应用后再点击刷新。</div>`;
    return;
  }

  matched.forEach(line => {
    const m = line.match(/\[PROBE\]\[([^\]]+)\]\[([^\]]+)\]\s*(.+?)\s*=>\s*(.+)$/);
    const row = document.createElement("div");
    row.className = "probe-row";

    if (m) {
      const [, pkg, domain, key, val] = m;
      const isUnmapped = domain.includes("UNMAPPED");
      const actionBtnHtml = isUnmapped
        ? `<button class="btn-capture" data-pkg="${pkg}" data-key="${key}">+ 捕获入库</button>`
        : "";

      row.innerHTML = `
        <div style="min-width:0;word-break:break-all;">
          <span class="probe-tag ${isUnmapped ? "unmapped" : ""}">${domain}</span>
          <b style="color:#00e5ff;margin-left:6px;">${pkg}</b>
          <span style="margin-left:8px;color:#f0f6fc;">${key}</span>
          <span style="color:#8b949e;"> ➔ ${val}</span>
        </div>
        ${actionBtnHtml}
      `;

      const capBtn = row.querySelector(".btn-capture");
      if (capBtn) {
        capBtn.onclick = async () => {
          const route = normalizeRoute(appState.appRoutes[pkg]);
          if (!route || !appState.templates[route.tplId]) {
            toast(`未找到 ${pkg} 绑定的模板`);
            return;
          }
          const tpl = appState.templates[route.tplId];
          const customVal = prompt(`为模板 [${tpl.name}] 设置属性 ${key} 的伪装值：`, "1");
          if (customVal === null) return;
          const newEntry = `${key}=${customVal.trim()}`;
          tpl.CUSTOM_PROPS = tpl.CUSTOM_PROPS ? `${tpl.CUSTOM_PROPS}\n${newEntry}` : newEntry;
          await saveAndSyncConfig();
          toast(`已将 ${newEntry} 写入模板 [${tpl.name}]`);
        };
      }
    } else {
      row.textContent = line;
    }
    listEl.appendChild(row);
  });
}

function openTemplateModal(tpl = null) {
  document.getElementById("modalTitle").textContent = tpl ? "编辑 v4.0 全息硬件模板" : "铸造 v4.0 全息硬件模板";
  document.getElementById("tplId").value = tpl ? tpl.id : `tpl_${Date.now()}`;
  document.getElementById("tplName").value = tpl ? tpl.name : "";
  document.getElementById("tplModel").value = tpl ? tpl.MODEL : "";
  document.getElementById("tplBrand").value = tpl ? tpl.BRAND : "";
  document.getElementById("tplManufacturer").value = tpl ? tpl.MANUFACTURER : "";
  document.getElementById("tplDevice").value = tpl ? tpl.DEVICE : "";
  document.getElementById("tplProduct").value = tpl ? tpl.PRODUCT : "";
  document.getElementById("tplBoard").value = tpl ? tpl.BOARD : "";
  document.getElementById("tplHardware").value = tpl ? tpl.HARDWARE : "qcom";
  document.getElementById("tplSocMan").value = tpl ? (tpl.SOC_MANUFACTURER || "Qualcomm") : "Qualcomm";
  document.getElementById("tplSocModel").value = tpl ? (tpl.SOC_MODEL || "") : "";
  document.getElementById("tplAbis").value = tpl ? (tpl.SUPPORTED_ABIS || "arm64-v8a,armeabi-v7a,armeabi") : "arm64-v8a,armeabi-v7a,armeabi";
  document.getElementById("tplBuildId").value = tpl ? (tpl.BUILD_ID || "AP3A.240905.015.A2") : "AP3A.240905.015.A2";
  document.getElementById("tplBuildTime").value = tpl ? (tpl.BUILD_TIME || "1736899200000") : "1736899200000";
  document.getElementById("tplRelease").value = tpl ? (tpl.RELEASE || "15") : "15";
  document.getElementById("tplSdk").value = tpl ? (tpl.SDK_INT || "35") : "35";
  document.getElementById("tplPatch").value = tpl ? (tpl.SECURITY_PATCH || "2025-02-01") : "2025-02-01";
  document.getElementById("tplFingerprint").value = tpl ? tpl.FINGERPRINT : "";
  document.getElementById("tplGlVendor").value = tpl ? (tpl.GL_VENDOR || "Qualcomm") : "Qualcomm";
  document.getElementById("tplGlRenderer").value = tpl ? (tpl.GL_RENDERER || "Adreno (TM) 830") : "Adreno (TM) 830";
  document.getElementById("tplGlVersion").value = tpl ? (tpl.GL_VERSION || "OpenGL ES 3.2 V@0800.0") : "OpenGL ES 3.2 V@0800.0";
  document.getElementById("tplCpuHw").value = tpl ? (tpl.CPU_HARDWARE || "Qualcomm Technologies, Inc SM8750") : "Qualcomm Technologies, Inc SM8750";
  document.getElementById("tplCpuCores").value = tpl ? (tpl.CPU_CORES || 8) : 8;
  document.getElementById("tplCpuFreq").value = tpl ? (tpl.CPU_MAX_FREQ || 4320000) : 4320000;
  document.getElementById("tplRamGb").value = tpl ? (tpl.RAM_GB || 16) : 16;
  document.getElementById("tplSocId").value = tpl ? (tpl.SOC_ID || "618") : "618";
  document.getElementById("tplSocMachine").value = tpl ? (tpl.SOC_MACHINE || "SM8750") : "SM8750";
  document.getElementById("tplKernelRel").value = tpl ? (tpl.KERNEL_RELEASE || "6.6.30-android15-8-g94b8f12ab4c1") : "6.6.30-android15-8-g94b8f12ab4c1";
  document.getElementById("tplKernelVer").value = tpl ? (tpl.KERNEL_VERSION || "#1 SMP PREEMPT Fri Nov 15 12:00:00 UTC 2025") : "#1 SMP PREEMPT Fri Nov 15 12:00:00 UTC 2025";
  document.getElementById("tplCustomProps").value = tpl ? (tpl.CUSTOM_PROPS || "") : "";
  document.getElementById("templateModal").classList.add("show");
}

document.addEventListener("DOMContentLoaded", async () => {
  document.querySelectorAll(".tab-btn").forEach(btn => {
    btn.onclick = () => {
      document.querySelectorAll(".tab-btn").forEach(b => b.classList.remove("active"));
      document.querySelectorAll(".tab-panel").forEach(p => p.classList.remove("active"));
      btn.classList.add("active");
      document.getElementById(btn.dataset.tab).classList.add("active");
      if (btn.dataset.tab === "tab-probe") refreshProbeLogs();
    };
  });

  document.getElementById("btnNewTemplate").onclick = () => openTemplateModal(null);
  document.getElementById("btnCancelModal").onclick = () => {
    document.getElementById("templateModal").classList.remove("show");
  };

  document.getElementById("btnExportJson").onclick = async () => {
    const outPath = "/sdcard/Download/MirageForge_v4_Backup.json";
    await ksuExec(`cp -f "${CONFIG_FILE}" "${outPath}"`);
    toast(`已导出全息配置至 ${outPath}`);
  };

  document.getElementById("btnSaveTemplate").onclick = async () => {
    const id = document.getElementById("tplId").value;
    const name = document.getElementById("tplName").value.trim();
    const MODEL = document.getElementById("tplModel").value.trim();
    const BRAND = document.getElementById("tplBrand").value.trim();
    if (!name || !MODEL) {
      toast("请填写模板名称与手机型号 (MODEL)");
      return;
    }
    appState.templates[id] = {
      id,
      name,
      MODEL,
      BRAND,
      MANUFACTURER: document.getElementById("tplManufacturer").value.trim() || BRAND,
      DEVICE: document.getElementById("tplDevice").value.trim(),
      PRODUCT: document.getElementById("tplProduct").value.trim(),
      BOARD: document.getElementById("tplBoard").value.trim(),
      HARDWARE: document.getElementById("tplHardware").value.trim(),
      BOOTLOADER: "unknown",
      SOC_MANUFACTURER: document.getElementById("tplSocMan").value.trim(),
      SOC_MODEL: document.getElementById("tplSocModel").value.trim(),
      SUPPORTED_ABIS: document.getElementById("tplAbis").value.trim(),
      BUILD_ID: document.getElementById("tplBuildId").value.trim(),
      BUILD_TIME: document.getElementById("tplBuildTime").value.trim() || "1736899200000",
      RELEASE: document.getElementById("tplRelease").value.trim(),
      SDK_INT: document.getElementById("tplSdk").value.trim(),
      SECURITY_PATCH: document.getElementById("tplPatch").value.trim(),
      FINGERPRINT: document.getElementById("tplFingerprint").value.trim(),
      GL_VENDOR: document.getElementById("tplGlVendor").value.trim(),
      GL_RENDERER: document.getElementById("tplGlRenderer").value.trim(),
      GL_VERSION: document.getElementById("tplGlVersion").value.trim(),
      CPU_HARDWARE: document.getElementById("tplCpuHw").value.trim(),
      CPU_CORES: parseInt(document.getElementById("tplCpuCores").value, 10) || 8,
      CPU_MAX_FREQ: parseInt(document.getElementById("tplCpuFreq").value, 10) || 4320000,
      RAM_GB: parseInt(document.getElementById("tplRamGb").value, 10) || 16,
      SOC_ID: document.getElementById("tplSocId").value.trim() || "618",
      SOC_MACHINE: document.getElementById("tplSocMachine").value.trim() || "SM8750",
      KERNEL_RELEASE: document.getElementById("tplKernelRel").value.trim(),
      KERNEL_VERSION: document.getElementById("tplKernelVer").value.trim(),
      CUSTOM_PROPS: document.getElementById("tplCustomProps").value.trim()
    };
    await saveAndSyncConfig();
    renderTemplates();
    renderAppList();
    document.getElementById("templateModal").classList.remove("show");
    toast("v4.0 全息模板已保存并下发至底层引擎");
  };

  const uploadZone = document.getElementById("uploadZone");
  const fileInput = document.getElementById("propFileInput");
  uploadZone.onclick = () => fileInput.click();
  fileInput.onchange = (e) => {
    const file = e.target.files[0];
    if (!file) return;
    const reader = new FileReader();
    reader.onload = (ev) => {
      document.getElementById("propRawText").value = ev.target.result;
      const parsed = parseBuildPropText(ev.target.result);
      if (parsed) openTemplateModal(parsed);
    };
    reader.readAsText(file);
  };

  document.getElementById("btnParseProp").onclick = () => {
    const raw = document.getElementById("propRawText").value;
    const parsed = parseBuildPropText(raw);
    if (parsed) openTemplateModal(parsed);
  };

  document.getElementById("btnClearProp").onclick = () => {
    document.getElementById("propRawText").value = "";
  };

  document.getElementById("appSearchInput").oninput = (e) => {
    appState.searchQuery = e.target.value.trim().toLowerCase();
    renderAppList();
  };

  document.querySelectorAll(".chip[data-filter]").forEach(chip => {
    chip.onclick = () => {
      document.querySelectorAll(".chip[data-filter]").forEach(c => c.classList.remove("active"));
      chip.classList.add("active");
      appState.filterMode = chip.dataset.filter;
      renderAppList();
    };
  });

  document.getElementById("btnToggleSysApps").onclick = (e) => {
    appState.includeSystemApps = !appState.includeSystemApps;
    e.target.textContent = `系统应用: ${appState.includeSystemApps ? "开" : "关"}`;
    e.target.classList.toggle("active", appState.includeSystemApps);
    loadInstalledPackages();
  };

  document.getElementById("btnRefreshApps").onclick = () => {
    loadInstalledPackages();
    toast("已刷新应用列表");
  };

  document.getElementById("btnRefreshProbe").onclick = () => refreshProbeLogs();
  document.getElementById("probeFilterInput").oninput = () => refreshProbeLogs();
  document.getElementById("btnClearProbe").onclick = async () => {
    await ksuExec("logcat -c");
    refreshProbeLogs();
    toast("已清空探针日志缓存");
  };

  await loadConfig();
  await loadInstalledPackages();
});

const DATA_DIR = "/data/adb/mirage_forge";
const CONFIG_FILE = `${DATA_DIR}/config.json`;
const APPS_DIR = `${DATA_DIR}/apps`;

let appState = {
  templates: {},
  appRoutes: {},
  installedPackages: [],
  filterMode: "all",
  searchQuery: ""
};

// KSU WebUI 原生 Shell 桥接封装
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
      // 浏览器调试预览回退模式
      console.log("[Mock Exec]", command);
      resolve({ errno: 0, stdout: "", stderr: "" });
    }
  });
}

function toast(msg) {
  if (typeof ksu !== "undefined" && typeof ksu.toast === "function") {
    ksu.toast(msg);
  } else {
    const pill = document.getElementById("statusPill");
    pill.textContent = msg;
  }
}

// Base64 UTF-8 编解码，彻底避免 Shell 特殊字符转义损坏 JSON
function toBase64(str) {
  return btoa(unescape(encodeURIComponent(str)));
}

// 构建供 Zygisk C++ Companion 极速读取的单应用属性映射表
function buildRuntimeConf(tpl) {
  const lines = [
    `BUILD:MODEL=${tpl.MODEL || ""}`,
    `BUILD:BRAND=${tpl.BRAND || ""}`,
    `BUILD:MANUFACTURER=${tpl.MANUFACTURER || ""}`,
    `BUILD:DEVICE=${tpl.DEVICE || ""}`,
    `BUILD:PRODUCT=${tpl.PRODUCT || ""}`,
    `BUILD:HARDWARE=${tpl.HARDWARE || ""}`,
    `BUILD:BOARD=${tpl.BOARD || ""}`,
    `BUILD:FINGERPRINT=${tpl.FINGERPRINT || ""}`,
    `ro.product.model=${tpl.MODEL || ""}`,
    `ro.product.system.model=${tpl.MODEL || ""}`,
    `ro.product.vendor.model=${tpl.MODEL || ""}`,
    `ro.product.odm.model=${tpl.MODEL || ""}`,
    `ro.product.brand=${tpl.BRAND || ""}`,
    `ro.product.system.brand=${tpl.BRAND || ""}`,
    `ro.product.vendor.brand=${tpl.BRAND || ""}`,
    `ro.product.manufacturer=${tpl.MANUFACTURER || ""}`,
    `ro.product.system.manufacturer=${tpl.MANUFACTURER || ""}`,
    `ro.product.vendor.manufacturer=${tpl.MANUFACTURER || ""}`,
    `ro.product.device=${tpl.DEVICE || ""}`,
    `ro.product.system.device=${tpl.DEVICE || ""}`,
    `ro.product.vendor.device=${tpl.DEVICE || ""}`,
    `ro.product.name=${tpl.PRODUCT || ""}`,
    `ro.product.board=${tpl.BOARD || ""}`,
    `ro.board.platform=${tpl.BOARD || ""}`,
    `ro.hardware=${tpl.HARDWARE || ""}`,
    `ro.build.fingerprint=${tpl.FINGERPRINT || ""}`,
    `ro.system.build.fingerprint=${tpl.FINGERPRINT || ""}`,
    `ro.vendor.build.fingerprint=${tpl.FINGERPRINT || ""}`
  ];
  return lines.filter(l => !l.endsWith("=")).join("\n") + "\n";
}

// 保存配置并同步生成 /data/adb/mirage_forge/apps/<pkg>.conf
async function saveAndSyncConfig() {
  const payload = JSON.stringify({
    templates: appState.templates,
    appRoutes: appState.appRoutes
  }, null, 2);

  const b64Config = toBase64(payload);
  let script = `mkdir -p "${APPS_DIR}" && echo "${b64Config}" | base64 -d > "${CONFIG_FILE}" && rm -f "${APPS_DIR}"/*.conf\n`;

  for (const [pkg, tplId] of Object.entries(appState.appRoutes)) {
    const tpl = appState.templates[tplId];
    if (tpl && /^[a-zA-Z0-9._]+$/.test(pkg)) {
      const confB64 = toBase64(buildRuntimeConf(tpl));
      script += `echo "${confB64}" | base64 -d > "${APPS_DIR}/${pkg}.conf"\n`;
    }
  }

  await ksuExec(script);
  const activeCount = Object.keys(appState.appRoutes).length;
  document.getElementById("statusPill").textContent = `已伪装 ${activeCount} 款应用`;
}

// 加载配置
async function loadConfig() {
  const res = await ksuExec(`cat "${CONFIG_FILE}"`);
  if (res.errno === 0 && res.stdout.trim()) {
    try {
      const parsed = JSON.parse(res.stdout);
      appState.templates = parsed.templates || {};
      appState.appRoutes = parsed.appRoutes || {};
    } catch (e) {
      console.error("Config parse error:", e);
    }
  }
  if (Object.keys(appState.templates).length === 0) {
    appState.templates = {
      xiaomi_15_pro: {
        id: "xiaomi_15_pro",
        name: "Xiaomi 15 Pro (澎湃旗舰)",
        MODEL: "2410DPN6CC",
        BRAND: "Xiaomi",
        MANUFACTURER: "Xiaomi",
        DEVICE: "haotian",
        PRODUCT: "haotian",
        HARDWARE: "qcom",
        BOARD: "sun",
        FINGERPRINT: "Xiaomi/haotian/haotian:15/AP3A.240905.015.A2/OS2.0.12.0.VOBCNXM:user/release-keys"
      }
    };
  }
  renderTemplates();
  const activeCount = Object.keys(appState.appRoutes).length;
  document.getElementById("statusPill").textContent = `已伪装 ${activeCount} 款应用`;
}

// 加载已安装第三方应用列表
async function loadInstalledPackages() {
  let pkgs = [];
  if (typeof ksu !== "undefined" && typeof ksu.listPackages === "function") {
    try {
      pkgs = JSON.parse(ksu.listPackages("user"));
    } catch (e) {}
  }
  if (!pkgs || pkgs.length === 0) {
    const res = await ksuExec("pm list packages -3 | sed 's/package://g'");
    if (res.stdout) {
      pkgs = res.stdout.split(/\r?\n/).map(s => s.trim()).filter(Boolean);
    }
  }
  // 将已经配置过的系统应用或特殊包名也合并进来
  const configuredPkgs = Object.keys(appState.appRoutes);
  appState.installedPackages = Array.from(new Set([...configuredPkgs, ...pkgs])).sort();
  renderAppList();
}

// 渲染模板卡片
function renderTemplates() {
  const container = document.getElementById("templateList");
  container.innerHTML = "";

  Object.values(appState.templates).forEach(tpl => {
    const card = document.createElement("div");
    card.className = "tpl-card";
    card.innerHTML = `
      <div class="tpl-head">
        <div class="tpl-title">${tpl.name}</div>
        <div class="tpl-badge">${tpl.BRAND || "Custom"}</div>
      </div>
      <div class="tpl-meta">
        <span>型号: <b>${tpl.MODEL || "-"}</b></span>
        <span>代号: <b>${tpl.DEVICE || "-"}</b></span>
        <span>制造商: <b>${tpl.MANUFACTURER || "-"}</b></span>
        <span>平台: <b>${tpl.BOARD || "-"}</b></span>
      </div>
      <div class="tpl-actions">
        <button class="btn-secondary btn-edit" data-id="${tpl.id}">编辑</button>
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
      for (const [pkg, routeId] of Object.entries(appState.appRoutes)) {
        if (routeId === id) delete appState.appRoutes[pkg];
      }
      await saveAndSyncConfig();
      renderTemplates();
      renderAppList();
      toast("模板已删除");
    };
  });
}

// 一键解析 build.prop 核心引擎
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

  const pick = (...keys) => {
    for (const k of keys) {
      if (map[k]) return map[k];
    }
    return "";
  };

  const model = pick(
    "ro.product.marketname",
    "ro.product.model",
    "ro.product.vendor.model",
    "ro.product.odm.model",
    "ro.product.system.model"
  );
  const brand = pick(
    "ro.product.brand",
    "ro.product.vendor.brand",
    "ro.product.odm.brand",
    "ro.product.system.brand"
  );
  const manufacturer = pick(
    "ro.product.manufacturer",
    "ro.product.vendor.manufacturer",
    "ro.product.odm.manufacturer",
    "ro.product.system.manufacturer",
    brand
  );
  const device = pick(
    "ro.product.device",
    "ro.product.vendor.device",
    "ro.product.odm.device",
    "ro.product.system.device",
    "ro.build.product"
  );
  const product = pick(
    "ro.product.name",
    "ro.product.vendor.name",
    "ro.product.odm.name",
    "ro.product.system.name",
    device
  );
  const board = pick("ro.board.platform", "ro.product.board");
  const hardware = pick("ro.hardware", "ro.boot.hardware", "qcom");
  const fingerprint = pick(
    "ro.build.fingerprint",
    "ro.product.build.fingerprint",
    "ro.vendor.build.fingerprint",
    "ro.system.build.fingerprint"
  );

  if (!model && !brand && !fingerprint) {
    toast("未识别到有效的 build.prop 属性字段！");
    return null;
  }

  return {
    id: `tpl_${Date.now()}`,
    name: `${brand ? brand + " " : ""}${model || device || "导入机型"}`,
    MODEL: model,
    BRAND: brand,
    MANUFACTURER: manufacturer || brand,
    DEVICE: device,
    PRODUCT: product || device,
    BOARD: board,
    HARDWARE: hardware,
    FINGERPRINT: fingerprint
  };
}

// 渲染应用分流列表
function renderAppList() {
  const container = document.getElementById("appList");
  container.innerHTML = "";

  const tplOptions = [`<option value="">-- 保持真实机型 (不伪装) --</option>`]
    .concat(
      Object.values(appState.templates).map(
        t => `<option value="${t.id}">${t.name} (${t.MODEL})</option>`
      )
    )
    .join("");

  const filtered = appState.installedPackages.filter(pkg => {
    if (appState.filterMode === "configured" && !appState.appRoutes[pkg]) return false;
    if (appState.searchQuery && !pkg.toLowerCase().includes(appState.searchQuery)) return false;
    return true;
  });

  filtered.forEach(pkg => {
    const currentTpl = appState.appRoutes[pkg] || "";
    const row = document.createElement("div");
    row.className = "app-item";
    row.innerHTML = `
      <div class="app-pkg">${pkg}</div>
      <select class="app-select" data-pkg="${pkg}">${tplOptions}</select>
    `;
    const selectEl = row.querySelector("select");
    selectEl.value = currentTpl;
    selectEl.onchange = async (e) => {
      const val = e.target.value;
      if (val) {
        appState.appRoutes[pkg] = val;
      } else {
        delete appState.appRoutes[pkg];
      }
      await saveAndSyncConfig();
      toast(val ? `已为 ${pkg} 启用伪装` : `已恢复 ${pkg} 默认机型`);
    };
    container.appendChild(row);
  });
}

// 弹窗控制
function openTemplateModal(tpl = null) {
  document.getElementById("modalTitle").textContent = tpl ? "编辑机型模板" : "新建机型模板";
  document.getElementById("tplId").value = tpl ? tpl.id : `tpl_${Date.now()}`;
  document.getElementById("tplName").value = tpl ? tpl.name : "";
  document.getElementById("tplModel").value = tpl ? tpl.MODEL : "";
  document.getElementById("tplBrand").value = tpl ? tpl.BRAND : "";
  document.getElementById("tplManufacturer").value = tpl ? tpl.MANUFACTURER : "";
  document.getElementById("tplDevice").value = tpl ? tpl.DEVICE : "";
  document.getElementById("tplProduct").value = tpl ? tpl.PRODUCT : "";
  document.getElementById("tplBoard").value = tpl ? tpl.BOARD : "";
  document.getElementById("tplHardware").value = tpl ? tpl.HARDWARE : "qcom";
  document.getElementById("tplFingerprint").value = tpl ? tpl.FINGERPRINT : "";
  document.getElementById("templateModal").classList.add("show");
}

// 事件绑定
document.addEventListener("DOMContentLoaded", async () => {
  // Tab 切换
  document.querySelectorAll(".tab-btn").forEach(btn => {
    btn.onclick = () => {
      document.querySelectorAll(".tab-btn").forEach(b => b.classList.remove("active"));
      document.querySelectorAll(".tab-panel").forEach(p => p.classList.remove("active"));
      btn.classList.add("active");
      document.getElementById(btn.dataset.tab).classList.add("active");
    };
  });

  // 新建模板按钮
  document.getElementById("btnNewTemplate").onclick = () => openTemplateModal(null);
  document.getElementById("btnCancelModal").onclick = () => {
    document.getElementById("templateModal").classList.remove("show");
  };

  // 保存模板
  document.getElementById("btnSaveTemplate").onclick = async () => {
    const id = document.getElementById("tplId").value;
    const name = document.getElementById("tplName").value.trim();
    const MODEL = document.getElementById("tplModel").value.trim();
    const BRAND = document.getElementById("tplBrand").value.trim();
    if (!name || !MODEL) {
      toast("请至少填写模板名称和手机型号(MODEL)");
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
      FINGERPRINT: document.getElementById("tplFingerprint").value.trim()
    };
    await saveAndSyncConfig();
    renderTemplates();
    renderAppList();
    document.getElementById("templateModal").classList.remove("show");
    toast("机型模板已保存并同步");
  };

  // 本地 .prop 文件导入
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

  // 按钮触发解析
  document.getElementById("btnParseProp").onclick = () => {
    const raw = document.getElementById("propRawText").value;
    const parsed = parseBuildPropText(raw);
    if (parsed) openTemplateModal(parsed);
  };

  document.getElementById("btnClearProp").onclick = () => {
    document.getElementById("propRawText").value = "";
  };

  // 应用列表过滤与搜索
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

  document.getElementById("btnRefreshApps").onclick = () => {
    loadInstalledPackages();
    toast("已刷新应用列表");
  };

  await loadConfig();
  await loadInstalledPackages();
});

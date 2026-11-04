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

// 编译单应用全息配置 (.conf)，供 C++ Companion 守护进程零解析开销直读
function buildRuntimeConf(tpl) {
  const lines = [
    `META:MEMFD_SHIELD=${tpl.MEMFD_SHIELD !== false ? "1" : "0"}`,
    // Java android.os.Build 反射池
    `BUILD:MODEL=${tpl.MODEL || ""}`,
    `BUILD:BRAND=${tpl.BRAND || ""}`,
    `BUILD:MANUFACTURER=${tpl.MANUFACTURER || ""}`,
    `BUILD:DEVICE=${tpl.DEVICE || ""}`,
    `BUILD:PRODUCT=${tpl.PRODUCT || ""}`,
    `BUILD:HARDWARE=${tpl.HARDWARE || ""}`,
    `BUILD:BOARD=${tpl.BOARD || ""}`,
    `BUILD:SOC_MANUFACTURER=${tpl.SOC_MANUFACTURER || ""}`,
    `BUILD:SOC_MODEL=${tpl.SOC_MODEL || ""}`,
    `BUILD:ID=${tpl.BUILD_ID || ""}`,
    `BUILD:FINGERPRINT=${tpl.FINGERPRINT || ""}`,
    `BUILD:TAGS=release-keys`,
    `BUILD:TYPE=user`,
    // Java android.os.Build$VERSION 反射池
    `VERSION:RELEASE=${tpl.RELEASE || ""}`,
    `VERSION:SDK_INT=${tpl.SDK_INT || ""}`,
    `VERSION:SECURITY_PATCH=${tpl.SECURITY_PATCH || ""}`,
    // Native 多分区 SystemProperties 映射池
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
    `ro.hardware=${tpl.HARDWARE || ""}`,
    `ro.boot.hardware=${tpl.HARDWARE || ""}`,
    `ro.soc.manufacturer=${tpl.SOC_MANUFACTURER || ""}`,
    `ro.soc.model=${tpl.SOC_MODEL || ""}`,
    `ro.build.id=${tpl.BUILD_ID || ""}`,
    `ro.build.version.release=${tpl.RELEASE || ""}`,
    `ro.build.version.sdk=${tpl.SDK_INT || ""}`,
    `ro.build.version.security_patch=${tpl.SECURITY_PATCH || ""}`,
    `ro.build.fingerprint=${tpl.FINGERPRINT || ""}`,
    `ro.product.build.fingerprint=${tpl.FINGERPRINT || ""}`,
    `ro.system.build.fingerprint=${tpl.FINGERPRINT || ""}`,
    `ro.vendor.build.fingerprint=${tpl.FINGERPRINT || ""}`,
    `ro.odm.build.fingerprint=${tpl.FINGERPRINT || ""}`,
    `ro.build.tags=release-keys`,
    `ro.build.type=user`
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

  for (const [pkg, tplId] of Object.entries(appState.appRoutes)) {
    const tpl = appState.templates[tplId];
    if (tpl && /^[a-zA-Z0-9._]+$/.test(pkg)) {
      const confB64 = toBase64(buildRuntimeConf(tpl));
      script += `echo "${confB64}" | base64 -d > "${APPS_DIR}/${pkg}.conf"\n`;
    }
  }
  script += `chmod 0700 "${DATA_DIR}" "${APPS_DIR}" && chmod 0600 "${CONFIG_FILE}" "${APPS_DIR}"/*.conf 2>/dev/null\n`;

  await ksuExec(script);
  const activeCount = Object.keys(appState.appRoutes).length;
  document.getElementById("statusPill").textContent = `已伪装 ${activeCount} 款应用`;
}

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
  renderTemplates();
  await saveAndSyncConfig();
}

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
  const configuredPkgs = Object.keys(appState.appRoutes);
  appState.installedPackages = Array.from(new Set([...configuredPkgs, ...pkgs])).sort();
  renderAppList();
}

function renderTemplates() {
  const container = document.getElementById("templateList");
  container.innerHTML = "";

  Object.values(appState.templates).forEach(tpl => {
    const customCount = tpl.CUSTOM_PROPS
      ? tpl.CUSTOM_PROPS.split(/\r?\n/).filter(l => l.trim() && l.includes("=")).length
      : 0;
    const card = document.createElement("div");
    card.className = "tpl-card";
    card.innerHTML = `
      <div class="tpl-head">
        <div class="tpl-title">${tpl.name}</div>
        <div class="tpl-badge">${tpl.BRAND || "Custom"} · API ${tpl.SDK_INT || "-"}</div>
      </div>
      <div class="tpl-meta">
        <span>型号: <b>${tpl.MODEL || "-"}</b></span>
        <span>代号: <b>${tpl.DEVICE || "-"}</b></span>
        <span>SoC: <b>${tpl.SOC_MODEL || tpl.BOARD || "-"}</b></span>
        <span>附加属性: <b>${customCount} 项</b></span>
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

// 深度解析 build.prop 并自动归集附加厂商属性
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

  const consumedKeys = new Set();
  const pick = (...keys) => {
    for (const k of keys) {
      consumedKeys.add(k);
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
  const buildId = pick("ro.build.id", "ro.product.build.id");
  const release = pick("ro.build.version.release", "ro.product.build.version.release");
  const sdk = pick("ro.build.version.sdk", "ro.product.build.version.sdk");
  const patch = pick("ro.build.version.security_patch");
  const fingerprint = pick("ro.build.fingerprint", "ro.product.build.fingerprint", "ro.vendor.build.fingerprint", "ro.system.build.fingerprint");

  if (!model && !brand && !fingerprint) {
    toast("未识别到有效的 build.prop 属性！");
    return null;
  }

  // 将剩余有价值的厂商属性自动收入 CUSTOM_PROPS
  const extraLines = [];
  for (const [k, v] of Object.entries(map)) {
    if (!consumedKeys.has(k) && (k.startsWith("ro.") || k.startsWith("persist."))) {
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
    SOC_MANUFACTURER: socMan || "Qualcomm",
    SOC_MODEL: socModel,
    BUILD_ID: buildId,
    RELEASE: release || "15",
    SDK_INT: sdk || "35",
    SECURITY_PATCH: patch,
    FINGERPRINT: fingerprint,
    MEMFD_SHIELD: true,
    CUSTOM_PROPS: extraLines.slice(0, 60).join("\n")
  };
}

function renderAppList() {
  const container = document.getElementById("appList");
  container.innerHTML = "";

  const tplOptions = [`<option value="">-- 保持真实机型 (零注入) --</option>`]
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
      <div style="display:flex;gap:6px;align-items:center;">
        <select class="app-select" data-pkg="${pkg}">${tplOptions}</select>
        <button class="btn-secondary btn-kill" data-pkg="${pkg}" title="强行停止并立即生效">⚡重启</button>
      </div>
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
      toast(val ? `已为 ${pkg} 绑定伪装` : `已恢复 ${pkg} 原生环境`);
    };
    row.querySelector(".btn-kill").onclick = async () => {
      await ksuExec(`am force-stop "${pkg}"`);
      toast(`已强行停止 ${pkg}，重新打开即刻生效`);
    };
    container.appendChild(row);
  });
}

function openTemplateModal(tpl = null) {
  document.getElementById("modalTitle").textContent = tpl ? "编辑全息机型模板" : "新建全息机型模板";
  document.getElementById("tplId").value = tpl ? tpl.id : `tpl_${Date.now()}`;
  document.getElementById("tplName").value = tpl ? tpl.name : "";
  document.getElementById("tplModel").value = tpl ? tpl.MODEL : "";
  document.getElementById("tplBrand").value = tpl ? tpl.BRAND : "";
  document.getElementById("tplManufacturer").value = tpl ? tpl.MANUFACTURER : "";
  document.getElementById("tplDevice").value = tpl ? tpl.DEVICE : "";
  document.getElementById("tplProduct").value = tpl ? tpl.PRODUCT : "";
  document.getElementById("tplBoard").value = tpl ? tpl.BOARD : "";
  document.getElementById("tplHardware").value = tpl ? tpl.HARDWARE : "qcom";
  document.getElementById("tplSocMan").value = tpl ? (tpl.SOC_MANUFACTURER || "") : "Qualcomm";
  document.getElementById("tplSocModel").value = tpl ? (tpl.SOC_MODEL || "") : "";
  document.getElementById("tplBuildId").value = tpl ? (tpl.BUILD_ID || "") : "";
  document.getElementById("tplRelease").value = tpl ? (tpl.RELEASE || "15") : "15";
  document.getElementById("tplSdk").value = tpl ? (tpl.SDK_INT || "35") : "35";
  document.getElementById("tplPatch").value = tpl ? (tpl.SECURITY_PATCH || "") : "";
  document.getElementById("tplFingerprint").value = tpl ? tpl.FINGERPRINT : "";
  document.getElementById("tplMemfd").checked = tpl ? tpl.MEMFD_SHIELD !== false : true;
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
    };
  });

  document.getElementById("btnNewTemplate").onclick = () => openTemplateModal(null);
  document.getElementById("btnCancelModal").onclick = () => {
    document.getElementById("templateModal").classList.remove("show");
  };

  document.getElementById("btnSaveTemplate").onclick = async () => {
    const id = document.getElementById("tplId").value;
    const name = document.getElementById("tplName").value.trim();
    const MODEL = document.getElementById("tplModel").value.trim();
    const BRAND = document.getElementById("tplBrand").value.trim();
    if (!name || !MODEL) {
      toast("请填写模板名称和手机型号(MODEL)");
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
      SOC_MANUFACTURER: document.getElementById("tplSocMan").value.trim(),
      SOC_MODEL: document.getElementById("tplSocModel").value.trim(),
      BUILD_ID: document.getElementById("tplBuildId").value.trim(),
      RELEASE: document.getElementById("tplRelease").value.trim(),
      SDK_INT: document.getElementById("tplSdk").value.trim(),
      SECURITY_PATCH: document.getElementById("tplPatch").value.trim(),
      FINGERPRINT: document.getElementById("tplFingerprint").value.trim(),
      MEMFD_SHIELD: document.getElementById("tplMemfd").checked,
      CUSTOM_PROPS: document.getElementById("tplCustomProps").value.trim()
    };
    await saveAndSyncConfig();
    renderTemplates();
    renderAppList();
    document.getElementById("templateModal").classList.remove("show");
    toast("全息模板已保存并同步至底层引擎");
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

  document.getElementById("btnRefreshApps").onclick = () => {
    loadInstalledPackages();
    toast("已刷新应用列表");
  };

  await loadConfig();
  await loadInstalledPackages();
});

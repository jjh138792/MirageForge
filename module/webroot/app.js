const CLI_BIN = "/data/adb/modules/mirage_forge/bin/mirageforge_cli";
const LAUNCH_LOG = "/data/adb/mirage_forge/launch_history.log";

let appState = {
  version: "1.0.0",
  templates: {},
  appRoutes: {},
  installedApps: [],
  includeSystemApps: false,
  filterMode: "all",
  searchQuery: "",
  unmappedOnly: false
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
      console.log("[CLI Mock]", command);
      resolve({ errno: 0, stdout: "{}", stderr: "" });
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

// 从 mirageforge_cli state --json 同步全局权威状态
async function syncStateFromCli() {
  const res = await ksuExec(`"${CLI_BIN}" state --json`);
  if (res.errno === 0 && res.stdout.trim()) {
    try {
      const data = JSON.parse(res.stdout);
      appState.version = data.version || "1.0.0";
      appState.templates = data.templates || {};
      appState.appRoutes = data.appRoutes || {};
      const activeCount = Object.keys(appState.appRoutes).length;
      document.getElementById("statusPill").textContent = `v${appState.version} · 已伪装 ${activeCount} 款应用`;
    } catch (e) {
      console.error("Failed to parse CLI state JSON:", e);
    }
  }
  renderTemplates();
  renderAppList();
}

// 读取已安装应用列表并结合 KSU 原生图标与名称 API
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
      versionName: meta.versionName || ""
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
      await ksuExec(`"${CLI_BIN}" template delete "${id}" --json`);
      await syncStateFromCli();
      toast("已通过 CLI 删除模板并同步路由");
    };
  });
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
    const route = appState.appRoutes[pkg] || null;
    const currentTpl = route ? route.tplId : "";
    const row = document.createElement("div");
    row.className = "app-item";

    let switchesHtml = "";
    if (route) {
      switchesHtml = `
        <div class="app-switches">
          <span class="sw-pill ${route.build ? "on" : ""}" data-sw="build">🧬 Bionic/ART属性: ${route.build ? "开" : "关"}</span>
          <span class="sw-pill ${route.gl ? "on" : ""}" data-sw="gl">🎮 Vulkan/GLES/DRM: ${route.gl ? "开" : "关"}</span>
          <span class="sw-pill ${route.vfs ? "on" : ""}" data-sw="vfs">🛡️ 隐身VFS/Maps净化: ${route.vfs ? "开" : "关"}</span>
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

    const pushRouteToCli = async (tplId, b, g, v, p, restart = false) => {
      const cmd = `"${CLI_BIN}" route set "${pkg}" "${tplId}" --build ${b ? 1 : 0} --gl ${g ? 1 : 0} --vfs ${v ? 1 : 0} --probe ${p ? 1 : 0} ${restart ? "--restart" : ""}`;
      await ksuExec(cmd);
      await syncStateFromCli();
    };

    selectEl.onchange = async (e) => {
      const val = e.target.value;
      if (val) {
        await pushRouteToCli(
          val,
          route ? route.build : true,
          route ? route.gl : true,
          route ? route.vfs : true,
          route ? route.probe : false,
          false
        );
        toast(`已为 ${app.appLabel} 挂载全息引擎`);
      } else {
        await ksuExec(`"${CLI_BIN}" route remove "${pkg}"`);
        await syncStateFromCli();
        toast(`已解除 ${app.appLabel} 伪装`);
      }
    };

    row.querySelector(".btn-kill").onclick = async () => {
      await ksuExec(`am force-stop "${pkg}"`);
      toast(`已重启 ${app.appLabel}`);
    };

    row.querySelectorAll(".sw-pill").forEach(pill => {
      pill.onclick = async () => {
        if (!route) return;
        const key = pill.dataset.sw;
        const nextRoute = { ...route, [key]: !route[key] };
        await pushRouteToCli(nextRoute.tplId, nextRoute.build, nextRoute.gl, nextRoute.vfs, nextRoute.probe, false);
      };
    });

    container.appendChild(row);
  });
}

// 刷新注入遥测与幻境探针雷达 (调用 mirageforge_cli probe list --json)
async function refreshProbeAndTelemetry() {
  // 1. 读取最近注入遥测
  const teleRes = await ksuExec(`tail -n 25 "${LAUNCH_LOG}" 2>/dev/null`);
  const teleEl = document.getElementById("telemetryList");
  teleEl.innerHTML = "";
  const teleLines = (teleRes.stdout || "").split(/\r?\n/).filter(Boolean).reverse();
  if (teleLines.length === 0) {
    teleEl.innerHTML = `<div class="probe-empty">暂无注入记录。启动任何已绑定模板的目标应用后会自动在此显示。</div>`;
  } else {
    teleLines.forEach(line => {
      const [ts, pkg, pid, model] = line.split("|");
      if (!pkg) return;
      const dateStr = ts ? new Date(Number(ts) * 1000).toLocaleTimeString() : "";
      const row = document.createElement("div");
      row.className = "probe-row";
      row.innerHTML = `
        <div>
          <span class="probe-tag">INJECTED</span>
          <b style="color:#00e5ff;margin-left:6px;">${pkg}</b>
          <span style="color:#8b949e;margin-left:6px;">(PID: ${pid})</span>
          <span style="color:#56d364;margin-left:8px;">➔ 伪装为: ${model}</span>
        </div>
        <span style="color:#8b949e;font-size:10px;">${dateStr}</span>
      `;
      teleEl.appendChild(row);
    });
  }

  // 2. 调用 mirageforge_cli probe list --json 获取结构化探针日志
  const filterVal = document.getElementById("probeFilterInput").value.trim();
  const unmappedFlag = appState.unmappedOnly ? "--unmapped" : "";
  const pkgOpt = filterVal ? `--pkg "${filterVal.replace(/"/g, "")}"` : "";
  const res = await ksuExec(`"${CLI_BIN}" probe list ${pkgOpt} ${unmappedFlag} --limit 150 --json`);

  const listEl = document.getElementById("probeLogList");
  listEl.innerHTML = "";

  let events = [];
  if (res.errno === 0 && res.stdout.trim()) {
    try {
      events = JSON.parse(res.stdout);
    } catch (e) {}
  }

  if (!Array.isArray(events) || events.length === 0) {
    listEl.innerHTML = `<div class="probe-empty">暂无匹配的探针审计记录。请先在「应用分流矩阵」中点亮目标应用的 [📡 幻境探针] 并启动该应用。</div>`;
    return;
  }

  events.forEach(ev => {
    const row = document.createElement("div");
    row.className = "probe-row";
    const actionBtnHtml = ev.unmapped
      ? `<button class="btn-capture" data-pkg="${ev.pkg}" data-key="${ev.key}">+ 捕获入库</button>`
      : "";

    row.innerHTML = `
      <div style="min-width:0;word-break:break-all;">
        <span class="probe-tag ${ev.unmapped ? "unmapped" : ""}">${ev.domain}</span>
        <b style="color:#00e5ff;margin-left:6px;">${ev.pkg}</b>
        <span style="margin-left:8px;color:#f0f6fc;">${ev.key}</span>
        <span style="color:#8b949e;"> ➔ ${ev.value}</span>
      </div>
      ${actionBtnHtml}
    `;

    const capBtn = row.querySelector(".btn-capture");
    if (capBtn) {
      capBtn.onclick = async () => {
        const customVal = prompt(`为 [${ev.pkg}] 所绑定的模板设置属性 ${ev.key} 的伪装值：`, "1");
        if (customVal === null) return;
        const safeVal = customVal.trim().replace(/"/g, "");
        await ksuExec(`"${CLI_BIN}" probe capture "${ev.pkg}" "${ev.key}" "${safeVal}"`);
        await syncStateFromCli();
        toast(`已通过 CLI 将 ${ev.key}=${safeVal} 捕获入库！`);
      };
    }
    listEl.appendChild(row);
  });
}

function openTemplateModal(tpl = null) {
  document.getElementById("modalTitle").textContent = tpl ? "编辑全息硬件模板" : "铸造全息硬件模板";
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

// 调用 C++ mirageforge_cli parse-prop - 解析 build.prop 文本
async function parsePropViaCli(rawText) {
  if (!rawText || !rawText.trim()) {
    toast("请先粘贴或导入 build.prop 内容");
    return;
  }
  const b64 = toBase64(rawText);
  const res = await ksuExec(`echo "${b64}" | base64 -d | "${CLI_BIN}" parse-prop -`);
  if (res.errno === 0 && res.stdout.trim()) {
    try {
      const parsed = JSON.parse(res.stdout);
      if (parsed && parsed.MODEL) {
        openTemplateModal(parsed);
        toast("mirageforge_cli 炼金解析成功！");
        return;
      }
    } catch (e) {}
  }
  toast("未识别到有效的 build.prop 属性字段");
}

document.addEventListener("DOMContentLoaded", async () => {
  document.querySelectorAll(".tab-btn").forEach(btn => {
    btn.onclick = () => {
      document.querySelectorAll(".tab-btn").forEach(b => b.classList.remove("active"));
      document.querySelectorAll(".tab-panel").forEach(p => p.classList.remove("active"));
      btn.classList.add("active");
      document.getElementById(btn.dataset.tab).classList.add("active");
      if (btn.dataset.tab === "tab-probe") refreshProbeAndTelemetry();
    };
  });

  document.getElementById("btnNewTemplate").onclick = () => openTemplateModal(null);
  document.getElementById("btnCancelModal").onclick = () => {
    document.getElementById("templateModal").classList.remove("show");
  };

  document.getElementById("btnExportJson").onclick = async () => {
    const outPath = "/sdcard/Download/MirageForge_v1.0.0_Backup.json";
    await ksuExec(`"${CLI_BIN}" export "${outPath}"`);
    toast(`已通过 CLI 导出备份至 ${outPath}`);
  };

  // 通过 Base64 管道将模板 JSON 提交给 mirageforge_cli template set-stdin
  document.getElementById("btnSaveTemplate").onclick = async () => {
    const id = document.getElementById("tplId").value.trim();
    const name = document.getElementById("tplName").value.trim();
    const MODEL = document.getElementById("tplModel").value.trim();
    const BRAND = document.getElementById("tplBrand").value.trim();
    if (!name || !MODEL) {
      toast("请填写模板名称与手机型号 (MODEL)");
      return;
    }

    const tplObj = {
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
      CPU_CORES: String(parseInt(document.getElementById("tplCpuCores").value, 10) || 8),
      CPU_MAX_FREQ: String(parseInt(document.getElementById("tplCpuFreq").value, 10) || 4320000),
      RAM_GB: String(parseInt(document.getElementById("tplRamGb").value, 10) || 16),
      SOC_ID: document.getElementById("tplSocId").value.trim() || "618",
      SOC_MACHINE: document.getElementById("tplSocMachine").value.trim() || "SM8750",
      KERNEL_RELEASE: document.getElementById("tplKernelRel").value.trim(),
      KERNEL_VERSION: document.getElementById("tplKernelVer").value.trim(),
      CUSTOM_PROPS: document.getElementById("tplCustomProps").value.trim()
    };

    const b64Json = toBase64(JSON.stringify(tplObj));
    await ksuExec(`echo "${b64Json}" | base64 -d | "${CLI_BIN}" template set-stdin`);
    await syncStateFromCli();
    document.getElementById("templateModal").classList.remove("show");
    toast("已通过 mirageforge_cli 原子编译并保存模板");
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
      parsePropViaCli(ev.target.result);
    };
    reader.readAsText(file);
  };

  document.getElementById("btnParseProp").onclick = () => {
    parsePropViaCli(document.getElementById("propRawText").value);
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

  document.getElementById("btnToggleUnmapped").onclick = (e) => {
    appState.unmappedOnly = !appState.unmappedOnly;
    e.target.classList.toggle("active", appState.unmappedOnly);
    refreshProbeAndTelemetry();
  };

  document.getElementById("btnRefreshProbe").onclick = () => refreshProbeAndTelemetry();
  document.getElementById("probeFilterInput").oninput = () => refreshProbeAndTelemetry();

  document.getElementById("btnClearProbe").onclick = async () => {
    await ksuExec(`"${CLI_BIN}" probe clear`);
    refreshProbeAndTelemetry();
    toast("已清空探针日志");
  };

  document.getElementById("btnClearTelemetry").onclick = async () => {
    await ksuExec(`rm -f "${LAUNCH_LOG}"`);
    refreshProbeAndTelemetry();
    toast("已清空注入遥测记录");
  };

  await syncStateFromCli();
  await loadInstalledPackages();
});

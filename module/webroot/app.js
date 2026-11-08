const CLI_BIN = "/data/adb/modules/mirage_forge/bin/mirageforge_cli";

let appState = {
  version: "1.0.1",
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

// 将 "0-5:614400:3532800;6-7:806400:4320000" 转化为 "6核@3.53GHz + 2核@4.32GHz"
function formatClustersSummary(clustersStr, fallbackCores, fallbackMaxFreq) {
  if (!clustersStr || !clustersStr.includes(":")) {
    return `${fallbackCores || 8}核 @ ${((fallbackMaxFreq || 4320000) / 1e6).toFixed(2)}GHz`;
  }
  const parts = clustersStr.split(";").map(s => s.trim()).filter(Boolean);
  const desc = [];
  for (const seg of parts) {
    const [range, , maxKhz] = seg.split(":");
    if (!range || !maxKhz) continue;
    const [start, end] = range.split("-").map(n => parseInt(n, 10));
    const count = (!isNaN(start) && !isNaN(end) && end >= start) ? (end - start + 1) : 1;
    const ghz = (parseInt(maxKhz, 10) / 1e6).toFixed(2);
    desc.push(`${count}核@${ghz}GHz`);
  }
  return desc.length > 0 ? desc.join(" + ") : `${fallbackCores || 8}核`;
}

async function syncStateFromCli() {
  const res = await ksuExec(`"${CLI_BIN}" state --json`);
  if (res.errno === 0 && res.stdout.trim()) {
    try {
      const data = JSON.parse(res.stdout);
      appState.version = data.version || "1.0.1";
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
    const ddrType = tpl.DDR_TYPE || "LPDDR5X";
    const ddrVendor = tpl.DDR_VENDOR || "SKhynix";
    const clusterSummary = formatClustersSummary(tpl.CPU_CLUSTERS, tpl.CPU_CORES, tpl.CPU_MAX_FREQ);
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
        <span class="full-span">对称内存: <b>${tpl.RAM_GB || 16}GB ${ddrType} (${ddrVendor})</b></span>
        <span class="full-span">单核分簇: <b>${clusterSummary}</b></span>
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
          <span class="sw-pill ${route.vfs ? "on" : ""}" data-sw="vfs">🛡️ 单核频率/对称内存/VFS: ${route.vfs ? "开" : "关"}</span>
          <span class="sw-pill ${route.probe ? "probe-on" : ""}" data-sw="probe">📡 探针与调用栈: ${route.probe ? "嗅探中" : "关"}</span>
          <span class="sw-pill ${route.sdkSpoof ? "sdk-on" : ""}" data-sw="sdkSpoof" title="默认关闭以保护应用不因跨 Android 版本调错 API 闪退">⚠️ 强改SDK版本: ${route.sdkSpoof ? "开" : "关(安全)"}</span>
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

    const pushRouteToCli = async (tplId, b, g, v, p, s, restart = false) => {
      const cmd = `"${CLI_BIN}" route set "${pkg}" "${tplId}" --build ${b ? 1 : 0} --gl ${g ? 1 : 0} --vfs ${v ? 1 : 0} --probe ${p ? 1 : 0} --sdk ${s ? 1 : 0} ${restart ? "--restart" : ""}`;
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
          route ? Boolean(route.sdkSpoof) : false,
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
        await pushRouteToCli(
          nextRoute.tplId,
          nextRoute.build,
          nextRoute.gl,
          nextRoute.vfs,
          nextRoute.probe,
          Boolean(nextRoute.sdkSpoof),
          false
        );
      };
    });

    container.appendChild(row);
  });
}

// 刷新崩溃现场、注入遥测与带调用栈的幻境探针雷达 (直接从 SCM_RIGHTS 持久化文件读取)
async function refreshProbeAndTelemetry() {
  // 1. 查询 Native 崩溃信号日志
  const crashRes = await ksuExec(`"${CLI_BIN}" probe crashes --limit 25 --json`);
  const crashEl = document.getElementById("crashLogList");
  crashEl.innerHTML = "";
  let crashes = [];
  if (crashRes.errno === 0 && crashRes.stdout.trim()) {
    try { crashes = JSON.parse(crashRes.stdout); } catch (e) {}
  }

  if (!Array.isArray(crashes) || crashes.length === 0) {
    crashEl.innerHTML = `<div class="probe-empty">运行正常，未捕获到任何应用 Native 崩溃信号。</div>`;
  } else {
    crashes.forEach(c => {
      const row = document.createElement("div");
      row.className = "probe-row crash-row";
      row.innerHTML = `
        <div class="probe-row-head">
          <div>
            <span class="probe-tag crash">${c.signal}</span>
            <b style="color:#ff8da3;margin-left:6px;">${c.pkg}</b>
            <span style="color:#f0f6fc;margin-left:8px;">${c.detail}</span>
          </div>
        </div>
        ${c.stack ? `<div class="stack-trace crash-stack">💥 Crash Stack: ${c.stack}</div>` : ""}
      `;
      crashEl.appendChild(row);
    });
  }

  // 2. 查询注入遥测
  const teleRes = await ksuExec(`"${CLI_BIN}" probe telemetry --limit 25 --json`);
  const teleEl = document.getElementById("telemetryList");
  teleEl.innerHTML = "";
  let teleList = [];
  if (teleRes.errno === 0 && teleRes.stdout.trim()) {
    try { teleList = JSON.parse(teleRes.stdout); } catch (e) {}
  }

  if (!Array.isArray(teleList) || teleList.length === 0) {
    teleEl.innerHTML = `<div class="probe-empty">暂无注入记录。启动任何已绑定模板的目标应用后会自动在此显示。</div>`;
  } else {
    teleList.forEach(t => {
      const dateStr = t.timestamp ? new Date(Number(t.timestamp) * 1000).toLocaleTimeString() : "";
      const row = document.createElement("div");
      row.className = "probe-row";
      row.innerHTML = `
        <div class="probe-row-head">
          <div>
            <span class="probe-tag">INJECTED</span>
            <b style="color:#00e5ff;margin-left:6px;">${t.pkg}</b>
            <span style="color:#8b949e;margin-left:6px;">(PID: ${t.pid})</span>
            <span style="color:#56d364;margin-left:8px;">➔ ${t.model} · ${t.gpu} · ${t.ram}</span>
          </div>
          <span style="color:#8b949e;font-size:10px;">${dateStr}</span>
        </div>
      `;
      teleEl.appendChild(row);
    });
  }

  // 3. 查询带调用栈的探针日志 (probe_events.log)
  const filterVal = document.getElementById("probeFilterInput").value.trim();
  const unmappedFlag = appState.unmappedOnly ? "--unmapped" : "";
  const pkgOpt = filterVal ? `--pkg "${filterVal.replace(/"/g, "")}"` : "";
  const res = await ksuExec(`"${CLI_BIN}" probe list ${pkgOpt} ${unmappedFlag} --limit 150 --json`);

  const listEl = document.getElementById("probeLogList");
  listEl.innerHTML = "";

  let events = [];
  if (res.errno === 0 && res.stdout.trim()) {
    try { events = JSON.parse(res.stdout); } catch (e) {}
  }

  if (!Array.isArray(events) || events.length === 0) {
    listEl.innerHTML = `<div class="probe-empty">暂无匹配的探针审计记录。请先在「应用分流矩阵」中点亮目标应用的 [📡 探针与调用栈]，点击右侧 [⚡重启] 并打开该应用。</div>`;
    return;
  }

  events.forEach(ev => {
    const row = document.createElement("div");
    row.className = "probe-row";
    const actionBtnHtml = ev.unmapped
      ? `<button class="btn-capture" data-pkg="${ev.pkg}" data-key="${ev.key}">+ 捕获入库</button>`
      : "";

    const stackHtml = ev.stack
      ? `<div class="stack-trace">↳ 调用栈: ${ev.stack}</div>`
      : "";

    row.innerHTML = `
      <div class="probe-row-head">
        <div style="min-width:0;word-break:break-all;">
          <span class="probe-tag ${ev.unmapped ? "unmapped" : ""}">${ev.domain}</span>
          <b style="color:#00e5ff;margin-left:6px;">${ev.pkg}</b>
          <span style="margin-left:8px;color:#f0f6fc;">${ev.key}</span>
          <span style="color:#8b949e;"> ➔ ${ev.value}</span>
        </div>
        ${actionBtnHtml}
      </div>
      ${stackHtml}
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
  document.getElementById("tplCpuClusters").value = tpl ? (tpl.CPU_CLUSTERS || "0-5:614400:3532800;6-7:806400:4320000") : "0-5:614400:3532800;6-7:806400:4320000";
  document.getElementById("tplRamGb").value = tpl ? (tpl.RAM_GB || 16) : 16;
  document.getElementById("tplDdrType").value = tpl ? (tpl.DDR_TYPE || "LPDDR5X") : "LPDDR5X";
  document.getElementById("tplDdrVendor").value = tpl ? (tpl.DDR_VENDOR || "SKhynix") : "SKhynix";
  document.getElementById("tplSocId").value = tpl ? (tpl.SOC_ID || "618") : "618";
  document.getElementById("tplSocMachine").value = tpl ? (tpl.SOC_MACHINE || "SM8750") : "SM8750";
  document.getElementById("tplKernelRel").value = tpl ? (tpl.KERNEL_RELEASE || "6.6.30-android15-8-g94b8f12ab4c1") : "6.6.30-android15-8-g94b8f12ab4c1";
  document.getElementById("tplKernelVer").value = tpl ? (tpl.KERNEL_VERSION || "#1 SMP PREEMPT Fri Nov 15 12:00:00 UTC 2025") : "#1 SMP PREEMPT Fri Nov 15 12:00:00 UTC 2025";
  document.getElementById("tplCustomProps").value = tpl ? (tpl.CUSTOM_PROPS || "") : "";
  document.getElementById("templateModal").classList.add("show");
}

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

  // 异构分簇预设快捷按钮
  document.querySelectorAll(".preset-tag").forEach(btn => {
    btn.onclick = () => {
      document.getElementById("tplCpuClusters").value = btn.dataset.clusters;
      if (btn.dataset.max) {
        document.getElementById("tplCpuFreq").value = btn.dataset.max;
      }
      toast("已填入异构核心分簇拓扑预设");
    };
  });

  document.getElementById("btnNewTemplate").onclick = () => openTemplateModal(null);
  document.getElementById("btnCancelModal").onclick = () => {
    document.getElementById("templateModal").classList.remove("show");
  };

  document.getElementById("btnExportJson").onclick = async () => {
    const outPath = "/sdcard/Download/MirageForge_v1.0.1_Backup.json";
    await ksuExec(`"${CLI_BIN}" export "${outPath}"`);
    toast(`已通过 CLI 导出备份至 ${outPath}`);
  };

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
      CPU_CLUSTERS: document.getElementById("tplCpuClusters").value.trim() || "0-5:614400:3532800;6-7:806400:4320000",
      RAM_GB: String(parseInt(document.getElementById("tplRamGb").value, 10) || 16),
      DDR_TYPE: document.getElementById("tplDdrType").value.trim() || "LPDDR5X",
      DDR_VENDOR: document.getElementById("tplDdrVendor").value.trim() || "SKhynix",
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
    toast("已清空持久化探针与崩溃日志文件");
  };

  document.getElementById("btnClearTelemetry").onclick = async () => {
    await ksuExec(`"${CLI_BIN}" probe clear-telemetry`);
    refreshProbeAndTelemetry();
    toast("已清空注入遥测记录");
  };

  await syncStateFromCli();
  await loadInstalledPackages();
});

# MirageForge 配置与命令行技术文档 (v1.3.0)

**[English](en_CONFIG.md)** | **[返回主页](../README.md)**

本文档面向进阶用户、脚本自动化场景及排障排查，说明 MirageForge 的数据目录布局、路由与编译文件格式、底层三引擎工作模式及 `mirageforge_cli` 命令行接口。

---

## 1. 数据目录布局

所有运行时配置与日志统一保存在 `/data/adb/mirage_forge/`（目录权限 `0700`，文件权限 `0600`）：

| 路径 | 作用 |
| :--- | :--- |
| `templates/<id>.tpl` | 单套硬件模板 JSON（由 CLI 或 WebUI 维护） |
| `routes.db` | 应用路由主索引表（v3 16 列管道分隔格式；Zygisk Companion 按其 `mtime` 热重载缓存） |
| `apps/<pkg>.conf` | 编译后的单应用扁平 `KEY=VALUE` 配置文件，供 Zygisk Companion 直接分发 |
| `config.json` | 完整状态聚合镜像（模板 + 路由 + 自动托管配置） |
| `auto_spoof.json` | 新安装应用自动托管策略配置 |
| `known_pkgs.db` | `watch-installs` 维护的存量用户应用基线快照 |
| `watch_installs.pid` | `inotify` 新应用监听守护进程 PID 文件 |
| `runtime_status.json` | Companion 落盘的各应用最近一次真实生效状态与降级诊断 |
| `probe_events.log` | 幻境探针事件与 Native 调用栈日志 |
| `crash_events.log` | 探针开启时捕获的真实 Native 崩溃信号堆栈 |
| `launch_history.log` | 应用注入遥测记录 |

---

## 2. 核心伪装引擎模式说明

### 2.1 系统属性三引擎 (`propMode`)

无论选择哪种模式，Java 层 `android.os.Build` / `Build$VERSION` 反射均同步生效；底层系统属性按以下策略执行，并在环境受限时按 **`mount_ns` → `cow` → `hook`** 自动安全降级：

| 模式值 | 实现原理 | 隐蔽性与特点 |
| :--- | :--- | :--- |
| `hook` | Bionic `__system_property_*` PLT Hook + 只读匿名属性区 `[anon:SystemProperties]` | 兼容性最高，用于兜底魔改 ROM；存在 GOT/PLT 与匿名映射痕迹。 |
| `cow` | 对命中的属性区执行 `MAP_PRIVATE \| MAP_FIXED` 写时复制，原地覆写已有属性并在属性区尾部空闲空间向 `prop_trie` 原生插入跨品牌新属性，写完恢复 `r--p` | 0 属性 PLT Hook、无 `[anon:SystemProperties]`；在 `smaps` 中可见 `r--p` 与少量 `Private_Dirty`。 |
| `mount_ns` | 在 `preAppSpecialize` 由 Root Companion 复制属性区至私有 tmpfs，覆写并插入节点后克隆原 SELinux 标签，于私有挂载命名空间只读 bind-mount 覆盖，App 内以 `PROT_READ \| MAP_SHARED \| MAP_FIXED` 刷新映射 | 0 属性 PLT Hook、`maps` 保持 `r--s`、`Private_Dirty: 0 kB`，可应对直接 `svc #0` 裸读与脏页审计。 |

### 2.2 VFS 文件节点两阶段 MountNS (`mountNs`)

* 开启 `mountNs=1` 时，Companion 在 `preAppSpecialize` 通过 `SCM_RIGHTS` 向 App 交付独立 `socketpair`，待 `postAppSpecialize`（`SpecializeCommon` 与 KernelSU / Zygisk Next unmount 清理全部结束）后，再将伪装后的 `/proc/*` 与 `/sys/*` 节点挂入 App 私有挂载命名空间。
* 挂载完成后在 App 侧执行 `/proc` 与 `/sys` 双哨兵物理核验，验证通过即记录全部符号链接别名（如 `cpuX/cpufreq/* -> cpufreq/policyY/*`）并**彻底免挂 29 个文件 I/O PLT Hook**；同时后台线程通过保留的纯内存 tmpfs FD 对 `/proc/meminfo` 与核心频率执行等宽 `pwrite` 活体微调。

### 2.3 零驻留自卸载 (`zeroResidence`)

当某应用仅启用「纯 COW / MountNS 系统属性」且未启用 GPU、高刷 / HDR、内存 Binder 拦截、系统调用级 CPU / 内存 Hook 或探针时，`postAppSpecialize` 完成 Java `Build` 反射后直接触发 `DLCLOSE_MODULE_LIBRARY`，从 `/proc/self/maps` 彻底卸载自身 `.so`。

---

## 3. 路由表格式 (`routes.db`)

v1.3.0 使用 16 列 `|` 分隔格式（自动无损向上迁移 v1.2.x 的 6 列与 13 列旧表）：

```text
pkg|tplId|build|gl|vfs|probe|sdkSpoof|refreshRate|mountNs|cowProp|cpu|mem|vfsNodes|propMode|hdrSpoof|overridesB64
```

| 列索引 | 字段 | 取值说明 |
| :--- | :--- | :--- |
| 0–1 | `pkg` / `tplId` | 目标应用包名 / 绑定的模板 ID |
| 2–5 | `build` / `gl` / `vfs` / `probe` | `0` 或 `1`（`vfs` 为 `cpu \|\| mem \|\| vfsNodes` 兼容聚合位） |
| 6–7 | `sdkSpoof` / `refreshRate` | SDK 强改开关 / 刷新率（`0` 表示不改，有效范围 `90`–`240`） |
| 8–9 | `mountNs` / `cowProp` | VFS 命名空间挂载开关 / 兼容衍生位（`build && propMode == "cow"`） |
| 10–12 | `cpu` / `mem` / `vfsNodes` | 细粒度 CPU / 内存 / 硬件文件节点独立开关 |
| 13 | `propMode` | `hook` / `cow` / `mount_ns` |
| 14 | `hdrSpoof` | `0` 或 `1`（HDR10 / HLG / HDR10+ / Dolby Vision 能力声明） |
| 15 | `overridesB64` | URL-Safe Base64 编码的单应用局部覆写 JSON（空串表示无覆写） |

局部覆写 JSON 结构示例：

```json
{
  "ramGb": 24,
  "cpuCores": 8,
  "customProps": "ro.product.marketname=Xiaomi 15 Ultra\npersist.vendor.demo=1"
}
```

---

## 4. 编译产物格式 (`apps/<pkg>.conf`)

每行一条 `KEY=VALUE`，严格遵循「未开启的开关不输出对应段」的 0 改动门控：

* **总控段 (`META:*`)**：`META:ENABLE_BUILD`、`META:PROP_MODE`、`META:ENABLE_GL`、`META:ENABLE_HDR`、`META:ENABLE_CPU`、`META:ENABLE_MEM`、`META:ENABLE_VFS_NODES`、`META:ENABLE_MOUNT_NS`、`META:ENABLE_PROBE`、`META:FORCE_SPOOF_SDK`、`META:REFRESH_RATE`。
* **图形段 (`GLES:*` / `VULKAN:*`)**：OpenGL ES 厂商 / 渲染器 / 版本，以及独立的 Vulkan 设备名、Vendor/Device ID、API / 驱动版本、驱动名与驱动信息。
* **硬件拓扑段 (`VFS:*`)**：`CPU_HARDWARE`、`CPU_CORES`、`CPU_MAX_FREQ`、`CPU_CLUSTERS`（格式 `起始核-结束核:最小kHz:最大kHz`，多簇以 `;` 分隔）、`RAM_GB`、`DDR_TYPE`、`DDR_VENDOR`、`SOC_ID`、`SOC_MACHINE`、`KERNEL_RELEASE`、`KERNEL_VERSION`。
* **Java 反射段 (`BUILD:*` / `VERSION:*`)**：映射至 `android.os.Build` 与 `android.os.Build$VERSION` 静态字段。
* **系统属性段（无前缀）**：直接写入 Bionic `prop_area` 或属性表的全分区 `ro.*` / `sys.*` / `persist.*` 键值对（自动过滤 DPI 键，并在 `FORCE_SPOOF_SDK=0` 时过滤全部 SDK / Release 键）。

---

## 5. 新应用自动托管配置 (`auto_spoof.json`)

由开机脚本 `service.sh` 拉起的 `mirageforge_cli watch-installs` 通过 Linux `inotify` 阻塞监听 `/data/system/packages.list`（无事件时 CPU 占用 0%），热读取以下配置：

```json
{
  "enabled": true,
  "tplId": "xiaomi_15_pro",
  "build": true,
  "propMode": "cow",
  "gl": true,
  "cpu": true,
  "mem": true,
  "vfsNodes": true,
  "mountNs": false,
  "hdrSpoof": false,
  "refreshRate": 0,
  "excludePackages": [
    "com.android.*",
    "com.example.keepnative"
  ]
}
```

* 仅处理普通用户应用（跨多用户取 `uid % 100000` 位于 `10000..=19999`）。
* 首次开启自动建立存量快照 `known_pkgs.db`，不会误改已安装的老应用。
* 内置安全排除名单始终跳过 KernelSU / KSU Next / APatch / Magisk / MMRL / KSUWebUI / LSPosed / Termux / Shell。

---

## 6. `mirageforge_cli` 命令行速查

安装后自动链接至 `/system/bin/mirageforge_cli`，需 Root 权限执行：

```bash
# 查看模块概况、已配置应用与自动托管守护状态
su -c "mirageforge_cli status"
su -c "mirageforge_cli status --json"

# 探测宿主硬件与 Root/Zygisk 环境 / 一键克隆本机为模板
su -c "mirageforge_cli device-info --json"
su -c "mirageforge_cli clone-device --id my_host --save"

# 模板管理（解析 build.prop / 分享码导入导出 / 删除）
su -c "mirageforge_cli parse-prop /sdcard/Download/build.prop --save"
su -c "mirageforge_cli template export-share xiaomi_15_pro"
su -c "mirageforge_cli template import-share 'mf://...' --save"
su -c "mirageforge_cli template delete <tpl_id>"

# 配置应用路由（支持三引擎属性模式、细粒度开关、HDR、刷新率与局部覆写）
su -c "mirageforge_cli route set flar2.devcheck xiaomi_15_pro \
  --build 1 --prop-mode mount_ns --gl 1 --hdr 1 \
  --cpu 1 --mem 1 --vfs-nodes 1 --mount-ns 1 \
  --fps 120 --ram-override 24 --cores-override 8 --restart"

# 移除应用伪装（恢复 0 改动）
su -c "mirageforge_cli route remove flar2.devcheck --restart"

# 查看引擎真实生效态（Companion 落盘）
su -c "mirageforge_cli runtime-status"
su -c "mirageforge_cli runtime-status clear"

# 新应用自动托管状态与手动单次扫描
su -c "mirageforge_cli auto-spoof status"
su -c "mirageforge_cli watch-installs --once"

# 探针日志、崩溃堆栈与一键诊断包导出
su -c "mirageforge_cli probe list --limit 30"
su -c "mirageforge_cli probe list --pkg flar2.devcheck --unmapped"
su -c "mirageforge_cli probe crashes --limit 10"
su -c "mirageforge_cli probe bundle /sdcard/Download/mirage_diag.txt"
```
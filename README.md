<div align="center">

<img src="webui/public/mirageforge-logo.svg" width="88" height="88" alt="MirageForge Logo" />

# 蜃楼工坊 · MirageForge

**「千面万象，一念幻化」—— 基于 Zygisk 的按应用机型与硬件信息伪装模块**

[![Version](https://img.shields.io/badge/Version-v1.2.0-8b5cf6?style=flat-square)](https://github.com/jjh138792/MirageForge/releases)
[![Zygisk](https://img.shields.io/badge/Zygisk-Supported-06b6d4?style=flat-square)](https://github.com/topjohnwu/zygisk-module-sample)
[![CLI](https://img.shields.io/badge/CLI-Rust%202021-f97316?style=flat-square)](https://www.rust-lang.org/)
[![License](https://img.shields.io/badge/License-GPL--3.0--only-f43f5e?style=flat-square)](LICENSE)

**简体中文** | **[English](docs/en_README.md)**

</div>

---

## 📖 写在前面

这是一个使用 **Zygisk** 实现的机型与硬件参数伪装模块。

说实话，市面上做机型伪装的模块已经很多了，其他大佬们的作品显然都比我这个要成熟、好用得多。所以这个项目纯粹就是我自己闲着没事随手写写、折腾着玩的产物。

如果你不小心点开了本仓库的 Commit 历史，看到满屏毫无营养的提交记录、修完东墙补西墙的连环报错，请不要惊讶，那是我真实的拉胯代码管理水平（甚至连现在的规范 Commit 信息都是让 AI 帮我编的）。不过既然造都造出来了，还是非常欢迎各位大佬提 **Issue** 或 **PR** 一起交流或者抢救一下我的代码！

---

## 🎯 它到底能干嘛？

简单来说，它可以在**不修改系统分区、不影响其他正常应用**的前提下，单独让指定的游戏或应用认为你的手机是另一台设备：

### 1. 解锁游戏高帧率与专属画质
* **机型与显卡（GPU）双重伪装**：很多手游不仅检测手机型号，还会检测显卡型号来限制画质和帧率。模块支持同时伪装手机型号与 OpenGL / Vulkan 显卡渲染器名称（例如伪装成小米 15 Pro、ROG 9 Pro、红魔 10 Pro+、vivo X200 Pro，以及配套的 `Adreno (TM) 830` 或 `Immortalis-G925` 显卡），开启游戏内置的 120Hz / 165Hz 极高帧率与极致画质。

### 2. 全方位的硬件与系统参数伪装
不只是改个手机名字，连各种硬件检测软件（如 DevCheck、Device Info HW）会查的细节都能一起改：
* **完整的系统属性**：支持伪装品牌、型号、设备代号、主板平台（`BOARD`）、处理器型号（`SOC_MODEL`）、构建号、系统版本号（`DISPLAY_ID` / `INCREMENTAL`）、安全补丁日期、完整指纹，以及跨品牌系统的专属属性（比如在类原生系统上伪装澎湃 OS / OriginOS 的系统属性）和底层安全状态属性。
* **CPU 与每颗核心的频率**：支持伪装处理器型号描述、核心数，以及**每一颗 CPU 核心的最高频率和实时跳动频率**（支持大小核/超大核分簇设置，看单核频率也是逼真的动态波动，不会死板卡在同一个数字）。
* **运行内存（RAM）**：支持同时伪装**已安装的物理内存规格**（如 `16GB LPDDR5X`、`24GB LPDDR5T`）与**系统可用内存大小**。

### 3. 按应用独立调整 DPI（屏幕缩放）
* 不需要修改系统全局 DPI，可以单独为某一款游戏或应用设置独立的屏幕密度（比如单独把某个应用设为 `320` 或 `480` DPI），改完重启该应用即可生效。

### 4. 一键导入 `build.prop` 生成配置
* 懒得手动填几十个参数？直接把从别的手机或 ROM 提取出来的 `build.prop` 文件导入 WebUI，模块会自动提取里面的机型、指纹、安全补丁、DPI，并自动推导出配套的处理器频率和显卡参数，一键生成可用的机型模板。

### 5. 硬件探测抓包（探针雷达）
* 遇到某个应用伪装后还是识别不对？可以在分流列表里单独给它打开 **「📡 探针与调用栈」** 开关。
* 打开应用跑几秒，回到面板就能清楚看到它刚才偷偷读取了哪些系统属性、访问了哪些硬件文件、甚至具体是哪个 `.so` 库发起的调用。如果发现它读了模板里没配的冷门属性，点一下 **「+ 捕获入库」** 就能直接加进模板里。

### 6. 干净省心，不改的绝对不碰
* **按需开关**：每个应用下面都有独立的功能开关（改属性、改显卡、改 CPU/内存文件、开探针、改 SDK 版本）。**没开启的选项、或者没配置伪装的应用，模块完全不会去动它**，不拖慢日常应用启动速度。
* **默认保护应用不闪退**：默认保持宿主机的真实 Android SDK 版本，防止部分系统工具或应用因为跨安卓大版本调用不存在的系统接口而崩溃；有特殊需求也可以按应用单独开启强改 SDK。

---

## 🚀 怎么安装和使用？

### 1. 环境要求
* **系统版本**：Android 8.0 (API 26) ~ Android 16
* **Root 与 Zygisk 环境**：
  * **Magisk** (v26.0+)：开启内置 Zygisk，或安装 **Zygisk Next / ReZygisk**
  * **KernelSU / KernelSU Next / APatch**：需配合安装 **Zygisk Next** 或 **ReZygisk** 模块
* **刷入方式**：支持在 Magisk / KernelSU / APatch 管理器内直接刷入，也支持在 TWRP 等 Recovery 下刷入。

### 2. WebUI 界面操作（推荐）
刷入模块重启后，直接在 **KernelSU / APatch / MMRL / KSUWebUI** 管理器中点开「蜃楼工坊 · MirageForge」即可进入可视化面板（支持浅色护眼/深色模式跟随系统切换、简/繁/英三语切换以及在线检查更新）：
1. **全息熔炉**：管理内置的旗舰机型模板，或点击「铸造新机型」自己新建/编辑一套硬件配置。
2. **属性炼金**：上传或粘贴 `build.prop` 文件内容，自动解析成机型模板。
3. **分流矩阵**：搜索你要伪装的应用，在下拉框里选好机型模板，按需调整下方的功能开关或独立 DPI，点一下右侧的 ⚡ 按钮重启应用即可生效。
4. **幻境探针**：查看应用启动遥测记录、崩溃诊断日志，以及开启探针后的属性与文件读取抓包记录。

### 3. 命令行工具 (`mirageforge_cli`)
如果你喜欢用终端或者写脚本，模块安装后会自动把 `mirageforge_cli` 放到系统环境变量里，打开 Termux 或 ADB Shell 就能直接用：

```bash
# 查看当前模块状态与已生效的应用列表
su -c "mirageforge_cli status"

# 解析本地的 build.prop 并直接保存为新模板
su -c "mirageforge_cli parse-prop /sdcard/Download/build.prop --save"

# 给指定应用绑定模板（支持设置各模块开关、独立 DPI，并自动重启目标应用）
su -c "mirageforge_cli route set com.tencent.tmgp.sgame rog_9_pro --build 1 --gl 1 --vfs 1 --probe 0 --sdk 0 --dpi 480 --restart"

# 解除指定应用的伪装
su -c "mirageforge_cli route remove com.tencent.tmgp.sgame --restart"

# 查看探针抓取到的硬件读取记录 (--unmapped 仅看未配置的属性)
su -c "mirageforge_cli probe list --limit 30"
```

---

## 🛠️ 编译构建

仓库内置了完整的 GitHub Actions 工作流：
* **日常构建验证 (`Build CI`)**：每次推送代码时自动编译前端 WebUI、Rust CLI 与四大架构的 Zygisk 动态库。
* **正式版发布 (`Build Release`)**：手动触发运行，自动读取 `module/module.prop` 中的版本号打包发布 GitHub Release，并同步更新仓库的 `update.json`。

---

## 🤖 坦白局 & 致谢

为了保持诚实：**这份看起来人模狗样的 README 文档（以及项目里相当一部分被我折磨出来的代码重构）是由 Google Gemini 写的**。如果没有 AI 帮我收拾烂摊子，这篇文档大概真的就只有最开始那三行字。

---

## 📄 开源协议

本项目基于 **[GPL-3.0-only](LICENSE)** 协议开源。欢迎自由使用、修改与分享，但衍生作品必须同样保持开源。

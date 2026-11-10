<div align="center">

# 蜃楼工坊 · MirageForge

**「千面万象，一念幻化」—— 应用级硬件拓扑虚拟化与逆向探针 Zygisk 模块**

[![Version](https://img.shields.io/badge/Version-v1.1.0-8b5cf6?style=flat-square)](https://github.com/jjh138792/MirageForge/releases)
[![Zygisk](https://img.shields.io/badge/Zygisk-API%20v4%20(C%2B%2B17)-06b6d4?style=flat-square)](https://github.com/topjohnwu/zygisk-module-sample)
[![CLI](https://img.shields.io/badge/CLI-Rust%202021-f97316?style=flat-square)](https://www.rust-lang.org/)
[![License](https://img.shields.io/badge/License-GPL--3.0--only-f43f5e?style=flat-square)](LICENSE)

**简体中文** | **[English](docs/en_README.md)**

</div>

---

## 📖 写在前面

如你所见，这是一个基于 **Zygisk** 的机型与硬件信息伪装模块。

说实话，做机型伪装的模块在圈子里早就满大街都是了，前辈和大佬们的作品肯定比我这个要成熟稳定得多。这个项目起初就是我自己闲着没事随便写写、折腾着玩的。但本着“来都来了，名字都叫**蜃楼工坊**了，总不能只改两行 `build.prop` 就收工吧”的心态，不知不觉把 **Bionic 底层属性池**、**Vulkan/OpenGL 渲染器伪装**、**单核动态频率**、**Rust 命令行中枢**和一套现代化的 **WebUI** 全给缝进去了。

虽然功能堆得挺杂，但肯定还有很多考虑不周的地方，非常欢迎各位提交 **Issue** 或 **PR** 一起交流（或者帮我修 Bug）。

---

## ✨ 它到底能干嘛？

市面上普通改机模块能改的 `android.os.Build` 和系统属性它都能改；普通模块懒得管或者容易露馅的细节，它也顺手填了坑：

### 1. 🛑 真正的「零改动」按需加载 (Zero-Touch On-Demand)
* **不改的应用绝不乱动**：在 `onLoad` 阶段不做任何符号解析或内存分配。只要不是你在配置里指定的应用，模块在启动瞬间直接调用 `DLCLOSE_MODULE_LIBRARY` 卸载走人，0 内存占用、0 注入痕迹。
* **没开的开关绝不瞎 Hook**：每个应用都有 5 个独立子开关（`属性/Build`、`GPU/DRM`、`CPU/内存/VFS`、`探针与调用栈`、`强改SDK`）。你没开的选项，底层直接跳过注册，绝不搞“先改掉再改回来”的假把式。
* **外科手术式 PLT 过滤**：只针对系统核心运行时桥接库与应用自身 `.so`（含未解压直映的 `.apk` 内置库）挂载 Hook，不扫无关的字体/UI 库，告别开启模块后的应用启动卡顿。

### 2. 🧬 AOSP 1:1 内存对齐的 Bionic 属性池
* **防裸指针强转刺探**：严格按照 Android Bionic `struct prop_info` 的真实内存布局（`4字节 serial + 92字节 value + 柔性数组 name`）铸造无锁只读句柄。就算遇到不讲武德的风控 SDK 直接用 `(const char*)pi + 4` 强转读内存，也能读出合法的伪装值而不会闪退。
* **全表遍历与跨品牌属性注入**：不仅拦截 `find` / `read_callback` / `get` / `popen("getprop")`，还完整接管了 `__system_property_foreach`。哪怕你在原生 Pixel 上伪装小米澎湃 OS，遍历全表时也能看到完整的 `ro.miui.*` 属性。

### 3. 🎮 Vulkan + OpenGL ES + MediaDrm 显卡幻化
* **解锁高帧率与极致画质**：现在的大型手游早就默认切到 Vulkan 管线了。本模块同时拦截 OpenGL ES（`glGetString`）与 **Vulkan**（`vkGetPhysicalDeviceProperties` / `Properties2`），同步修改 `deviceName` 与 `vendorID`（比如把你的老芯片伪装成 `Adreno (TM) 830` 或 `Immortalis-G925 MC12`），顺带连 `libmediandk.so` 的 DRM 硬件厂商信息也一并改掉。

### 4. ⚡ 异构单核动态频率 & 对称物理内存 (VFS 护盾)
* **拒绝「6GB 物理内存配 16GB 可用内存」的医学奇迹**：
  * 按照真实旗舰机的硬件 Carveout 预留比例生成 `/proc/meminfo`，并与 `sysinfo()`、`sysconf(_SC_PHYS_PAGES)` 保持严格一致。
  * 同步伪装 `/proc/ddr_info` 及 12 项底层 Bootloader DDR 属性（如 `16GB LPDDR5X (SKhynix)`），让检测软件里的“已安装物理内存”和“可用内存”彻底对称。
* **每颗核心都有自己的脾气（异构分簇动态频率）**：
  * 支持按核心分簇配置频率范围（如骁龙 8 至尊版的 `6+2` 架构、天玑 9400 的 `4+3+1` 架构），自动覆盖 `cpu0~7/cpufreq/*` 与 `policy0~7/*`。
  * 当应用读取单核实时频率（`scaling_cur_freq`）时，引擎会按照 19.2MHz 晶振步进的真实 OPP 频点表返回微抖动频率，8 颗核心各自跳动，不再死板卡死在同一个数字。
* **句柄隐身衣与 Maps 自动脱水**：
  * 内置无锁原子位图 `FdCloakTracker`，拦截 `readlink("/proc/self/fd/N")` 与 `fstat`，抹除 `memfd` 匿名文件特征。
  * 当应用偷偷读取 `/proc/self/maps` 或 `mountinfo` 查岗时，自动过滤掉包含 `zygisk`、`mirage_forge`、`magisk`、`kernelsu` 的敏感行。

### 5. 📡 幻境探针与调用栈回溯雷达 (Mirage Probe)
* **看看应用到底在查你什么**：给目标应用点亮「探针」开关后，它在后台偷偷读了哪个属性、开了哪个硬件文件、甚至是由哪个 `.so` 的哪个函数偏移量（例如 `libtersafe2.so+0x8f410 <- libc.so!...`）发起的调用，都会通过 Root 守护进程异步落盘记录。
* **一键抓包入库**：看到探针里标黄的 `UNMAPPED`（未映射敏感属性），在面板里点一下 **「+ 捕获入库」** 就能直接写进当前机型模板。
* **崩溃现场抓拍**：自带 `SIGSEGV` / `SIGABRT` 信号拦截器，万一应用崩了，第一时间把故障内存地址和 Native 崩溃调用栈拍下来留档。

### 6. 🦀 Rust 命令行中枢 + 顺眼的现代化 WebUI
* **Rust `mirageforge_cli`**：配置管理、`.prop` 文件一键解析推导、原子级 `.conf` 编译全部由静态编译的 Rust 二进制接管，稳且快。
* **好用的 WebUI**：兼容 KernelSU / APatch / MMRL (WebUI-X)，自带预设模板、`.prop` 一键导入炼金、应用分流矩阵和探针查看器，支持简/繁/英切换。毕竟都这年头了，把界面做得顺眼点、带点流畅动效和触感反馈是基本修养。

---

## 📂 目录结构

```text
MirageForge/
├── .github/workflows/build.yml    # 云端打工人：自动编译前端 + Rust CLI + 四架构 Zygisk 库
├── zygisk/src/main/cpp/           # [C++] Zygisk API v4 核心注入与按需 Hook 引擎
├── cli/                           # [Rust] mirageforge_cli 独立命令行控制中枢
├── webui/                         # [Vue + TS] WebUI 前端工程 (构建产物自动输出至 module/webroot)
└── module/                        # 模块刷入包骨架 (支持 Manager 与 TWRP Recovery 刷入)
```

---

## 🚀 怎么用？

### 1. 刷入模块
1. 去 [Releases](https://github.com/jjh138792/MirageForge/releases)（或者 GitHub Actions 构建产物）下载最新的刷入包。
2. 确保你的设备已启用 **Zygisk**（Magisk 内置 Zygisk，或者 KernelSU / APatch 安装 **Zygisk Next / ReZygisk**）。
3. 在管理器内刷入（也支持在 TWRP 等 Recovery 下直接刷入），重启手机。

### 2. 打开 WebUI 配置
重启后在管理器里点击模块的 **WebUI** 按钮即可进入控制台：
* **全息熔炉**：直接使用内置的旗舰模板（小米 15 Pro、ROG 9 Pro、红魔 10 Pro+、vivo X200 Pro），或者自己新建/编辑机型、GPU 与 CPU 分簇频率。
* **属性炼金**：把从别的 ROM 提取出来的 `build.prop` 扔进去，一键解析生成全套模板。
* **分流矩阵**：给不同的 App 绑定不同的机型模板，按需勾选下方的微模块开关，点右侧的 ⚡ 按钮即可强行停止该应用并立即生效。
* **幻境探针**：开启某应用的探针开关后运行一下该应用，回来刷新就能看到带 `.so` 调用栈的硬件读取记录。

### 3. 终端命令 (`mirageforge_cli`)
如果你喜欢敲命令，模块刷入后已经把 `mirageforge_cli` 软链到了 `/system/bin/mirageforge_cli`，打开 Termux 或 ADB Shell 就能直接调戏：

```bash
# 查看当前模块状态与生效列表
su -c "mirageforge_cli status"

# 一键解析 build.prop 并保存为模板
su -c "mirageforge_cli parse-prop /sdcard/Download/build.prop --save"

# 为指定应用绑定模板并立即重启该应用
su -c "mirageforge_cli route set com.tencent.tmgp.sgame rog_9_pro --build 1 --gl 1 --vfs 1 --probe 0 --sdk 0 --restart"

# 查看最近捕获到的探针与调用栈日志
su -c "mirageforge_cli probe list --limit 30"
```

---

## 🤣 关于本仓库的 Commit 历史与坦白局

1. **请勿模仿我的 Git 习惯**：
   如果你不幸点开了本仓库的 Commit 记录，你会看到大量诸如 `Update`、`fix`、甚至为了改一行拼写错误反复触发 GitHub Actions 的抽象操作。没错，我就是把 GitHub Actions 当成了免费的云端交叉编译器在用，属于典型的“只要能跑通 CI，Commit 信息写什么都行”的野生派写法，请各位代码洁癖大佬高抬贵手。
   
2. **诚实坦白局（Powered by Gemini）**：
   不装了，摊牌了——本项目我在写的时候其实用过辅助的，不管是代码，还是你现在看到的这篇 README，**其实 Google Gemini都有辅助功劳**。
   
   我在这个项目里扮演的核心角色是：**写部分代码 + 提出各种刁钻需求的产品经理 + 真机刷入抓 Bug 的人肉测试员 + 当 Gemini 写出弱智错误或逻辑漏洞时负责骂醒它的监工**。事实证明，只要监工盯得紧，AI 还是能搓出点好玩意儿的。

---

## 📄 开源协议

本项目采用 **[GPL-3.0-only](LICENSE)** 协议开源。

欢迎随便拿去研究、修改或者提 PR，但任何基于本项目的修改版或衍生品也必须保持 **GPL-3.0-only** 开源，严禁闭源打包拿去卖钱。

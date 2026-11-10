<div align="center">

# 蜃楼工坊 · MirageForge

**「千面万象，一念幻化」—— 新一代全息硬件虚拟化与逆向探针 Zygisk 引擎**

[![Version](https://img.shields.io/badge/Version-v1.1.0-8b5cf6?style=flat-square)](https://github.com/jjh138792/MirageForge/releases)
[![Zygisk](https://img.shields.io/badge/Zygisk-API%20v4%20(C%2B%2B17)-06b6d4?style=flat-square)](https://github.com/topjohnwu/zygisk-module-sample)
[![CLI](https://img.shields.io/badge/CLI-Rust%202021-f97316?style=flat-square)](https://www.rust-lang.org/)
[![WebUI](https://img.shields.io/badge/WebUI-Vue%203%20%2B%20TS%20Liquid%20Glass-10b981?style=flat-square)](https://kernelsu.org/zh_CN/guide/module-webui.html)
[![License](https://img.shields.io/badge/License-GPL--3.0--only-f43f5e?style=flat-square)](LICENSE)

</div>

---

## 📖 写在前面

这是一款基于 **Zygisk** 的应用级机型与硬件拓扑伪装模块。

老实说，市面上做机型伪装的模块已经很多了，前辈和大佬们的作品显然都比我这个要成熟得多，所以这个项目起初也就是自己随手写写、折腾着玩的产物。不过既然取名叫 **「蜃楼工坊 · MirageForge」**，写着写着还是忍不住把架构往死里堆了堆料——从 **AOSP 1:1 内存对齐的 Bionic 属性池**、**Vulkan/GLES 渲染管线劫持**、**异构单核动态频率与对称物理内存虚拟化**，到 **Rust 编写的原生 CLI 控制中枢** 与 **Vue 3 液态玻璃 WebUI**，全给塞进去了。

代码肯定还有不少粗糙的地方，非常欢迎各位大佬提交 **Issue** 或 **PR** 一起交流改进！

---

## ✨ 核心特性与技术架构

不同于传统仅修改 `build.prop` 或反射几个 `android.os.Build` 字符串的简单方案，**MirageForge v1.1.0** 采用 **「C++17 Zygisk 按需微模块 + Rust CLI 原子编译器 + Vue 3 液态玻璃前端」** 三层解耦架构：

### 1. 🛡️ 绝对「零改动」按需微模块架构 (Zero-Touch On-Demand)
* **未伪装应用 0 干扰**：`onLoad` 阶段执行 **0 次 `dlsym`、0 次内存分配**。未在分流矩阵中绑定的应用在 `preAppSpecialize` 阶段经 Companion 内存缓存 `O(1)` 判定后立即调用 `DLCLOSE_MODULE_LIBRARY` 卸载，不留任何注入痕迹。
* **子模块开关级完全隔离**：每个应用支持独立开关 **`[Bionic/ART 属性]`**、**`[Vulkan/GLES/DRM]`**、**`[单核频率/对称内存/VFS]`**、**`[探针与调用栈]`**、**`[强改 SDK 版本]`** 五大微模块。**未开启的选项绝不注册对应的 PLT Hook、绝不触碰原系统数据**（杜绝“先改再改回来”的伪零改动）。
* **外科手术式 PLT 挂载**：精准过滤系统核心桥接库与应用私有库（含 `extractNativeLibs="false"` 的 APK 内置库），搭配 ThinLTO 编译优化，应用启动零卡顿。

### 2. 🧬 AOSP 1:1 无锁 Bionic 属性内存池 (Lock-Free Prop Arena)
* **防裸指针偏移强转检测**：严格按照 AOSP Bionic `struct prop_info` 的真实二进制内存布局（`uint32_t serial` [4B] + `char value[92]` [92B] + `char name[]`）铸造只读冻结态虚拟句柄。即使风控 SDK 不调 API、直接通过 `(const char*)pi + 4` 强转读取内存，也能拿到合法伪装值且绝不崩溃。
* **全链路属性合围**：完整接管 `__system_property_find`、`__system_property_read_callback`、`__system_property_read`、`__system_property_get`、`__system_property_foreach`（全表遍历注入跨品牌属性，如在 Pixel 上伪装澎湃 OS 的 `ro.miui.*`）、`__system_property_find_nth` 以及 `popen("getprop ...")` 管道命令。

### 3. 🎮 Vulkan + OpenGL ES + MediaDrm 渲染管线幻化
* **双图形 API 劫持**：同时拦截 OpenGL ES（`glGetString` / `eglGetProcAddress`）与现代手游默认启用的 **Vulkan**（`vkGetInstanceProcAddr` / `vkGetPhysicalDeviceProperties` / `vkGetPhysicalDeviceProperties2`），同步篡改 `VkPhysicalDeviceProperties.deviceName` 与 PCI `vendorID`（如 `Adreno (TM) 830` 或 `Immortalis-G925 MC12`），真正解锁绑定 GPU 型号的游戏极高帧率与极致画质。
* **DRM 硬件标识同步**：拦截 `libmediandk.so` 的 `AMediaDrm_getPropertyString`，同步伪装底层 `vendor` 与 `description`。

### 4. ⚡ 异构单核动态频率与对称物理内存 (On-Demand VFS & FdCloak)
* **对称物理内存（告别“6GB 物理内存 vs 16GB 可用内存”穿帮）**：
  * 引入真实硬件 Carveout 折算算法生成 `/proc/meminfo`，并与 `sysinfo()`、`sysconf(_SC_PHYS_PAGES)` 保持字节级一致。
  * 同步合成 `/proc/ddr_info`、`/sys/devices/system/chip-info/ddr_info` 及 12 项底层 Bootloader DDR 属性（如 `ro.boot.hardware.ddr=16GB,SKhynix,LPDDR5X`），确保系统信息与硬件检测工具显示的「已安装物理内存」与「可用内存」严丝合缝。
* **异构多簇单核频率微抖动引擎**：
  * 支持自定义异构分簇拓扑（如骁龙 8 至尊版 `6+2`：`0-5:614400:3532800;6-7:806400:4320000`、天玑 9400 `4+3+1` 等）。
  * 按需合成 `cpu0~7/cpufreq/*` 与 `cpufreq/policy0~7/*` 全部节点。当检测软件读取每颗核心的实时频率（`scaling_cur_freq` / `cpuinfo_cur_freq`）或通过 `lseek(fd, 0, SEEK_SET)` 循环刷新时，引擎根据真实 19.2MHz 晶振步进的 OPP 频点表返回动态微抖动频率。
* **`FdCloakTracker` 无锁位图隐身衣 + Maps 实时脱敏**：
  * 采用 `O(1)` 原子位图追踪虚拟 `memfd`，拦截 `readlink`、`readlinkat`、`fstat(64)`、`stat(64)`、`lstat(64)`、`fstatat(64)`、`access`，反查 `/proc/self/fd/<N>` 时完美还原为真实系统路径，隐藏 `/memfd:` 痕迹。
  * 当目标应用读取 `/proc/self/maps`、`/proc/self/smaps` 或 `/proc/self/mountinfo` 时，实时剥离包含 `zygisk`、`mirage_forge`、`magisk`、`kernelsu`、`/data/adb` 的敏感行。

### 5. 📡 幻境探针与 Native 调用栈回溯雷达 (Mirage Probe)
* **精准定位风控探测点**：内置基于 `<unwind.h>` `_Unwind_Backtrace` 与 `dladdr` 的符号反解器。开启探针后，目标应用读取了哪个属性、打开了哪个硬件节点、由哪个 `.so` 的哪个函数偏移量（如 `libtersafe2.so+0x8f410 <- libc.so!__system_property_read_callback+0x28`）发起，全部由 Root Companion 异步守护线程实时落盘至 `probe_events.log`。
* **未映射属性一键捕获**：探针一旦发现目标应用读取了模板中尚未配置的敏感属性（标记为 `UNMAPPED`），在 WebUI 中点击 **「+ 捕获入库」** 即可一键写入对应模板并立即重编译。
* **Native 崩溃信号诊断台**：内置 `SIGSEGV` / `SIGABRT` / `SIGBUS` / `SIGILL` 信号捕获器，应用闪退时自动记录故障地址与崩溃调用栈至 `crash_events.log`。

### 6. 🦀 Rust 原生 CLI 中枢 + 🔮 Vue 3 液态玻璃 WebUI
* **Rust `mirageforge_cli`**：纯 Rust 编写并静态编译，提供 `Write-Fsync-Rename` 原子级配置编译、`.prop` 文件一键炼金解析（自动推导 SoC、GPU、异构分簇与 LPDDR 规格）及探针日志管理。
* **Vue 3 + TypeScript 液态玻璃界面**：内置 SVG 光学折射滤镜（`#liquid-refraction`）、动态流体极光画布、手指追光折射高光（`v-touch-glass`）、多级物理触觉振动反馈以及 **简 / 繁 / EN** 三语实时热切换，全面兼容 **KernelSU / APatch / MMRL WebUI-X**。

---

## 📂 项目目录结构

```text
MirageForge/
├── .github/workflows/
│   └── build.yml                  # 全自动 CI：构建 Vue 3 WebUI + Rust CLI + 四架构 Zygisk .so
├── zygisk/src/main/cpp/           # [C++17] Zygisk API v4 核心虚拟化引擎
│   ├── CMakeLists.txt
│   ├── mirage_forge.cpp           # Zygisk 生命周期主控与 Root Companion 内存缓存/异步落盘服务
│   └── include/
│       ├── mirage_config.hpp      # 按需微模块隔离配置容器
│       ├── mirage_logger.hpp      # _Unwind_Backtrace 调用栈回溯与崩溃信号捕获器
│       ├── fd_cloak_tracker.hpp   # O(1) 原子位图 memfd 隐身追踪器
│       ├── bionic_prop_engine.hpp # AOSP 1:1 无锁 Bionic 属性内存池
│       ├── vfs_sysinfo_engine.hpp # 按需懒加载 VFS、异构单核动态频率与 sysinfo/sysconf 引擎
│       ├── graphics_vulkan_gles.hpp # Vulkan + OpenGL ES + MediaDrm 劫持引擎
│       ├── linker_plt_engine.hpp  # 外科手术式 ELF 过滤器与增量 dlopen 追踪器
│       └── jvm_art_engine.hpp     # Java Build 全类型反射引擎
├── cli/                           # [Rust 2021] 独立二进制控制中枢 (mirageforge_cli)
│   ├── Cargo.toml
│   └── src/
│       ├── main.rs                # CLI 命令路由入口
│       ├── models.rs              # Serde 强类型数据模型
│       ├── fs_atomic.rs           # Write-Fsync-Rename 原子文件 IO
│       ├── alchemy.rs             # build.prop 炼金解析与硬件拓扑自动推导
│       ├── compiler.rs            # 微模块 .conf 按需编译器 (未开启项 0 输出)
│       ├── store.rs               # 模板库与分流路由持久化管理
│       └── probe.rs               # 探针日志、崩溃栈与注入遥测收割分析器
├── webui/                         # [Vue 3 + TS + Vite] 液态玻璃 WebUI 工程
│   ├── package.json
│   ├── vite.config.ts             # 编译打包至 ../module/webroot
│   └── src/
│       ├── styles/liquid-glass.css
│       ├── bridge/webui-x.ts      # WebUI-X & KSU 桥接 + 触觉振动 + 追光指令
│       ├── i18n/index.ts          # 简 / 繁 / EN 国际化字典
│       ├── store/mirageStore.ts   # 对接 Rust CLI 的响应式状态库
│       ├── App.vue                # 悬浮灵动岛导航主框架
│       └── components/            # 全息熔炉 / 属性炼金 / 分流矩阵 / 探针雷达四大组件
└── module/                        # 刷入包模板目录 (支持 Manager 与 TWRP Recovery 双栖刷入)
    ├── META-INF/com/google/android/
    ├── module.prop
    ├── customize.sh
    ├── action.sh
    └── uninstall.sh
```

---

## 🚀 安装与使用指南

### 1. 环境要求
* **Android 版本**：Android 8.0 (API 26) ~ Android 16
* **Root 与 Zygisk 环境**：
  * **Magisk** (v26.0+)：开启内置 Zygisk 或安装 **Zygisk Next / ReZygisk**
  * **KernelSU / KernelSU Next / APatch**：需配合安装 **Zygisk Next** 或 **ReZygisk** 模块
* **刷入方式**：支持在 Magisk / KernelSU / APatch 管理器内直接刷入，同时也支持在 **TWRP / OrangeFox Recovery** 下刷入。

### 2. WebUI 可视化配置
刷入模块并重启手机后，在 **KernelSU / APatch / MMRL / KSUWebUI** 管理器中点击「蜃楼工坊 · MirageForge」打开 WebUI：
1. **全息熔炉**：查看预置的 4 套满血旗舰模板（小米 15 Pro、ROG 9 Pro、红魔 10 Pro+、vivo X200 Pro），或点击「铸造新机型」自定义机型、GPU 渲染器、异构 CPU 分簇频率与 LPDDR 内存规格。
2. **属性炼金**：上传从任意 ROM 提取的 `build.prop` 文件或直接粘贴属性文本，Rust 引擎会自动提取机型指纹并推导出配套的 GPU 与 CPU 分簇拓扑。
3. **分流矩阵**：为指定应用选择要伪装的机型模板，并按需点亮下方的 5 个微模块胶囊开关，点击右侧 ⚡ 即可强行停止目标应用并立即生效。
4. **幻境探针**：为目标应用开启 `📡 探针与调用栈` 后运行应用，即可在此页面查看实时注入遥测、Native 崩溃诊断以及每一条属性/文件读取背后的 `.so!函数+偏移量` 调用栈。

### 3. `mirageforge_cli` 终端命令行用法
安装模块后，`mirageforge_cli` 会自动挂载至系统 `PATH`（位于 `/system/bin/mirageforge_cli` 或 `/data/adb/modules/mirage_forge/bin/mirageforge_cli`），支持在 Termux 或 ADB Shell 中直接调用：

```bash
# 1. 查看引擎状态与当前生效的分流路由
su -c "mirageforge_cli status"

# 2. 解析本地 build.prop 文件并自动保存为新模板
su -c "mirageforge_cli parse-prop /sdcard/Download/build.prop --save"

# 3. 为指定应用绑定模板并设置微模块开关 (--restart 自动重启目标应用)
su -c "mirageforge_cli route set com.tencent.tmgp.sgame rog_9_pro --build 1 --gl 1 --vfs 1 --probe 0 --sdk 0 --restart"

# 4. 解除指定应用的伪装
su -c "mirageforge_cli route remove com.tencent.tmgp.sgame --restart"

# 5. 查看最近的硬件探测与 Native 调用栈回溯 (--unmapped 仅看未映射属性)
su -c "mirageforge_cli probe list --limit 30"

# 6. 查看捕获到的 Native 崩溃信号与故障调用栈
su -c "mirageforge_cli probe crashes"
```

---

## 🛠️ 编译构建

本项目已配置完整的 **GitHub Actions** 流水线（[`.github/workflows/build.yml`](.github/workflows/build.yml)），每次 Push 或 Pull Request 都会自动完成三端构建并产出刷入包。如需在本地编译：

```bash
# 1. 构建 Vue 3 + TypeScript 液态玻璃 WebUI (产物自动输出至 module/webroot)
cd webui
npm install && npm run build
cd ..

# 2. 使用 cargo-ndk 交叉编译 Rust CLI (以 arm64-v8a 为例)
cd cli
cargo ndk -t arm64-v8a --platform 26 build --release
cp target/aarch64-linux-android/release/mirageforge_cli ../module/bin/mirageforge_cli
cd ..

# 3. 使用 Android NDK & CMake 编译 Zygisk 引擎
cmake -B build/arm64-v8a \
  -DCMAKE_TOOLCHAIN_FILE=$ANDROID_NDK_HOME/build/cmake/android.toolchain.cmake \
  -DANDROID_ABI=arm64-v8a \
  -DANDROID_PLATFORM=android-26 \
  -DCMAKE_BUILD_TYPE=Release \
  zygisk/src/main/cpp
cmake --build build/arm64-v8a --config Release
cp build/arm64-v8a/libmirage_forge.so module/zygisk/arm64-v8a.so

# 4. 打包刷入包
cd module && zip -r9 ../MirageForge-Zygisk-v1.1.0.zip .
```

---

## 🤝 贡献与反馈 (Issues / PRs)

* 如果你在测试某款应用或硬件检测工具时发现了漏网的硬件节点、属性字段或兼容性问题，欢迎直接开启模块的 **「📡 探针与调用栈」** 抓取日志，并提交 [Issue](https://github.com/jjh138792/MirageForge/issues)！
* 非常欢迎提交 [Pull Request](https://github.com/jjh138792/MirageForge/pulls) 补充更多旗舰机型的异构拓扑预设、优化 Hook 逻辑或改进 WebUI 动效。

---

## 📄 开源协议 (License)

本项目基于 **[GPL-3.0-only](LICENSE)** 协议开源。

* 你可以自由地使用、修改和分发本项目源码，但任何基于本项目的衍生作品或修改版本在分发时**必须同样以 GPL-3.0-only 协议开放完整源代码**，且不得用于闭源商业倒卖。

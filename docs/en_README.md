<div align="center">

# MirageForge (蜃楼工坊)

**Per-App Hardware Topology Virtualization & Native Stacktrace Probe for Zygisk**

[![Version](https://img.shields.io/badge/Version-v1.1.0-8b5cf6?style=flat-square)](https://github.com/jjh138792/MirageForge/releases)
[![Zygisk](https://img.shields.io/badge/Zygisk-API%20v4%20(C%2B%2B17)-06b6d4?style=flat-square)](https://github.com/topjohnwu/zygisk-module-sample)
[![CLI](https://img.shields.io/badge/CLI-Rust%202021-f97316?style=flat-square)](https://www.rust-lang.org/)
[![License](https://img.shields.io/badge/License-GPL--3.0--only-f43f5e?style=flat-square)](../LICENSE)

**[简体中文](../README.md)** | **English**

</div>

---

## 📖 Preface

**MirageForge** is a per-app device and hardware topology spoofing module powered by **Zygisk**.

To be completely honest, there are already plenty of device spoofing modules out there, and most of them made by veteran developers are probably much better than mine. This project started as a casual side-project just for fun. However, once I named it **MirageForge**, I figured it would be embarrassing if it only modified two lines of `build.prop`. So I ended up throwing in an **AOSP 1:1 Bionic property arena**, **Vulkan/GLES GPU spoofing**, **heterogeneous per-core dynamic CPU frequencies**, a **Rust CLI core**, and a modern **WebUI**.

It is still a work in progress and definitely has rough edges, so **Issues** and **Pull Requests** are more than welcome!

---

## ✨ What Does It Actually Do?

Besides spoofing basic `android.os.Build` fields and system properties like regular modules, MirageForge focuses on fixing the subtle hardware inconsistencies that anti-cheat and device-fingerprinting SDKs love to check:

### 1. 🛑 True "Zero-Touch" On-Demand Architecture
* **Zero Interference for Unconfigured Apps**: `onLoad` performs **0 `dlsym` lookups and 0 memory allocations**. If an app isn't in your target list, the module immediately unloads itself (`DLCLOSE_MODULE_LIBRARY`) during `preAppSpecialize` via an `O(1)` in-memory cache check in the Root Companion.
* **Strict Sub-Module Isolation**: Each target app has 5 independent toggles (`Bionic/ART Props`, `Vulkan/GLES/DRM`, `Per-Core Freq/DDR/VFS`, `Probe & Backtrace`, and `Force Spoof SDK`). Disabled sub-modules never register their PLT hooks and never touch original values—no "modify then restore" hacks.
* **Surgical PLT Filtering**: Only hooks essential system runtime bridge libraries and app-private `.so` libraries (including uncompressed in-APK libraries with `extractNativeLibs="false"`), skipping hundreds of irrelevant UI/font libraries to keep app cold-starts fast.

### 2. 🧬 AOSP 1:1 Lock-Free Bionic Property Arena
* **Immune to Raw Pointer Dereferencing**: Virtual `prop_info` handles are allocated using the exact binary memory layout of AOSP Bionic (`4-byte serial + 92-byte value + flexible name array`). Even if an aggressive risk-control SDK bypasses libc APIs and reads `(const char*)pi + 4` directly, it gets the spoofed value without crashing.
* **Full Table Enumeration (`__system_property_foreach`)**: Intercepts `find`, `read_callback`, `read`, `get`, `popen("getprop")`, and `foreach`. Cross-brand properties (e.g., spoofing Xiaomi HyperOS `ro.miui.*` on a Pixel) appear seamlessly even during full property table scans.

### 3. 🎮 Vulkan + OpenGL ES + MediaDrm GPU Spoofing
* **Unlock High Frame Rates & Graphics Settings**: Modern mobile games default to Vulkan. MirageForge hooks both OpenGL ES (`glGetString` / `eglGetProcAddress`) and **Vulkan** (`vkGetInstanceProcAddr` / `vkGetPhysicalDeviceProperties` / `Properties2`), spoofing `deviceName` and PCI `vendorID` (e.g., `Adreno (TM) 830` or `Immortalis-G925 MC12`), as well as `AMediaDrm_getPropertyString` in `libmediandk.so`.

### 4. ⚡ Heterogeneous Per-Core Cpufreq & Symmetric DDR Memory (VFS Shield)
* **No More "6GB Installed RAM with 16GB Available RAM"**:
  * Calculates realistic hardware carveout deductions for `/proc/meminfo` and keeps `sysinfo()` and `sysconf(_SC_PHYS_PAGES)` 100% consistent.
  * Synthesizes `/proc/ddr_info`, `/sys/devices/system/chip-info/ddr_info`, and 12 bootloader DDR properties (e.g., `16GB LPDDR5X (SKhynix)`) so "Installed Physical RAM" and "Available RAM" match in hardware inspection apps.
* **Dynamic Per-Core Frequency Jitter**:
  * Supports heterogeneous CPU cluster definitions (e.g., Snapdragon 8 Elite `6+2` or Dimensity 9400 `4+3+1`) covering `cpu0~7/cpufreq/*` and `policy0~7/*`.
  * When an app reads `scaling_cur_freq` or rewinds the descriptor via `lseek(fd, 0, SEEK_SET)`, the engine returns live fluctuating frequencies aligned to 19.2MHz crystal oscillator OPP steps.
* **Lock-Free FD Cloaking & Live Maps Sanitizer**:
  * `FdCloakTracker` uses an `O(1)` atomic bitmap to disguise virtual `memfd` descriptors when inspected via `readlink("/proc/self/fd/N")` or `fstat`.
  * Automatically strips lines containing `zygisk`, `mirage_forge`, `magisk`, `kernelsu`, or `/data/adb` when target apps read `/proc/self/maps` or `mountinfo`.

### 5. 📡 Mirage Probe & Native Stacktrace Radar
* **See What Apps Are Snooping**: Enable the Probe toggle for any app to log which properties, files, or GPU strings it reads, complete with live `_Unwind_Backtrace` + `dladdr` native caller chains (e.g., `libtersafe2.so+0x8f410 <- libc.so!...`), written asynchronously to disk by the Root Companion.
* **One-Tap Property Capture**: Spot an `UNMAPPED` property in the radar? Click **"+ Capture Prop"** in the WebUI to inject it directly into your active template.
* **Native Crash Inspector**: Catches `SIGSEGV`, `SIGABRT`, `SIGBUS`, and `SIGILL` to log the fault address and native backtrace into `crash_events.log`.

### 6. 🦀 Rust CLI Core + Modern WebUI
* **Rust `mirageforge_cli`**: Statically linked Rust binary handling atomic `Write-Fsync-Rename` config compilation, `build.prop` parsing/inference, and log harvesting.
* **Clean WebUI**: Compatible with KernelSU, APatch, and MMRL (WebUI-X), with built-in English / Simplified Chinese / Traditional Chinese support.

---

## 🚀 Quick Start

1. Download the flashable zip from [Releases](https://github.com/jjh138792/MirageForge/releases) (or GitHub Actions artifacts).
2. Flash it in **Magisk** (with Zygisk enabled), **KernelSU**, **APatch** (with Zygisk Next / ReZygisk), or via **TWRP Recovery**, then reboot.
3. Open the **WebUI** from your root manager, or use `mirageforge_cli` in Termux / ADB Shell:

```bash
# Check module status and active app routes
su -c "mirageforge_cli status"

# Parse a ROM's build.prop and save it as a template
su -c "mirageforge_cli parse-prop /sdcard/Download/build.prop --save"

# Bind a template to an app and force-stop it to apply immediately
su -c "mirageforge_cli route set com.tencent.tmgp.sgame rog_9_pro --build 1 --gl 1 --vfs 1 --probe 0 --sdk 0 --restart"

# View recent hardware probe events & native stacktraces
su -c "mirageforge_cli probe list --limit 30"
```

---

## 🤣 About My Commit History

1. **Please Do Not Look at My Git Commits**:
   If you check the commit history of this repo, you will witness a crime scene of `Update`, `fix`, and shameless trial-and-error pushes just to use GitHub Actions as my free cloud cross-compiler. If you are a Git hygiene purist, please look away for your own sanity.

---

## 📄 License

Licensed under the **[GPL-3.0-only](../LICENSE)** license. Feel free to fork, modify, and submit PRs, but any derivative work must remain open-source under GPL-3.0-only.

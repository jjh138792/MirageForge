<div align="center">

<img src="../webui/public/mirageforge-logo.svg" width="88" height="88" alt="MirageForge Logo" />

# MirageForge

**Per-App Device & Hardware Spoofing Zygisk Module**

[![Version](https://img.shields.io/badge/Version-v1.2.0-8b5cf6?style=flat-square)](https://github.com/jjh138792/MirageForge/releases)
[![Zygisk](https://img.shields.io/badge/Zygisk-Supported-06b6d4?style=flat-square)](https://github.com/topjohnwu/zygisk-module-sample)
[![CLI](https://img.shields.io/badge/CLI-Rust%202021-f97316?style=flat-square)](https://www.rust-lang.org/)
[![License](https://img.shields.io/badge/License-GPL--3.0--only-f43f5e?style=flat-square)](../LICENSE)

**[简体中文](../README.md)** | **English**

</div>

---

## 📖 Foreword

**MirageForge** is a Zygisk module for per-app device model and hardware spoofing.

To be completely honest, there are already plenty of device spoofing modules out there, and those made by other developers are obviously much better and more mature than mine. I started this project mostly just for fun and to tinker around in my spare time.

If you happen to peek into the Git commit history and see a disaster of meaningless commit messages and back-to-back panic fixes, please don't judge—that's just my terrible commit habit in the wild (even the clean commit messages you see recently were written by AI). Still, **Issues** and **Pull Requests** are always more than welcome!

---

## 🎯 What Does It Actually Do?

In short, it tricks specific apps or games into thinking you are using a different phone—**without modifying your system partition or affecting any other apps**:

### 1. Unlock High Frame Rates & Graphics in Games
* **Model & GPU Spoofing**: Many mobile games check both your phone model and your GPU model before unlocking 120Hz/165Hz modes or ultra graphics. MirageForge spoofs both device info and OpenGL / Vulkan GPU names (e.g., spoofing as Xiaomi 15 Pro, ROG Phone 9 Pro, RedMagic 10 Pro+, or vivo X200 Pro with `Adreno (TM) 830` or `Immortalis-G925`).

### 2. Comprehensive Hardware & System Info Spoofing
It goes far beyond changing `Build.MODEL`, covering the details checked by hardware inspection apps (like DevCheck or Device Info HW):
* **Rich System Properties**: Spoofs Brand, Model, Device, Board (`Build.BOARD`), SoC Model (`Build.SOC_MODEL`), Build ID, Display ID (`Build.DISPLAY`), Incremental version, Security Patch date, Fingerprint, cross-brand ROM properties (like MIUI/HyperOS or OriginOS properties on AOSP), and restricted bootloader/security properties.
* **CPU & Per-Core Frequencies**: Spoofs processor descriptions, core counts, and **individual min/max/live frequencies for every single CPU core** (simulating real big.LITTLE / Prime core clusters with natural frequency fluctuations).
* **RAM Specifications**: Spoofs both **installed physical RAM specs** (e.g., `16GB LPDDR5X`, `24GB LPDDR5T`) and **available system memory**.

### 3. Per-App DPI Scaling
* Change the screen density (DPI) for individual apps or games (e.g., setting a specific app to `320` or `480` DPI) without touching your global system display resolution.

### 4. One-Click `build.prop` Import
* Don't want to type dozens of fields manually? Just import any `build.prop` file extracted from another ROM. MirageForge automatically extracts the identity, fingerprint, security patch, and DPI, and infers the matching CPU cluster frequencies and GPU model to create a ready-to-use template.

### 5. Hardware Access Sniffer (Probe Radar)
* Wondering how a stubborn app still detects your device? Turn on the **`📡 Probe & Backtrace`** switch for that app.
* Run the app for a few seconds, and the WebUI will show you which system properties and hardware files it read, along with the exact native library (`.so`) and function offset that made the call. If you spot an unconfigured property, click **`+ Capture Prop`** to add it to your template on the fly.

### 6. Zero Interference When Turned Off
* **Granular Per-App Switches**: Every app has independent toggles for Properties, GPU, CPU/RAM VFS, Probe, and SDK version. **Unconfigured apps and disabled switches are not touched at all**, keeping app launches fast and clean.
* **Crash-Safe by Default**: Keeps your real Android SDK version by default so apps won't crash from calling mismatched system APIs, while still offering an optional per-app SDK spoof toggle when needed.

---

## 🚀 Installation & Usage

### 1. Requirements
* **Android**: Android 8.0 (API 26) ~ Android 16
* **Root & Zygisk**:
  * **Magisk** (v26.0+) with built-in Zygisk enabled, or **Zygisk Next / ReZygisk**
  * **KernelSU / KernelSU Next / APatch** with **Zygisk Next** or **ReZygisk** installed

### 2. Using the WebUI (Recommended)
After flashing and rebooting, open **MirageForge** from **KernelSU / APatch / MMRL / KSUWebUI** (supports Auto/Light/Dark themes, English/Chinese switch, and online update checks):
1. **Templates**: Browse built-in flagship presets or create/edit your own hardware template.
2. **Prop Alchemy**: Upload or paste a `build.prop` file to generate a template automatically.
3. **App Matrix**: Pick a target app, select a template, toggle specific spoofing modules or per-app DPI, and tap ⚡ to restart the app.
4. **Probe Radar**: Inspect launch telemetry, native crash logs, and live property/file access stacktraces.

### 3. Using the CLI (`mirageforge_cli`)
For terminal lovers and script automation, `mirageforge_cli` is available directly in your `PATH`:

```bash
# Check module status and active app routes
su -c "mirageforge_cli status"

# Parse a build.prop file and save it as a template
su -c "mirageforge_cli parse-prop /sdcard/Download/build.prop --save"

# Bind a template to an app with custom switches and per-app DPI
su -c "mirageforge_cli route set com.tencent.tmgp.sgame rog_9_pro --build 1 --gl 1 --vfs 1 --probe 0 --sdk 0 --dpi 480 --restart"

# Remove spoofing for an app
su -c "mirageforge_cli route remove com.tencent.tmgp.sgame --restart"
```

---

## 🤖 Honesty Corner & Credits

In the spirit of full transparency: **this README (as well as a hefty chunk of the code refactoring in this repository) was written by Google Gemini**. Without AI cleaning up my mess, this README would still be the three-line placeholder it started as.

---

## 📄 License

Licensed under the **[GPL-3.0-only](../LICENSE)** license.

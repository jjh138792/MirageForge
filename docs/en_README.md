<div align="center">

<img src="../webui/public/mirageforge-logo.svg" width="88" height="88" alt="MirageForge Logo" />

# MirageForge

**Per-App Device & Hardware Spoofing Zygisk Module**

[![Version](https://img.shields.io/badge/Version-v1.3.0-8b5cf6?style=flat-square)](https://github.com/jjh138792/MirageForge/releases)
[![Zygisk](https://img.shields.io/badge/Zygisk-Supported-06b6d4?style=flat-square)](https://github.com/topjohnwu/zygisk-module-sample)
[![License](https://img.shields.io/badge/License-GPL--3.0--only-f43f5e?style=flat-square)](../LICENSE)

**[简体中文](../README.md)** | **English** | **[Configuration Guide](en_CONFIG.md)**

</div>

---

## 📖 Foreword

**MirageForge** is a Zygisk module for per-app device model and hardware spoofing.

To be completely honest, there are already plenty of device spoofing modules out there, and those made by other developers are obviously much better and more mature than mine. I started this project mostly just for fun and to tinker around in my spare time.

If you happen to peek into the Git commit history and see a disaster of meaningless commit messages and back-to-back panic fixes, please don't judge—that's just my terrible commit habit in the wild (even the clean commit messages recently were written by AI). Still, **Issues** and **Pull Requests** are always more than welcome!

---

## 🎯 What Can It Do for You?

In short, it makes selected apps or games believe your phone is another device—**without touching your system partition or interfering with your other daily apps**:

### 1. Unlock Higher Frame Rates, Graphics & HDR
* **Device + GPU Spoofing**: Many games check both your phone model and GPU before unlocking 120Hz/165Hz modes or ultra settings. MirageForge spoofs device info alongside OpenGL / Vulkan GPU names, with optional high refresh rate (90Hz–165Hz) and **HDR / Dolby Vision** capability spoofing.
* **One-Tap Scene Presets**: Don't want to flip switches one by one? Pick **Max Stealth**, **Gaming FPS**, or **Full Spoof** right inside each app's settings page.

### 2. Full-Device Hardware Details
It goes well beyond just changing `Build.MODEL`:
* **System & Brand Info**: Brand, model, codename, SoC model, security patch date, fingerprint, and cross-brand ROM attributes (e.g., MIUI/HyperOS or OriginOS fields on AOSP).
* **CPU & Live Frequencies**: Processor name, core count, and per-core frequency ranges that fluctuate naturally instead of freezing on a static value.
* **Memory (RAM)**: Spoofs both physical RAM specs (such as `16GB LPDDR5X` or `24GB LPDDR5T`) and reported total/available memory.

### 3. Effortless Templates & Auto-Enrollment
* **Multiple Ways to Get Profiles**: Comes with built-in flagship templates, an **Online Template Hub**, `mf://` share codes, one-click `build.prop` import, and **Clone This Device** to capture your current phone as a template.
* **Per-App Overrides**: Need slightly different RAM, core count, or a couple of extra properties for just one app? Override them per app without duplicating an entire template.
* **Auto-Spoof New Installs**: Optionally apply your default template automatically whenever you install a new user app, while safely skipping root managers and shell tools.

### 4. Stay Out of the Way When Unused
* **Independent Per-App Switches**: Toggle System Props, CPU, GPU, Memory, and Hardware File Nodes separately. **Unconfigured apps and disabled features are left completely untouched.**
* **Flexible Property Modes**: **Double-tap or long-press** the *Spoof System Props* row in an app's page to switch between compatibility mode and stealth modes, and check the real live status badge directly in the app list.
* **Crash-Safe by Default**: Keeps your real Android SDK version unless you explicitly enable SDK spoofing for a specific app.

### 5. Built-In Troubleshooting Probe
If an app still recognizes your real device, temporarily turn on **Probe & Backtrace** for that app. Run it for a few seconds and check the WebUI to see what properties or files it queried—then tap **`+ Capture Prop`** to add any missing property straight into your template.

---

## 🚀 Installation & Usage

### 1. Requirements
* **Android**: Android 8.0 (API 26) ~ Android 17 (API 37)
* **Root & Zygisk**:
  * **Magisk** (v26.0+) with built-in Zygisk enabled, or **Zygisk Next / ReZygisk**
  * **KernelSU / KernelSU Next / APatch** with **Zygisk Next** or **ReZygisk**
* Flash the release ZIP in your root manager and reboot.

### 2. Using the WebUI (Recommended)
Open **MirageForge** from **KernelSU / APatch / MMRL / KSUWebUI** (supports Auto/Light/Dark themes, 5 accent colors, horizontal swipe navigation, multi-language UI, and in-app OTA updates):
1. **Home**: View host device & Root/Zygisk status, active spoof counts, and update alerts.
2. **Templates**: Manage hardware profiles, pull from the Online Hub, import `build.prop` or `mf://` share codes, or clone your host device.
3. **Apps**: Select an app, bind a template, toggle capabilities (double-tap/long-press *Spoof System Props* to switch engine mode), and tap ⚡ to restart the app.
4. **Settings**: Customize UI appearance, default spoofing backends, Auto-Spoof for new apps, Probe Radar, and diagnostic bundle export.

### 3. CLI & Advanced Configuration
For configuration schemas, engine modes, and `mirageforge_cli` commands, see:
* 👉 **[Configuration & CLI Reference (docs/en_CONFIG.md)](en_CONFIG.md)**

---

## 🤖 Honesty Corner & Credits

In the spirit of full transparency: **this README (as well as a hefty chunk of the code refactoring in this repository) was written by Google Gemini**. Without AI cleaning up my mess, this README would still be the three-line placeholder it started as.

### Credits
- [device_faker](https://github.com/Seyud/device_faker) —— Some features and ideas were inspired by this project.

---

## 📄 License

Licensed under **[GPL-3.0-only](../LICENSE)**. If you find this module useful, feel free to leave a **✨ Star**!

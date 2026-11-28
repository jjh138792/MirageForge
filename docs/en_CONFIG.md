# MirageForge Configuration & CLI Reference (v1.3.0)

**[简体中文](CONFIG.md)** | **[Back to README](en_README.md)**

This reference documents MirageForge's storage layout, route and compiled config schemas, spoofing backend mechanics, and `mirageforge_cli` commands.

---

## 1. Storage Layout

All runtime data resides under `/data/adb/mirage_forge/` (dir mode `0700`, file mode `0600`):

| Path | Description |
| :--- | :--- |
| `templates/<id>.tpl` | Individual hardware template JSON files |
| `routes.db` | Primary app routing table (v3 16-column pipe-delimited; Zygisk Companion hot-reloads on `mtime` change) |
| `apps/<pkg>.conf` | Compiled flat `KEY=VALUE` profile consumed directly by Zygisk Companion |
| `config.json` | Full aggregated state snapshot (templates, routes, and auto-spoof config) |
| `auto_spoof.json` | Auto-enrollment policy for newly installed user apps |
| `known_pkgs.db` | Baseline user-package snapshot maintained by `watch-installs` |
| `watch_installs.pid` | PID lock file for the `inotify` watcher daemon |
| `runtime_status.json` | Per-app last live injection status and fallback diagnostics written by Companion |
| `probe_events.log` | Probe radar events and native backtraces |
| `crash_events.log` | Fatal native crash stacktraces captured when Probe is enabled |
| `launch_history.log` | App attachment telemetry log |

---

## 2. Spoofing Backends

### 2.1 Tri-Engine System Properties (`propMode`)

Java `android.os.Build` / `Build$VERSION` reflection is applied across all modes. Native Bionic properties follow the selected backend with automatic fallback **`mount_ns` → `cow` → `hook`**:

| Mode | Mechanism | Stealth Characteristics |
| :--- | :--- | :--- |
| `hook` | Bionic `__system_property_*` PLT hooks + read-only `[anon:SystemProperties]` arena | Maximum compatibility for heavily customized ROMs; leaves PLT/GOT and anonymous VMA traces. |
| `cow` | `MAP_PRIVATE \| MAP_FIXED` copy-on-write on matched prop areas, in-place overwrite + native `prop_trie` tail insertion for cross-brand properties, then re-protected to `r--p` | Zero property PLT hooks and no `[anon:SystemProperties]`; exposes `r--p` and small `Private_Dirty` pages in `smaps`. |
| `mount_ns` | Companion clones prop areas to private tmpfs in `preAppSpecialize`, patches/inserts entries, clones original SELinux contexts, read-only bind-mounts inside private mount namespace, and remaps in-process via `PROT_READ \| MAP_SHARED \| MAP_FIXED` | Zero property PLT hooks, clean `r--s` mappings, `Private_Dirty: 0 kB`; resists raw `svc #0` reads and dirty-page checks. |

### 2.2 Two-Stage VFS MountNS (`mountNs`)

* When `mountNs=1`, Companion passes a dedicated Unix `socketpair` via `SCM_RIGHTS` during `preAppSpecialize`. Actual `/proc/*` and `/sys/*` bind-mounts run in `postAppSpecialize`—after `SpecializeCommon` and KernelSU / Zygisk Next unmount passes finish.
* The app performs a fast two-sentinel readability check (`/proc` and `/sys`). On verification, canonical paths and all symlink aliases (e.g., `cpuX/cpufreq/* -> cpufreq/policyY/*`) are registered, **bypassing all 29 legacy file I/O PLT hooks**. A lightweight Companion thread updates `/proc/meminfo` and live core frequencies in-place via fixed-width `pwrite` on pure-RAM tmpfs FDs.

### 2.3 Zero-Residence Self-Unload (`zeroResidence`)

When an app only enables `cow` or `mount_ns` system properties without GPU, refresh-rate / HDR, Binder memory hook, syscall VFS hooks, or Probe, MirageForge invokes `DLCLOSE_MODULE_LIBRARY` immediately after `postAppSpecialize` reflection, leaving zero `.so` footprint in `/proc/self/maps`.

---

## 3. Route Table Format (`routes.db`)

v1.3.0 uses a 16-column `|`-delimited schema (legacy 6-col and 13-col tables are migrated automatically):

```text
pkg|tplId|build|gl|vfs|probe|sdkSpoof|refreshRate|mountNs|cowProp|cpu|mem|vfsNodes|propMode|hdrSpoof|overridesB64
```

* `0–1`: `pkg` / `tplId`
* `2–5`: `build` / `gl` / `vfs` (`cpu || mem || vfsNodes`) / `probe` (`0` or `1`)
* `6–7`: `sdkSpoof` (`0`/`1`) / `refreshRate` (`0` = native, valid range `90`–`240`)
* `8–9`: `mountNs` (`0`/`1`) / `cowProp` (legacy derived flag: `build && propMode == "cow"`)
* `10–12`: `cpu` / `mem` / `vfsNodes` (`0`/`1`)
* `13`: `propMode` (`hook` | `cow` | `mount_ns`)
* `14`: `hdrSpoof` (`0`/`1`, Dolby Vision / HDR10 / HLG / HDR10+ capabilities)
* `15`: `overridesB64` — URL-Safe Base64 JSON for per-app overrides

Example per-app override JSON:

```json
{
  "ramGb": 24,
  "cpuCores": 8,
  "customProps": "ro.product.marketname=Xiaomi 15 Ultra\npersist.vendor.demo=1"
}
```

---

## 4. Compiled App Profile (`apps/<pkg>.conf`)

Flat `KEY=VALUE` file compiled with strict zero-touch gating (disabled features emit zero keys):

* `META:*`: Module switches (`ENABLE_BUILD`, `PROP_MODE`, `ENABLE_GL`, `ENABLE_HDR`, `ENABLE_CPU`, `ENABLE_MEM`, `ENABLE_VFS_NODES`, `ENABLE_MOUNT_NS`, `ENABLE_PROBE`, `FORCE_SPOOF_SDK`, `REFRESH_RATE`).
* `GLES:*` / `VULKAN:*`: Decoupled OpenGL ES and Vulkan physical device / driver properties.
* `VFS:*`: CPU topology (`CPU_CLUSTERS` format: `first-last:minKhz:maxKhz;...`), RAM/DDR specs, SoC IDs, and kernel strings.
* `BUILD:*` / `VERSION:*`: Static Java fields reflected into `android.os.Build` and `android.os.Build$VERSION`.
* Unprefixed keys: System properties (`ro.*`, `sys.*`, `persist.*`); DPI keys are stripped and SDK/Release keys are filtered unless `FORCE_SPOOF_SDK=1`.

---

## 5. Auto-Spoof Policy (`auto_spoof.json`)

Launched by `service.sh`, `mirageforge_cli watch-installs` blocks on Linux `inotify` over `/data/system/packages.list` (0% idle CPU) and hot-reads `auto_spoof.json`:

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

* Matches multi-user app UIDs where `uid % 100000` is in `10000..=19999`.
* Captures a baseline snapshot (`known_pkgs.db`) on first enable so existing apps are untouched.
* Built-in safety exclusions always skip KernelSU, KSU Next, APatch, Magisk, MMRL, KSUWebUI, LSPosed, Termux, and Shell.

---

## 6. `mirageforge_cli` Command Reference

```bash
# Status, host hardware info & host cloning
su -c "mirageforge_cli status [--json]"
su -c "mirageforge_cli device-info --json"
su -c "mirageforge_cli clone-device --id my_host --save"

# Templates, build.prop parsing & mf:// share codes
su -c "mirageforge_cli parse-prop /sdcard/Download/build.prop --save"
su -c "mirageforge_cli template export-share xiaomi_15_pro"
su -c "mirageforge_cli template import-share 'mf://...' --save"
su -c "mirageforge_cli template delete <tpl_id>"

# Per-app route configuration
su -c "mirageforge_cli route set flar2.devcheck xiaomi_15_pro \
  --build 1 --prop-mode mount_ns --gl 1 --hdr 1 \
  --cpu 1 --mem 1 --vfs-nodes 1 --mount-ns 1 \
  --fps 120 --ram-override 24 --cores-override 8 --restart"
su -c "mirageforge_cli route remove flar2.devcheck --restart"

# Live runtime status & auto-spoof daemon
su -c "mirageforge_cli runtime-status"
su -c "mirageforge_cli runtime-status clear"
su -c "mirageforge_cli auto-spoof status"
su -c "mirageforge_cli watch-installs --once"

# Probe radar, crash logs & diagnostic bundle
su -c "mirageforge_cli probe list --pkg flar2.devcheck --unmapped --limit 30"
su -c "mirageforge_cli probe crashes --limit 10"
su -c "mirageforge_cli probe bundle /sdcard/Download/mirage_diag.txt"
```
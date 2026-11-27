// SPDX-License-Identifier: GPL-3.0-only
// Copyright (c) 2025-2026 jjh138792
// MirageForge: https://github.com/jjh138792/MirageForge

use crate::fs_atomic::{ensure_dir, is_safe_token, read_file_str, write_file_atomic};
use crate::models::{DATA_ROOT_DIR, KNOWN_PKGS_DB, WATCHER_PID_FILE};
use crate::store::StoreEngine;
use std::collections::BTreeSet;
use std::ffi::CString;
use std::fs;
use std::io;
use std::os::raw::{c_char, c_int, c_void};
use std::process;
use std::thread;
use std::time::{Duration, SystemTime};

const PACKAGES_DIR: &str = "/data/system";
const PACKAGES_LIST_PATH: &str = "/data/system/packages.list";

// Linux / Android 全架构一致的 inotify 事件位
const IN_ATTRIB: u32 = 0x0000_0004;
const IN_CLOSE_WRITE: u32 = 0x0000_0008;
const IN_MOVED_TO: u32 = 0x0000_0080;
const IN_CREATE: u32 = 0x0000_0100;

#[repr(C)]
struct InotifyEvent {
    wd: c_int,
    mask: u32,
    cookie: u32,
    len: u32,
}

extern "C" {
    fn inotify_init1(flags: c_int) -> c_int;
    fn inotify_add_watch(fd: c_int, pathname: *const c_char, mask: u32) -> c_int;
    fn read(fd: c_int, buf: *mut c_void, count: usize) -> isize;
    fn close(fd: c_int) -> c_int;
}

// 内置安全排除名单：防止误将 Root 管理器、WebUI 宿主或终端工具加入伪装
const BUILTIN_EXACT_EXCLUDES: &[&str] = &[
    "me.weishu.kernelsu",
    "com.rifsxd.ksunext",
    "me.bmax.apatch",
    "com.topjohnwu.magisk",
    "io.github.vvb2060.magisk",
    "io.github.huskydg.magisk",
    "com.dergoogler.mmrl",
    "com.dergoogler.mmrl.wx",
    "io.github.a13e300.ksuwebui",
    "org.lsposed.manager",
    "com.termux",
    "com.android.shell",
];

const BUILTIN_KEYWORD_EXCLUDES: &[&str] = &[
    "kernelsu",
    "ksunext",
    "apatch",
    "magisk",
    "ksuwebui",
    "lsposed",
    "mirageforge",
];

pub fn get_running_watcher_pid() -> Option<u32> {
    let raw = read_file_str(WATCHER_PID_FILE);
    let pid = raw.trim().parse::<u32>().ok()?;
    if pid == 0 || pid == process::id() {
        return None;
    }
    let cmdline = fs::read(format!("/proc/{}/cmdline", pid)).ok()?;
    let cmd_str = String::from_utf8_lossy(&cmdline);
    if cmd_str.contains("mirageforge_cli") && cmd_str.contains("watch-installs") {
        Some(pid)
    } else {
        None
    }
}

pub fn parse_installed_user_packages() -> BTreeSet<String> {
    let mut pkgs = BTreeSet::new();
    let raw = read_file_str(PACKAGES_LIST_PATH);
    for line in raw.lines() {
        let mut parts = line.split_whitespace();
        let pkg = match parts.next() {
            Some(p) if is_safe_token(p) => p,
            _ => continue,
        };
        let uid = match parts.next().and_then(|u| u.parse::<u32>().ok()) {
            Some(u) => u,
            None => continue,
        };
        // 兼容 Android 多用户：uid = user_id * 100000 + app_id
        let app_id = uid % 100_000;
        if (10_000..=19_999).contains(&app_id) {
            pkgs.insert(pkg.to_string());
        }
    }
    pkgs
}

fn load_known_packages() -> BTreeSet<String> {
    let mut known = BTreeSet::new();
    let raw = read_file_str(KNOWN_PKGS_DB);
    for line in raw.lines() {
        let tr = line.trim();
        if !tr.is_empty() && !tr.starts_with('#') && is_safe_token(tr) {
            known.insert(tr.to_string());
        }
    }
    known
}

fn save_known_packages(pkgs: &BTreeSet<String>) {
    ensure_dir(DATA_ROOT_DIR, 0o700);
    let mut out = String::with_capacity(pkgs.len() * 28 + 48);
    out.push_str("# MirageForge Known User Packages Snapshot\n");
    for p in pkgs {
        out.push_str(p);
        out.push('\n');
    }
    write_file_atomic(KNOWN_PKGS_DB, &out, 0o600);
}

fn is_excluded_package(pkg: &str, user_excludes: &[String]) -> bool {
    let lower = pkg.to_ascii_lowercase();
    if BUILTIN_EXACT_EXCLUDES.iter().any(|&ex| ex == lower) {
        return true;
    }
    if BUILTIN_KEYWORD_EXCLUDES.iter().any(|&kw| lower.contains(kw)) {
        return true;
    }
    for rule in user_excludes {
        let r = rule.trim().to_ascii_lowercase();
        if r.is_empty() {
            continue;
        }
        if let Some(prefix) = r.strip_suffix('*') {
            if !prefix.is_empty() && lower.starts_with(prefix) {
                return true;
            }
        } else if lower == r {
            return true;
        }
    }
    false
}

/// 执行单次包名快照比对：
/// - init_only = true 或尚未建立基线时：仅写入当前存量应用快照，不触发自动伪装
/// - 检测到新安装且未排除的应用时：热读取 auto_spoof.json 与 StoreEngine，原子写入并编译 .conf
pub fn scan_once(init_only: bool) -> usize {
    let mut current = parse_installed_user_packages();
    if current.is_empty() {
        thread::sleep(Duration::from_millis(300));
        current = parse_installed_user_packages();
        if current.is_empty() {
            return 0;
        }
    }

    let known = load_known_packages();
    if init_only || known.is_empty() {
        save_known_packages(&current);
        return 0;
    }

    let new_pkgs: Vec<String> = current.difference(&known).cloned().collect();
    if current != known {
        save_known_packages(&current);
    }
    if new_pkgs.is_empty() {
        return 0;
    }

    let cfg = StoreEngine::load_auto_spoof_file();
    if !cfg.enabled || cfg.tpl_id.is_empty() {
        return 0;
    }

    let mut store = StoreEngine::new();
    store.init_storage(false);
    if !store.templates.contains_key(&cfg.tpl_id) {
        return 0;
    }

    let mut added = 0usize;
    for pkg in new_pkgs {
        if is_excluded_package(&pkg, &cfg.exclude_packages) {
            continue;
        }
        if store.routes.contains_key(&pkg) {
            continue;
        }
        let route = cfg.build_route_for(&pkg);
        if route.is_any_enabled() {
            store.routes.insert(pkg, route);
            added += 1;
        }
    }

    if added > 0 {
        store.save_all_and_compile();
    }
    added
}

fn read_packages_mtime() -> Option<SystemTime> {
    fs::metadata(PACKAGES_LIST_PATH).ok()?.modified().ok()
}

fn run_fallback_mtime_loop() {
    let mut last_mtime = read_packages_mtime();
    loop {
        thread::sleep(Duration::from_secs(12));
        let cur = read_packages_mtime();
        if cur != last_mtime {
            last_mtime = cur;
            thread::sleep(Duration::from_millis(500));
            let _ = scan_once(false);
        }
    }
}

pub fn run_watch_daemon() {
    if get_running_watcher_pid().is_some() {
        return;
    }

    ensure_dir(DATA_ROOT_DIR, 0o700);
    write_file_atomic(WATCHER_PID_FILE, &format!("{}\n", process::id()), 0o600);

    // 启动先确保基线快照已就绪，并补扫一次开机前可能已变化的状态
    if load_known_packages().is_empty() {
        let _ = scan_once(true);
    } else {
        let _ = scan_once(false);
    }

    let fd = unsafe { inotify_init1(0) };
    if fd < 0 {
        run_fallback_mtime_loop();
        return;
    }

    let c_dir = match CString::new(PACKAGES_DIR) {
        Ok(c) => c,
        Err(_) => {
            unsafe { close(fd) };
            run_fallback_mtime_loop();
            return;
        }
    };

    let mask = IN_MOVED_TO | IN_CLOSE_WRITE | IN_CREATE | IN_ATTRIB;
    let wd = unsafe { inotify_add_watch(fd, c_dir.as_ptr(), mask) };
    if wd < 0 {
        unsafe { close(fd) };
        run_fallback_mtime_loop();
        return;
    }

    let mut buf = [0u8; 4096];
    let hdr_size = std::mem::size_of::<InotifyEvent>();

    loop {
        let n = unsafe { read(fd, buf.as_mut_ptr() as *mut c_void, buf.len()) };
        if n <= 0 {
            let err = io::Error::last_os_error();
            if err.kind() == io::ErrorKind::Interrupted {
                continue;
            }
            thread::sleep(Duration::from_secs(2));
            continue;
        }

        let total = n as usize;
        let mut offset = 0usize;
        let mut packages_touched = false;

        while offset + hdr_size <= total {
            let ev = unsafe {
                std::ptr::read_unaligned(buf.as_ptr().add(offset) as *const InotifyEvent)
            };
            let name_start = offset + hdr_size;
            let name_len = ev.len as usize;
            if name_start + name_len > total {
                break;
            }

            if name_len == 0 {
                packages_touched = true;
            } else {
                let raw_name = &buf[name_start..name_start + name_len];
                let nul_pos = raw_name
                    .iter()
                    .position(|&b| b == 0)
                    .unwrap_or(raw_name.len());
                if let Ok(name_str) = std::str::from_utf8(&raw_name[..nul_pos]) {
                    if name_str == "packages.list" {
                        packages_touched = true;
                    }
                }
            }

            offset = name_start + name_len;
        }

        if packages_touched {
            // 防抖：等待 PackageManagerService 完成原子写入后再扫描
            thread::sleep(Duration::from_millis(600));
            let _ = scan_once(false);
        }
    }
}

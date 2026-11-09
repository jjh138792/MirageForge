use crate::fs_atomic::{is_safe_token, read_file_str, truncate_file, write_file_atomic};
use crate::models::*;
use crate::store::StoreEngine;
use std::collections::HashSet;
use std::fs;
use std::process::Command;

pub struct ProbeDoctor;

impl ProbeDoctor {
    fn rotate_log_if_needed(path: &str) {
        if let Ok(meta) = fs::metadata(path) {
            if meta.len() > 2 * 1024 * 1024 {
                let raw = read_file_str(path);
                if raw.len() > 800 * 1024 {
                    if let Some(pos) = raw[raw.len() - 800 * 1024..].find('\n') {
                        let start = raw.len() - 800 * 1024 + pos + 1;
                        write_file_atomic(path, &raw[start..], 0o600);
                    }
                }
            }
        }
    }

    pub fn query_probe_logs(filter_pkg: &str, unmapped_only: bool, limit: usize) -> Vec<ProbeEvent> {
        Self::rotate_log_if_needed(PROBE_LOG_FILE);
        let raw = read_file_str(PROBE_LOG_FILE);

        let mut events = Vec::new();
        let mut dedup = HashSet::new();
        let filter_lower = filter_pkg.to_ascii_lowercase();

        for line in raw.lines().rev() {
            if let Some(pos) = line.find("[PROBE][") {
                if let Some(ev) = Self::parse_probe_line(&line[pos..]) {
                    if unmapped_only && !ev.unmapped {
                        continue;
                    }
                    if !filter_lower.is_empty() {
                        let matched = ev.pkg.to_ascii_lowercase().contains(&filter_lower)
                            || ev.key.to_ascii_lowercase().contains(&filter_lower)
                            || ev.domain.to_ascii_lowercase().contains(&filter_lower)
                            || ev.stack.to_ascii_lowercase().contains(&filter_lower);
                        if !matched {
                            continue;
                        }
                    }
                    let sig = format!("{}|{}|{}|{}", ev.pkg, ev.domain, ev.key, ev.value);
                    if dedup.insert(sig) {
                        events.push(ev);
                        if events.len() >= limit {
                            break;
                        }
                    }
                }
            }
        }
        events
    }

    pub fn query_crash_logs(limit: usize) -> Vec<CrashEvent> {
        Self::rotate_log_if_needed(CRASH_LOG_FILE);
        let raw = read_file_str(CRASH_LOG_FILE);
        let mut crashes = Vec::new();

        for line in raw.lines().rev() {
            if let Some(pos) = line.find("[CRASH][") {
                if let Some(ce) = Self::parse_crash_line(&line[pos..]) {
                    crashes.push(ce);
                    if crashes.len() >= limit {
                        break;
                    }
                }
            }
        }
        crashes
    }

    pub fn query_telemetry(limit: usize) -> Vec<TelemetryEvent> {
        let raw = read_file_str(LAUNCH_LOG_FILE);
        let mut list = Vec::new();

        for line in raw.lines().rev() {
            let tr = line.trim();
            if tr.is_empty() {
                continue;
            }
            let parts: Vec<&str> = tr.split('|').collect();
            if parts.len() >= 4 {
                list.push(TelemetryEvent {
                    timestamp: parts[0].parse::<i64>().unwrap_or(0),
                    pkg: parts[1].to_string(),
                    pid: parts[2].parse::<i32>().unwrap_or(0),
                    model: parts[3].to_string(),
                    gpu: parts.get(4).copied().unwrap_or("Adreno GPU").to_string(),
                    ram: parts.get(5).copied().unwrap_or("16GB").to_string(),
                });
                if list.len() >= limit {
                    break;
                }
            }
        }
        list
    }

    pub fn capture_property(store: &mut StoreEngine, pkg: &str, key: &str, val: &str) -> bool {
        if !is_safe_token(pkg) || key.trim().is_empty() {
            return false;
        }
        let tpl_id = match store.routes.get(pkg) {
            Some(r) => r.tpl_id.clone(),
            None => return false,
        };
        let tpl = match store.templates.get_mut(&tpl_id) {
            Some(t) => t,
            None => return false,
        };

        let prefix = format!("{}=", key.trim());
        let new_entry = format!("{}{}", prefix, val.trim());
        let mut lines: Vec<String> = tpl
            .custom_props
            .lines()
            .map(|s| s.trim().to_string())
            .filter(|s| !s.is_empty())
            .collect();

        let mut updated = false;
        for line in &mut lines {
            if line.starts_with(&prefix) {
                *line = new_entry.clone();
                updated = true;
                break;
            }
        }
        if !updated {
            lines.push(new_entry);
        }
        tpl.custom_props = lines.join("\n");
        store.save_all_and_compile()
    }

    pub fn clear_logs() {
        truncate_file(PROBE_LOG_FILE);
        truncate_file(CRASH_LOG_FILE);
        let _ = Command::new("logcat").arg("-c").status();
    }

    pub fn clear_telemetry() {
        let _ = fs::remove_file(LAUNCH_LOG_FILE);
    }

    fn parse_probe_line(s: &str) -> Option<ProbeEvent> {
        let rest = s.strip_prefix("[PROBE][")?;
        let p1 = rest.find(']')?;
        let pkg = rest[..p1].to_string();

        let rest2 = rest[p1 + 1..].strip_prefix('[')?;
        let p2 = rest2.find(']')?;
        let domain = rest2[..p2].to_string();

        let body = &rest2[p2 + 1..];
        let arrow = body.find("=>")?;
        let key = body[..arrow].trim().to_string();
        let after_arrow = &body[arrow + 2..];

        let (value, stack) = if let Some(at_pos) = after_arrow.find("@@") {
            (
                after_arrow[..at_pos].trim().to_string(),
                after_arrow[at_pos + 2..].trim().to_string(),
            )
        } else {
            (after_arrow.trim().to_string(), String::new())
        };

        let unmapped = domain.contains("UNMAPPED");
        Some(ProbeEvent {
            pkg,
            domain,
            key,
            value,
            stack,
            unmapped,
        })
    }

    fn parse_crash_line(s: &str) -> Option<CrashEvent> {
        let rest = s.strip_prefix("[CRASH][")?;
        let p1 = rest.find(']')?;
        let pkg = rest[..p1].to_string();

        let rest2 = rest[p1 + 1..].strip_prefix('[')?;
        let p2 = rest2.find(']')?;
        let signal = rest2[..p2].to_string();

        let body = rest2[p2 + 1..].trim();
        let (detail, stack) = if let Some(pipe_pos) = body.find("| Stack:") {
            (
                body[..pipe_pos].trim().to_string(),
                body[pipe_pos + 8..].trim().to_string(),
            )
        } else {
            (body.to_string(), String::new())
        };

        Some(CrashEvent {
            pkg,
            signal,
            detail,
            stack,
        })
    }
}

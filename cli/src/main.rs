mod alchemy;
mod compiler;
mod fs_atomic;
mod models;
mod probe;
mod store;

use alchemy::parse_build_prop;
use fs_atomic::{is_safe_token, read_file_str, write_file_atomic};
use models::*;
use probe::ProbeDoctor;
use std::env;
use std::io::{self, Read};
use std::process;
use store::StoreEngine;

fn has_flag(args: &[String], flag: &str) -> bool {
    args.iter().any(|a| a == flag)
}

fn get_opt<'a>(args: &'a [String], opt: &str, def: &'a str) -> &'a str {
    for i in 0..args.len().saturating_sub(1) {
        if args[i] == opt {
            return &args[i + 1];
        }
    }
    def
}

fn read_stdin_all() -> String {
    let mut buf = String::new();
    let _ = io::stdin().read_to_string(&mut buf);
    buf
}

fn main() {
    let args: Vec<String> = env::args().collect();
    let meta = read_module_meta();

    if args.len() < 2 {
        println!(
            "蜃楼工坊 · MirageForge Rust CLI Engine v{} (code {}) [from module.prop]",
            meta.version, meta.version_code
        );
        return;
    }

    let cmd = args[1].as_str();
    let json_mode = has_flag(&args, "--json");

    if cmd == "zygisk-fetch" {
        if args.len() < 3 || !is_safe_token(&args[2]) {
            process::exit(1);
        }
        let conf_path = format!("{}/{}.conf", APPS_DIR, args[2]);
        let content = read_file_str(&conf_path);
        if content.is_empty() {
            process::exit(1);
        }
        print!("{}", content);
        return;
    }

    let mut store = StoreEngine::new();

    if cmd == "init" {
        store.init_storage(true);
        if json_mode {
            println!(
                "{{\"ok\":true,\"version\":\"{}\",\"versionCode\":{}}}",
                store.meta.version, store.meta.version_code
            );
        } else {
            println!(
                "[MirageForge] Rust CLI v{} initialized ({} templates ready).",
                store.meta.version,
                store.templates.len()
            );
        }
        return;
    }

    store.init_storage(false);

    match cmd {
        "status" => {
            if json_mode {
                println!(
                    "{{\"ok\":true,\"version\":\"{}\",\"versionCode\":{},\"activeApps\":{},\"templateCount\":{}}}",
                    store.meta.version,
                    store.meta.version_code,
                    store.routes.len(),
                    store.templates.len()
                );
            } else {
                println!("==========================================");
                println!("  蜃楼工坊 · MirageForge v{} (Rust Core)", store.meta.version);
                println!("==========================================");
                println!("- 已储备全息模板数: {} 套", store.templates.len());
                println!("- 当前生效分流应用: {} 款", store.routes.len());
                for r in store.routes.values() {
                    let m_name = store
                        .templates
                        .get(&r.tpl_id)
                        .map(|t| t.name.as_str())
                        .unwrap_or(&r.tpl_id);
                    println!(
                        "  * {} -> [{} | Build:{} GL:{} VFS:{} Probe:{} SDK:{} DPI:{}]",
                        r.pkg_name,
                        m_name,
                        r.build as u8,
                        r.gl as u8,
                        r.vfs as u8,
                        r.probe as u8,
                        r.sdk_spoof as u8,
                        r.dpi
                    );
                }
            }
        }
        "state" => {
            println!("{}", store.export_full_json());
        }
        "sync" => {
            store.save_all_and_compile();
            println!("{{\"ok\":true}}");
        }
        "template" => {
            let sub = args.get(2).map(|s| s.as_str()).unwrap_or("list");
            match sub {
                "list" => println!("{}", store.export_full_json()),
                "get" => {
                    if let Some(id) = args.get(3) {
                        if let Some(tpl) = store.templates.get(id) {
                            println!("{}", serde_json::to_string(tpl).unwrap_or_default());
                            return;
                        }
                    }
                    println!("{{\"ok\":false,\"error\":\"not_found\"}}");
                    process::exit(1);
                }
                "set" | "set-stdin" => {
                    let payload = if sub == "set-stdin" {
                        read_stdin_all()
                    } else {
                        get_opt(&args, "--json", "").to_string()
                    };
                    match serde_json::from_str::<DeviceTemplate>(&payload) {
                        Ok(tpl) => {
                            let id = tpl.id.clone();
                            if store.upsert_template(tpl) {
                                println!("{{\"ok\":true,\"id\":\"{}\"}}", id);
                            } else {
                                println!("{{\"ok\":false,\"error\":\"invalid_template\"}}");
                                process::exit(1);
                            }
                        }
                        Err(_) => {
                            println!("{{\"ok\":false,\"error\":\"json_parse_error\"}}");
                            process::exit(1);
                        }
                    }
                }
                "delete" => {
                    let ok = args.get(3).map(|id| store.delete_template(id)).unwrap_or(false);
                    println!("{{\"ok\":{}}}", ok);
                    if !ok {
                        process::exit(1);
                    }
                }
                _ => process::exit(1),
            }
        }
        "parse-prop" => {
            let src = match args.get(2) {
                Some(s) => s.as_str(),
                None => process::exit(1),
            };
            let raw = if src == "-" {
                read_stdin_all()
            } else {
                read_file_str(src)
            };
            let custom_id = get_opt(&args, "--id", "");
            let cid_opt = if custom_id.is_empty() { None } else { Some(custom_id) };

            if let Some(parsed) = parse_build_prop(&raw, cid_opt) {
                if has_flag(&args, "--save") {
                    store.upsert_template(parsed.clone());
                }
                println!("{}", serde_json::to_string(&parsed).unwrap_or_default());
            } else {
                println!("{{\"ok\":false,\"error\":\"no_valid_props\"}}");
                process::exit(1);
            }
        }
        "route" => {
            let sub = args.get(2).map(|s| s.as_str()).unwrap_or("");
            if sub == "set" && args.len() >= 5 {
                let route = AppRoute {
                    pkg_name: args[3].clone(),
                    tpl_id: args[4].clone(),
                    build: get_opt(&args, "--build", "1") == "1",
                    gl: get_opt(&args, "--gl", "1") == "1",
                    vfs: get_opt(&args, "--vfs", "1") == "1",
                    probe: get_opt(&args, "--probe", "0") == "1",
                    sdk_spoof: get_opt(&args, "--sdk", "0") == "1",
                    dpi: get_opt(&args, "--dpi", "0").parse::<i32>().unwrap_or(0),
                };
                let restart = has_flag(&args, "--restart");
                let ok = store.set_route(route, restart);
                println!("{{\"ok\":{}}}", ok);
                if !ok {
                    process::exit(1);
                }
            } else if sub == "remove" && args.len() >= 4 {
                let restart = has_flag(&args, "--restart");
                let ok = store.remove_route(&args[3], restart);
                println!("{{\"ok\":{}}}", ok);
                if !ok {
                    process::exit(1);
                }
            } else {
                process::exit(1);
            }
        }
        "probe" => {
            let sub = args.get(2).map(|s| s.as_str()).unwrap_or("list");
            let limit = get_opt(&args, "--limit", "150").parse::<usize>().unwrap_or(150).max(1);
            match sub {
                "list" => {
                    let filter_pkg = get_opt(&args, "--pkg", "");
                    let unmapped = has_flag(&args, "--unmapped");
                    let events = ProbeDoctor::query_probe_logs(filter_pkg, unmapped, limit);
                    if json_mode {
                        println!("{}", serde_json::to_string(&events).unwrap_or_else(|_| "[]".into()));
                    } else {
                        for e in events {
                            print!("[{}] {} : {} => {}", e.domain, e.pkg, e.key, e.value);
                            if !e.stack.is_empty() {
                                print!("\n    \\_ Stack: {}", e.stack);
                            }
                            println!();
                        }
                    }
                }
                "crashes" => {
                    let crashes = ProbeDoctor::query_crash_logs(limit);
                    if json_mode {
                        println!("{}", serde_json::to_string(&crashes).unwrap_or_else(|_| "[]".into()));
                    } else {
                        for c in crashes {
                            println!(
                                "[CRASH][{}] {} ({})\n    \\_ Stack: {}",
                                c.signal, c.pkg, c.detail, c.stack
                            );
                        }
                    }
                }
                "telemetry" => {
                    let tele = ProbeDoctor::query_telemetry(limit);
                    if json_mode {
                        println!("{}", serde_json::to_string(&tele).unwrap_or_else(|_| "[]".into()));
                    } else {
                        for t in tele {
                            println!(
                                "[PID:{}] {} => {} | {} | {}",
                                t.pid, t.pkg, t.model, t.gpu, t.ram
                            );
                        }
                    }
                }
                "capture" if args.len() >= 6 => {
                    let ok = ProbeDoctor::capture_property(&mut store, &args[3], &args[4], &args[5]);
                    println!("{{\"ok\":{}}}", ok);
                    if !ok {
                        process::exit(1);
                    }
                }
                "clear" => {
                    ProbeDoctor::clear_logs();
                    println!("{{\"ok\":true}}");
                }
                "clear-telemetry" => {
                    ProbeDoctor::clear_telemetry();
                    println!("{{\"ok\":true}}");
                }
                _ => process::exit(1),
            }
        }
        "export" if args.len() >= 3 => {
            let ok = write_file_atomic(&args[2], &store.export_full_json(), 0o644);
            println!("{{\"ok\":{}}}", ok);
            if !ok {
                process::exit(1);
            }
        }
        _ => process::exit(1),
    }
}

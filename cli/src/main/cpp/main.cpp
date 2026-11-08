#include <iostream>
#include "cli_common.hpp"
#include "prop_alchemy.hpp"
#include "store_engine.hpp"
#include "probe_doctor.hpp"

using namespace mirage::cli;

static void print_usage() {
    std::cout
        << "========================================================\n"
        << "  蜃楼工坊 · MirageForge CLI Engine v" << MIRAGE_VERSION << " (code " << MIRAGE_VERSION_CODE << ")\n"
        << "========================================================\n"
        << "Usage: mirageforge_cli <command> [options]\n\n"
        << "Core Commands:\n"
        << "  init                                     Initialize storage & seed default templates\n"
        << "  status [--json]                          Show engine status & active routes\n"
        << "  state [--json]                           Dump full templates + routes state for WebUI\n"
        << "  sync                                     Recompile all runtime .conf files atomically\n\n"
        << "Template Commands:\n"
        << "  template list [--json]                   List all hardware templates\n"
        << "  template get <id> [--json]               Get a single template by ID\n"
        << "  template set --json '<payload>'          Create or update template from JSON\n"
        << "  template set-stdin                       Create or update template reading JSON from stdin\n"
        << "  template delete <id>                     Delete template and unbind apps\n\n"
        << "Prop Alchemy Commands:\n"
        << "  parse-prop <file|-> [--save] [--id <id>] Parse build.prop & infer GPU/SoC/Clusters/DDR\n\n"
        << "App Route Commands:\n"
        << "  route set <pkg> <tpl_id> [--build 0|1] [--gl 0|1] [--vfs 0|1] [--probe 0|1] [--sdk 0|1] [--restart]\n"
        << "  route remove <pkg> [--restart]           Remove spoofing for package\n\n"
        << "Zygisk & Probe Commands:\n"
        << "  zygisk-fetch <pkg>                       Output compiled KV config for Zygisk Companion\n"
        << "  probe list [--pkg <p>] [--unmapped] [--limit <n>] [--json]\n"
        << "  probe crashes [--limit <n>] [--json]     Show native crash signal diagnostics & backtraces\n"
        << "  probe telemetry [--limit <n>] [--json]   Show app injection telemetry history\n"
        << "  probe capture <pkg> <key> <val>          Capture unmapped prop into bound template\n"
        << "  probe clear                              Clear persistent probe & crash log files\n"
        << "  probe clear-telemetry                    Clear launch telemetry history\n";
}

static bool has_flag(int argc, char **argv, const std::string &flag) {
    for (int i = 1; i < argc; ++i) {
        if (flag == argv[i]) return true;
    }
    return false;
}

static std::string get_opt(int argc, char **argv, const std::string &opt, const std::string &def = "") {
    for (int i = 1; i + 1 < argc; ++i) {
        if (opt == argv[i]) return argv[i + 1];
    }
    return def;
}

static std::string read_stdin_all() {
    std::ostringstream ss;
    ss << std::cin.rdbuf();
    return ss.str();
}

int main(int argc, char **argv) {
    if (argc < 2) {
        print_usage();
        return 0;
    }

    std::string cmd = argv[1];
    bool json_mode = has_flag(argc, argv, "--json");

    if (cmd == "zygisk-fetch") {
        if (argc < 3 || !is_safe_token(argv[2])) return 1;
        std::string conf_path = std::string(APPS_DIR) + "/" + argv[2] + ".conf";
        std::string content = read_file_to_string(conf_path);
        if (content.empty()) return 1;
        std::cout << content;
        return 0;
    }

    StoreEngine store;

    if (cmd == "init") {
        store.init_storage(true);
        // 确保 SCM_RIGHTS 持久化日志文件存在且权限正确
        int p_fd = open(PROBE_LOG_FILE, O_WRONLY | O_CREAT | O_APPEND | O_CLOEXEC, 0600);
        if (p_fd >= 0) close(p_fd);
        int c_fd = open(CRASH_LOG_FILE, O_WRONLY | O_CREAT | O_APPEND | O_CLOEXEC, 0600);
        if (c_fd >= 0) close(c_fd);

        if (json_mode) {
            std::cout << "{\"ok\":true,\"version\":\"" << MIRAGE_VERSION << "\"}\n";
        } else {
            std::cout << "[MirageForge] Initialized v" << MIRAGE_VERSION
                      << " storage (" << store.templates().size() << " templates ready).\n";
        }
        return 0;
    }

    store.init_storage(false);

    if (cmd == "status") {
        if (json_mode) {
            std::cout << "{"
                      << "\"ok\":true,"
                      << "\"version\":\"" << MIRAGE_VERSION << "\","
                      << "\"versionCode\":" << MIRAGE_VERSION_CODE << ","
                      << "\"activeApps\":" << store.routes().size() << ","
                      << "\"templateCount\":" << store.templates().size()
                      << "}\n";
        } else {
            std::cout << "==========================================\n"
                      << "  蜃楼工坊 · MirageForge v" << MIRAGE_VERSION << " 状态报告\n"
                      << "==========================================\n"
                      << "- 已储备全息模板数: " << store.templates().size() << " 套\n"
                      << "- 当前生效分流应用: " << store.routes().size() << " 款\n";
            for (const auto &kv : store.routes()) {
                const AppRoute &r = kv.second;
                auto it = store.templates().find(r.tpl_id);
                std::string m_name = (it != store.templates().end()) ? it->second.name : r.tpl_id;
                std::cout << "  * " << r.pkg_name << " -> [" << m_name
                          << " | Build:" << r.enable_build
                          << " GL:" << r.enable_gl
                          << " VFS:" << r.enable_vfs
                          << " Probe:" << r.enable_probe
                          << " SDK:" << r.force_spoof_sdk << "]\n";
            }
        }
        return 0;
    }

    if (cmd == "state") {
        std::cout << store.export_full_json() << "\n";
        return 0;
    }

    if (cmd == "sync") {
        store.save_all_and_compile();
        std::cout << "{\"ok\":true}\n";
        return 0;
    }

    if (cmd == "template") {
        std::string sub = (argc >= 3) ? argv[2] : "list";
        if (sub == "list") {
            std::cout << store.export_full_json() << "\n";
            return 0;
        }
        if (sub == "get" && argc >= 4) {
            auto it = store.templates().find(argv[3]);
            if (it == store.templates().end()) {
                std::cout << "{\"ok\":false,\"error\":\"not_found\"}\n";
                return 1;
            }
            std::cout << template_to_json(it->second) << "\n";
            return 0;
        }
        if (sub == "set" || sub == "set-stdin") {
            std::string payload = (sub == "set-stdin") ? read_stdin_all() : get_opt(argc, argv, "--json");
            auto kv = parse_flat_json_object(payload);
            DeviceTemplate tpl = template_from_kv_map(kv);
            if (!store.upsert_template(tpl)) {
                std::cout << "{\"ok\":false,\"error\":\"invalid_template\"}\n";
                return 1;
            }
            std::cout << "{\"ok\":true,\"id\":\"" << json_escape(tpl.id) << "\"}\n";
            return 0;
        }
        if (sub == "delete" && argc >= 4) {
            bool ok = store.delete_template(argv[3]);
            std::cout << (ok ? "{\"ok\":true}\n" : "{\"ok\":false}\n");
            return ok ? 0 : 1;
        }
    }

    if (cmd == "parse-prop") {
        if (argc < 3) return 1;
        std::string src = argv[2];
        std::string raw_text = (src == "-") ? read_stdin_all() : read_file_to_string(src);
        std::string custom_id = get_opt(argc, argv, "--id");

        DeviceTemplate parsed;
        if (!PropAlchemy::parse_build_prop(raw_text, &parsed, custom_id)) {
            std::cout << "{\"ok\":false,\"error\":\"no_valid_props\"}\n";
            return 1;
        }
        if (has_flag(argc, argv, "--save")) {
            store.upsert_template(parsed);
        }
        std::cout << template_to_json(parsed) << "\n";
        return 0;
    }

    if (cmd == "route") {
        std::string sub = (argc >= 3) ? argv[2] : "";
        if (sub == "set" && argc >= 5) {
            AppRoute r;
            r.pkg_name        = argv[3];
            r.tpl_id          = argv[4];
            r.enable_build    = (get_opt(argc, argv, "--build", "1") == "1");
            r.enable_gl       = (get_opt(argc, argv, "--gl", "1") == "1");
            r.enable_vfs      = (get_opt(argc, argv, "--vfs", "1") == "1");
            r.enable_probe    = (get_opt(argc, argv, "--probe", "0") == "1");
            r.force_spoof_sdk = (get_opt(argc, argv, "--sdk", "0") == "1");
            bool restart      = has_flag(argc, argv, "--restart");

            bool ok = store.set_route(r, restart);
            std::cout << (ok ? "{\"ok\":true}\n" : "{\"ok\":false}\n");
            return ok ? 0 : 1;
        }
        if (sub == "remove" && argc >= 4) {
            bool restart = has_flag(argc, argv, "--restart");
            bool ok = store.remove_route(argv[3], restart);
            std::cout << (ok ? "{\"ok\":true}\n" : "{\"ok\":false}\n");
            return ok ? 0 : 1;
        }
    }

    if (cmd == "probe") {
        std::string sub = (argc >= 3) ? argv[2] : "list";
        size_t limit = static_cast<size_t>(std::atoi(get_opt(argc, argv, "--limit", "150").c_str()));
        if (limit == 0) limit = 150;

        if (sub == "list") {
            std::string filter_pkg = get_opt(argc, argv, "--pkg");
            bool unmapped_only = has_flag(argc, argv, "--unmapped");
            auto events = ProbeDoctor::query_probe_logs(filter_pkg, unmapped_only, limit);
            if (json_mode) {
                std::cout << ProbeDoctor::events_to_json(events) << "\n";
            } else {
                for (const auto &e : events) {
                    std::cout << "[" << e.domain << "] " << e.pkg << " : " << e.key << " => " << e.value;
                    if (!e.stack.empty()) std::cout << "\n    \\_ Stack: " << e.stack;
                    std::cout << "\n";
                }
            }
            return 0;
        }
        if (sub == "crashes") {
            auto crashes = ProbeDoctor::query_crash_logs(limit);
            if (json_mode) {
                std::cout << ProbeDoctor::crashes_to_json(crashes) << "\n";
            } else {
                for (const auto &c : crashes) {
                    std::cout << "[CRASH][" << c.signal << "] " << c.pkg << " (" << c.detail << ")\n"
                              << "    \\_ Stack: " << c.stack << "\n";
                }
            }
            return 0;
        }
        if (sub == "telemetry") {
            auto tele = ProbeDoctor::query_telemetry(limit);
            if (json_mode) {
                std::cout << ProbeDoctor::telemetry_to_json(tele) << "\n";
            } else {
                for (const auto &t : tele) {
                    std::cout << "[PID:" << t.pid << "] " << t.pkg << " => " << t.model
                              << " | " << t.gpu << " | " << t.ram << "\n";
                }
            }
            return 0;
        }
        if (sub == "capture" && argc >= 6) {
            bool ok = ProbeDoctor::capture_property_into_template(store, argv[3], argv[4], argv[5]);
            std::cout << (ok ? "{\"ok\":true}\n" : "{\"ok\":false}\n");
            return ok ? 0 : 1;
        }
        if (sub == "clear") {
            ProbeDoctor::clear_logs();
            std::cout << "{\"ok\":true}\n";
            return 0;
        }
        if (sub == "clear-telemetry") {
            ProbeDoctor::clear_telemetry();
            std::cout << "{\"ok\":true}\n";
            return 0;
        }
    }

    if (cmd == "export" && argc >= 3) {
        bool ok = write_file_atomic(argv[2], store.export_full_json(), 0644);
        std::cout << (ok ? "{\"ok\":true}\n" : "{\"ok\":false}\n");
        return ok ? 0 : 1;
    }

    print_usage();
    return 1;
}

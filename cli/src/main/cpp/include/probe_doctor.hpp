#pragma once

#include <cstdio>
#include "store_engine.hpp"

namespace mirage::cli {

struct ProbeEvent {
    std::string pkg;
    std::string domain;
    std::string key;
    std::string value;
    std::string stack; // v1.0.1 新增: 精确到 .so!symbol+0xoffset 的调用栈回溯链
    bool unmapped = false;
};

struct CrashEvent {
    std::string pkg;
    std::string signal;
    std::string detail;
    std::string stack;
};

struct TelemetryEvent {
    long long timestamp = 0;
    std::string pkg;
    int pid = 0;
    std::string model;
    std::string gpu;
    std::string ram;
};

class ProbeDoctor {
public:
    static std::vector<ProbeEvent> query_probe_logs(const std::string &filter_pkg = "",
                                                    bool unmapped_only = false,
                                                    size_t limit = 150) {
        std::vector<ProbeEvent> events;
        FILE *fp = popen("logcat -d -s MirageForge:I 2>/dev/null", "r");
        if (!fp) return events;

        char buf[1536];
        std::vector<std::string> raw_lines;
        while (std::fgets(buf, sizeof(buf), fp)) {
            const char *tag_pos = std::strstr(buf, "[PROBE][");
            if (tag_pos) {
                raw_lines.emplace_back(tag_pos);
            }
        }
        pclose(fp);

        for (auto it = raw_lines.rbegin(); it != raw_lines.rend(); ++it) {
            ProbeEvent ev;
            if (!parse_probe_line(*it, &ev)) continue;
            if (!filter_pkg.empty() &&
                ev.pkg.find(filter_pkg) == std::string::npos &&
                ev.key.find(filter_pkg) == std::string::npos &&
                ev.stack.find(filter_pkg) == std::string::npos) {
                continue;
            }
            if (unmapped_only && !ev.unmapped) continue;

            events.push_back(ev);
            if (events.size() >= limit) break;
        }
        return events;
    }

    static std::vector<CrashEvent> query_crash_logs(size_t limit = 50) {
        std::vector<CrashEvent> crashes;
        FILE *fp = popen("logcat -d -s MirageForge:E 2>/dev/null", "r");
        if (!fp) return crashes;

        char buf[1536];
        std::vector<std::string> raw_lines;
        while (std::fgets(buf, sizeof(buf), fp)) {
            const char *tag_pos = std::strstr(buf, "[CRASH][");
            if (tag_pos) {
                raw_lines.emplace_back(tag_pos);
            }
        }
        pclose(fp);

        for (auto it = raw_lines.rbegin(); it != raw_lines.rend(); ++it) {
            CrashEvent ce;
            if (!parse_crash_line(*it, &ce)) continue;
            crashes.push_back(ce);
            if (crashes.size() >= limit) break;
        }
        return crashes;
    }

    static std::vector<TelemetryEvent> query_telemetry(size_t limit = 50) {
        std::vector<TelemetryEvent> list;
        std::string raw = read_file_to_string(LAUNCH_LOG_FILE);
        auto lines = split_lines(raw);

        for (auto it = lines.rbegin(); it != lines.rend(); ++it) {
            if (it->empty()) continue;
            auto parts = split_char(*it, '|');
            if (parts.size() >= 4) {
                TelemetryEvent te;
                te.timestamp = std::atoll(parts[0].c_str());
                te.pkg       = parts[1];
                te.pid       = std::atoi(parts[2].c_str());
                te.model     = parts[3];
                te.gpu       = (parts.size() >= 5) ? parts[4] : "Adreno GPU";
                te.ram       = (parts.size() >= 6) ? parts[5] : "16GB";
                list.push_back(te);
                if (list.size() >= limit) break;
            }
        }
        return list;
    }

    static std::string events_to_json(const std::vector<ProbeEvent> &events) {
        std::ostringstream ss;
        ss << "[";
        for (size_t i = 0; i < events.size(); ++i) {
            if (i > 0) ss << ",";
            const auto &e = events[i];
            ss << "{"
               << "\"pkg\":\"" << json_escape(e.pkg) << "\","
               << "\"domain\":\"" << json_escape(e.domain) << "\","
               << "\"key\":\"" << json_escape(e.key) << "\","
               << "\"value\":\"" << json_escape(e.value) << "\","
               << "\"stack\":\"" << json_escape(e.stack) << "\","
               << "\"unmapped\":" << (e.unmapped ? "true" : "false")
               << "}";
        }
        ss << "]";
        return ss.str();
    }

    static std::string crashes_to_json(const std::vector<CrashEvent> &crashes) {
        std::ostringstream ss;
        ss << "[";
        for (size_t i = 0; i < crashes.size(); ++i) {
            if (i > 0) ss << ",";
            const auto &c = crashes[i];
            ss << "{"
               << "\"pkg\":\"" << json_escape(c.pkg) << "\","
               << "\"signal\":\"" << json_escape(c.signal) << "\","
               << "\"detail\":\"" << json_escape(c.detail) << "\","
               << "\"stack\":\"" << json_escape(c.stack) << "\""
               << "}";
        }
        ss << "]";
        return ss.str();
    }

    static std::string telemetry_to_json(const std::vector<TelemetryEvent> &items) {
        std::ostringstream ss;
        ss << "[";
        for (size_t i = 0; i < items.size(); ++i) {
            if (i > 0) ss << ",";
            const auto &t = items[i];
            ss << "{"
               << "\"timestamp\":" << t.timestamp << ","
               << "\"pkg\":\"" << json_escape(t.pkg) << "\","
               << "\"pid\":" << t.pid << ","
               << "\"model\":\"" << json_escape(t.model) << "\","
               << "\"gpu\":\"" << json_escape(t.gpu) << "\","
               << "\"ram\":\"" << json_escape(t.ram) << "\""
               << "}";
        }
        ss << "]";
        return ss.str();
    }

    static bool capture_property_into_template(StoreEngine &store,
                                               const std::string &pkg,
                                               const std::string &prop_key,
                                               const std::string &prop_val) {
        if (!is_safe_token(pkg) || prop_key.empty()) return false;
        auto r_it = store.routes().find(pkg);
        if (r_it == store.routes().end()) return false;

        auto &tpls = store.templates_mut();
        auto t_it = tpls.find(r_it->second.tpl_id);
        if (t_it == tpls.end()) return false;

        DeviceTemplate &tpl = t_it->second;
        std::vector<std::string> lines = split_lines(tpl.custom_props);
        bool updated = false;
        std::string prefix = prop_key + "=";

        for (std::string &line : lines) {
            if (line.rfind(prefix, 0) == 0) {
                line = prefix + prop_val;
                updated = true;
                break;
            }
        }
        if (!updated) {
            lines.push_back(prefix + prop_val);
        }

        std::ostringstream ss;
        for (size_t i = 0; i < lines.size(); ++i) {
            if (lines[i].empty()) continue;
            if (ss.tellp() > 0) ss << "\n";
            ss << lines[i];
        }
        tpl.custom_props = ss.str();
        return store.save_all_and_compile();
    }

    static void clear_logs() {
        std::system("logcat -c >/dev/null 2>&1");
    }

    static void clear_telemetry() {
        unlink(LAUNCH_LOG_FILE);
    }

private:
    // 解析格式: [PROBE][com.pkg][DOMAIN] key => val @@ caller_chain
    static bool parse_probe_line(const std::string &s, ProbeEvent *out) {
        if (s.rfind("[PROBE][", 0) != 0) return false;
        size_t p1 = s.find(']', 8);
        if (p1 == std::string::npos) return false;
        std::string pkg = s.substr(8, p1 - 8);

        if (p1 + 1 >= s.size() || s[p1 + 1] != '[') return false;
        size_t p2 = s.find(']', p1 + 2);
        if (p2 == std::string::npos) return false;
        std::string domain = s.substr(p1 + 2, p2 - (p1 + 2));

        size_t arrow = s.find("=>", p2 + 1);
        if (arrow == std::string::npos) return false;

        std::string key = trim(s.substr(p2 + 1, arrow - (p2 + 1)));
        std::string rest = s.substr(arrow + 2);

        std::string val;
        std::string stack;
        size_t at_pos = rest.find("@@");
        if (at_pos != std::string::npos) {
            val   = trim(rest.substr(0, at_pos));
            stack = trim(rest.substr(at_pos + 2));
        } else {
            val   = trim(rest);
            stack = "";
        }

        out->pkg      = pkg;
        out->domain   = domain;
        out->key      = key;
        out->value    = val;
        out->stack    = stack;
        out->unmapped = (domain.find("UNMAPPED") != std::string::npos);
        return true;
    }

    // 解析格式: [CRASH][com.pkg][SIGSEGV] code=1 fault_addr=0x0 | Stack: libA.so!sym+0x10 <- ...
    static bool parse_crash_line(const std::string &s, CrashEvent *out) {
        if (s.rfind("[CRASH][", 0) != 0) return false;
        size_t p1 = s.find(']', 8);
        if (p1 == std::string::npos) return false;
        std::string pkg = s.substr(8, p1 - 8);

        if (p1 + 1 >= s.size() || s[p1 + 1] != '[') return false;
        size_t p2 = s.find(']', p1 + 2);
        if (p2 == std::string::npos) return false;
        std::string sig = s.substr(p1 + 2, p2 - (p1 + 2));

        std::string rest = trim(s.substr(p2 + 1));
        size_t pipe_pos = rest.find("| Stack:");
        if (pipe_pos != std::string::npos) {
            out->detail = trim(rest.substr(0, pipe_pos));
            out->stack  = trim(rest.substr(pipe_pos + 8));
        } else {
            out->detail = rest;
            out->stack  = "";
        }
        out->pkg    = pkg;
        out->signal = sig;
        return true;
    }
};

} // namespace mirage::cli

#pragma once

#include <cstdio>
#include "store_engine.hpp"

namespace mirage::cli {

struct ProbeEvent {
    std::string pkg;
    std::string domain;
    std::string key;
    std::string value;
    bool unmapped = false;
};

class ProbeDoctor {
public:
    static std::vector<ProbeEvent> query_probe_logs(const std::string &filter_pkg = "",
                                                    bool unmapped_only = false,
                                                    size_t limit = 150) {
        std::vector<ProbeEvent> events;
        FILE *fp = popen("logcat -d -s MirageForge:I 2>/dev/null", "r");
        if (!fp) return events;

        char buf[1024];
        std::vector<std::string> raw_lines;
        while (std::fgets(buf, sizeof(buf), fp)) {
            const char *tag_pos = std::strstr(buf, "[PROBE][");
            if (tag_pos) {
                raw_lines.emplace_back(tag_pos);
            }
        }
        pclose(fp);

        // 逆序遍历（最新日志排在最前）
        for (auto it = raw_lines.rbegin(); it != raw_lines.rend(); ++it) {
            ProbeEvent ev;
            if (!parse_probe_line(*it, &ev)) continue;
            if (!filter_pkg.empty() && ev.pkg.find(filter_pkg) == std::string::npos &&
                ev.key.find(filter_pkg) == std::string::npos) {
                continue;
            }
            if (unmapped_only && !ev.unmapped) continue;

            events.push_back(ev);
            if (events.size() >= limit) break;
        }
        return events;
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
               << "\"unmapped\":" << (e.unmapped ? "true" : "false")
               << "}";
        }
        ss << "]";
        return ss.str();
    }

    // 将探针发现的未映射属性直接追加或更新到目标应用绑定的模板中
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

private:
    // 解析格式: [PROBE][com.pkg][DOMAIN] key => val
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
        std::string val = trim(s.substr(arrow + 2));

        out->pkg      = pkg;
        out->domain   = domain;
        out->key      = key;
        out->value    = val;
        out->unmapped = (domain.find("UNMAPPED") != std::string::npos);
        return true;
    }
};

} // namespace mirage::cli

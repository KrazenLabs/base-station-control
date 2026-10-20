#include "panel_module.hpp"

#include <vector>

#include "status_report.hpp"
#include "text.hpp"

namespace bsc {

namespace {

Json::Value line(const std::string& text, const std::string& tone) {
    Json::Value entry(Json::objectValue);
    entry["text"] = text;
    entry["tone"] = tone;
    return entry;
}

std::string stateLine(const Json::Value& status, std::string* tone) {
    const std::string adapter = textMember(status, "adapter");
    const long long listed = integerMember(status, "listed").value_or(0);
    const long long on = integerMember(status, "on").value_or(0);
    *tone = "";
    if (adapter == "off") {
        *tone = "warn";
        return "Bluetooth is disabled";
    }
    if (adapter == "missing") {
        *tone = "error";
        return "No Bluetooth adapter";
    }
    if (adapter != "on") {
        *tone = "warn";
        return "Bluetooth is still starting";
    }
    if (listed == 0) {
        return "No Base Stations found yet";
    }
    if (on == 0) {
        return listed == 1 ? "Base Station disabled" : "All Base Stations disabled";
    }
    *tone = "ok";
    if (on >= listed) {
        return listed == 1 ? "Base Station enabled" : "All Base Stations enabled";
    }
    return std::to_string(on) + " of " + std::to_string(listed) + " Base Stations enabled";
}

}

Json::Value panelStatus(const std::optional<Json::Value>& status, double wallNow) {
    Json::Value lines(Json::arrayValue);
    if (!status || !status->isObject() || !statusIsFresh(*status, wallNow)) {
        lines.append(line("The service is not running", "error"));
    } else {
        std::string tone;
        const std::string state = stateLine(*status, &tone);
        lines.append(line(state, tone));
        bool busy = false;
        for (const Json::Value& station : member(*status, "stations")) {
            busy = busy || (boolMember(station, "busy") && !boolMember(station, "ignored"));
        }
        if (busy) {
            lines.append(line("Switching…", "warn"));
        }
        const std::string automatic = textMember(*status, "automation_text");
        if (!automatic.empty() && automatic != "nothing") {
            lines.append(line("Auto-rule: " + automatic, ""));
        }
    }
    Json::Value root(Json::objectValue);
    root["lines"] = lines;
    return root;
}

std::string panelRunSummary(const RunReport& report) {
    if (!report.reason.empty()) {
        return capitalizeFirst("stopped early: " + report.reason);
    }
    for (const RunStation& station : report.stations) {
        if (station.result == StationResult::Failed) {
            return station.title + ": " + station.message;
        }
    }
    size_t heard = 0;
    for (const RunStation& station : report.stations) {
        if (station.result != StationResult::NotHeard) {
            ++heard;
        }
    }
    const char* done = report.turnsOn() ? "enabled" : "disabled";
    if (heard == 0) {
        return "No Base Station found";
    }
    if (heard == report.stations.size()) {
        return std::string("All Base Stations ") + done;
    }
    return std::string("Base Stations ") + done;
}

std::string panelTimeoutText(double seconds) {
    return "Timed out: no answer from service within " + formatNumber(seconds, 0) + " s";
}

std::string panelInterruptedText() {
    return "Stopped waiting for service";
}

}

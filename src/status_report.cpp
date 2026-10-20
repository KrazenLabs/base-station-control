#include "status_report.hpp"

#include <algorithm>
#include <cmath>

#include "lighthouse.hpp"
#include "text.hpp"

namespace bsc {

namespace {

std::string padRight(const std::string& text, size_t width) {
    return text.size() >= width ? text + " " : text + std::string(width - text.size(), ' ');
}

std::string widgetText(const std::string& name) {
    switch (widgetStateFromName(name).value_or(WidgetState::Waiting)) {
        case WidgetState::Shown: return "shown in Steam's bar";
        case WidgetState::Off: return "turned off in config.json";
        case WidgetState::NoAnchor: return "waiting for Steam's bar";
        case WidgetState::NoPopup: return "Quick Access popup is not reachable";
        default: return "waiting for Steam";
    }
}

Json::Value stationJson(const Station& station, const StationView& view) {
    Json::Value entry(Json::objectValue);
    entry["name"] = station.name();
    entry["title"] = view.title;
    entry["nickname"] = station.record.nickname;
    entry["channel"] = station.record.channel ? Json::Value(*station.record.channel) : Json::Value(Json::nullValue);
    entry["ignored"] = station.record.ignored;
    entry["state"] = powerStateName(view.state);
    entry["presence"] = presenceName(view.presence);
    entry["on"] = view.on;
    entry["busy"] = view.busy;
    entry["caption"] = view.caption;
    entry["error"] = view.error;
    entry["rssi"] = station.rssi ? Json::Value(*station.rssi) : Json::Value(Json::nullValue);
    entry["last_seen"] = station.record.lastSeenWall;
    return entry;
}

std::string stationLine(const Json::Value& station, size_t titleWidth, double wallNow) {
    std::string line = "  " + padRight(textMember(station, "title"), titleWidth) + padRight(textMember(station, "name"), 14);
    std::string state = textMember(station, "caption");
    const size_t separator = state.find(" · ");
    if (separator != std::string::npos) {
        state = state.substr(0, separator);
    }
    line += padRight(state, 22);
    const std::optional<long long> rssi = integerMember(station, "rssi");
    line += padRight(rssi ? std::to_string(*rssi) + " dBm" : "", 9);
    const double lastSeen = numberMember(station, "last_seen").value_or(0.0);
    line += lastSeen > 0.0 ? "heard " + formatAge(wallNow - lastSeen) : "never connected";
    if (boolMember(station, "ignored")) {
        line += "  (ignored)";
    }
    return line;
}

}

bool statusIsFresh(const Json::Value& status, double wallNow) {
    const std::optional<double> updated = numberMember(status, "updated");
    if (!updated) {
        return false;
    }
    const double age = wallNow - *updated;
    return age <= kStatusFreshSeconds && age >= -60.0;
}

Json::Value statusJson(const StationRegistry& registry, const Controller& controller, const ViewInputs& inputs,
                       const ServiceStatus& service, double wallNow) {
    Json::Value root(Json::objectValue);
    root["updated"] = wallNow;
    root["version"] = service.version;
    root["adapter"] = adapterStateName(service.adapter);
    root["scanning"] = service.scanning;
    root["widget"] = widgetStateName(service.widget);
    root["menu_open"] = service.menuOpen;
    root["popup_open"] = service.popupOpen;
    Json::Value stations(Json::arrayValue);
    int on = 0;
    int listed = 0;
    for (const Station* station : registry.all()) {
        const StationView view = describeStation(*station, controller, inputs);
        stations.append(stationJson(*station, view));
        if (!station->record.ignored) {
            ++listed;
            if (view.presence != Presence::Missing && countsAsOn(view.state)) {
                ++on;
            }
        }
    }
    root["stations"] = stations;
    root["on"] = on;
    root["listed"] = listed;
    root["automation"] = optionValuesJson(service.automation, service.apps);
    root["automation_text"] = describeOptions(service.automation, service.apps);
    root["sleep_hook"] = service.sleepHook;
    root["shutdown_hook"] = service.shutdownHook;
    root["automation_run"] = service.automationRun;
    root["apps"] = service.appsText;
    root["state_problem"] = service.stateProblem;
    return root;
}

std::string formatAge(double seconds) {
    seconds = std::max(seconds, 0.0);
    if (seconds < 90.0) {
        return std::to_string(static_cast<long long>(std::lround(seconds))) + " s ago";
    }
    if (seconds < 90.0 * 60.0) {
        return std::to_string(static_cast<long long>(std::lround(seconds / 60.0))) + " min ago";
    }
    if (seconds < 36.0 * 3600.0) {
        return std::to_string(static_cast<long long>(std::lround(seconds / 3600.0))) + " h ago";
    }
    return std::to_string(static_cast<long long>(std::lround(seconds / 86400.0))) + " days ago";
}

std::string formatStatusText(const Json::Value& status, double wallNow) {
    std::string out;
    const bool fresh = statusIsFresh(status, wallNow);
    std::string service = "running";
    if (!fresh) {
        const double age = wallNow - numberMember(status, "updated").value_or(0.0);
        service = "not responding (status is " + formatNumber(age, 0) + " s old)";
    } else {
        service += boolMember(status, "scanning") ? ", scanning" : ", not scanning";
        if (boolMember(status, "popup_open")) {
            service += " (Quick Access popup open)";
        } else if (boolMember(status, "menu_open")) {
            service += " (Steam menu open)";
        }
    }
    out += "service    " + service + "\n";
    out += "bluetooth  " + textMember(status, "adapter") + "\n";
    out += "widget     " + widgetText(textMember(status, "widget")) + "\n";
    if (member(status, "automation_text").isString()) {
        out += "automatic  " + textMember(status, "automation_text") + "\n";
        for (const char* hook : {"sleep", "shutdown"}) {
            const std::string text = textMember(status, (std::string(hook) + "_hook").c_str());
            if (!text.empty() && text != "ready" && text != "not needed") {
                out += std::string("           ") + hook + " hook: " + text + "\n";
            }
        }
        const std::string run = textMember(status, "automation_run");
        if (!run.empty()) {
            out += "           now: " + run + "\n";
        }
    }
    const std::string apps = textMember(status, "apps");
    if (!apps.empty()) {
        out += "apps       " + apps + "\n";
    }
    const std::string stateProblem = textMember(status, "state_problem");
    if (!stateProblem.empty()) {
        out += "could not save data: " + stateProblem + "\n";
    }
    const long long listed = integerMember(status, "listed").value_or(0);
    out += "stations   " +
           (listed == 0 ? std::string("none found yet")
                        : std::to_string(integerMember(status, "on").value_or(0)) + " of " + std::to_string(listed) + " on") +
           "\n";
    size_t titleWidth = 10;
    const Json::Value& stations = member(status, "stations");
    for (const Json::Value& station : stations) {
        titleWidth = std::max(titleWidth, textMember(station, "title").size() + 2);
    }
    for (const Json::Value& station : stations) {
        out += stationLine(station, titleWidth, wallNow) + "\n";
        const std::string error = textMember(station, "error");
        if (!error.empty()) {
            out += "      " + error + "\n";
        }
    }
    return out;
}

}

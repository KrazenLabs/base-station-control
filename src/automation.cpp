#include "automation.hpp"


#include "common.hpp"
#include "text.hpp"

namespace bsc {

namespace {

constexpr const char* kAppsKey = "apps";
constexpr const char* kOnLaunchKey = "on_launch";
constexpr const char* kOffQuitKey = "off_quit";

OptionInfo headsetOption(OptionKind kind, const char* key, const char* word, const char* description, const char* phrase,
                         const char* title, bool turnsOn) {
    OptionInfo info;
    info.id.kind = kind;
    info.key = key;
    info.word = word;
    info.description = description;
    info.phrase = phrase;
    info.title = title;
    info.turnsOn = turnsOn;
    return info;
}

OptionInfo appLaunchOption(const WatchedApp& app) {
    OptionInfo info;
    info.id = appOption(app.id, true);
    info.key = "on_" + app.slug + "_launch";
    info.word = app.slug + "-launch";
    info.description = "enable all on " + app.name + " start";
    info.phrase = "when " + app.name + " starts";
    info.title = "On launching " + app.name;
    info.turnsOn = true;
    return info;
}

OptionInfo appQuitOption(const WatchedApp& app) {
    OptionInfo info;
    info.id = appOption(app.id, false);
    info.key = "off_" + app.slug + "_quit";
    info.word = app.slug + "-quit";
    info.description = "disable all on " + app.name + " quit";
    info.phrase = "when " + app.name + " quits";
    info.title = "On quitting " + app.name;
    info.turnsOn = false;
    return info;
}

std::string listText(const std::vector<std::string>& items) {
    if (items.size() <= 2) {
        return joinWords(items, " and ");
    }
    const std::vector<std::string> head(items.begin(), items.end() - 1);
    return joinWords(head, ", ") + " and " + items.back();
}

std::optional<uint32_t> appIdFromKey(const std::string& key) {
    const std::optional<long long> id = parseInteger(key);
    if (!id || *id <= 0 || *id > 0xFFFFFFFFLL) {
        return std::nullopt;
    }
    return static_cast<uint32_t>(*id);
}

}

bool operator==(const OptionId& a, const OptionId& b) {
    return a.kind == b.kind && a.appId == b.appId;
}

std::vector<OptionInfo> optionList(const AppList& apps) {
    std::vector<OptionInfo> list = {
        headsetOption(OptionKind::OnAtStartup, "on_at_startup", "startup", "turn all on when headset starts", "at startup",
                      "On boot", true),
        headsetOption(OptionKind::OnAfterSleep, "on_after_sleep", "wake", "turn all on when headset wakes from sleep",
                      "after sleep", "On wake from sleep", true),
    };
    for (const WatchedApp& app : apps) {
        list.push_back(appLaunchOption(app));
    }
    list.push_back(headsetOption(OptionKind::OffAtSleep, "off_at_sleep", "sleep", "turn all off when headset goes to sleep",
                                 "at sleep", "On sleep", false));
    list.push_back(headsetOption(OptionKind::OffAtShutdown, "off_at_shutdown", "shutdown",
                                 "turn all off when headset shuts down", "at shutdown", "On shutdown", false));
    for (const WatchedApp& app : apps) {
        list.push_back(appQuitOption(app));
    }
    return list;
}

std::optional<OptionInfo> findOption(const AppList& apps, std::string_view keyOrWord) {
    for (const OptionInfo& info : optionList(apps)) {
        if (equalsIgnoreCase(keyOrWord, info.key) || equalsIgnoreCase(keyOrWord, info.word)) {
            return info;
        }
    }
    return std::nullopt;
}

OptionId appOption(uint32_t appId, bool launch) {
    OptionId id;
    id.kind = launch ? OptionKind::OnAppLaunch : OptionKind::OffAppQuit;
    id.appId = appId;
    return id;
}

bool optionValue(const AutomationOptions& options, const OptionId& id) {
    switch (id.kind) {
        case OptionKind::OnAtStartup: return options.onAtStartup;
        case OptionKind::OnAfterSleep: return options.onAfterSleep;
        case OptionKind::OffAtSleep: return options.offAtSleep;
        case OptionKind::OffAtShutdown: return options.offAtShutdown;
        case OptionKind::OnAppLaunch:
        case OptionKind::OffAppQuit: {
            const auto found = options.apps.find(id.appId);
            if (found == options.apps.end()) {
                return false;
            }
            return id.kind == OptionKind::OnAppLaunch ? found->second.onLaunch : found->second.offQuit;
        }
    }
    return false;
}

void setOptionValue(AutomationOptions& options, const OptionId& id, bool value) {
    switch (id.kind) {
        case OptionKind::OnAtStartup: options.onAtStartup = value; break;
        case OptionKind::OnAfterSleep: options.onAfterSleep = value; break;
        case OptionKind::OffAtSleep: options.offAtSleep = value; break;
        case OptionKind::OffAtShutdown: options.offAtShutdown = value; break;
        case OptionKind::OnAppLaunch: options.apps[id.appId].onLaunch = value; break;
        case OptionKind::OffAppQuit: options.apps[id.appId].offQuit = value; break;
    }
}

std::string describeOptions(const AutomationOptions& options, const AppList& apps) {
    std::vector<std::string> on;
    std::vector<std::string> off;
    for (const OptionInfo& info : optionList(apps)) {
        if (optionValue(options, info.id)) {
            (info.turnsOn ? on : off).push_back(info.phrase);
        }
    }
    std::vector<std::string> parts;
    if (!on.empty()) parts.push_back("all on " + listText(on));
    if (!off.empty()) parts.push_back("all off " + listText(off));
    return parts.empty() ? std::string("nothing") : joinWords(parts, "; ");
}

Json::Value optionsToJson(const AutomationOptions& options) {
    Json::Value automation(Json::objectValue);
    automation["on_at_startup"] = options.onAtStartup;
    automation["on_after_sleep"] = options.onAfterSleep;
    automation["off_at_sleep"] = options.offAtSleep;
    automation["off_at_shutdown"] = options.offAtShutdown;
    Json::Value apps(Json::objectValue);
    for (const auto& entry : options.apps) {
        if (!entry.second.onLaunch && !entry.second.offQuit) {
            continue;
        }
        Json::Value app(Json::objectValue);
        app[kOnLaunchKey] = entry.second.onLaunch;
        app[kOffQuitKey] = entry.second.offQuit;
        apps[std::to_string(entry.first)] = app;
    }
    automation[kAppsKey] = apps;
    return automation;
}

AutomationOptions optionsFromJson(const Json::Value& automation, const AppList& apps) {
    AutomationOptions options;
    options.onAtStartup = boolMember(automation, "on_at_startup");
    options.onAfterSleep = boolMember(automation, "on_after_sleep");
    options.offAtSleep = boolMember(automation, "off_at_sleep");
    options.offAtShutdown = boolMember(automation, "off_at_shutdown");
    for (const WatchedApp& app : apps) {
        if (boolMember(automation, appLaunchOption(app).key.c_str())) {
            options.apps[app.id].onLaunch = true;
        }
        if (boolMember(automation, appQuitOption(app).key.c_str())) {
            options.apps[app.id].offQuit = true;
        }
    }
    const Json::Value& saved = member(automation, kAppsKey);
    if (saved.isObject()) {
        for (const std::string& key : saved.getMemberNames()) {
            const std::optional<uint32_t> id = appIdFromKey(key);
            if (!id) {
                continue;
            }
            AppSwitching& switching = options.apps[*id];
            switching.onLaunch = boolMember(saved[key], kOnLaunchKey);
            switching.offQuit = boolMember(saved[key], kOffQuitKey);
        }
    }
    return options;
}

Json::Value optionValuesJson(const AutomationOptions& options, const AppList& apps) {
    Json::Value values(Json::objectValue);
    for (const OptionInfo& info : optionList(apps)) {
        values[info.key] = optionValue(options, info.id);
    }
    return values;
}

}

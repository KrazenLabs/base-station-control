#include "settings.hpp"

#include <algorithm>
#include <vector>

#include "text.hpp"

namespace bsc {

namespace {


struct NumberSetting {
    const char* key;
    double Settings::*member;
    double minimum;
    double maximum;
};

struct IntegerSetting {
    const char* key;
    int Settings::*member;
    int minimum;
    int maximum;
};

struct BoolSetting {
    const char* key;
    bool Settings::*member;
};

constexpr const char* kOffModeKey = "off_mode";
constexpr const char* kScanPatternKey = "scan_pattern";

const std::vector<NumberSetting>& numberSettings() {
    static const std::vector<NumberSetting> settings = {
        {"scan_linger_seconds", &Settings::scanLingerSeconds, 0.0, 600.0},
        {"scan_refresh_seconds", &Settings::scanRefreshSeconds, 0.0, 60.0},
        {"missing_after_seconds", &Settings::missingAfterSeconds, 3.0, 120.0},
        {"startup_scan_seconds", &Settings::startupScanSeconds, 0.0, 600.0},
        {"background_scan_minutes", &Settings::backgroundScanMinutes, 0.0, 1440.0},
        {"background_scan_seconds", &Settings::backgroundScanSeconds, 3.0, 120.0},
        {"connect_timeout_seconds", &Settings::connectTimeoutSeconds, 5.0, 120.0},
    };
    return settings;
}

const std::vector<IntegerSetting>& integerSettings() {
    static const std::vector<IntegerSetting> settings = {
        {"steam_cef_port", &Settings::steamCefPort, 1, 65535},
        {"command_attempts", &Settings::commandAttempts, 1, 10},
        {"parallel_commands", &Settings::parallelCommands, 1, 8},
    };
    return settings;
}

const std::vector<BoolSetting>& boolSettings() {
    static const std::vector<BoolSetting> settings = {
        {"steam_bar_widget", &Settings::steamBarWidget},
        {"add_new_stations", &Settings::addNewStations},
        {"debug_log", &Settings::debugLog},
    };
    return settings;
}

const char* offModeName(PowerCommand command) {
    return command == PowerCommand::Standby ? "standby" : "sleep";
}

bool isKnownKey(const std::string& key) {
    const auto named = [&key](const auto& setting) { return key == setting.key; };
    return key == kOffModeKey || key == kScanPatternKey ||
           std::any_of(numberSettings().begin(), numberSettings().end(), named) ||
           std::any_of(integerSettings().begin(), integerSettings().end(), named) ||
           std::any_of(boolSettings().begin(), boolSettings().end(), named);
}

}

Json::Value settingsJson(const Settings& settings) {
    Json::Value root(Json::objectValue);
    for (const BoolSetting& setting : boolSettings()) {
        root[setting.key] = settings.*setting.member;
    }
    for (const IntegerSetting& setting : integerSettings()) {
        root[setting.key] = settings.*setting.member;
    }
    for (const NumberSetting& setting : numberSettings()) {
        root[setting.key] = settings.*setting.member;
    }
    root[kOffModeKey] = offModeName(settings.offCommand);
    root[kScanPatternKey] = settings.scanPattern;
    return root;
}

Settings parseSettings(const Json::Value& root, const Logger& warn) {
    Settings settings;
    if (!root.isObject()) {
        warn("config.json is invalid, using default settings");
        return settings;
    }
    for (const std::string& key : root.getMemberNames()) {
        if (!isKnownKey(key)) {
            warn("ignoring unknown setting: " + key);
        }
    }
    for (const NumberSetting& setting : numberSettings()) {
        if (!root.isMember(setting.key)) {
            continue;
        }
        const std::optional<double> value = numberMember(root, setting.key);
        if (!value || *value < setting.minimum || *value > setting.maximum) {
            warn(std::string(setting.key) + " must be a number from " + formatNumber(setting.minimum, 0) + " to " +
                 formatNumber(setting.maximum, 0) + "; keeping the default");
            continue;
        }
        settings.*setting.member = *value;
    }
    for (const IntegerSetting& setting : integerSettings()) {
        if (!root.isMember(setting.key)) {
            continue;
        }
        const std::optional<long long> value = integerMember(root, setting.key);
        if (!value || *value < setting.minimum || *value > setting.maximum) {
            warn(std::string(setting.key) + " must be a whole number from " + std::to_string(setting.minimum) + " to " +
                 std::to_string(setting.maximum));
            continue;
        }
        settings.*setting.member = static_cast<int>(*value);
    }
    for (const BoolSetting& setting : boolSettings()) {
        if (!root.isMember(setting.key)) {
            continue;
        }
        if (!root[setting.key].isBool()) {
            warn(std::string(setting.key) + " must be true or false; keeping the default");
            continue;
        }
        settings.*setting.member = root[setting.key].asBool();
    }
    if (root.isMember(kOffModeKey)) {
        const std::string mode = textMember(root, kOffModeKey);
        if (mode == "sleep" || mode == "standby") {
            settings.offCommand = mode == "standby" ? PowerCommand::Standby : PowerCommand::Sleep;
        } else {
            warn("off_mode must be one of \"sleep\", \"standby\"; keeping the default");
        }
    }
    if (root.isMember(kScanPatternKey)) {
        if (root[kScanPatternKey].isString()) {
            settings.scanPattern = root[kScanPatternKey].asString();
        } else {
            warn("scan_pattern must be a string; keeping the default");
        }
    }
    return settings;
}

Settings loadSettings(const std::filesystem::path& path, const Logger& log) {
    const JsonFile file = readJsonFile(path);
    switch (file.status) {
        case JsonFileStatus::Ok:
            return parseSettings(file.value, log);
        case JsonFileStatus::Missing: {
            std::string error;
            if (writeJsonFile(path, settingsJson(Settings{}), &error)) {
                log("config.json was missing, created default config");
            } else {
                log("config.json is missing and could not be written (" + error + "); using default settings");
            }
            return Settings{};
        }
        case JsonFileStatus::Unreadable:
            log("failed to read config (" + file.error + "); using default settings");
            return Settings{};
        case JsonFileStatus::Invalid:
            log("config.json is invalid, using default settings");
            return Settings{};
    }
    return Settings{};
}

ControllerSettings controllerSettingsFrom(const Settings& settings) {
    ControllerSettings controller;
    controller.offCommand = settings.offCommand;
    controller.attempts = settings.commandAttempts;
    controller.maxParallel = settings.parallelCommands;
    return controller;
}

BlueZSettings bluezSettingsFrom(const Settings& settings) {
    BlueZSettings bluez;
    bluez.scanPattern = settings.scanPattern;
    bluez.scanRefreshSeconds = settings.scanRefreshSeconds;
    bluez.connectTimeoutSeconds = settings.connectTimeoutSeconds;
    return bluez;
}

}

#pragma once

#include <filesystem>
#include <string>

#include <json/json.h>

#include "bluez.hpp"
#include "common.hpp"
#include "controller.hpp"
#include "lighthouse.hpp"

namespace bsc {

struct Settings {
    bool steamBarWidget = true;
    int steamCefPort = 8080;
    PowerCommand offCommand = PowerCommand::Sleep;
    bool addNewStations = true;
    std::string scanPattern = kLighthouseNamePrefix;
    double scanLingerSeconds = 10.0;
    double scanRefreshSeconds = 4.0;
    double missingAfterSeconds = 12.0;
    double startupScanSeconds = 15.0;
    double backgroundScanMinutes = 0.0;
    double backgroundScanSeconds = 8.0;
    int commandAttempts = 3;
    int parallelCommands = 4;
    double connectTimeoutSeconds = 20.0;
    bool debugLog = false;
};

Json::Value settingsJson(const Settings& settings);
Settings parseSettings(const Json::Value& root, const Logger& warn);
Settings loadSettings(const std::filesystem::path& path, const Logger& log);
ControllerSettings controllerSettingsFrom(const Settings& settings);
BlueZSettings bluezSettingsFrom(const Settings& settings);

}

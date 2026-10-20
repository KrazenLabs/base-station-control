#pragma once

#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include <json/json.h>

#include "watched_apps.hpp"

namespace bsc {

enum class OptionKind { OnAtStartup, OnAfterSleep, OffAtSleep, OffAtShutdown, OnAppLaunch, OffAppQuit };

struct OptionId {
    OptionKind kind = OptionKind::OnAtStartup;
    uint32_t appId = 0;
};

bool operator==(const OptionId& a, const OptionId& b);

struct AppSwitching {
    bool onLaunch = false;
    bool offQuit = false;
};

struct AutomationOptions {
    bool onAtStartup = false;
    bool onAfterSleep = false;
    bool offAtSleep = false;
    bool offAtShutdown = false;
    std::map<uint32_t, AppSwitching> apps;
};

struct OptionInfo {
    OptionId id;
    std::string key;
    std::string word;
    std::string description;
    std::string phrase;
    std::string title;
    bool turnsOn = false;
};

std::vector<OptionInfo> optionList(const AppList& apps);
std::optional<OptionInfo> findOption(const AppList& apps, std::string_view keyOrWord);
OptionId appOption(uint32_t appId, bool launch);
bool optionValue(const AutomationOptions& options, const OptionId& id);
void setOptionValue(AutomationOptions& options, const OptionId& id, bool value);
std::string describeOptions(const AutomationOptions& options, const AppList& apps);
Json::Value optionsToJson(const AutomationOptions& options);
AutomationOptions optionsFromJson(const Json::Value& automation, const AppList& apps);
Json::Value optionValuesJson(const AutomationOptions& options, const AppList& apps);

}

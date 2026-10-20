#pragma once

#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "app_watch.hpp"

namespace bsc {

inline constexpr const char* kNotifyBinding = "__bscNotify";

enum class WidgetState { Off, Waiting, Unreachable, NoPage, Lost, ScriptError, BadReport, NoAnchor, NoPopup, Shown };

const char* widgetStateName(WidgetState state);
std::optional<WidgetState> widgetStateFromName(std::string_view name);

enum class ActionKind { Set, AllOn, AllOff, Rename, Option, BluetoothOn, Unknown };

const char* actionKindName(ActionKind kind);
ActionKind actionKindFrom(std::string_view name);

struct WidgetAction {
    ActionKind kind = ActionKind::Unknown;
    std::string station;
    bool on = false;
    std::string key;
    std::string text;
    std::string session;
    int seq = 0;
};

struct WidgetReport {
    bool valid = false;
    bool installNeeded = false;
    bool anchored = false;
    bool visible = false;
    bool popupOpen = false;
    bool popupAttached = false;
    std::vector<WidgetAction> actions;
    bool appsReported = false;
    std::vector<AppReport> apps;
};

std::string widgetVersion(const std::string& script);
std::string installExpression(const std::string& script, const std::string& version, const std::string& dataJson);
std::string updateExpression(const std::string& version, const std::string& dataJson);
std::string removeExpression();
WidgetReport parseWidgetReport(const std::string& value);

}

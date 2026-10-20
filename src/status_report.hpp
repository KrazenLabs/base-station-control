#pragma once

#include <string>

#include <json/json.h>

#include "adapter_state.hpp"
#include "automation.hpp"
#include "controller.hpp"
#include "stations.hpp"
#include "widget_bridge.hpp"
#include "widget_model.hpp"

namespace bsc {

inline constexpr double kStatusFreshSeconds = 10.0;

struct ServiceStatus {
    std::string version;
    AdapterState adapter = AdapterState::Unknown;
    bool scanning = false;
    WidgetState widget = WidgetState::Off;
    bool menuOpen = false;
    bool popupOpen = false;
    AutomationOptions automation;
    AppList apps;
    std::string sleepHook;
    std::string shutdownHook;
    std::string automationRun;
    std::string appsText;
    std::string stateProblem;
};

bool statusIsFresh(const Json::Value& status, double wallNow);
Json::Value statusJson(const StationRegistry& registry, const Controller& controller, const ViewInputs& inputs,
                       const ServiceStatus& service, double wallNow);
std::string formatAge(double seconds);
std::string formatStatusText(const Json::Value& status, double wallNow);

}

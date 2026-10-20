#pragma once

#include <optional>
#include <string>

#include <json/json.h>

#include "switch_run.hpp"

namespace bsc {

Json::Value panelStatus(const std::optional<Json::Value>& status, double wallNow);
std::string panelRunSummary(const RunReport& report);
std::string panelTimeoutText(double seconds);
std::string panelInterruptedText();

}

#pragma once

#include <optional>
#include <string>
#include <vector>

#include "automation.hpp"
#include "controller.hpp"
#include "stations.hpp"
#include "watched_apps.hpp"

namespace bsc {

enum class AnswerStyle { StationLines, PanelSummary };

struct GroupSwitch {
    bool on = true;
    AnswerStyle style = AnswerStyle::StationLines;
};

struct CommandResult {
    bool ok = true;
    std::string message;
    std::vector<std::string> waitFor;
    std::optional<GroupSwitch> groupSwitch;
    double scanSeconds = 0.0;
    bool powerOnBluetooth = false;
    bool stateChanged = false;
    bool optionsChanged = false;
};

struct CommandContext {
    StationRegistry& registry;
    Controller& controller;
    AutomationOptions& options;
    const AppList& apps;
};

std::optional<std::vector<std::string>> resolveStations(const StationRegistry& registry,
                                                        const std::vector<std::string>& queries, std::string* error);
CommandResult renameStation(StationRegistry& registry, const std::string& name, const std::string& nickname);
CommandResult executeCommand(CommandContext context, const std::vector<std::string>& words, int tag);
std::string describeOutcome(const StationRegistry& registry, const Outcome& outcome);
bool isServiceCommand(const std::string& word);
bool isLongCommand(const std::string& word);

}

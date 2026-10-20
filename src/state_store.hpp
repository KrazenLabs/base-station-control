#pragma once

#include <filesystem>
#include <optional>
#include <string>
#include <vector>

#include <json/json.h>

#include "automation.hpp"
#include "common.hpp"
#include "stations.hpp"
#include "watched_apps.hpp"

namespace bsc {

struct SavedState {
    std::vector<StationRecord> stations;
    AutomationOptions automation;
    std::string bootId;
};

enum class StateLoad { Fresh, Loaded, Recovered, Unreadable };

struct LoadedState {
    SavedState state;
    StateLoad how = StateLoad::Fresh;
    std::string problem;

    bool writable() const { return how != StateLoad::Unreadable; }
};

std::string formatState(const SavedState& state);
std::optional<SavedState> parseState(const std::string& text, const AppList& apps, const Logger& warn);
LoadedState loadState(const std::filesystem::path& path, const AppList& apps, const Logger& log);
bool saveState(const std::filesystem::path& path, const SavedState& state, const Logger& log);

}

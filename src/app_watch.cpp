#include "app_watch.hpp"

namespace bsc {

AppWatcher::AppWatcher(double quitDelaySeconds) : quitDelaySeconds_(quitDelaySeconds) {}

std::vector<AppEvent> AppWatcher::update(const std::vector<AppReport>& reports, double now) {
    std::vector<AppEvent> events;
    for (const AppReport& report : reports) {
        State& state = states_[report.id];
        state.installed = report.known && report.installed;
        if (!report.known) {
            continue;
        }
        if (!state.seen) {
            state.seen = true;
            state.running = report.running;
            state.stoppedSince = -1.0;
            continue;
        }
        if (report.running) {
            state.stoppedSince = -1.0;
            if (!state.running) {
                state.running = true;
                events.push_back({report.id, true});
            }
        } else if (state.running && state.stoppedSince < 0.0) {
            state.stoppedSince = now;
        }
    }
    for (const AppEvent& event : tick(now)) {
        events.push_back(event);
    }
    return events;
}

std::vector<AppEvent> AppWatcher::tick(double now) {
    std::vector<AppEvent> events;
    for (auto& entry : states_) {
        State& state = entry.second;
        if (state.running && state.stoppedSince >= 0.0 && now - state.stoppedSince >= quitDelaySeconds_) {
            state.running = false;
            state.stoppedSince = -1.0;
            events.push_back({entry.first, false});
        }
    }
    return events;
}

bool AppWatcher::installed(uint32_t id) const {
    const auto found = states_.find(id);
    return found != states_.end() && found->second.installed;
}

bool AppWatcher::running(uint32_t id) const {
    const auto found = states_.find(id);
    return found != states_.end() && found->second.running;
}

bool AppWatcher::anyOtherRunning(uint32_t id) const {
    for (const auto& entry : states_) {
        if (entry.first != id && entry.second.running) {
            return true;
        }
    }
    return false;
}

std::vector<uint32_t> AppWatcher::installedApps() const {
    std::vector<uint32_t> ids;
    for (const auto& entry : states_) {
        if (entry.second.installed) {
            ids.push_back(entry.first);
        }
    }
    return ids;
}

}

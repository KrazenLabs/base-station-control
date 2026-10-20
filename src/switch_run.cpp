#include "switch_run.hpp"

#include <algorithm>

#include "text.hpp"

namespace bsc {

namespace {

double listenSeconds(RunKind kind, const RunTiming& timing) {
    switch (kind) {
        case RunKind::Startup: return timing.listenStartupSeconds;
        case RunKind::Wake: return timing.listenWakeSeconds;
        case RunKind::AppLaunch: return timing.listenAppLaunchSeconds;
        case RunKind::AppQuit: return timing.listenAppQuitSeconds;
        case RunKind::AllOn:
        case RunKind::AllOff: return timing.listenAllSeconds;
        case RunKind::Sleep:
        case RunKind::Shutdown: return timing.listenOffSeconds;
    }
    return timing.listenOffSeconds;
}

bool onlyFreshAdverts(RunKind kind) {
    return kind == RunKind::Startup || kind == RunKind::Wake || kind == RunKind::AppLaunch;
}

std::string stationLabel(const RunStation& station) {
    return station.title == station.name ? station.name : station.title + " (" + station.name + ")";
}

std::vector<std::string> titlesWith(const RunReport& report, StationResult result) {
    std::vector<std::string> titles;
    for (const RunStation& station : report.stations) {
        if (station.result == result) {
            titles.push_back(result == StationResult::Failed ? station.title + " (" + station.message + ")" : station.title);
        }
    }
    return titles;
}

}

bool runTurnsOn(RunKind kind) {
    return kind == RunKind::Startup || kind == RunKind::Wake || kind == RunKind::AppLaunch || kind == RunKind::AllOn;
}

const char* runText(RunKind kind) {
    switch (kind) {
        case RunKind::Startup: return "headset started";
        case RunKind::Wake: return "headset woke up";
        case RunKind::Sleep: return "headset going to sleep";
        case RunKind::Shutdown: return "headset shutting down";
        case RunKind::AppLaunch: return "app started";
        case RunKind::AppQuit: return "app quit";
        case RunKind::AllOn: return "all on";
        case RunKind::AllOff: return "all off";
    }
    return "";
}

bool RunReport::anyFailed() const {
    return std::any_of(stations.begin(), stations.end(),
                       [](const RunStation& station) { return station.result == StationResult::Failed; });
}

std::string runSummary(const RunReport& report) {
    const char* direction = report.turnsOn() ? "on" : "off";
    std::vector<std::string> parts;
    const std::vector<std::string> switched = titlesWith(report, StationResult::Switched);
    const std::vector<std::string> already = titlesWith(report, StationResult::Already);
    const std::vector<std::string> failed = titlesWith(report, StationResult::Failed);
    const std::vector<std::string> notHeard = titlesWith(report, StationResult::NotHeard);
    if (!switched.empty()) parts.push_back(std::string("switched ") + direction + " " + joinWords(switched, ", "));
    if (!already.empty()) parts.push_back(std::string("already ") + direction + ": " + joinWords(already, ", "));
    if (!failed.empty()) parts.push_back("failed: " + joinWords(failed, ", "));
    if (!notHeard.empty()) parts.push_back("not heard: " + joinWords(notHeard, ", "));
    if (parts.empty()) parts.push_back("no known Base Stations");
    return report.label + ": " + joinWords(parts, "; ") + " (" + formatNumber(report.seconds, 1) + " s" +
           (report.reason.empty() ? "" : ", " + report.reason) + ")";
}

std::vector<std::string> runStationLines(const RunReport& report) {
    std::vector<std::string> lines;
    for (const RunStation& station : report.stations) {
        std::string text;
        switch (station.result) {
            case StationResult::NotHeard: text = "not heard, skipped"; break;
            case StationResult::Pending: text = "no result yet"; break;
            default: text = station.message; break;
        }
        lines.push_back(stationLabel(station) + ": " + text);
    }
    if (!report.reason.empty()) {
        lines.push_back("stopped early: " + report.reason);
    } else if (lines.empty()) {
        lines.push_back("no known Base Stations");
    }
    return lines;
}

SwitchRun::SwitchRun(RunTiming timing) : timing_(timing) {}

std::string SwitchRun::label() const {
    return label_.empty() ? std::string(runText(kind_)) : label_;
}

void SwitchRun::start(RunKind kind, double now, double budgetSeconds, int tag, std::string label) {
    active_ = true;
    kind_ = kind;
    label_ = std::move(label);
    tag_ = tag;
    startedAt_ = now;
    stations_.clear();
    report_.reset();
    heardSince_ = onlyFreshAdverts(kind) ? now : now - timing_.heardBeforeSeconds;
    deadline_ = budgetSeconds > 0.0 ? now + std::max(0.0, budgetSeconds - timing_.marginSeconds) : 0.0;
    const double listen = listenSeconds(kind, timing_);
    listenUntil_ = deadline_ > 0.0 ? std::min(now + listen, deadline_) : now + listen;
    messages_.push_back(this->label() + ": turning Base Stations " + (runTurnsOn(kind) ? "on" : "off"));
}

RunStation* SwitchRun::decided(const std::string& name) {
    for (RunStation& station : stations_) {
        if (station.name == name) {
            return &station;
        }
    }
    return nullptr;
}

void SwitchRun::decide(const Station& station, Controller& controller, const StationRegistry& registry) {
    const bool on = runTurnsOn(kind_);
    const PowerState state = powerStateFromByte(*station.livePowerByte);
    const bool hasJob = controller.job(station.name()).has_value();
    RunStation entry;
    entry.name = station.name();
    entry.title = station.title();
    if (!hasJob && (on ? countsAsOn(state) : isOff(state))) {
        entry.result = StationResult::Already;
        entry.message = on ? "already on" : "already off";
    } else {
        controller.request(registry, station.name(), controller.commandFor(on), tag_);
        entry.requested = true;
    }
    stations_.push_back(entry);
}

bool SwitchRun::jobsLeft(const Controller& controller) const {
    return std::any_of(stations_.begin(), stations_.end(), [&controller](const RunStation& station) {
        return station.requested && station.result == StationResult::Pending && controller.job(station.name).has_value();
    });
}

void SwitchRun::cancelJobs(Controller& controller, const std::string& reason) {
    for (RunStation& station : stations_) {
        if (station.requested && station.result == StationResult::Pending && controller.job(station.name)) {
            controller.cancel(station.name, reason);
            station.result = StationResult::Failed;
            station.message = reason;
        }
    }
}

void SwitchRun::tick(const StationRegistry& registry, Controller& controller, double now) {
    if (!active_) {
        return;
    }
    const std::vector<const Station*> listed = registry.listed();
    if (now < listenUntil_) {
        for (const Station* station : listed) {
            if (!decided(station->name()) && station->livePowerByte && station->seenSince(heardSince_)) {
                decide(*station, controller, registry);
            }
        }
    }
    const bool allHeard = std::all_of(listed.begin(), listed.end(),
                                      [this](const Station* station) { return decided(station->name()) != nullptr; });
    const bool listened = now >= listenUntil_ || allHeard;
    const bool pending = jobsLeft(controller);
    if (deadline_ > 0.0 && now >= deadline_ && pending) {
        cancelJobs(controller, "out of time");
        end(&registry, now, "out of time");
        messages_.push_back(runSummary(*report_));
        return;
    }
    if (listened && !pending) {
        end(&registry, now, "");
        messages_.push_back(runSummary(*report_));
    }
}

void SwitchRun::addOutcome(const Outcome& outcome) {
    if (!active_ || std::find(outcome.tags.begin(), outcome.tags.end(), tag_) == outcome.tags.end()) {
        return;
    }
    RunStation* station = decided(outcome.station);
    if (!station || !station->requested) {
        return;
    }
    switch (outcome.result) {
        case JobResult::Switched: station->result = StationResult::Switched; break;
        case JobResult::AlreadyThere: station->result = StationResult::Already; break;
        default: station->result = StationResult::Failed; break;
    }
    station->message = outcome.message;
}

void SwitchRun::stop(Controller& controller, const std::string& reason, double now) {
    if (!active_) {
        return;
    }
    cancelJobs(controller, reason);
    end(nullptr, now, reason);
    messages_.push_back(label() + ": stopped (" + reason + ") after " + formatNumber(report_->seconds, 1) + " s");
}

void SwitchRun::end(const StationRegistry* registry, double now, const std::string& reason) {
    if (registry) {
        for (const Station* station : registry->listed()) {
            if (!decided(station->name())) {
                RunStation entry;
                entry.name = station->name();
                entry.title = station->title();
                entry.result = StationResult::NotHeard;
                stations_.push_back(entry);
            }
        }
    }
    for (RunStation& station : stations_) {
        if (station.requested && station.result == StationResult::Pending) {
            station.result = StationResult::Failed;
            station.message = "no result";
        }
    }
    RunReport report;
    report.kind = kind_;
    report.tag = tag_;
    report.label = label();
    report.reason = reason;
    report.seconds = now - startedAt_;
    report.stations = stations_;
    report_ = std::move(report);
    active_ = false;
}

std::optional<RunReport> SwitchRun::takeReport() {
    std::optional<RunReport> taken;
    taken.swap(report_);
    return taken;
}

std::vector<std::string> SwitchRun::takeMessages() {
    std::vector<std::string> taken;
    taken.swap(messages_);
    return taken;
}

}

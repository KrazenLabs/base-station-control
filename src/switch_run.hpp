#pragma once

#include <optional>
#include <string>
#include <vector>

#include "controller.hpp"
#include "stations.hpp"

namespace bsc {

enum class RunKind { Startup, Wake, Sleep, Shutdown, AppLaunch, AppQuit, AllOn, AllOff };

bool runTurnsOn(RunKind kind);
const char* runText(RunKind kind);

struct RunTiming {
    double listenStartupSeconds = 60.0;
    double listenWakeSeconds = 20.0;
    double listenAppLaunchSeconds = 20.0;
    double listenAppQuitSeconds = 10.0;
    double listenOffSeconds = 2.0;
    double listenAllSeconds = 8.0;
    double heardBeforeSeconds = 5.0;
    double marginSeconds = 0.35;
};

enum class StationResult { Pending, Switched, Already, Failed, NotHeard };

struct RunStation {
    std::string name;
    std::string title;
    bool requested = false;
    StationResult result = StationResult::Pending;
    std::string message;
};

struct RunReport {
    RunKind kind = RunKind::AllOn;
    int tag = 0;
    std::string label;
    std::string reason;
    double seconds = 0.0;
    std::vector<RunStation> stations;

    bool turnsOn() const { return runTurnsOn(kind); }
    bool anyFailed() const;
    bool ok() const { return reason.empty() && !anyFailed(); }
};

std::string runSummary(const RunReport& report);
std::vector<std::string> runStationLines(const RunReport& report);

class SwitchRun {
public:
    explicit SwitchRun(RunTiming timing = {});

    void start(RunKind kind, double now, double budgetSeconds, int tag, std::string label = "");
    void stop(Controller& controller, const std::string& reason, double now);
    bool active() const { return active_; }
    RunKind kind() const { return kind_; }
    std::string label() const;
    int tag() const { return tag_; }
    bool listening(double now) const { return active_ && now < listenUntil_; }
    void tick(const StationRegistry& registry, Controller& controller, double now);
    void addOutcome(const Outcome& outcome);
    std::optional<RunReport> takeReport();
    std::vector<std::string> takeMessages();

private:
    RunStation* decided(const std::string& name);
    bool jobsLeft(const Controller& controller) const;
    void cancelJobs(Controller& controller, const std::string& reason);
    void decide(const Station& station, Controller& controller, const StationRegistry& registry);
    void end(const StationRegistry* registry, double now, const std::string& reason);

    RunTiming timing_;
    bool active_ = false;
    RunKind kind_ = RunKind::AllOn;
    std::string label_;
    double startedAt_ = 0.0;
    double heardSince_ = 0.0;
    double listenUntil_ = 0.0;
    double deadline_ = 0.0;
    int tag_ = 0;
    std::vector<RunStation> stations_;
    std::optional<RunReport> report_;
    std::vector<std::string> messages_;
};

}

#pragma once

#include <cstdint>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "common.hpp"
#include "lighthouse.hpp"
#include "power_link.hpp"
#include "stations.hpp"

namespace bsc {

struct ControllerSettings {
    PowerCommand offCommand = PowerCommand::Sleep;
    int attempts = 3;
    int maxParallel = 4;
    double freshSeconds = 5.0;
    double findTimeoutSeconds = 15.0;
    double confirmOnSeconds = 20.0;
    double confirmOffSeconds = 12.0;
    double retryDelaySeconds = 1.5;
    double errorSeconds = 30.0;
};

enum class JobPhase { Connecting, Retrying, Sending, Starting, ShuttingDown };

struct JobInfo {
    PowerCommand command = PowerCommand::On;
    bool identify = false;
    JobPhase phase = JobPhase::Connecting;
};

enum class JobResult { Switched, AlreadyThere, Blinking, Failed, Replaced, Stopped, UnknownStation, Busy };

bool jobSucceeded(JobResult result);

struct Outcome {
    std::string station;
    PowerCommand command = PowerCommand::On;
    bool identify = false;
    JobResult result = JobResult::Failed;
    std::string message;
    std::vector<int> tags;

    bool ok() const { return jobSucceeded(result); }
};

class Controller {
public:
    Controller(ControllerSettings settings, PowerLink& link, Logger log);

    PowerCommand commandFor(bool on) const { return on ? PowerCommand::On : settings_.offCommand; }
    void request(const StationRegistry& registry, const std::string& station, PowerCommand command, int tag = 0);
    void identify(const StationRegistry& registry, const std::string& station, int tag = 0);
    void tick(const StationRegistry& registry, double now);
    bool busy() const { return !jobs_.empty(); }
    std::optional<JobInfo> job(const std::string& station) const;
    std::optional<std::string> recentError(const std::string& station, double now) const;
    std::vector<Outcome> takeOutcomes();
    void cancel(const std::string& station, const std::string& reason);
    void cancelAll();

private:
    enum class Stage { Queued, Finding, Writing, Confirming, RetryWait };
    enum class Step { Continue, Done };

    struct Job {
        std::string station;
        PowerCommand command = PowerCommand::On;
        bool identify = false;
        bool wakes = false;
        Stage stage = Stage::Queued;
        int attempts = 0;
        double deadline = 0.0;
        double writtenAt = 0.0;
        uint64_t generation = 0;
        std::string devicePath;
        std::optional<LinkResult> writeResult;
        std::optional<PowerCommand> retarget;
        std::vector<int> tags;
        std::vector<int> retargetTags;
    };

    int writesInFlight() const;
    void startWrite(Job& job, const Station& station);
    Step advance(Job& job, const Station& station, double now);
    Step advanceQueued(Job& job, const Station& station, double now);
    Step advanceFinding(Job& job, const Station& station, double now);
    Step advanceWriting(Job& job, double now);
    Step finishIdentify(Job& job, const LinkResult& result, double now);
    Step advanceConfirming(Job& job, const Station& station, double now);
    void switchToRetarget(Job& job);
    Step retryOrFail(Job& job, double now, const std::string& message);
    void answer(const Job& job, JobResult result, const std::string& message, std::vector<int> tags);
    void supersede(Job& job);
    void finish(Job& job, JobResult result, const std::string& message, double now);

    ControllerSettings settings_;
    PowerLink& link_;
    Logger log_;
    std::map<std::string, Job> jobs_;
    std::map<std::string, std::pair<std::string, double>> errors_;
    std::vector<Outcome> outcomes_;
    std::shared_ptr<bool> alive_ = std::make_shared<bool>(true);
};

}

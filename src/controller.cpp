#include "controller.hpp"

#include <algorithm>

namespace bsc {

namespace {

const char* targetWord(PowerCommand command) {
    return command == PowerCommand::On ? "on" : (command == PowerCommand::Standby ? "to standby" : "off");
}

const char* alreadyText(PowerCommand command) {
    return command == PowerCommand::On ? "already on" : (command == PowerCommand::Standby ? "already in standby" : "already off");
}

bool liveStateSatisfies(const Station& station, PowerCommand command, double now, double freshSeconds) {
    return station.livePowerByte && station.lastSeenMonotonic >= 0.0 && now - station.lastSeenMonotonic <= freshSeconds &&
           satisfies(powerStateFromByte(*station.livePowerByte), command);
}

}

bool jobSucceeded(JobResult result) {
    return result == JobResult::Switched || result == JobResult::AlreadyThere || result == JobResult::Blinking;
}

Controller::Controller(ControllerSettings settings, PowerLink& link, Logger log)
    : settings_(settings), link_(link), log_(std::move(log)) {}

void Controller::request(const StationRegistry& registry, const std::string& station, PowerCommand command, int tag) {
    const Station* known = registry.find(station);
    if (!known) {
        Outcome outcome;
        outcome.station = station;
        outcome.command = command;
        outcome.result = JobResult::UnknownStation;
        outcome.message = "unknown station";
        if (tag) outcome.tags.push_back(tag);
        outcomes_.push_back(outcome);
        return;
    }
    errors_.erase(known->name());
    const auto found = jobs_.find(known->name());
    if (found == jobs_.end()) {
        Job job;
        job.station = known->name();
        job.command = command;
        if (tag) job.tags.push_back(tag);
        jobs_.emplace(job.station, job);
        return;
    }
    Job& job = found->second;
    if (job.stage == Stage::Writing) {
        if (!job.identify && job.command == command) {
            job.retarget.reset();
            job.tags.insert(job.tags.end(), job.retargetTags.begin(), job.retargetTags.end());
            job.retargetTags.clear();
            if (tag) job.tags.push_back(tag);
        } else {
            job.retarget = command;
            if (tag) job.retargetTags.push_back(tag);
        }
        return;
    }
    if (!job.identify && job.command == command) {
        if (tag) job.tags.push_back(tag);
        return;
    }
    supersede(job);
    if (tag) job.tags.push_back(tag);
    job.command = command;
    job.identify = false;
    job.stage = Stage::Queued;
    job.attempts = 0;
    ++job.generation;
}

void Controller::identify(const StationRegistry& registry, const std::string& station, int tag) {
    const Station* known = registry.find(station);
    Outcome refusal;
    refusal.station = station;
    refusal.identify = true;
    if (tag) refusal.tags.push_back(tag);
    if (!known) {
        refusal.result = JobResult::UnknownStation;
        refusal.message = "unknown station";
        outcomes_.push_back(refusal);
        return;
    }
    if (jobs_.count(known->name())) {
        refusal.result = JobResult::Busy;
        refusal.message = "is busy, try again in a moment";
        outcomes_.push_back(refusal);
        return;
    }
    Job job;
    job.station = known->name();
    job.identify = true;
    job.wakes = !countsAsOn(known->knownState());
    if (tag) job.tags.push_back(tag);
    jobs_.emplace(job.station, job);
}

int Controller::writesInFlight() const {
    return static_cast<int>(std::count_if(jobs_.begin(), jobs_.end(),
                                          [](const auto& entry) { return entry.second.stage == Stage::Writing; }));
}

void Controller::startWrite(Job& job, const Station& station) {
    job.stage = Stage::Writing;
    job.devicePath = station.devicePath;
    job.writeResult.reset();
    ++job.attempts;
    const uint64_t generation = ++job.generation;
    const std::string name = job.station;
    const std::weak_ptr<bool> alive = alive_;
    const std::string uuid = job.identify ? kIdentifyCharacteristicUuid : kPowerCharacteristicUuid;
    const uint8_t value = job.identify ? 0x00 : commandByte(job.command);
    log_(std::string(job.identify ? "identifying " : "switching ") + name +
         (job.identify ? "" : std::string(" ") + targetWord(job.command)) +
         (job.attempts > 1 ? " (attempt " + std::to_string(job.attempts) + ")" : ""));
    link_.write(station.devicePath, uuid, {value}, !job.identify, [this, alive, name, generation](const LinkResult& result) {
        if (alive.expired()) {
            return;
        }
        const auto found = jobs_.find(name);
        if (found != jobs_.end() && found->second.generation == generation && found->second.stage == Stage::Writing) {
            found->second.writeResult = result;
        }
    });
}

void Controller::answer(const Job& job, JobResult result, const std::string& message, std::vector<int> tags) {
    Outcome outcome;
    outcome.station = job.station;
    outcome.command = job.command;
    outcome.identify = job.identify;
    outcome.result = result;
    outcome.message = message;
    outcome.tags = std::move(tags);
    outcomes_.push_back(outcome);
}

void Controller::finish(Job& job, JobResult result, const std::string& message, double now) {
    answer(job, result, message, job.tags);
    const bool ok = jobSucceeded(result);
    if (!ok) {
        errors_[job.station] = {message, now + settings_.errorSeconds};
    }
    log_(job.station + ": " + (ok ? "" : "failed: ") + message);
}

void Controller::supersede(Job& job) {
    if (job.tags.empty()) {
        return;
    }
    answer(job, JobResult::Replaced, "replaced by newer command", job.tags);
    job.tags.clear();
}

Controller::Step Controller::retryOrFail(Job& job, double now, const std::string& message) {
    if (job.attempts < settings_.attempts) {
        job.stage = Stage::RetryWait;
        job.deadline = now + settings_.retryDelaySeconds;
        return Step::Continue;
    }
    finish(job, JobResult::Failed, message, now);
    return Step::Done;
}

void Controller::switchToRetarget(Job& job) {
    supersede(job);
    job.command = *job.retarget;
    job.identify = false;
    job.retarget.reset();
    job.tags = std::move(job.retargetTags);
    job.retargetTags.clear();
    job.attempts = 0;
    job.stage = Stage::Queued;
}

Controller::Step Controller::advanceQueued(Job& job, const Station& station, double now) {
    if (!job.identify && liveStateSatisfies(station, job.command, now, settings_.freshSeconds)) {
        finish(job, JobResult::AlreadyThere, alreadyText(job.command), now);
        return Step::Done;
    }
    job.stage = Stage::Finding;
    job.deadline = now + settings_.findTimeoutSeconds;
    return advanceFinding(job, station, now);
}

Controller::Step Controller::advanceFinding(Job& job, const Station& station, double now) {
    const bool reachable = !station.devicePath.empty() && link_.canReach(station.devicePath);
    if (reachable) {
        job.deadline = now + settings_.findTimeoutSeconds;
        if (writesInFlight() < settings_.maxParallel) {
            startWrite(job, station);
        }
        return Step::Continue;
    }
    if (now >= job.deadline) {
        finish(job, JobResult::Failed, "no connection (unplugged or out of range?)", now);
        return Step::Done;
    }
    return Step::Continue;
}

Controller::Step Controller::advanceWriting(Job& job, double now) {
    if (!job.writeResult) {
        return Step::Continue;
    }
    const LinkResult result = *job.writeResult;
    job.writeResult.reset();
    if (!result.ok()) {
        log_(job.station + ": " + describeLinkResult(result));
    }
    if (job.identify) {
        return finishIdentify(job, result, now);
    }
    if (job.retarget) {
        switchToRetarget(job);
        return Step::Continue;
    }
    if (!result.ok()) {
        return retryOrFail(job, now, linkFailureText(result.failure));
    }
    if (result.readBack && satisfies(powerStateFromByte(*result.readBack), job.command)) {
        finish(job, JobResult::Switched, std::string("switched ") + targetWord(job.command), now);
        return Step::Done;
    }
    job.stage = Stage::Confirming;
    job.writtenAt = now;
    job.deadline = now + (job.command == PowerCommand::On ? settings_.confirmOnSeconds : settings_.confirmOffSeconds);
    return Step::Continue;
}

Controller::Step Controller::finishIdentify(Job& job, const LinkResult& result, double now) {
    const bool lastAttempt = result.ok() || job.retarget || job.attempts >= settings_.attempts;
    if (!lastAttempt) {
        return retryOrFail(job, now, linkFailureText(result.failure));
    }
    const JobResult outcome = result.ok() ? JobResult::Blinking : JobResult::Failed;
    const std::string message =
        result.ok() ? (job.wakes ? "starting up, then blinking" : "blinking") : linkFailureText(result.failure);
    if (!job.retarget) {
        finish(job, outcome, message, now);
        return Step::Done;
    }
    answer(job, outcome, message, job.tags);
    log_(job.station + ": " + message);
    job.tags.clear();
    switchToRetarget(job);
    return Step::Continue;
}

Controller::Step Controller::advanceConfirming(Job& job, const Station& station, double now) {
    if (station.seenSince(job.writtenAt) && station.livePowerByte &&
        satisfies(powerStateFromByte(*station.livePowerByte), job.command)) {
        finish(job, JobResult::Switched, std::string("switched ") + targetWord(job.command), now);
        return Step::Done;
    }
    if (now >= job.deadline) {
        return retryOrFail(job, now, "did not respond");
    }
    return Step::Continue;
}

Controller::Step Controller::advance(Job& job, const Station& station, double now) {
    switch (job.stage) {
        case Stage::Queued: return advanceQueued(job, station, now);
        case Stage::Finding: return advanceFinding(job, station, now);
        case Stage::Writing: return advanceWriting(job, now);
        case Stage::Confirming: return advanceConfirming(job, station, now);
        case Stage::RetryWait:
            if (now >= job.deadline) {
                job.stage = Stage::Finding;
                job.deadline = now + settings_.findTimeoutSeconds;
            }
            return Step::Continue;
    }
    return Step::Continue;
}

void Controller::tick(const StationRegistry& registry, double now) {
    for (auto it = jobs_.begin(); it != jobs_.end();) {
        Job& job = it->second;
        const Station* station = registry.find(job.station);
        Step step = Step::Done;
        if (station) {
            step = advance(job, *station, now);
        } else {
            finish(job, JobResult::Failed, "no longer known", now);
        }
        it = step == Step::Done ? jobs_.erase(it) : std::next(it);
    }
    for (auto it = errors_.begin(); it != errors_.end();) {
        it = it->second.second <= now ? errors_.erase(it) : std::next(it);
    }
}

std::optional<JobInfo> Controller::job(const std::string& station) const {
    const auto found = jobs_.find(station);
    if (found == jobs_.end()) {
        return std::nullopt;
    }
    const Job& job = found->second;
    JobInfo info;
    info.command = job.retarget.value_or(job.command);
    info.identify = job.identify;
    switch (job.stage) {
        case Stage::Queued:
        case Stage::Finding: info.phase = job.attempts > 0 ? JobPhase::Retrying : JobPhase::Connecting; break;
        case Stage::Writing:
            info.phase = link_.phase(job.devicePath) == LinkPhase::Sending ? JobPhase::Sending : JobPhase::Connecting;
            break;
        case Stage::Confirming: info.phase = job.command == PowerCommand::On ? JobPhase::Starting : JobPhase::ShuttingDown; break;
        case Stage::RetryWait: info.phase = JobPhase::Retrying; break;
    }
    return info;
}

std::optional<std::string> Controller::recentError(const std::string& station, double now) const {
    const auto found = errors_.find(station);
    if (found == errors_.end() || found->second.second <= now) {
        return std::nullopt;
    }
    return found->second.first;
}

std::vector<Outcome> Controller::takeOutcomes() {
    std::vector<Outcome> taken;
    taken.swap(outcomes_);
    return taken;
}

void Controller::cancel(const std::string& station, const std::string& reason) {
    const auto found = jobs_.find(station);
    if (found == jobs_.end()) {
        return;
    }
    Job& job = found->second;
    std::vector<int> tags = job.tags;
    tags.insert(tags.end(), job.retargetTags.begin(), job.retargetTags.end());
    answer(job, JobResult::Stopped, reason, tags);
    log_(job.station + ": stopped: " + reason);
    jobs_.erase(found);
}

void Controller::cancelAll() {
    jobs_.clear();
}

}

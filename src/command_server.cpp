#include "command_server.hpp"

#include <algorithm>

#include "panel_module.hpp"
#include "text.hpp"

namespace bsc {

namespace {

constexpr double kMaxRequestAgeSeconds = 30.0;
constexpr double kWaitSeconds = 90.0;
constexpr double kStaleAnswerSeconds = 120.0;

}

CommandServer::CommandServer(std::filesystem::path directory, Logger log)
    : mailbox_(std::move(directory)), log_(std::move(log)) {}

std::vector<IncomingCommand> CommandServer::takeCommands() {
    TakenRequests taken = mailbox_.takeRequests(kMaxRequestAgeSeconds);
    for (const MailboxRequest& request : taken.expired) {
        log_("ignored expired command: " + joinWords(request.words, " "));
    }
    std::vector<IncomingCommand> commands;
    for (MailboxRequest& request : taken.fresh) {
        commands.push_back({std::move(request.id), std::move(request.words)});
    }
    return commands;
}

void CommandServer::ready(const std::string& requestId, bool ok, const std::string& message) {
    MailboxAnswer answer;
    answer.ok = ok;
    answer.message = message;
    ready_.emplace_back(requestId, answer);
}

void CommandServer::answerNow(const std::string& requestId, bool ok, const std::string& message) {
    ready(requestId, ok, message);
    sendReadyAnswers();
}

void CommandServer::waitForStations(const std::string& requestId, int tag, std::vector<std::string> stations, double now) {
    Waiting waiting;
    waiting.requestId = requestId;
    waiting.stations = std::move(stations);
    waiting.deadline = now + kWaitSeconds;
    waiting_[tag] = waiting;
}

void CommandServer::waitForRun(const std::string& requestId, int runTag, AnswerStyle style, double now) {
    Waiting waiting;
    waiting.requestId = requestId;
    waiting.forRun = true;
    waiting.style = style;
    waiting.deadline = now + kWaitSeconds;
    waiting_[runTag] = waiting;
}

void CommandServer::onOutcome(const Outcome& outcome, const StationRegistry& registry) {
    for (const int tag : outcome.tags) {
        const auto found = waiting_.find(tag);
        if (found == waiting_.end() || found->second.forRun) {
            continue;
        }
        Waiting& waiting = found->second;
        waiting.lines.push_back(describeOutcome(registry, outcome));
        waiting.ok = waiting.ok && outcome.ok();
        waiting.stations.erase(std::remove(waiting.stations.begin(), waiting.stations.end(), outcome.station),
                               waiting.stations.end());
        if (waiting.stations.empty()) {
            ready(waiting.requestId, waiting.ok, joinWords(waiting.lines, "\n"));
            waiting_.erase(found);
        }
    }
}

void CommandServer::onRunReport(const RunReport& report) {
    const auto found = waiting_.find(report.tag);
    if (found == waiting_.end() || !found->second.forRun) {
        return;
    }
    const std::string message = found->second.style == AnswerStyle::PanelSummary
                                    ? panelRunSummary(report)
                                    : joinWords(runStationLines(report), "\n");
    ready(found->second.requestId, report.ok(), message);
    waiting_.erase(found);
}

void CommandServer::expire(double now) {
    for (auto it = waiting_.begin(); it != waiting_.end();) {
        if (now < it->second.deadline) {
            ++it;
            continue;
        }
        std::vector<std::string> lines = it->second.lines;
        lines.push_back(it->second.forRun ? std::string("no result yet")
                                          : "no result yet for: " + joinWords(it->second.stations, ", "));
        ready(it->second.requestId, false, joinWords(lines, "\n"));
        it = waiting_.erase(it);
    }
    sendReadyAnswers();
}

void CommandServer::sendReadyAnswers() {
    for (const auto& entry : ready_) {
        std::string error;
        if (!mailbox_.answer(entry.first, entry.second, &error)) {
            log_("failed to answer command: " + error);
        }
    }
    ready_.clear();
}

void CommandServer::stop() {
    for (const auto& entry : waiting_) {
        ready(entry.second.requestId, false, "service stopped before command finished");
    }
    waiting_.clear();
    sendReadyAnswers();
}

void CommandServer::cleanUp() {
    mailbox_.removeStale(kStaleAnswerSeconds);
}

}

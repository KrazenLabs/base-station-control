#pragma once

#include <filesystem>
#include <map>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "commands.hpp"
#include "common.hpp"
#include "controller.hpp"
#include "stations.hpp"
#include "switch_run.hpp"

namespace bsc {

struct IncomingCommand {
    std::string requestId;
    std::vector<std::string> words;
};

class CommandServer {
public:
    CommandServer(std::filesystem::path directory, Logger log);

    std::vector<IncomingCommand> takeCommands();
    void answerNow(const std::string& requestId, bool ok, const std::string& message);
    void waitForStations(const std::string& requestId, int tag, std::vector<std::string> stations, double now);
    void waitForRun(const std::string& requestId, int runTag, AnswerStyle style, double now);
    void onOutcome(const Outcome& outcome, const StationRegistry& registry);
    void onRunReport(const RunReport& report);
    void expire(double now);
    bool hasReadyAnswers() const { return !ready_.empty(); }
    void sendReadyAnswers();
    void stop();
    void cleanUp();

private:
    struct Waiting {
        std::string requestId;
        bool forRun = false;
        AnswerStyle style = AnswerStyle::StationLines;
        std::vector<std::string> stations;
        std::vector<std::string> lines;
        bool ok = true;
        double deadline = 0.0;
    };

    void ready(const std::string& requestId, bool ok, const std::string& message);

    Mailbox mailbox_;
    Logger log_;
    std::map<int, Waiting> waiting_;
    std::vector<std::pair<std::string, MailboxAnswer>> ready_;
};

}

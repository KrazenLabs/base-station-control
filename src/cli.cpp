#include "cli.hpp"

#include <cstdio>
#include <optional>

#include <json/json.h>

#include "commands.hpp"
#include "common.hpp"
#include "panel_module.hpp"
#include "status_report.hpp"

namespace bsc {

namespace {

constexpr double kShortTimeoutSeconds = 10.0;
constexpr double kLongTimeoutSeconds = 95.0;
constexpr double kPanelActionSeconds = 25.0;
constexpr const char* kStartHint = "start it with: systemctl --user start base-station-control";
constexpr const char* kJournalHint = "check: journalctl --user -u base-station-control";

std::optional<Json::Value> readStatus(const Paths& paths) {
    const JsonFile file = readJsonFile(paths.status);
    if (file.status != JsonFileStatus::Ok || !file.value.isObject()) {
        return std::nullopt;
    }
    return file.value;
}

bool serviceIsRunning(const Paths& paths) {
    const std::optional<Json::Value> status = readStatus(paths);
    return status && statusIsFresh(*status, wallSeconds());
}

std::optional<std::string> submit(const Paths& paths, const std::vector<std::string>& words, std::string* error) {
    Mailbox mailbox(paths.requests);
    return mailbox.submit(words, error);
}

WaitResult wait(const Paths& paths, const std::string& id, double seconds) {
    Mailbox mailbox(paths.requests);
    return waitForAnswer(mailbox, id, seconds, [] { return stopRequested(); });
}

}

int runStatus(const Paths& paths) {
    const std::optional<Json::Value> status = readStatus(paths);
    if (!status) {
        std::printf("service not running (start with: systemctl --user start base-station-control)\n");
        return 1;
    }
    std::printf("%s", formatStatusText(*status, wallSeconds()).c_str());
    return 0;
}

int runServiceCommand(const Paths& paths, const std::vector<std::string>& words) {
    if (!serviceIsRunning(paths)) {
        std::fprintf(stderr, "the base-station-control service is not running; %s\n", kStartHint);
        return 1;
    }
    std::string error;
    const std::optional<std::string> id = submit(paths, words, &error);
    if (!id) {
        std::fprintf(stderr, "could not send command: %s\n", error.c_str());
        return 1;
    }
    const bool longCommand = isLongCommand(words.front());
    if (longCommand) {
        std::fprintf(stderr, "switching, please wait...\n");
    }
    const WaitResult result = wait(paths, *id, longCommand ? kLongTimeoutSeconds : kShortTimeoutSeconds);
    switch (result.outcome) {
        case WaitOutcome::Answered:
            std::printf("%s\n", result.answer.message.c_str());
            return result.answer.ok ? 0 : 1;
        case WaitOutcome::Interrupted:
            std::fprintf(stderr, "stopped waiting for service\n");
            return 1;
        case WaitOutcome::TimedOut:
            break;
    }
    std::fprintf(stderr, "no answer from service; %s\n", kJournalHint);
    return 1;
}

int runPanelStatus(const Paths& paths) {
    std::printf("%s\n", writeJson(panelStatus(readStatus(paths), wallSeconds())).c_str());
    return 0;
}

int runPanelAction(const Paths& paths, bool on) {
    if (!serviceIsRunning(paths)) {
        std::printf("Service is not running\n");
        return 1;
    }
    std::string error;
    const std::optional<std::string> id = submit(paths, {"panel-action", on ? "all-on" : "all-off"}, &error);
    if (!id) {
        std::printf("Failed to send command: %s\n", error.c_str());
        return 1;
    }
    const WaitResult result = wait(paths, *id, kPanelActionSeconds);
    switch (result.outcome) {
        case WaitOutcome::Answered:
            std::printf("%s\n", result.answer.message.c_str());
            return result.answer.ok ? 0 : 1;
        case WaitOutcome::Interrupted:
            std::printf("%s\n", panelInterruptedText().c_str());
            return 1;
        case WaitOutcome::TimedOut:
            break;
    }
    std::printf("%s\n", panelTimeoutText(kPanelActionSeconds).c_str());
    return 1;
}

}

#include <cstdio>
#include <string>
#include <vector>

#include "app_info.hpp"
#include "automation.hpp"
#include "cli.hpp"
#include "commands.hpp"
#include "common.hpp"
#include "paths.hpp"
#include "service.hpp"
#include "watched_apps.hpp"

namespace {

constexpr size_t kUsageWidth = 92;
constexpr const char* kUsageIndent = "                               ";

std::string optionWords() {
    std::vector<std::string> on;
    std::vector<std::string> off;
    for (const bsc::OptionInfo& info : bsc::optionList(bsc::builtInApps())) {
        (info.turnsOn ? on : off).push_back(info.word);
    }
    return "<when> is " + bsc::joinWords(on, ", ") + " (all on), " + bsc::joinWords(off, ", ") +
           " (all off); no arguments: list";
}

std::string wrapped(const std::string& first, const std::string& text) {
    std::string out = first;
    size_t column = out.size();
    size_t start = 0;
    while (start < text.size()) {
        size_t end = text.find(' ', start);
        if (end == std::string::npos) {
            end = text.size();
        }
        const std::string word = text.substr(start, end - start);
        if (column + 1 + word.size() > kUsageWidth && column > first.size()) {
            out += std::string("\n") + kUsageIndent + word;
            column = std::string(kUsageIndent).size() + word.size();
        } else {
            out += (column == first.size() && first.back() == ' ' ? "" : " ") + word;
            column += 1 + word.size();
        }
        start = end + 1;
    }
    return out + "\n";
}

int usage() {
    const std::string text =
        std::string("usage: base-station-control <command>\n"
                    "  status\n"
                    "  on <station>... | all        turn base stations on\n"
                    "  off <station>... | all       turn base stations off (sleep, or standby per config.json)\n"
                    "  identify <station>           causes LED to blink\n"
                    "  name <station> <nickname>    name a base station (- to remove)\n"
                    "  ignore | unignore <station>  hide a station (a neighbour's) / unhide it\n"
                    "  forget <station>             remove a station from the list\n"
                    "  scan [seconds]               scan for base stations now (default 15 s)\n") +
        wrapped("  auto [<when> on|off]         switch all automatically;", optionWords()) +
        "  bluetooth on                 enable Bluetooth\n"
        "  run                          start background service\n"
        "  panel-status, panel-action all-on|all-off\n"
        "                               for the Community Apps tab\n"
        "  version\n";
    std::fputs(text.c_str(), stderr);
    return 2;
}

}

int main(int argc, char** argv) {
    bsc::installStopHandlers();
    std::vector<std::string> args(argv + 1, argv + argc);
    if (args.empty()) {
        return usage();
    }
    const std::string command = args.front();
    const bsc::Paths paths = bsc::defaultPaths();

    if (command == "run" && args.size() == 1) {
        return bsc::runService(paths, bsc::kAppVersion);
    }
    if ((command == "status" || command == "list") && args.size() == 1) {
        return bsc::runStatus(paths);
    }
    if (command == "panel-status" && args.size() == 1) {
        return bsc::runPanelStatus(paths);
    }
    if (command == "panel-action" && args.size() == 2 && (args[1] == "all-on" || args[1] == "all-off")) {
        return bsc::runPanelAction(paths, args[1] == "all-on");
    }
    if ((command == "version" || command == "--version") && args.size() == 1) {
        std::printf("%s %s\n", bsc::kAppId, bsc::kAppVersion);
        return 0;
    }
    if (bsc::isServiceCommand(command)) {
        return bsc::runServiceCommand(paths, args);
    }
    return usage();
}

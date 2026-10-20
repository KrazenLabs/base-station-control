#include "commands.hpp"

#include <algorithm>

#include "text.hpp"

namespace bsc {

namespace {

CommandResult failure(const std::string& message) {
    CommandResult result;
    result.ok = false;
    result.message = message;
    return result;
}

CommandResult success(const std::string& message) {
    CommandResult result;
    result.message = message;
    return result;
}

std::string label(const Station& station) {
    return station.title() == station.name() ? station.name() : station.title() + " (" + station.name() + ")";
}

std::optional<std::string> resolveOne(const StationRegistry& registry, const std::string& query, std::string* error) {
    const std::optional<std::vector<std::string>> names = resolveStations(registry, {query}, error);
    if (!names) {
        return std::nullopt;
    }
    return names->front();
}

CommandResult groupSwitch(const StationRegistry& registry, bool on, AnswerStyle style) {
    if (registry.listed().empty()) {
        return failure(style == AnswerStyle::PanelSummary ? std::string("No Base Stations registered")
                                                          : std::string("no Base Stations registered yet"));
    }
    CommandResult result;
    result.groupSwitch = GroupSwitch{on, style};
    return result;
}

CommandResult switchStations(CommandContext context, const std::vector<std::string>& words, int tag) {
    const bool on = words[0] == "on";
    if (words.size() < 2) {
        return failure("usage: base-station-control " + words[0] + " <station>... | all");
    }
    const std::vector<std::string> queries(words.begin() + 1, words.end());
    if (queries.size() == 1 && equalsIgnoreCase(queries.front(), "all")) {
        return groupSwitch(context.registry, on, AnswerStyle::StationLines);
    }
    std::string error;
    const std::optional<std::vector<std::string>> names = resolveStations(context.registry, queries, &error);
    if (!names) {
        return failure(error);
    }
    CommandResult result;
    for (const std::string& name : *names) {
        context.controller.request(context.registry, name, context.controller.commandFor(on), tag);
        result.waitFor.push_back(name);
    }
    return result;
}

CommandResult panelAction(const StationRegistry& registry, const std::vector<std::string>& words) {
    if (words.size() != 2 || (words[1] != "all-on" && words[1] != "all-off")) {
        return failure("usage: base-station-control panel-action all-on|all-off");
    }
    return groupSwitch(registry, words[1] == "all-on", AnswerStyle::PanelSummary);
}

CommandResult rename(StationRegistry& registry, const std::vector<std::string>& words) {
    if (words.size() < 3) {
        return failure("usage: base-station-control name <station> <nickname>   (use - to remove)");
    }
    std::string error;
    const std::optional<std::string> name = resolveOne(registry, words[1], &error);
    if (!name) {
        return failure(error);
    }
    const std::string nickname = joinWords(std::vector<std::string>(words.begin() + 2, words.end()), " ");
    return renameStation(registry, *name, nickname == "-" ? std::string() : nickname);
}

CommandResult automation(AutomationOptions& options, const AppList& apps, const std::vector<std::string>& words) {
    if (words.size() == 1) {
        const std::vector<OptionInfo> list = optionList(apps);
        size_t width = 16;
        for (const OptionInfo& info : list) {
            width = std::max(width, info.word.size() + 1);
        }
        std::vector<std::string> lines;
        for (const OptionInfo& info : list) {
            std::string word = info.word;
            word.resize(width, ' ');
            lines.push_back(word + (optionValue(options, info.id) ? "on    " : "off   ") + info.description);
        }
        return success(joinWords(lines, "\n"));
    }
    const std::optional<OptionInfo> option = words.size() == 3 ? findOption(apps, words[1]) : std::nullopt;
    if (!option || (words[2] != "on" && words[2] != "off")) {
        return failure("usage: base-station-control auto [<when> on|off]; no arguments lists every <when>");
    }
    const bool value = words[2] == "on";
    CommandResult result = success(option->description + ": " + words[2]);
    if (optionValue(options, option->id) != value) {
        setOptionValue(options, option->id, value);
        result.optionsChanged = true;
    }
    return result;
}

CommandResult changeListing(StationRegistry& registry, const std::vector<std::string>& words) {
    if (words.size() != 2) {
        return failure("usage: base-station-control " + words[0] + " <station>");
    }
    std::string error;
    const std::optional<std::string> name = resolveOne(registry, words[1], &error);
    if (!name) {
        return failure(error);
    }
    const std::string described = label(*registry.find(*name));
    CommandResult result;
    result.stateChanged = true;
    if (words[0] == "ignore") {
        registry.setIgnored(*name, true);
        result.message = described + " now ignored";
    } else if (words[0] == "unignore") {
        registry.setIgnored(*name, false);
        result.message = described + " is shown and switched again";
    } else {
        registry.forget(*name);
        result.message = "forgot " + described + " (use ignore to hide permanently)";
    }
    return result;
}

CommandResult identify(CommandContext context, const std::vector<std::string>& words, int tag) {
    if (words.size() != 2) {
        return failure("usage: base-station-control identify <station>");
    }
    std::string error;
    const std::optional<std::string> name = resolveOne(context.registry, words[1], &error);
    if (!name) {
        return failure(error);
    }
    context.controller.identify(context.registry, *name, tag);
    CommandResult result;
    result.waitFor.push_back(*name);
    return result;
}

CommandResult scan(const std::vector<std::string>& words) {
    if (words.size() > 2) {
        return failure("usage: base-station-control scan [seconds]");
    }
    double seconds = 15.0;
    if (words.size() == 2) {
        const std::optional<long long> value = parseInteger(words[1]);
        if (!value || *value < 1 || *value > 600) {
            return failure("seconds must be a whole number from 1 to 600");
        }
        seconds = static_cast<double>(*value);
    }
    CommandResult result = success("scanning for " + formatNumber(seconds, 0) + " s; check result with: base-station-control status");
    result.scanSeconds = seconds;
    return result;
}

CommandResult bluetooth(const std::vector<std::string>& words) {
    if (words.size() != 2 || words[1] != "on") {
        return failure("usage: base-station-control bluetooth on");
    }
    CommandResult result = success("enabling Bluetooth");
    result.powerOnBluetooth = true;
    return result;
}

}

CommandResult renameStation(StationRegistry& registry, const std::string& name, const std::string& nickname) {
    const Station* station = registry.find(name);
    if (!station) {
        return failure("unknown station " + name);
    }
    if (nickname.empty()) {
        if (station->record.nickname.empty()) {
            return success(name + " has no name");
        }
        registry.setNickname(name, "");
        CommandResult result = success("removed the name of " + name);
        result.stateChanged = true;
        return result;
    }
    if (!isValidNickname(nickname)) {
        return failure("1 to 32 characters, no quotes, backslashes or line breaks");
    }
    if (equalsIgnoreCase(nickname, "all")) {
        return failure("\"all\" is a reserved name");
    }
    for (const Station* other : registry.lookup(nickname)) {
        if (other->name() != station->name()) {
            return failure("\"" + nickname + "\" is already in use, choose a different name");
        }
    }
    if (station->record.nickname == nickname) {
        return success(station->name() + " is already called \"" + nickname + "\"");
    }
    const std::string stationName = station->name();
    registry.setNickname(stationName, nickname);
    CommandResult result = success(stationName + " is now called \"" + nickname + "\"");
    result.stateChanged = true;
    return result;
}

bool isServiceCommand(const std::string& word) {
    static const std::vector<std::string> commands = {"on",     "off",    "identify", "name",      "ignore", "unignore",
                                                      "forget", "scan",   "bluetooth", "auto"};
    return std::find(commands.begin(), commands.end(), word) != commands.end();
}

bool isLongCommand(const std::string& word) {
    return word == "on" || word == "off" || word == "identify";
}

std::optional<std::vector<std::string>> resolveStations(const StationRegistry& registry,
                                                        const std::vector<std::string>& queries, std::string* error) {
    std::vector<std::string> names;
    for (const std::string& query : queries) {
        const std::vector<const Station*> matches = registry.lookup(query);
        if (matches.empty()) {
            *error = "no station matches \"" + query + "\" (see: base-station-control status)";
            return std::nullopt;
        }
        if (matches.size() > 1) {
            std::vector<std::string> candidates;
            for (const Station* station : matches) {
                candidates.push_back(station->name());
            }
            *error = "\"" + query + "\" matches " + std::to_string(matches.size()) + " stations (" +
                     joinWords(candidates, ", ") + "); use full name";
            return std::nullopt;
        }
        if (std::find(names.begin(), names.end(), matches.front()->name()) == names.end()) {
            names.push_back(matches.front()->name());
        }
    }
    return names;
}

CommandResult executeCommand(CommandContext context, const std::vector<std::string>& words, int tag) {
    if (words.empty()) {
        return failure("no command");
    }
    const std::string& command = words[0];
    if (command == "on" || command == "off") {
        return switchStations(context, words, tag);
    }
    if (command == "panel-action") {
        return panelAction(context.registry, words);
    }
    if (command == "identify") {
        return identify(context, words, tag);
    }
    if (command == "name") {
        return rename(context.registry, words);
    }
    if (command == "auto") {
        return automation(context.options, context.apps, words);
    }
    if (command == "ignore" || command == "unignore" || command == "forget") {
        return changeListing(context.registry, words);
    }
    if (command == "scan") {
        return scan(words);
    }
    if (command == "bluetooth") {
        return bluetooth(words);
    }
    return failure("unknown command: " + command);
}

std::string describeOutcome(const StationRegistry& registry, const Outcome& outcome) {
    const Station* station = registry.find(outcome.station);
    const std::string who = station ? label(*station) : outcome.station;
    return who + ": " + outcome.message;
}

}

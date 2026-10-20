#include "state_store.hpp"

#include <system_error>

#include "text.hpp"

namespace bsc {

namespace {

constexpr int kStateVersion = 2;
constexpr int kMaxBackups = 99;

std::filesystem::path freeBackupPath(const std::filesystem::path& path) {
    std::filesystem::path backup = path;
    backup += ".broken";
    for (int number = 2; number <= kMaxBackups; ++number) {
        std::error_code ec;
        if (!std::filesystem::exists(backup, ec) && !ec) {
            return backup;
        }
        backup = path;
        backup += ".broken." + std::to_string(number);
    }
    return backup;
}

Json::Value stationJson(const StationRecord& record) {
    Json::Value entry(Json::objectValue);
    entry["name"] = record.name;
    entry["address"] = record.address;
    entry["nickname"] = record.nickname;
    entry["channel"] = record.channel ? Json::Value(*record.channel) : Json::Value(Json::nullValue);
    entry["ignored"] = record.ignored;
    entry["first_seen"] = record.firstSeenWall;
    entry["last_seen"] = record.lastSeenWall;
    entry["last_power"] = record.lastPowerByte ? Json::Value(static_cast<int>(*record.lastPowerByte))
                                               : Json::Value(Json::nullValue);
    return entry;
}

std::optional<StationRecord> stationFrom(const Json::Value& entry, const Logger& warn) {
    const std::string name = textMember(entry, "name");
    if (!isLighthouseName(name)) {
        if (warn) {
            warn("state.json: skipping entry without valid station name");
        }
        return std::nullopt;
    }
    StationRecord record;
    record.name = name;
    record.address = textMember(entry, "address");
    const std::string nickname = textMember(entry, "nickname");
    if (nickname.empty() || isValidNickname(nickname)) {
        record.nickname = nickname;
    } else if (warn) {
        warn("state.json: dropping invalid nickname of " + record.name);
    }
    const std::optional<long long> channel = integerMember(entry, "channel");
    if (channel && *channel >= 1 && *channel <= 16) {
        record.channel = static_cast<int>(*channel);
    }
    record.ignored = boolMember(entry, "ignored");
    record.firstSeenWall = numberMember(entry, "first_seen").value_or(0.0);
    record.lastSeenWall = numberMember(entry, "last_seen").value_or(0.0);
    const std::optional<long long> power = integerMember(entry, "last_power");
    if (power && *power >= 0 && *power <= 255) {
        record.lastPowerByte = static_cast<uint8_t>(*power);
    }
    return record;
}

SavedState stateFrom(const Json::Value& root, const AppList& apps, const Logger& warn) {
    const std::optional<long long> version = integerMember(root, "version");
    if (version && *version > kStateVersion && warn) {
        warn("state.json was written by a newer version; trying to recover");
    }
    SavedState state;
    for (const Json::Value& entry : member(root, "stations")) {
        if (std::optional<StationRecord> record = stationFrom(entry, warn)) {
            state.stations.push_back(std::move(*record));
        }
    }
    state.automation = optionsFromJson(member(root, "automation"), apps);
    state.bootId = textMember(root, "boot_id");
    return state;
}

}

std::string formatState(const SavedState& state) {
    Json::Value root(Json::objectValue);
    root["version"] = kStateVersion;
    Json::Value stations(Json::arrayValue);
    for (const StationRecord& record : state.stations) {
        stations.append(stationJson(record));
    }
    root["stations"] = stations;
    root["automation"] = optionsToJson(state.automation);
    root["boot_id"] = state.bootId;
    return writeJson(root, JsonLayout::Indented) + "\n";
}

std::optional<SavedState> parseState(const std::string& text, const AppList& apps, const Logger& warn) {
    std::string error;
    const std::optional<Json::Value> root = parseJson(text, &error);
    if (!root || !root->isObject()) {
        if (warn) {
            warn("state.json is not valid JSON (" + (root ? std::string("not an object") : error) + ")");
        }
        return std::nullopt;
    }
    return stateFrom(*root, apps, warn);
}

LoadedState loadState(const std::filesystem::path& path, const AppList& apps, const Logger& log) {
    LoadedState loaded;
    const JsonFile file = readJsonFile(path);
    if (file.status == JsonFileStatus::Missing) {
        return loaded;
    }
    if (file.status == JsonFileStatus::Unreadable) {
        loaded.how = StateLoad::Unreadable;
        loaded.problem = "failed to read state.json (" + file.error + ")";
        log(loaded.problem);
        return loaded;
    }
    if (file.status == JsonFileStatus::Invalid || !file.value.isObject()) {
        std::error_code ec;
        std::filesystem::rename(path, freeBackupPath(path), ec);
        loaded.problem = "state.json is invalid (" +
                         (file.status == JsonFileStatus::Invalid ? file.error : std::string("not an object")) + ")";
        loaded.how = ec ? StateLoad::Unreadable : StateLoad::Recovered;
        log(loaded.problem);
        return loaded;
    }
    loaded.how = StateLoad::Loaded;
    loaded.state = stateFrom(file.value, apps, log);
    return loaded;
}

bool saveState(const std::filesystem::path& path, const SavedState& state, const Logger& log) {
    std::string error;
    WriteOptions options;
    options.durable = true;
    if (!writeFileAtomically(path, formatState(state), &error, options)) {
        log("failed to save state: " + error);
        return false;
    }
    return true;
}

}

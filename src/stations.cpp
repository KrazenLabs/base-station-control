#include "stations.hpp"

#include <algorithm>

#include "text.hpp"

namespace bsc {

namespace {

bool listedBefore(const Station* a, const Station* b) {
    const int channelA = a->record.channel.value_or(1000);
    const int channelB = b->record.channel.value_or(1000);
    if (channelA != channelB) {
        return channelA < channelB;
    }
    return a->record.name < b->record.name;
}

std::optional<int> channelQuery(std::string_view query) {
    const std::string lower = toLower(trim(query));
    std::string digits;
    if (startsWith(lower, "channel")) {
        digits = trim(std::string_view(lower).substr(7));
    } else if (startsWith(lower, "ch")) {
        digits = trim(std::string_view(lower).substr(2));
    } else {
        digits = lower;
    }
    const std::optional<long long> number = parseInteger(digits);
    if (!number || *number < 1 || *number > 16) {
        return std::nullopt;
    }
    return static_cast<int>(*number);
}

}

std::optional<uint8_t> Station::knownPowerByte() const {
    return livePowerByte ? livePowerByte : record.lastPowerByte;
}

PowerState Station::knownState() const {
    const std::optional<uint8_t> value = knownPowerByte();
    return value ? powerStateFromByte(*value) : PowerState::Unknown;
}

std::string Station::title() const {
    return record.nickname.empty() ? defaultTitle() : record.nickname;
}

std::string Station::defaultTitle() const {
    if (record.channel) {
        return "Channel " + std::to_string(*record.channel);
    }
    return record.name;
}

std::string Station::serial() const {
    return serialFromName(record.name);
}

StationRegistry::StationRegistry(std::vector<StationRecord> records) {
    for (StationRecord& record : records) {
        if (!isLighthouseName(record.name) || find(record.name)) {
            continue;
        }
        Station station;
        station.record = std::move(record);
        stations_.push_back(std::move(station));
    }
}

Station* StationRegistry::findMutable(std::string_view name) {
    for (Station& station : stations_) {
        if (equalsIgnoreCase(station.record.name, name)) {
            return &station;
        }
    }
    return nullptr;
}

const Station* StationRegistry::find(std::string_view name) const {
    for (const Station& station : stations_) {
        if (equalsIgnoreCase(station.record.name, name)) {
            return &station;
        }
    }
    return nullptr;
}

RegistryChange StationRegistry::observe(const Sighting& sighting, double monotonicNow, double wallNow, bool addNew) {
    RegistryChange change = RegistryChange::Live;
    Station* station = findMutable(sighting.name);
    if (!station) {
        if (!addNew) {
            return RegistryChange::None;
        }
        Station added;
        added.record.name = sighting.name;
        added.record.firstSeenWall = wallNow;
        stations_.push_back(std::move(added));
        station = &stations_.back();
        change = RegistryChange::Persistent;
    }
    StationRecord& record = station->record;
    if (!sighting.address.empty() && record.address != sighting.address) {
        record.address = sighting.address;
        change = RegistryChange::Persistent;
    }
    if (sighting.channel && record.channel != sighting.channel) {
        record.channel = sighting.channel;
        change = RegistryChange::Persistent;
    }
    if (sighting.powerByte) {
        if (record.lastPowerByte != sighting.powerByte) {
            record.lastPowerByte = sighting.powerByte;
            change = RegistryChange::Persistent;
        }
        station->livePowerByte = sighting.powerByte;
    }
    if (!sighting.devicePath.empty()) {
        station->devicePath = sighting.devicePath;
    }
    if (sighting.rssi) {
        station->rssi = sighting.rssi;
    }
    station->lastSeenMonotonic = monotonicNow;
    record.lastSeenWall = wallNow;
    return change;
}

std::vector<const Station*> StationRegistry::all() const {
    std::vector<const Station*> result;
    for (const Station& station : stations_) {
        result.push_back(&station);
    }
    std::sort(result.begin(), result.end(), listedBefore);
    return result;
}

std::vector<const Station*> StationRegistry::listed() const {
    std::vector<const Station*> result;
    for (const Station* station : all()) {
        if (!station->record.ignored) {
            result.push_back(station);
        }
    }
    return result;
}

std::vector<const Station*> StationRegistry::lookup(std::string_view query) const {
    const std::string wanted = trim(query);
    std::vector<const Station*> matches;
    if (wanted.empty()) {
        return matches;
    }
    for (const Station* station : all()) {
        const StationRecord& record = station->record;
        if (equalsIgnoreCase(record.name, wanted) || equalsIgnoreCase(station->serial(), wanted) ||
            (!record.nickname.empty() && equalsIgnoreCase(record.nickname, wanted))) {
            matches.push_back(station);
        }
    }
    if (!matches.empty()) {
        return matches;
    }
    if (const std::optional<int> channel = channelQuery(wanted)) {
        for (const Station* station : all()) {
            if (station->record.channel == channel) {
                matches.push_back(station);
            }
        }
    }
    return matches;
}

bool StationRegistry::setNickname(std::string_view name, const std::string& nickname) {
    Station* station = findMutable(name);
    if (!station) {
        return false;
    }
    station->record.nickname = nickname;
    return true;
}

bool StationRegistry::setIgnored(std::string_view name, bool ignored) {
    Station* station = findMutable(name);
    if (!station) {
        return false;
    }
    station->record.ignored = ignored;
    return true;
}

bool StationRegistry::forget(std::string_view name) {
    const auto found = std::find_if(stations_.begin(), stations_.end(), [&](const Station& station) {
        return equalsIgnoreCase(station.record.name, name);
    });
    if (found == stations_.end()) {
        return false;
    }
    stations_.erase(found);
    return true;
}

std::vector<StationRecord> StationRegistry::records() const {
    std::vector<StationRecord> result;
    for (const Station* station : all()) {
        result.push_back(station->record);
    }
    return result;
}

}

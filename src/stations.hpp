#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "lighthouse.hpp"

namespace bsc {

struct StationRecord {
    std::string name;
    std::string address;
    std::string nickname;
    std::optional<int> channel;
    bool ignored = false;
    double firstSeenWall = 0.0;
    double lastSeenWall = 0.0;
    std::optional<uint8_t> lastPowerByte;
};

struct Station {
    StationRecord record;
    std::string devicePath;
    std::optional<int> rssi;
    std::optional<uint8_t> livePowerByte;
    double lastSeenMonotonic = -1.0;

    const std::string& name() const { return record.name; }
    std::optional<uint8_t> knownPowerByte() const;
    PowerState knownState() const;
    std::string title() const;
    std::string defaultTitle() const;
    std::string serial() const;
    bool seenSince(double monotonic) const { return lastSeenMonotonic >= 0.0 && lastSeenMonotonic >= monotonic; }
};

enum class RegistryChange { None, Live, Persistent };

class StationRegistry {
public:
    explicit StationRegistry(std::vector<StationRecord> records = {});

    RegistryChange observe(const Sighting& sighting, double monotonicNow, double wallNow, bool addNew);
    const Station* find(std::string_view name) const;
    std::vector<const Station*> listed() const;
    std::vector<const Station*> all() const;
    std::vector<const Station*> lookup(std::string_view query) const;
    bool setNickname(std::string_view name, const std::string& nickname);
    bool setIgnored(std::string_view name, bool ignored);
    bool forget(std::string_view name);
    std::vector<StationRecord> records() const;

private:
    Station* findMutable(std::string_view name);

    std::vector<Station> stations_;
};

}

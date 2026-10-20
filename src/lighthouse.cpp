#include "lighthouse.hpp"

#include <cctype>

#include "text.hpp"

namespace bsc {

namespace {

constexpr size_t kSerialDigits = 8;
constexpr size_t kChannelOffset = 2;
constexpr size_t kPowerOffset = 4;
constexpr int kMaxChannel = 16;

}

PowerState powerStateFromByte(uint8_t value) {
    switch (value) {
        case 0x00:
            return PowerState::Sleep;
        case 0x02:
            return PowerState::Standby;
        case 0x0b:
            return PowerState::On;
        case 0x01:
        case 0x08:
        case 0x09:
            return PowerState::Booting;
        default:
            return PowerState::Unknown;
    }
}

const char* powerStateName(PowerState state) {
    switch (state) {
        case PowerState::Sleep: return "sleep";
        case PowerState::Standby: return "standby";
        case PowerState::Booting: return "booting";
        case PowerState::On: return "on";
        default: return "unknown";
    }
}

const char* powerStateText(PowerState state) {
    switch (state) {
        case PowerState::Sleep: return "Sleeping";
        case PowerState::Standby: return "Standby";
        case PowerState::Booting: return "Starting up";
        case PowerState::On: return "Active";
        default: return "Unknown state";
    }
}

bool countsAsOn(PowerState state) {
    return state == PowerState::On || state == PowerState::Booting;
}

uint8_t commandByte(PowerCommand command) {
    switch (command) {
        case PowerCommand::On: return 0x01;
        case PowerCommand::Standby: return 0x02;
        default: return 0x00;
    }
}

bool satisfies(PowerState state, PowerCommand command) {
    switch (command) {
        case PowerCommand::On: return countsAsOn(state);
        case PowerCommand::Standby: return state == PowerState::Standby;
        default: return state == PowerState::Sleep;
    }
}

bool isOff(PowerState state) {
    return state == PowerState::Sleep || state == PowerState::Standby;
}

bool isLighthouseName(std::string_view name) {
    if (name.size() != 4 + kSerialDigits || !startsWith(name, kLighthouseNamePrefix)) {
        return false;
    }
    for (size_t i = 4; i < name.size(); ++i) {
        if (std::isxdigit(static_cast<unsigned char>(name[i])) == 0) {
            return false;
        }
    }
    return true;
}

std::string serialFromName(std::string_view name) {
    return isLighthouseName(name) ? std::string(name.substr(4)) : std::string(name);
}

std::optional<Sighting> sightingFrom(const Advert& advert) {
    if (!isLighthouseName(advert.name)) {
        return std::nullopt;
    }
    Sighting sighting;
    sighting.name = advert.name;
    sighting.address = advert.address;
    sighting.devicePath = advert.devicePath;
    sighting.rssi = advert.rssi;
    if (advert.valveData && advert.valveData->size() > kPowerOffset) {
        const std::vector<uint8_t>& data = *advert.valveData;
        const int channel = data[kChannelOffset];
        if (channel >= 1 && channel <= kMaxChannel) {
            sighting.channel = channel;
        }
        sighting.powerByte = data[kPowerOffset];
    }
    return sighting;
}

}

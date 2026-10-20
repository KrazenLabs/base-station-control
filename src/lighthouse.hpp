#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace bsc {

constexpr uint16_t kValveCompanyId = 0x055D;
constexpr const char* kLighthouseNamePrefix = "LHB-";
constexpr const char* kPowerCharacteristicUuid = "00001525-1212-efde-1523-785feabcd124";
constexpr const char* kIdentifyCharacteristicUuid = "00008421-1212-efde-1523-785feabcd124";

enum class PowerState { Unknown, Sleep, Standby, Booting, On };
enum class PowerCommand { On, Sleep, Standby };

PowerState powerStateFromByte(uint8_t value);
const char* powerStateName(PowerState state);
const char* powerStateText(PowerState state);
bool countsAsOn(PowerState state);
uint8_t commandByte(PowerCommand command);
bool satisfies(PowerState state, PowerCommand command);
bool isOff(PowerState state);

bool isLighthouseName(std::string_view name);
std::string serialFromName(std::string_view name);

struct Advert {
    std::string devicePath;
    std::string address;
    std::string name;
    std::optional<int> rssi;
    std::optional<std::vector<uint8_t>> valveData;
};

struct Sighting {
    std::string name;
    std::string address;
    std::string devicePath;
    std::optional<int> rssi;
    std::optional<int> channel;
    std::optional<uint8_t> powerByte;
};

std::optional<Sighting> sightingFrom(const Advert& advert);

}

#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include <json/json.h>

#include "adapter_state.hpp"
#include "automation.hpp"
#include "controller.hpp"
#include "lighthouse.hpp"
#include "stations.hpp"
#include "watched_apps.hpp"

namespace bsc {

enum class Presence { Live, Checking, Missing, Remembered };
enum class Tone { None, On, Busy, Error };
enum class Led { Off, On, Booting, Busy, Error, Missing };
enum class IconMode { BluetoothOff, None, Error, Busy, On, Off };

const char* presenceName(Presence presence);
const char* toneName(Tone tone);
const char* ledName(Led led);
const char* iconModeName(IconMode mode);
const char* jobPhaseText(JobPhase phase);

struct ViewInputs {
    AdapterState adapter = AdapterState::Unknown;
    bool scanning = false;
    double scanningSince = 0.0;
    double now = 0.0;
    double missingAfterSeconds = 12.0;
    std::string note;
    Tone noteTone = Tone::None;
    std::string handledSession;
    int handledSeq = 0;
    AutomationOptions automation;
    AppList apps;
    std::string sleepHookError;
    std::string shutdownHookError;
    std::vector<uint32_t> installedApps;
};

struct StationView {
    std::string id;
    std::string title;
    std::string serial;
    PowerState state = PowerState::Unknown;
    Presence presence = Presence::Remembered;
    bool on = false;
    bool busy = false;
    bool enabled = false;
    std::string caption;
    Tone tone = Tone::None;
    Led led = Led::Off;
    std::string error;
};

StationView describeStation(const Station& station, const Controller& controller, const ViewInputs& inputs);
std::vector<StationView> describeStations(const StationRegistry& registry, const Controller& controller,
                                          const ViewInputs& inputs);
std::string failureNote(const std::vector<StationView>& views);
Json::Value optionsData(const ViewInputs& inputs);
Json::Value widgetData(const StationRegistry& registry, const Controller& controller, const ViewInputs& inputs);
std::string bluetoothFailureReason(const std::string& errorText);

}

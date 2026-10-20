#include "widget_model.hpp"

#include <algorithm>

#include "text.hpp"
#include "widget_bridge.hpp"

namespace bsc {

namespace {

std::string shortError(const std::string& message) {
    return capitalizeFirst(message.substr(0, message.find(" (")));
}

Led ledFor(PowerState state) {
    switch (state) {
        case PowerState::On: return Led::On;
        case PowerState::Booting: return Led::Booting;
        default: return Led::Off;
    }
}

Tone toneFor(PowerState state) {
    switch (state) {
        case PowerState::On: return Tone::On;
        case PowerState::Booting: return Tone::Busy;
        default: return Tone::None;
    }
}

Json::Value emptyBlock(const std::string& title, const std::string& text) {
    Json::Value block(Json::objectValue);
    block["title"] = title;
    block["text"] = text;
    return block;
}

Presence presenceOf(const Station& station, const ViewInputs& inputs) {
    const bool heardRecently =
        station.lastSeenMonotonic >= 0.0 && inputs.now - station.lastSeenMonotonic <= inputs.missingAfterSeconds;
    if (heardRecently) {
        return Presence::Live;
    }
    if (!inputs.scanning) {
        return Presence::Remembered;
    }
    return inputs.now - inputs.scanningSince >= inputs.missingAfterSeconds ? Presence::Missing : Presence::Checking;
}

std::string hookError(const OptionInfo& info, const ViewInputs& inputs) {
    switch (info.id.kind) {
        case OptionKind::OffAtSleep: return inputs.sleepHookError;
        case OptionKind::OffAtShutdown: return inputs.shutdownHookError;
        default: return "";
    }
}

bool optionShown(const OptionInfo& info, bool on, const ViewInputs& inputs) {
    const bool forApp = info.id.kind == OptionKind::OnAppLaunch || info.id.kind == OptionKind::OffAppQuit;
    if (!forApp || on) {
        return true;
    }
    return std::find(inputs.installedApps.begin(), inputs.installedApps.end(), info.id.appId) != inputs.installedApps.end();
}

IconMode iconModeFor(AdapterState adapter, int known, bool anyError, bool anyBusy, int onCount) {
    if (adapter == AdapterState::Off || adapter == AdapterState::Missing) return IconMode::BluetoothOff;
    if (known == 0) return IconMode::None;
    if (anyError) return IconMode::Error;
    if (anyBusy) return IconMode::Busy;
    return onCount > 0 ? IconMode::On : IconMode::Off;
}

}

const char* presenceName(Presence presence) {
    switch (presence) {
        case Presence::Live: return "live";
        case Presence::Checking: return "checking";
        case Presence::Missing: return "missing";
        case Presence::Remembered: return "remembered";
    }
    return "remembered";
}

const char* toneName(Tone tone) {
    switch (tone) {
        case Tone::None: return "";
        case Tone::On: return "on";
        case Tone::Busy: return "busy";
        case Tone::Error: return "error";
    }
    return "";
}

const char* ledName(Led led) {
    switch (led) {
        case Led::Off: return "off";
        case Led::On: return "on";
        case Led::Booting: return "booting";
        case Led::Busy: return "busy";
        case Led::Error: return "error";
        case Led::Missing: return "missing";
    }
    return "off";
}

const char* iconModeName(IconMode mode) {
    switch (mode) {
        case IconMode::BluetoothOff: return "bt-off";
        case IconMode::None: return "none";
        case IconMode::Error: return "error";
        case IconMode::Busy: return "busy";
        case IconMode::On: return "on";
        case IconMode::Off: return "off";
    }
    return "none";
}

const char* jobPhaseText(JobPhase phase) {
    switch (phase) {
        case JobPhase::Connecting: return "Connecting…";
        case JobPhase::Retrying: return "Retrying…";
        case JobPhase::Sending: return "Sending…";
        case JobPhase::Starting: return "Starting…";
        case JobPhase::ShuttingDown: return "Shutting down…";
    }
    return "Connecting…";
}

StationView describeStation(const Station& station, const Controller& controller, const ViewInputs& inputs) {
    StationView view;
    view.id = station.name();
    view.title = station.title();
    view.serial = station.serial();
    view.presence = presenceOf(station, inputs);
    view.state = station.knownState();
    view.on = countsAsOn(view.state);
    view.enabled = inputs.adapter == AdapterState::On && view.presence != Presence::Missing;

    std::string text;
    if (const std::optional<JobInfo> job = controller.job(station.name())) {
        view.busy = true;
        if (!job->identify) {
            view.on = job->command == PowerCommand::On;
        }
        text = jobPhaseText(job->phase);
        view.tone = Tone::Busy;
        view.led = Led::Busy;
    } else if (const std::optional<std::string> error = controller.recentError(station.name(), inputs.now)) {
        view.error = *error;
        text = shortError(*error);
        view.tone = Tone::Error;
        view.led = Led::Error;
        if (view.presence == Presence::Missing) {
            view.on = false;
        }
    } else if (view.presence == Presence::Missing) {
        text = "Not found";
        view.led = Led::Missing;
        view.on = false;
    } else if (view.state == PowerState::Unknown) {
        const std::optional<uint8_t> value = station.knownPowerByte();
        text = value ? "Unknown state " + hexByte(*value) : "Not available";
        view.led = Led::Off;
    } else {
        text = powerStateText(view.state);
        view.led = ledFor(view.state);
        view.tone = toneFor(view.state);
    }
    view.caption = view.title == station.name() ? text : text + " · " + view.serial;
    return view;
}

std::vector<StationView> describeStations(const StationRegistry& registry, const Controller& controller,
                                          const ViewInputs& inputs) {
    std::vector<StationView> views;
    for (const Station* station : registry.listed()) {
        views.push_back(describeStation(*station, controller, inputs));
    }
    return views;
}

std::string failureNote(const std::vector<StationView>& views) {
    std::vector<std::string> parts;
    for (const StationView& view : views) {
        if (!view.error.empty()) {
            parts.push_back(view.title + ": " + view.error);
        }
    }
    return joinWords(parts, "; ");
}

Json::Value optionsData(const ViewInputs& inputs) {
    Json::Value list(Json::arrayValue);
    for (const OptionInfo& info : optionList(inputs.apps)) {
        const bool on = optionValue(inputs.automation, info.id);
        if (!optionShown(info, on, inputs)) {
            continue;
        }
        const std::string error = hookError(info, inputs);
        const bool failed = on && !error.empty();
        Json::Value entry(Json::objectValue);
        entry["key"] = info.key;
        entry["section"] = info.turnsOn ? "Automatically turn on Base Stations" : "Automatically turn off Base Stations";
        entry["title"] = info.title;
        entry["on"] = on;
        entry["detail"] = failed ? "Base Stations may not turn off in time: " + error : "";
        entry["tone"] = toneName(failed ? Tone::Error : Tone::None);
        list.append(entry);
    }
    Json::Value options(Json::objectValue);
    options["items"] = list;
    return options;
}

Json::Value widgetData(const StationRegistry& registry, const Controller& controller, const ViewInputs& inputs) {
    const std::vector<StationView> views = describeStations(registry, controller, inputs);
    const bool bluetoothReady = inputs.adapter == AdapterState::On;
    int onCount = 0;
    bool anyBusy = false;
    bool anyError = false;
    bool canTurnOn = false;
    bool canTurnOff = false;
    Json::Value stations(Json::arrayValue);
    for (const StationView& view : views) {
        const bool present = view.presence != Presence::Missing;
        if (present && countsAsOn(view.state)) {
            ++onCount;
        }
        anyBusy = anyBusy || view.busy || (present && view.state == PowerState::Booting);
        anyError = anyError || !view.error.empty();
        canTurnOn = canTurnOn || (view.enabled && !view.busy && !view.on);
        canTurnOff = canTurnOff || (view.enabled && !view.busy && view.on);
        Json::Value entry(Json::objectValue);
        entry["id"] = view.id;
        entry["title"] = view.title;
        if (const Station* station = registry.find(view.id)) {
            entry["nickname"] = station->record.nickname;
            entry["defaultTitle"] = station->defaultTitle();
        }
        entry["caption"] = view.caption;
        entry["tone"] = toneName(view.tone);
        entry["led"] = ledName(view.led);
        entry["on"] = view.on;
        entry["busy"] = view.busy;
        entry["enabled"] = view.enabled;
        stations.append(entry);
    }
    const int known = static_cast<int>(views.size());

    const IconMode mode = iconModeFor(inputs.adapter, known, anyError, anyBusy, onCount);
    Json::Value icon(Json::objectValue);
    icon["mode"] = iconModeName(mode);
    icon["count"] = mode == IconMode::BluetoothOff ? 0 : onCount;
    icon["label"] = mode == IconMode::BluetoothOff ? std::string("Base stations: Bluetooth disabled")
                    : mode == IconMode::None       ? std::string("Base stations: none found")
                                                   : "Base stations: " + std::to_string(onCount) + " of " +
                                                       std::to_string(known) + " active";

    Json::Value data(Json::objectValue);
    data["stations"] = stations;
    data["icon"] = icon;
    data["allOn"]["enabled"] = bluetoothReady && canTurnOn;
    data["allOff"]["enabled"] = bluetoothReady && canTurnOff;

    data["extra"] = Json::Value(Json::nullValue);
    data["empty"] = Json::Value(Json::nullValue);
    if (inputs.adapter == AdapterState::Off) {
        data["empty"] = emptyBlock("Bluetooth is disabled", "Bluetooth must be enabled to control Base Stations.");
        data["extra"]["op"] = actionKindName(ActionKind::BluetoothOn);
        data["extra"]["label"] = "Enable Bluetooth";
    } else if (inputs.adapter == AdapterState::Missing) {
        data["empty"] = emptyBlock("No Bluetooth adapter", "The Bluetooth adapter may be malfunctioning.");
    } else if (inputs.adapter == AdapterState::Unknown) {
        data["empty"] = emptyBlock("Waiting on Bluetooth", "Bluetooth service is starting up.");
    } else if (known == 0) {
        data["empty"] = emptyBlock(inputs.scanning ? "Searching for base stations…" : "No base stations found yet",
                                   "Make sure your Base Stations are powered and in range.");
    }

    std::vector<std::string> notes;
    Tone tone = Tone::None;
    if (!inputs.note.empty()) {
        notes.push_back(inputs.note);
        tone = inputs.noteTone;
    }
    const std::string failures = failureNote(views);
    if (!failures.empty()) {
        notes.push_back(failures);
        tone = Tone::Error;
    }
    data["note"] = joinWords(notes, " ");
    data["noteTone"] = toneName(tone);
    data["options"] = optionsData(inputs);
    Json::Value watch(Json::arrayValue);
    for (const uint32_t id : appIds(inputs.apps)) {
        watch.append(id);
    }
    data["watchApps"] = watch;
    data["handled"]["session"] = inputs.handledSession;
    data["handled"]["seq"] = inputs.handledSeq;
    return data;
}

std::string bluetoothFailureReason(const std::string& errorText) {
    const std::string text = toLower(errorText);
    const auto has = [&text](const char* part) { return text.find(part) != std::string::npos; };
    if (has("no bluetooth adapter")) {
        return "no Bluetooth adapter";
    }
    if (has("rfkill") || has("blocked")) {
        return "blocked by the headset's settings";
    }
    if (has("timeout") || has("noreply")) {
        return "no response in time";
    }
    if (has("inprogress") || has("busy")) {
        return "Bluetooth is busy";
    }
    if (has("notready")) {
        return "Bluetooth is not ready";
    }
    if (has("disconnected") || has("serviceunknown") || has("unknownobject") || has("notavailable")) {
        return "Bluetooth service not available";
    }
    if (has("accessdenied") || has("notauthorized") || has("notpermitted") || has("authfailed")) {
        return "permission denied";
    }
    return "unknown error";
}

}

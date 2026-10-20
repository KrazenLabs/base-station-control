#include "widget_bridge.hpp"

#include <json/json.h>

#include "common.hpp"
#include "text.hpp"

namespace bsc {

namespace {

constexpr const char* kInstallNeeded = "install-needed";

struct StateName {
    WidgetState state;
    const char* name;
};

constexpr StateName kStateNames[] = {
    {WidgetState::Off, "off"},
    {WidgetState::Waiting, "waiting"},
    {WidgetState::Unreachable, "unreachable"},
    {WidgetState::NoPage, "no-page"},
    {WidgetState::Lost, "lost"},
    {WidgetState::ScriptError, "script-error"},
    {WidgetState::BadReport, "bad-report"},
    {WidgetState::NoAnchor, "no-anchor"},
    {WidgetState::NoPopup, "no-popup"},
    {WidgetState::Shown, "shown"},
};

struct ActionName {
    ActionKind kind;
    const char* name;
};

constexpr ActionName kActionNames[] = {
    {ActionKind::Set, "set"},
    {ActionKind::AllOn, "all-on"},
    {ActionKind::AllOff, "all-off"},
    {ActionKind::Rename, "rename"},
    {ActionKind::Option, "option"},
    {ActionKind::BluetoothOn, "bluetooth-on"},
};

std::string jsonString(const std::string& text) {
    return writeJson(Json::Value(text), JsonLayout::Compact, JsonCharacters::AsciiEscapes);
}

std::string dataOrNull(const std::string& dataJson) {
    return dataJson.empty() ? "null" : dataJson;
}

WidgetAction actionFrom(const Json::Value& item) {
    WidgetAction action;
    action.kind = actionKindFrom(textMember(item, "op"));
    action.station = textMember(item, "id");
    action.on = boolMember(item, "on");
    action.key = textMember(item, "key");
    action.text = textMember(item, "name");
    action.session = textMember(item, "session");
    const std::optional<long long> seq = integerMember(item, "seq");
    action.seq = seq && *seq > 0 && *seq < 0x7FFFFFFF ? static_cast<int>(*seq) : 0;
    return action;
}

}

const char* widgetStateName(WidgetState state) {
    for (const StateName& entry : kStateNames) {
        if (entry.state == state) {
            return entry.name;
        }
    }
    return "waiting";
}

std::optional<WidgetState> widgetStateFromName(std::string_view name) {
    for (const StateName& entry : kStateNames) {
        if (name == entry.name) {
            return entry.state;
        }
    }
    return std::nullopt;
}

const char* actionKindName(ActionKind kind) {
    for (const ActionName& entry : kActionNames) {
        if (entry.kind == kind) {
            return entry.name;
        }
    }
    return "";
}

ActionKind actionKindFrom(std::string_view name) {
    for (const ActionName& entry : kActionNames) {
        if (name == entry.name) {
            return entry.kind;
        }
    }
    return ActionKind::Unknown;
}

std::string widgetVersion(const std::string& script) {
    return fnv1aHex(script);
}

std::string installExpression(const std::string& script, const std::string& version, const std::string& dataJson) {
    return "(" + trim(script) + ")(" + jsonString(version) + ").update(" + dataOrNull(dataJson) + ")";
}

std::string updateExpression(const std::string& version, const std::string& dataJson) {
    return "(window.__bsc && window.__bsc.version === " + jsonString(version) + ") ? window.__bsc.update(" +
           dataOrNull(dataJson) + ") : \"" + kInstallNeeded + "\"";
}

std::string removeExpression() {
    return "(window.__bsc && typeof window.__bsc.dispose === 'function') ? window.__bsc.dispose() : 'absent'";
}

WidgetReport parseWidgetReport(const std::string& value) {
    WidgetReport report;
    if (value == kInstallNeeded) {
        report.installNeeded = true;
        return report;
    }
    const std::optional<Json::Value> parsed = parseJson(value);
    if (!parsed || !parsed->isObject()) {
        return report;
    }
    const Json::Value& root = *parsed;
    report.valid = true;
    report.anchored = textMember(root, "state") == "shown";
    report.visible = boolMember(root, "visible");
    report.popupOpen = boolMember(root, "popupOpen");
    report.popupAttached = textMember(root, "popup") == "attached";
    for (const Json::Value& item : member(root, "actions")) {
        if (item.isObject()) {
            WidgetAction action = actionFrom(item);
            if (action.kind != ActionKind::Unknown) {
                report.actions.push_back(std::move(action));
            }
        }
    }
    const Json::Value& apps = member(root, "apps");
    if (apps.isArray()) {
        report.appsReported = true;
        for (const Json::Value& item : apps) {
            const std::optional<long long> id = integerMember(item, "id");
            if (!id || *id <= 0 || *id > 0xFFFFFFFFLL) {
                continue;
            }
            AppReport app;
            app.id = static_cast<uint32_t>(*id);
            app.known = boolMember(item, "known");
            app.installed = boolMember(item, "installed");
            app.running = boolMember(item, "running");
            report.apps.push_back(app);
        }
    }
    return report;
}

}

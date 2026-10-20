#include "steam_widget.hpp"

namespace bsc {

namespace {

constexpr const char* kBarOverlayKey = "valve.steam.gamepadui.bar";
constexpr double kRetrySeconds = 5.0;
constexpr double kReconnectSeconds = 1.0;

}

SteamWidget::SteamWidget(int port, std::string script, Logger log)
    : port_(port), script_(std::move(script)), version_(widgetVersion(script_)), log_(std::move(log)) {}

void SteamWidget::report(WidgetState state, const std::string& message) {
    if (reported_ && state == state_) {
        return;
    }
    reported_ = true;
    state_ = state;
    if (!message.empty()) {
        log_(message);
    }
}

void SteamWidget::drop(double monotonicNow, WidgetState state, const std::string& message) {
    session_.reset();
    nextAttempt_ = monotonicNow + kReconnectSeconds;
    report(state, message);
}

bool SteamWidget::ensureSession(double monotonicNow, bool waitForRetry) {
    if (session_) {
        return true;
    }
    if (waitForRetry && monotonicNow < nextAttempt_) {
        return false;
    }
    nextAttempt_ = monotonicNow + kRetrySeconds;
    std::string error;
    const std::optional<std::string> targets = httpGetLocal(port_, "/json", &error);
    if (!targets) {
        report(WidgetState::Unreachable, "Cannot reach Steam's UI debugger on 127.0.0.1:" + std::to_string(port_) + " (" +
                                             error + "); waiting for Steam");
        return false;
    }
    const std::optional<std::string> path = findOverlayPagePath(*targets, kBarOverlayKey, false);
    if (!path) {
        report(WidgetState::NoPage, "Waiting for Steam's VR bar");
        return false;
    }
    session_ = DevToolsSession::open(port_, *path, &error);
    if (!session_) {
        report(WidgetState::Unreachable, "could not attach to Steam's VR bar: " + error);
        return false;
    }
    if (!session_->addBinding(kNotifyBinding)) {
        log_("failed to set up click notifications; clicks are picked up at the next poll");
    }
    return true;
}

bool SteamWidget::takePing() {
    if (!session_) {
        return false;
    }
    if (!session_->drainEvents()) {
        session_.reset();
        nextAttempt_ = 0.0;
        report(WidgetState::Lost, "lost the connection to Steam's VR bar, trying to reconnect");
        return true;
    }
    return session_->takeBindingCalls() > 0;
}

std::optional<WidgetReport> SteamWidget::exchange(const std::string& dataJson, double monotonicNow) {
    if (!dataJson.empty()) {
        lastData_ = dataJson;
    }
    if (!ensureSession(monotonicNow, true)) {
        return std::nullopt;
    }
    std::optional<EvaluationResult> result = session_->evaluate(updateExpression(version_, dataJson));
    if (!result) {
        drop(monotonicNow, WidgetState::Lost, "lost the connection to Steam's VR bar, trying to reconnect");
        return std::nullopt;
    }
    if (result->ok && parseWidgetReport(result->value).installNeeded) {
        result = session_->evaluate(installExpression(script_, version_, lastData_));
        if (!result) {
            drop(monotonicNow, WidgetState::Lost, "lost the connection to Steam's VR bar while installing, trying to reconnect");
            return std::nullopt;
        }
        if (result->ok) {
            log_("base station widget installed (version " + version_.substr(0, 8) + ")");
        }
    }
    if (!result->ok) {
        report(WidgetState::ScriptError, "widget script failed: " + result->value);
        return std::nullopt;
    }
    WidgetReport parsed = parseWidgetReport(result->value);
    if (!parsed.valid) {
        report(WidgetState::BadReport, "unexpected response from widget: " + result->value.substr(0, 200));
        return std::nullopt;
    }
    if (!parsed.anchored) {
        report(WidgetState::NoAnchor, "waiting for Steam battery widget");
    } else if (!parsed.popupAttached) {
        report(WidgetState::NoPopup, "Quick Access popup is not reachable");
    } else {
        report(WidgetState::Shown, "started successfully");
    }
    return parsed;
}

void SteamWidget::remove() {
    if (!ensureSession(0.0, false)) {
        return;
    }
    session_->evaluate(removeExpression());
    session_.reset();
}

}

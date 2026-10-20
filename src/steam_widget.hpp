#pragma once

#include <memory>
#include <optional>
#include <string>

#include "common.hpp"
#include "widget_bridge.hpp"
#include "widget_link.hpp"

namespace bsc {

class SteamWidget : public WidgetLink {
public:
    SteamWidget(int port, std::string script, Logger log);

    int fd() const override { return session_ ? session_->fd() : -1; }
    bool connected() const override { return session_ != nullptr; }
    bool takePing() override;
    std::optional<WidgetReport> exchange(const std::string& dataJson, double monotonicNow) override;
    void remove() override;
    WidgetState state() const override { return state_; }

private:
    bool ensureSession(double monotonicNow, bool waitForRetry);
    void drop(double monotonicNow, WidgetState state, const std::string& message);
    void report(WidgetState state, const std::string& message);

    int port_;
    std::string script_;
    std::string version_;
    Logger log_;
    std::unique_ptr<DevToolsSession> session_;
    double nextAttempt_ = 0.0;
    WidgetState state_ = WidgetState::Waiting;
    bool reported_ = false;
    std::string lastData_;
};

}

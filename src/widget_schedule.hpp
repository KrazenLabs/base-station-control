#pragma once

namespace bsc {

struct WidgetTiming {
    double activePollSeconds = 0.5;
    double idlePollSeconds = 8.0;
    double minimumGapSeconds = 0.15;
    double dataKeepaliveSeconds = 5.0;
};

class WidgetSchedule {
public:
    explicit WidgetSchedule(WidgetTiming timing = {});

    bool due(double now, bool ping, bool dataChanged, bool active) const;
    bool sendData(double now, bool dataChanged, bool connected) const;
    double waitSeconds(double now, bool dataChanged, bool active) const;
    void exchanged(double now, bool sentData);

private:
    double pollInterval(bool active) const;

    WidgetTiming timing_;
    double lastExchange_ = -1e18;
    double sentDataAt_ = -1e18;
};

}

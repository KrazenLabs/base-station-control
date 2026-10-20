#include "widget_schedule.hpp"

#include <algorithm>

namespace bsc {

WidgetSchedule::WidgetSchedule(WidgetTiming timing) : timing_(timing) {}

double WidgetSchedule::pollInterval(bool active) const {
    return active ? timing_.activePollSeconds : timing_.idlePollSeconds;
}

bool WidgetSchedule::due(double now, bool ping, bool dataChanged, bool active) const {
    const double sinceLast = now - lastExchange_;
    return ping || (dataChanged && sinceLast >= timing_.minimumGapSeconds) || sinceLast >= pollInterval(active);
}

bool WidgetSchedule::sendData(double now, bool dataChanged, bool connected) const {
    return dataChanged || !connected || now - sentDataAt_ >= timing_.dataKeepaliveSeconds;
}

double WidgetSchedule::waitSeconds(double now, bool dataChanged, bool active) const {
    double next = lastExchange_ + pollInterval(active);
    if (dataChanged) {
        next = std::min(next, lastExchange_ + timing_.minimumGapSeconds);
    }
    return std::max(0.0, next - now);
}

void WidgetSchedule::exchanged(double now, bool sentData) {
    lastExchange_ = now;
    if (sentData) {
        sentDataAt_ = now;
    }
}

}

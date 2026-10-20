#pragma once

#include <optional>
#include <string>

#include "widget_bridge.hpp"

namespace bsc {

class WidgetLink {
public:
    virtual ~WidgetLink() = default;

    virtual int fd() const = 0;
    virtual bool connected() const = 0;
    virtual bool takePing() = 0;
    virtual std::optional<WidgetReport> exchange(const std::string& dataJson, double monotonicNow) = 0;
    virtual void remove() = 0;
    virtual WidgetState state() const = 0;
};

}

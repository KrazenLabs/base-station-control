#pragma once

#include <cstdint>
#include <functional>
#include <memory>
#include <string>

#include "dbus_wire.hpp"

namespace bsc {

inline constexpr const char* kBusDaemonName = "org.freedesktop.DBus";
inline constexpr const char* kBusDaemonPath = "/org/freedesktop/DBus";

class Bus {
public:
    using ReplyHandler = std::function<void(const DBusMessage&)>;
    using SignalHandler = std::function<void(const DBusMessage&)>;

    virtual ~Bus() = default;

    virtual int fd() const = 0;
    virtual bool alive() const = 0;
    virtual bool passesFds() const = 0;
    virtual void call(DBusMessage message, double timeoutSeconds, ReplyHandler handler) = 0;
    virtual void addMatch(const std::string& rule) = 0;
    virtual void setSignalHandler(SignalHandler handler) = 0;
    virtual bool processIncoming(double monotonicNow) = 0;
    virtual void expireCalls(double monotonicNow) = 0;
};

using BusOpener = std::function<std::unique_ptr<Bus>(double monotonicNow, std::string* error)>;

enum class OwnerChange { None, Appeared, Vanished };

class NameOwnerWatch {
public:
    explicit NameOwnerWatch(std::string name);

    const std::string& name() const { return name_; }
    const std::string& owner() const { return owner_; }

    void start(Bus& bus);
    void reset();
    void learnFrom(const DBusMessage& reply);
    OwnerChange update(const DBusMessage& message);
    bool sentBy(const DBusMessage& message) const;

private:
    std::string name_;
    std::string owner_;
    uint64_t generation_ = 0;
};

}

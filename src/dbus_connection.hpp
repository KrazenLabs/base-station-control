#pragma once

#include <cstdint>
#include <deque>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <string>

#include "dbus_bus.hpp"
#include "dbus_wire.hpp"

namespace bsc {

extern const char* const kDBusTimeoutError;
extern const char* const kDBusDisconnectedError;

class DBusConnection : public Bus {
public:
    static std::unique_ptr<Bus> openSystemBus(double monotonicNow, std::string* error);
    ~DBusConnection() override;

    DBusConnection(const DBusConnection&) = delete;
    DBusConnection& operator=(const DBusConnection&) = delete;

    int fd() const override { return socket_; }
    bool alive() const override { return socket_ >= 0; }
    bool passesFds() const override { return passesFds_; }

    void call(DBusMessage message, double timeoutSeconds, ReplyHandler handler) override;
    void addMatch(const std::string& rule) override;
    void setSignalHandler(SignalHandler handler) override { signalHandler_ = std::move(handler); }
    bool processIncoming(double monotonicNow) override;
    void expireCalls(double monotonicNow) override;

    static std::unique_ptr<DBusConnection> connectTo(const std::string& socketPath, double monotonicNow, std::string* error);

private:
    struct PendingCall {
        ReplyHandler handler;
        double deadline = 0.0;
    };

    explicit DBusConnection(int socket) : socket_(socket) {}
    bool authenticate(std::string* error);
    bool hello(std::string* error);
    bool sendMessage(const DBusMessage& message, uint32_t serial);
    bool sendRaw(const std::string& bytes);
    bool readBlocking(std::string* into);
    bool readAuthLine(std::string* line);
    void receiveFds(const void* control, size_t length);
    void closeReceivedFds();
    bool decodeBuffered(std::vector<DBusMessage>* messages);
    void dispatch(const DBusMessage& message);
    void answerMethodCall(const DBusMessage& call);
    void close(const std::string& reason);

    int socket_ = -1;
    uint32_t nextSerial_ = 1;
    double now_ = 0.0;
    std::string buffered_;
    std::deque<int> receivedFds_;
    bool passesFds_ = false;
    std::string closeReason_;
    std::map<uint32_t, PendingCall> pending_;
    SignalHandler signalHandler_;
};

}

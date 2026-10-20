#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "common.hpp"
#include "dbus_bus.hpp"

namespace bsc {

enum class PowerEventKind { Sleeping, Resumed, ShuttingDown, ShutdownCancelled };

struct PowerEvent {
    PowerEventKind kind = PowerEventKind::Sleeping;
    std::string shutdownType;
};

enum class PowerLock { Sleep, Shutdown };

struct PowerLockStatus {
    bool wanted = false;
    bool held = false;
    bool paused = false;
    std::string error;
};

std::string describeLock(const PowerLockStatus& status);

std::string currentBootId();
bool isRestartType(const std::string& shutdownType);

class PowerEvents {
public:
    PowerEvents(BusOpener openBus, Logger log);
    ~PowerEvents();

    PowerEvents(const PowerEvents&) = delete;
    PowerEvents& operator=(const PowerEvents&) = delete;

    int fd() const { return bus_ ? bus_->fd() : -1; }
    void process(double now);
    void tick(double now);
    void want(PowerLock which, bool wanted);
    void release(PowerLock which);
    std::vector<PowerEvent> takeEvents();
    PowerLockStatus status(PowerLock which) const;
    double delayBudgetSeconds() const { return delayBudget_; }
    bool announcementPending() const { return plainShutdownAt_ >= 0.0; }

private:
    struct Lock {
        const char* what = "";
        const char* why = "";
        bool wanted = false;
        int fd = -1;
        bool requesting = false;
        bool paused = false;
        double pausedAt = 0.0;
        double retryAt = 0.0;
        uint64_t generation = 0;
        std::string error;
    };

    void connectBus();
    void dropBus(const std::string& reason);
    void onSignal(const DBusMessage& message);
    void request(Lock& lock);
    void close(Lock& lock);
    Lock& lockFor(PowerLock which);
    const Lock& lockFor(PowerLock which) const;
    void pause(Lock& lock);
    void announceShutdown(const std::string& type);
    void cancelShutdown();
    void reportOnce(const std::string& message);

    BusOpener openBus_;
    Logger log_;
    std::unique_ptr<Bus> bus_;
    NameOwnerWatch owner_;
    double now_ = 0.0;
    double reconnectAt_ = 0.0;
    double delayBudget_ = 5.0;
    Lock sleep_;
    Lock shutdown_;
    bool shutdownAnnounced_ = false;
    double plainShutdownAt_ = -1.0;
    std::vector<PowerEvent> events_;
    std::string reported_;
};

}

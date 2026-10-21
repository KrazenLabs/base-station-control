#include "power_events.hpp"

#include <algorithm>
#include <optional>

#include <fcntl.h>
#include <unistd.h>

#include "app_info.hpp"
#include "dbus_connection.hpp"
#include "text.hpp"

namespace bsc {

namespace {

constexpr const char* kLogind = "org.freedesktop.login1";
constexpr const char* kLogindPath = "/org/freedesktop/login1";
constexpr const char* kManager = "org.freedesktop.login1.Manager";
constexpr const char* kProperties = "org.freedesktop.DBus.Properties";
constexpr double kReconnectSeconds = 10.0;
constexpr double kRetrySeconds = 60.0;
constexpr double kCallTimeoutSeconds = 10.0;
constexpr double kPlainShutdownGraceSeconds = 0.25;
constexpr double kOverdueSeconds = 1.0;

std::string matchRule(const char* member) {
    return std::string("type='signal',sender='") + kLogind + "',path='" + kLogindPath + "',interface='" + kManager +
           "',member='" + member + "'";
}

bool firstBool(const DBusMessage& message) {
    return !message.body.empty() && message.body.front().asBool().value_or(false);
}

}

std::string currentBootId() {
    return trim(readFile("/proc/sys/kernel/random/boot_id").value_or(""));
}

bool isRestartType(const std::string& shutdownType) {
    return shutdownType == "reboot" || shutdownType == "kexec" || shutdownType == "soft-reboot";
}

std::string describeLock(const PowerLockStatus& status) {
    if (!status.wanted) {
        return "not needed";
    }
    if (status.paused) {
        return "in use";
    }
    if (status.held) {
        return "ready";
    }
    return status.error.empty() ? "waiting" : "failed: " + status.error;
}

PowerEvents::PowerEvents(BusOpener openBus, Logger log)
    : openBus_(std::move(openBus)), log_(std::move(log)), owner_(kLogind) {
    sleep_.what = "sleep";
    sleep_.why = "Turns the base stations off before the headset sleeps";
    shutdown_.what = "shutdown";
    shutdown_.why = "Turns the base stations off before the headset shuts down";
}

PowerEvents::~PowerEvents() {
    close(sleep_);
    close(shutdown_);
}

PowerEvents::Lock& PowerEvents::lockFor(PowerLock which) {
    return which == PowerLock::Sleep ? sleep_ : shutdown_;
}

const PowerEvents::Lock& PowerEvents::lockFor(PowerLock which) const {
    return which == PowerLock::Sleep ? sleep_ : shutdown_;
}

void PowerEvents::reportOnce(const std::string& message) {
    if (message != reported_) {
        reported_ = message;
        log_(message);
    }
}

void PowerEvents::connectBus() {
    std::string error;
    bus_ = openBus_(now_, &error);
    if (!bus_) {
        reportOnce("failed to watch for sleep or shutdown (system bus: " + error + ")");
        reconnectAt_ = now_ + kReconnectSeconds;
        return;
    }
    if (!bus_->passesFds()) {
        reportOnce("the system bus doesn't pass file descriptors");
    }
    bus_->setSignalHandler([this](const DBusMessage& message) { onSignal(message); });
    for (const char* member : {"PrepareForSleep", "PrepareForShutdown", "PrepareForShutdownWithMetadata"}) {
        bus_->addMatch(matchRule(member));
    }
    owner_.start(*bus_);
    readManagerProperty("InhibitDelayMaxUSec", [this](const DBusValue& value) {
        if (const std::optional<uint64_t> micros = value.asUnsigned()) {
            delayBudget_ = std::clamp(static_cast<double>(*micros) / 1e6, 0.5, 60.0);
        }
    });
    catchUpWithLogind();
    reported_.clear();
}

void PowerEvents::readManagerProperty(const char* property, std::function<void(const DBusValue&)> use) {
    bus_->call(dbusMethodCall(kLogind, kLogindPath, kProperties, "Get",
                              {DBusValue::string(kManager), DBusValue::string(property)}),
               kCallTimeoutSeconds, [this, use = std::move(use)](const DBusMessage& reply) {
                   owner_.learnFrom(reply);
                   if (reply.type == DBusMessageType::MethodReturn && !reply.body.empty()) {
                       use(reply.body.front());
                   }
               });
}

void PowerEvents::catchUpWithLogind() {
    readManagerProperty("PreparingForSleep", [this](const DBusValue& value) {
        const std::optional<bool> preparing = value.asBool();
        if (preparing && !*preparing && sleep_.paused) {
            resume();
        }
    });
    readManagerProperty("PreparingForShutdown", [this](const DBusValue& value) {
        const std::optional<bool> preparing = value.asBool();
        if (preparing && !*preparing) {
            cancelShutdown();
        }
    });
}

void PowerEvents::dropBus(const std::string& reason) {
    reportOnce("lost the connection to logind (" + reason + "); reconnecting");
    bus_.reset();
    owner_.reset();
    sleep_.requesting = false;
    shutdown_.requesting = false;
    reconnectAt_ = now_ + kReconnectSeconds;
}

void PowerEvents::process(double now) {
    now_ = now;
    if (!bus_) {
        return;
    }
    bus_->processIncoming(now);
    bus_->expireCalls(now);
    if (!bus_->alive()) {
        dropBus("bus closed connection");
    }
}

void PowerEvents::tick(double now) {
    now_ = now;
    if (!bus_ && now >= reconnectAt_) {
        connectBus();
    }
    if (plainShutdownAt_ >= 0.0 && now - plainShutdownAt_ >= kPlainShutdownGraceSeconds) {
        announceShutdown("");
    }
    for (Lock* lock : {&sleep_, &shutdown_}) {
        if (lock->fd >= 0 && (!lock->wanted || (lock->paused && now - lock->pausedAt > delayBudget_ + kOverdueSeconds))) {
            close(*lock);
        }
        if (lock->wanted && !lock->paused && lock->fd < 0 && !lock->requesting && now >= lock->retryAt && bus_ &&
            bus_->alive() && bus_->passesFds()) {
            request(*lock);
        }
    }
}

void PowerEvents::request(Lock& lock) {
    lock.requesting = true;
    const uint64_t generation = ++lock.generation;
    Lock* target = &lock;
    bus_->call(dbusMethodCall(kLogind, kLogindPath, kManager, "Inhibit",
                              {DBusValue::string(lock.what), DBusValue::string(kAppId), DBusValue::string(lock.why),
                               DBusValue::string("delay")}),
               kCallTimeoutSeconds, [this, target, generation](const DBusMessage& reply) {
                   if (target->generation != generation) {
                       return;
                   }
                   target->requesting = false;
                   if (reply.type != DBusMessageType::MethodReturn) {
                       if (reply.errorName == kDBusDisconnectedError) {
                           return;
                       }
                       target->error = reply.errorText();
                       target->retryAt = now_ + kRetrySeconds;
                       log_(std::string("logind refused ") + target->what + " delay: " + target->error);
                       return;
                   }
                   const std::optional<uint64_t> index =
                       reply.body.empty() ? std::nullopt : reply.body.front().asUnsigned();
                   if (!index || *index >= reply.fds.size()) {
                       target->error = "logind sent no lock";
                       target->retryAt = now_ + kRetrySeconds;
                       return;
                   }
                   const int kept = ::fcntl(reply.fds[*index], F_DUPFD_CLOEXEC, 3);
                   if (kept < 0) {
                       target->error = "could not keep the lock";
                       target->retryAt = now_ + kRetrySeconds;
                       return;
                   }
                   if (!target->wanted || target->paused || target->fd >= 0) {
                       ::close(kept);
                       return;
                   }
                   target->fd = kept;
                   target->error.clear();
               });
}

void PowerEvents::close(Lock& lock) {
    if (lock.fd >= 0) {
        ::close(lock.fd);
        lock.fd = -1;
    }
}

void PowerEvents::want(PowerLock which, bool wanted) {
    Lock& lock = lockFor(which);
    if (lock.wanted == wanted) {
        return;
    }
    lock.wanted = wanted;
    if (wanted) {
        lock.retryAt = 0.0;
    } else {
        close(lock);
        lock.error.clear();
    }
}

void PowerEvents::release(PowerLock which) {
    close(lockFor(which));
}

void PowerEvents::pause(Lock& lock) {
    lock.paused = true;
    lock.pausedAt = now_;
}

void PowerEvents::announceShutdown(const std::string& type) {
    plainShutdownAt_ = -1.0;
    if (shutdownAnnounced_) {
        return;
    }
    shutdownAnnounced_ = true;
    pause(shutdown_);
    PowerEvent event;
    event.kind = PowerEventKind::ShuttingDown;
    event.shutdownType = type;
    events_.push_back(event);
}

void PowerEvents::resume() {
    sleep_.paused = false;
    PowerEvent event;
    event.kind = PowerEventKind::Resumed;
    events_.push_back(event);
}

void PowerEvents::cancelShutdown() {
    plainShutdownAt_ = -1.0;
    if (!shutdownAnnounced_) {
        return;
    }
    shutdownAnnounced_ = false;
    shutdown_.paused = false;
    PowerEvent event;
    event.kind = PowerEventKind::ShutdownCancelled;
    events_.push_back(event);
}

void PowerEvents::onSignal(const DBusMessage& message) {
    const OwnerChange ownerChange = owner_.update(message);
    if (ownerChange == OwnerChange::Appeared && bus_) {
        catchUpWithLogind();
    }
    if (ownerChange != OwnerChange::None) {
        return;
    }
    if (!owner_.sentBy(message) || message.interface != kManager || message.path != kLogindPath) {
        return;
    }
    const bool starting = firstBool(message);
    if (message.member == "PrepareForSleep") {
        if (starting) {
            pause(sleep_);
            PowerEvent event;
            event.kind = PowerEventKind::Sleeping;
            events_.push_back(event);
        } else {
            resume();
        }
    } else if (message.member == "PrepareForShutdownWithMetadata") {
        if (!starting) {
            cancelShutdown();
            return;
        }
        std::string type;
        if (message.body.size() > 1) {
            if (const DBusValue* value = message.body[1].find("type")) {
                type = value->asText().value_or("");
            }
        }
        announceShutdown(type);
    } else if (message.member == "PrepareForShutdown") {
        if (!starting) {
            cancelShutdown();
        } else if (!shutdownAnnounced_ && plainShutdownAt_ < 0.0) {
            plainShutdownAt_ = now_;
        }
    }
}

std::vector<PowerEvent> PowerEvents::takeEvents() {
    std::vector<PowerEvent> taken;
    taken.swap(events_);
    return taken;
}

PowerLockStatus PowerEvents::status(PowerLock which) const {
    const Lock& lock = lockFor(which);
    PowerLockStatus status;
    status.wanted = lock.wanted;
    status.held = lock.fd >= 0;
    status.paused = lock.paused;
    status.error = lock.error;
    if (status.wanted && !status.held && !status.paused && status.error.empty()) {
        if (!bus_ || !bus_->alive()) {
            status.error = "logind is not reachable";
        } else if (!bus_->passesFds()) {
            status.error = "system bus does not pass file descriptors";
        }
    }
    return status;
}

}

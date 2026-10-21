#pragma once

#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "adapter_state.hpp"
#include "common.hpp"
#include "dbus_bus.hpp"
#include "lighthouse.hpp"
#include "power_link.hpp"

namespace bsc {

struct BlueZSettings {
    std::string scanPattern = kLighthouseNamePrefix;
    double scanRefreshSeconds = 4.0;
    double queueTimeoutSeconds = 60.0;
    double connectTimeoutSeconds = 20.0;
    double resolveTimeoutSeconds = 15.0;
    double writeTimeoutSeconds = 10.0;
};

class BlueZ : public PowerLink {
public:
    using AdvertHandler = std::function<void(const Advert&)>;
    using Done = std::function<void(const std::string& error)>;

    BlueZ(BlueZSettings settings, BusOpener openBus, Logger log, Logger debug = nullptr);
    ~BlueZ() override;

    void setAdvertHandler(AdvertHandler handler) { advertHandler_ = std::move(handler); }
    int fd() const { return bus_ ? bus_->fd() : -1; }
    void process(double monotonicNow);
    void tick(double monotonicNow);
    void setScanning(bool wanted) { scanWanted_ = wanted; }
    bool scanning() const { return scanSession_; }
    AdapterState adapterState() const;
    void powerOn(Done done);
    void shutdown();

    bool canReach(const std::string& devicePath) const override;
    void write(const std::string& devicePath, const std::string& characteristicUuid, std::vector<uint8_t> value,
               bool readBack, WriteDone done) override;
    LinkPhase phase(const std::string& devicePath) const override;

private:
    struct Device {
        std::string address;
        std::string name;
        std::optional<int> rssi;
        std::optional<std::vector<uint8_t>> valveData;
        bool connected = false;
        bool servicesResolved = false;
    };

    enum class Stage { Waiting, Connecting, Resolving, Writing, Reading };

    struct Operation {
        uint64_t id = 0;
        std::string uuid;
        std::vector<uint8_t> value;
        bool readBack = false;
        WriteDone done;
        Stage stage = Stage::Waiting;
        double deadline = 0.0;
        double startedAt = 0.0;
        double notBefore = 0.0;
        int quickRetries = 0;
    };

    void connectBus();
    void loadObjects();
    void resetObjects(const std::string& reason);
    void onSignal(const DBusMessage& message);
    void onOwnerChange(OwnerChange change);
    void applyInterfaces(const std::string& path, const DBusValue& interfaces, bool emit);
    void applyAdapter(const std::string& path, const DBusValue& properties);
    void applyDevice(const std::string& path, const DBusValue& properties, bool emit);
    void removeInterfaces(const std::string& path, const DBusValue& names);
    void updateDiscovery();
    void startDiscovery();
    void stopDiscovery(bool restart);
    bool connectionPending() const;
    void startConnections();
    void enterStage(Operation& operation, Stage stage);
    void connect(const std::string& devicePath);
    void continueOperation(const std::string& devicePath);
    void failResolvingOperation(const std::string& devicePath);
    void readBack(const std::string& devicePath, const std::string& characteristic);
    void emitReadBack(const std::string& devicePath, uint8_t value);
    void finishOperation(const std::string& devicePath, LinkResult result);
    void defer(std::function<void()> callback);
    void flushCompleted();
    std::optional<std::string> characteristicFor(const std::string& devicePath, const std::string& uuid) const;
    void callBlueZ(const std::string& path, const std::string& interface, const std::string& member,
                   std::vector<DBusValue> body, double timeout, Bus::ReplyHandler handler);
    void reportOnce(const std::string& key, const std::string& message);

    void debug(const std::string& message) const;

    BlueZSettings settings_;
    BusOpener openBus_;
    Logger log_;
    Logger debug_;
    AdvertHandler advertHandler_;
    std::unique_ptr<Bus> bus_;
    NameOwnerWatch owner_;
    double now_ = 0.0;
    double nextConnect_ = 0.0;
    double reloadAt_ = 0.0;
    bool objectsLoaded_ = false;
    bool bluezRunning_ = false;
    std::string adapterPath_;
    bool adapterPowered_ = false;
    std::map<std::string, Device> devices_;
    std::map<std::string, std::string> characteristics_;
    std::map<std::string, Operation> operations_;
    std::vector<std::function<void()>> completed_;
    bool scanWanted_ = false;
    bool scanSession_ = false;
    bool discovering_ = false;
    bool discoveryBusy_ = false;
    double discoveryRetryAt_ = 0.0;
    double discoveryStartedAt_ = 0.0;
    double discoveryStoppedAt_ = -1e18;
    uint64_t generation_ = 0;
    uint64_t nextOperationId_ = 1;
    std::string reported_;
};

}

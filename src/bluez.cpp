#include "bluez.hpp"

#include "text.hpp"

namespace bsc {

namespace {

constexpr const char* kService = "org.bluez";
constexpr const char* kAdapterInterface = "org.bluez.Adapter1";
constexpr const char* kDeviceInterface = "org.bluez.Device1";
constexpr const char* kCharacteristicInterface = "org.bluez.GattCharacteristic1";
constexpr const char* kPropertiesInterface = "org.freedesktop.DBus.Properties";
constexpr const char* kObjectManagerInterface = "org.freedesktop.DBus.ObjectManager";
constexpr double kReconnectSeconds = 5.0;
constexpr double kReloadSeconds = 5.0;
constexpr double kDiscoveryRetrySeconds = 10.0;
constexpr double kCallTimeoutSeconds = 10.0;
constexpr double kConnectAfterScanSeconds = 0.25;
constexpr double kQuickRetrySeconds = 0.3;
constexpr int kQuickConnectRetries = 2;

bool isUnder(const std::string& child, const std::string& parent) {
    return child.size() > parent.size() && startsWith(child, parent) && child[parent.size()] == '/';
}

std::string deviceOf(const std::string& characteristicPath) {
    const size_t service = characteristicPath.find("/service");
    return service == std::string::npos ? std::string() : characteristicPath.substr(0, service);
}

}

BlueZ::BlueZ(BlueZSettings settings, BusOpener openBus, Logger log, Logger debug)
    : settings_(std::move(settings)),
      openBus_(std::move(openBus)),
      log_(std::move(log)),
      debug_(std::move(debug)),
      owner_(kService) {}

void BlueZ::debug(const std::string& message) const {
    if (debug_) {
        debug_(message);
    }
}

BlueZ::~BlueZ() {
    shutdown();
}

void BlueZ::reportOnce(const std::string& key, const std::string& message) {
    if (reported_ == key) {
        return;
    }
    reported_ = key;
    log_(message);
}

AdapterState BlueZ::adapterState() const {
    if (!bus_ || !bluezRunning_ || !objectsLoaded_) {
        return AdapterState::Unknown;
    }
    if (adapterPath_.empty()) {
        return AdapterState::Missing;
    }
    return adapterPowered_ ? AdapterState::On : AdapterState::Off;
}

void BlueZ::callBlueZ(const std::string& path, const std::string& interface, const std::string& member,
                      std::vector<DBusValue> body, double timeout, Bus::ReplyHandler handler) {
    if (!bus_) {
        return;
    }
    bus_->call(dbusMethodCall(kService, path, interface, member, std::move(body)), timeout, std::move(handler));
}

void BlueZ::connectBus() {
    if (bus_ && bus_->alive()) {
        return;
    }
    if (now_ < nextConnect_) {
        return;
    }
    nextConnect_ = now_ + kReconnectSeconds;
    std::string error;
    bus_ = openBus_(now_, &error);
    if (!bus_) {
        reportOnce("bus-failed", "cannot reach the system bus (" + error + "); retrying");
        return;
    }
    ++generation_;
    bus_->setSignalHandler([this](const DBusMessage& message) { onSignal(message); });
    bus_->addMatch("type='signal',sender='org.bluez',interface='org.freedesktop.DBus.ObjectManager'");
    bus_->addMatch("type='signal',sender='org.bluez',interface='org.freedesktop.DBus.Properties',member='PropertiesChanged'");
    owner_.start(*bus_);
    loadObjects();
}

void BlueZ::loadObjects() {
    const uint64_t generation = generation_;
    callBlueZ("/", kObjectManagerInterface, "GetManagedObjects", {}, kCallTimeoutSeconds,
              [this, generation](const DBusMessage& reply) {
                  if (generation != generation_) {
                      return;
                  }
                  if (reply.type == DBusMessageType::Error) {
                      bluezRunning_ = false;
                      objectsLoaded_ = false;
                      reloadAt_ = now_ + kReloadSeconds;
                      reportOnce("no-bluez", "bluetoothd is not answering (" + reply.errorText() + "); waiting...");
                      return;
                  }
                  owner_.learnFrom(reply);
                  resetObjects("Bluetooth was reset");
                  bluezRunning_ = true;
                  if (!reply.body.empty()) {
                      for (const DBusValue& entry : reply.body.front().items) {
                          if (entry.items.size() == 2) {
                              applyInterfaces(entry.items[0].text, entry.items[1], false);
                          }
                      }
                  }
                  objectsLoaded_ = true;
                  reportOnce("ready", "connected to BlueZ; Bluetooth adapter " +
                                          (adapterPath_.empty() ? std::string("missing")
                                                                : adapterPath_ + (adapterPowered_ ? " is on" : " is off")));
              });
}

void BlueZ::resetObjects(const std::string& reason) {
    ++generation_;
    for (auto& entry : operations_) {
        WriteDone done = std::move(entry.second.done);
        if (done) {
            defer([done, reason]() { done(linkFailed(LinkFailure::BluetoothReset, reason)); });
        }
    }
    operations_.clear();
    devices_.clear();
    characteristics_.clear();
    adapterPath_.clear();
    adapterPowered_ = false;
    discovering_ = false;
    scanSession_ = false;
    discoveryBusy_ = false;
    objectsLoaded_ = false;
}

void BlueZ::onOwnerChange(OwnerChange change) {
    if (change == OwnerChange::Vanished) {
        bluezRunning_ = false;
        resetObjects("bluetoothd stopped");
        reportOnce("bluez-gone", "bluetoothd stopped; waiting...");
    } else if (change == OwnerChange::Appeared) {
        reportOnce("bluez-back", "bluetoothd started");
        loadObjects();
    }
}

void BlueZ::onSignal(const DBusMessage& message) {
    const OwnerChange change = owner_.update(message);
    if (change != OwnerChange::None) {
        onOwnerChange(change);
        return;
    }
    if (!owner_.sentBy(message)) {
        return;
    }
    if (message.interface == kObjectManagerInterface && message.body.size() >= 2) {
        if (!objectsLoaded_) {
            return;
        }
        if (message.member == "InterfacesAdded") {
            applyInterfaces(message.body[0].text, message.body[1], true);
        } else if (message.member == "InterfacesRemoved") {
            removeInterfaces(message.body[0].text, message.body[1]);
        }
        return;
    }
    if (message.interface == kPropertiesInterface && message.member == "PropertiesChanged" && message.body.size() >= 2) {
        if (!objectsLoaded_ || !startsWith(message.path, "/org/bluez")) {
            return;
        }
        const std::string& interface = message.body[0].text;
        if (interface == kDeviceInterface && devices_.count(message.path)) {
            applyDevice(message.path, message.body[1], true);
        } else if (interface == kAdapterInterface) {
            applyAdapter(message.path, message.body[1]);
        }
    }
}

void BlueZ::applyInterfaces(const std::string& path, const DBusValue& interfaces, bool emit) {
    for (const DBusValue& entry : interfaces.items) {
        if (entry.items.size() != 2) {
            continue;
        }
        const std::string& name = entry.items[0].text;
        const DBusValue& properties = entry.items[1];
        if (name == kAdapterInterface) {
            applyAdapter(path, properties);
        } else if (name == kDeviceInterface) {
            devices_[path];
            applyDevice(path, properties, emit);
        } else if (name == kCharacteristicInterface) {
            if (const DBusValue* uuid = properties.find("UUID")) {
                if (const std::optional<std::string> text = uuid->asText()) {
                    characteristics_[path] = toLower(*text);
                    continueOperation(deviceOf(path));
                }
            }
        }
    }
}

void BlueZ::applyAdapter(const std::string& path, const DBusValue& properties) {
    if (adapterPath_.empty()) {
        adapterPath_ = path;
    }
    if (path != adapterPath_) {
        return;
    }
    if (const DBusValue* powered = properties.find("Powered")) {
        if (const std::optional<bool> on = powered->asBool()) {
            if (*on != adapterPowered_ && objectsLoaded_) {
                log_(std::string("Bluetooth turned ") + (*on ? "on" : "off"));
            }
            adapterPowered_ = *on;
            if (!*on) {
                discovering_ = false;
                scanSession_ = false;
            }
        }
    }
    if (const DBusValue* discovering = properties.find("Discovering")) {
        if (const std::optional<bool> active = discovering->asBool()) {
            if (!*active && !discoveryBusy_) {
                discovering_ = false;
            }
        }
    }
}

void BlueZ::applyDevice(const std::string& path, const DBusValue& properties, bool emit) {
    Device& device = devices_[path];
    bool heard = false;
    bool linkChanged = false;
    for (const DBusValue& entry : properties.items) {
        if (entry.items.size() != 2) {
            continue;
        }
        const std::string& key = entry.items[0].text;
        const DBusValue& value = entry.items[1].unwrapped();
        if (key == "Address") {
            device.address = value.asText().value_or(device.address);
        } else if (key == "Name") {
            device.name = value.asText().value_or(device.name);
        } else if (key == "RSSI") {
            if (const std::optional<int64_t> rssi = value.asSigned()) {
                device.rssi = static_cast<int>(*rssi);
                heard = true;
            }
        } else if (key == "Connected") {
            if (const std::optional<bool> connected = value.asBool()) {
                linkChanged = linkChanged || device.connected != *connected;
                if (device.connected != *connected && emit) {
                    debug(path + ": Connected = " + (*connected ? "true" : "false"));
                }
                device.connected = *connected;
            }
        } else if (key == "ServicesResolved") {
            if (const std::optional<bool> resolved = value.asBool()) {
                linkChanged = linkChanged || device.servicesResolved != *resolved;
                if (device.servicesResolved != *resolved && emit) {
                    debug(path + ": ServicesResolved = " + (*resolved ? "true" : "false"));
                }
                device.servicesResolved = *resolved;
            }
        } else if (key == "ManufacturerData") {
            for (const DBusValue& company : value.items) {
                if (company.items.size() == 2 && company.items[0].bits == kValveCompanyId) {
                    std::optional<std::vector<uint8_t>> data = company.items[1].asBytes();
                    if (emit && data && data != device.valveData && data->size() > 4) {
                        debug(path + ": advertises power " + hexByte((*data)[4]));
                    }
                    device.valveData = std::move(data);
                    heard = true;
                }
            }
        }
    }
    if (linkChanged) {
        continueOperation(path);
    }
    if (emit && heard && advertHandler_ && isLighthouseName(device.name)) {
        Advert advert;
        advert.devicePath = path;
        advert.address = device.address;
        advert.name = device.name;
        advert.rssi = device.rssi;
        advert.valveData = device.valveData;
        advertHandler_(advert);
    }
}

void BlueZ::removeInterfaces(const std::string& path, const DBusValue& names) {
    for (const DBusValue& name : names.items) {
        if (name.text == kDeviceInterface) {
            devices_.erase(path);
            for (auto it = characteristics_.begin(); it != characteristics_.end();) {
                it = isUnder(it->first, path) ? characteristics_.erase(it) : std::next(it);
            }
            finishOperation(path, linkFailed(LinkFailure::Disappeared));
        } else if (name.text == kCharacteristicInterface) {
            characteristics_.erase(path);
        } else if (name.text == kAdapterInterface && path == adapterPath_) {
            adapterPath_.clear();
            adapterPowered_ = false;
            discovering_ = false;
        }
    }
}

void BlueZ::startDiscovery() {
    discoveryBusy_ = true;
    const uint64_t generation = generation_;
    std::vector<std::pair<std::string, DBusValue>> filter = {
        {"Transport", DBusValue::string("le")},
        {"DuplicateData", DBusValue::boolean(true)},
    };
    if (!settings_.scanPattern.empty()) {
        filter.emplace_back("Pattern", DBusValue::string(settings_.scanPattern));
    }
    callBlueZ(adapterPath_, kAdapterInterface, "SetDiscoveryFilter", {DBusValue::stringVariantDict(std::move(filter))},
              kCallTimeoutSeconds, [this, generation](const DBusMessage& reply) {
                  if (generation != generation_) {
                      return;
                  }
                  if (reply.type == DBusMessageType::Error) {
                      reportOnce("filter", "failed to apply scan filter (" + reply.errorText() + ")");
                  }
                  callBlueZ(adapterPath_, kAdapterInterface, "StartDiscovery", {}, kCallTimeoutSeconds,
                            [this, generation](const DBusMessage& started) {
                                if (generation != generation_) {
                                    return;
                                }
                                discoveryBusy_ = false;
                                const bool alreadyRunning = started.errorName == "org.bluez.Error.InProgress";
                                if (started.type != DBusMessageType::Error || alreadyRunning) {
                                    debug(std::string("scan started") + (alreadyRunning ? " (in progress)" : ""));
                                    discovering_ = true;
                                    scanSession_ = true;
                                    discoveryStartedAt_ = now_;
                                    return;
                                }
                                scanSession_ = false;
                                discoveryRetryAt_ = now_ + kDiscoveryRetrySeconds;
                                log_("failed to start scanning: " + started.errorText());
                            });
              });
}

void BlueZ::stopDiscovery(bool restart) {
    debug(restart ? "scan refresh" : "scan stopped");
    discoveryBusy_ = true;
    const uint64_t generation = generation_;
    callBlueZ(adapterPath_, kAdapterInterface, "StopDiscovery", {}, kCallTimeoutSeconds,
              [this, generation, restart](const DBusMessage&) {
                  if (generation != generation_) {
                      return;
                  }
                  discoveryBusy_ = false;
                  discovering_ = false;
                  discoveryStoppedAt_ = now_;
                  if (restart && scanWanted_ && adapterPowered_ && !connectionPending()) {
                      startDiscovery();
                  }
                  startConnections();
              });
}

void BlueZ::updateDiscovery() {
    if (!scanWanted_) {
        scanSession_ = false;
    }
    if (!bus_ || !objectsLoaded_ || adapterPath_.empty() || discoveryBusy_) {
        return;
    }
    if (!adapterPowered_) {
        discovering_ = false;
        scanSession_ = false;
        return;
    }
    if (connectionPending()) {
        if (discovering_) {
            stopDiscovery(false);
        }
        return;
    }
    if (scanWanted_ && !discovering_ && now_ >= discoveryRetryAt_) {
        startDiscovery();
    } else if (scanWanted_ && discovering_ && operations_.empty() && settings_.scanRefreshSeconds > 0.0 &&
               now_ - discoveryStartedAt_ >= settings_.scanRefreshSeconds) {
        stopDiscovery(true);
    } else if (!scanWanted_ && discovering_) {
        stopDiscovery(false);
    }
}

bool BlueZ::canReach(const std::string& devicePath) const {
    return bus_ && objectsLoaded_ && adapterPowered_ && devices_.count(devicePath) > 0;
}

LinkPhase BlueZ::phase(const std::string& devicePath) const {
    const auto found = operations_.find(devicePath);
    if (found == operations_.end()) {
        return LinkPhase::Idle;
    }
    const Stage stage = found->second.stage;
    return stage == Stage::Writing || stage == Stage::Reading ? LinkPhase::Sending : LinkPhase::Connecting;
}

std::optional<std::string> BlueZ::characteristicFor(const std::string& devicePath, const std::string& uuid) const {
    for (const auto& entry : characteristics_) {
        if (entry.second == uuid && isUnder(entry.first, devicePath)) {
            return entry.first;
        }
    }
    return std::nullopt;
}

void BlueZ::enterStage(Operation& operation, Stage stage) {
    operation.stage = stage;
    switch (stage) {
        case Stage::Waiting: operation.deadline = now_ + settings_.queueTimeoutSeconds; break;
        case Stage::Connecting: operation.deadline = now_ + settings_.connectTimeoutSeconds; break;
        case Stage::Resolving: operation.deadline = now_ + settings_.resolveTimeoutSeconds; break;
        case Stage::Writing:
        case Stage::Reading: operation.deadline = now_ + settings_.writeTimeoutSeconds; break;
    }
}

void BlueZ::write(const std::string& devicePath, const std::string& characteristicUuid, std::vector<uint8_t> value,
                  bool readBack, WriteDone done) {
    if (operations_.count(devicePath)) {
        defer([done]() { done(linkFailed(LinkFailure::Busy)); });
        return;
    }
    const auto device = devices_.find(devicePath);
    if (!bus_ || !bus_->alive() || !objectsLoaded_ || device == devices_.end()) {
        defer([done]() { done(linkFailed(LinkFailure::Unreachable)); });
        return;
    }
    Operation operation;
    operation.id = nextOperationId_++;
    operation.uuid = toLower(characteristicUuid);
    operation.value = std::move(value);
    operation.readBack = readBack;
    operation.done = std::move(done);
    operation.startedAt = now_;
    debug(devicePath + ": write requested" + (device->second.connected ? " (already connected)" : ", connecting"));
    if (device->second.connected) {
        enterStage(operation, Stage::Resolving);
        operations_[devicePath] = std::move(operation);
        continueOperation(devicePath);
        return;
    }
    enterStage(operation, Stage::Waiting);
    operations_[devicePath] = std::move(operation);
    updateDiscovery();
    startConnections();
}

bool BlueZ::connectionPending() const {
    for (const auto& entry : operations_) {
        if (entry.second.stage == Stage::Waiting || entry.second.stage == Stage::Connecting) {
            return true;
        }
    }
    return false;
}

void BlueZ::startConnections() {
    if (discovering_ || discoveryBusy_ || now_ - discoveryStoppedAt_ < kConnectAfterScanSeconds) {
        return;
    }
    const std::string* next = nullptr;
    uint64_t nextId = 0;
    for (const auto& entry : operations_) {
        const Operation& operation = entry.second;
        if (operation.stage == Stage::Connecting) {
            return;
        }
        if (operation.stage == Stage::Waiting && now_ >= operation.notBefore && (!next || operation.id < nextId)) {
            next = &entry.first;
            nextId = operation.id;
        }
    }
    if (next) {
        connect(*next);
    }
}

void BlueZ::connect(const std::string& devicePath) {
    const auto pending = operations_.find(devicePath);
    if (pending == operations_.end()) {
        return;
    }
    enterStage(pending->second, Stage::Connecting);
    const uint64_t id = pending->second.id;
    debug(devicePath + ": scan paused, Connect after " + formatNumber(now_ - pending->second.startedAt, 2) + " s");
    const uint64_t generation = generation_;
    callBlueZ(devicePath, kDeviceInterface, "Connect", {}, settings_.connectTimeoutSeconds + 2.0,
              [this, devicePath, id, generation](const DBusMessage& reply) {
                  if (generation != generation_) {
                      return;
                  }
                  const auto found = operations_.find(devicePath);
                  if (found == operations_.end() || found->second.id != id) {
                      return;
                  }
                  Operation& operation = found->second;
                  const bool failed = reply.type == DBusMessageType::Error;
                  debug(devicePath + ": Connect answered after " + formatNumber(now_ - operation.startedAt, 2) + " s" +
                        (failed ? ": " + reply.errorText() : std::string()));
                  if (failed && contains(reply.errorText(), "abort-by-local") && operation.quickRetries < kQuickConnectRetries) {
                      ++operation.quickRetries;
                      enterStage(operation, Stage::Waiting);
                      operation.notBefore = now_ + kQuickRetrySeconds;
                  } else if (failed) {
                      finishOperation(devicePath, linkFailed(LinkFailure::ConnectFailed, reply.errorText()));
                  } else {
                      enterStage(operation, Stage::Resolving);
                      continueOperation(devicePath);
                  }
                  startConnections();
              });
}

void BlueZ::continueOperation(const std::string& devicePath) {
    const auto found = operations_.find(devicePath);
    if (found == operations_.end() || found->second.stage != Stage::Resolving) {
        return;
    }
    const auto device = devices_.find(devicePath);
    if (device == devices_.end() || !device->second.servicesResolved) {
        return;
    }
    const std::optional<std::string> characteristic = characteristicFor(devicePath, found->second.uuid);
    if (!characteristic) {
        return;
    }
    Operation& operation = found->second;
    enterStage(operation, Stage::Writing);
    debug(devicePath + ": services ready, writing after " + formatNumber(now_ - operation.startedAt, 2) + " s");
    const uint64_t id = operation.id;
    const uint64_t generation = generation_;
    const std::string characteristicPath = *characteristic;
    std::vector<std::pair<std::string, DBusValue>> options = {{"type", DBusValue::string("request")}};
    callBlueZ(characteristicPath, kCharacteristicInterface, "WriteValue",
              {DBusValue::bytes(operation.value), DBusValue::stringVariantDict(std::move(options))},
              settings_.writeTimeoutSeconds + 2.0,
              [this, devicePath, characteristicPath, id, generation](const DBusMessage& reply) {
                  if (generation != generation_) {
                      return;
                  }
                  const auto current = operations_.find(devicePath);
                  if (current == operations_.end() || current->second.id != id) {
                      return;
                  }
                  const bool failed = reply.type == DBusMessageType::Error;
                  debug(devicePath + ": write answered after " + formatNumber(now_ - current->second.startedAt, 2) + " s" +
                        (failed ? ": " + reply.errorText() : std::string()));
                  if (failed) {
                      finishOperation(devicePath, linkFailed(LinkFailure::WriteFailed, reply.errorText()));
                  } else if (current->second.readBack) {
                      readBack(devicePath, characteristicPath);
                  } else {
                      finishOperation(devicePath, linkSucceeded());
                  }
              });
}

void BlueZ::readBack(const std::string& devicePath, const std::string& characteristic) {
    const auto found = operations_.find(devicePath);
    if (found == operations_.end()) {
        return;
    }
    enterStage(found->second, Stage::Reading);
    const uint64_t id = found->second.id;
    const uint64_t generation = generation_;
    callBlueZ(characteristic, kCharacteristicInterface, "ReadValue", {DBusValue::stringVariantDict({})},
              settings_.writeTimeoutSeconds + 2.0, [this, devicePath, id, generation](const DBusMessage& reply) {
                  if (generation != generation_) {
                      return;
                  }
                  const auto current = operations_.find(devicePath);
                  if (current == operations_.end() || current->second.id != id) {
                      return;
                  }
                  std::optional<uint8_t> value;
                  if (reply.type != DBusMessageType::Error && !reply.body.empty()) {
                      const std::optional<std::vector<uint8_t>> bytes = reply.body.front().asBytes();
                      if (bytes && !bytes->empty()) {
                          value = bytes->front();
                      }
                  }
                  debug(devicePath + ": read back " + (value ? hexByte(*value) : reply.errorText()) + " after " +
                        formatNumber(now_ - current->second.startedAt, 2) + " s");
                  if (value) {
                      emitReadBack(devicePath, *value);
                  }
                  finishOperation(devicePath, linkSucceeded(value));
              });
}

void BlueZ::emitReadBack(const std::string& devicePath, uint8_t value) {
    const auto device = devices_.find(devicePath);
    if (device == devices_.end() || !advertHandler_ || !isLighthouseName(device->second.name) ||
        !device->second.valveData || device->second.valveData->size() <= 4) {
        return;
    }
    Advert advert;
    advert.devicePath = devicePath;
    advert.address = device->second.address;
    advert.name = device->second.name;
    advert.rssi = device->second.rssi;
    std::vector<uint8_t> data = *device->second.valveData;
    data[4] = value;
    advert.valveData = data;
    advertHandler_(advert);
}

void BlueZ::finishOperation(const std::string& devicePath, LinkResult result) {
    const auto found = operations_.find(devicePath);
    if (found == operations_.end()) {
        return;
    }
    Operation operation = std::move(found->second);
    operations_.erase(found);
    if (devices_.count(devicePath)) {
        callBlueZ(devicePath, kDeviceInterface, "Disconnect", {}, 5.0, [](const DBusMessage&) {});
    }
    WriteDone done = std::move(operation.done);
    if (done) {
        defer([done, result]() { done(result); });
    }
}

void BlueZ::defer(std::function<void()> callback) {
    completed_.push_back(std::move(callback));
}

void BlueZ::flushCompleted() {
    while (!completed_.empty()) {
        std::vector<std::function<void()>> batch;
        batch.swap(completed_);
        for (const std::function<void()>& callback : batch) {
            callback();
        }
    }
}

void BlueZ::powerOn(Done done) {
    if (!bus_ || adapterPath_.empty()) {
        defer([done]() { done("no Bluetooth adapter"); });
        return;
    }
    callBlueZ(adapterPath_, kPropertiesInterface, "Set",
              {DBusValue::string(kAdapterInterface), DBusValue::string("Powered"),
               DBusValue::variant(DBusValue::boolean(true))},
              kCallTimeoutSeconds, [this, done](const DBusMessage& reply) {
                  const std::string error = reply.type == DBusMessageType::Error ? reply.errorText() : "";
                  defer([done, error]() { done(error); });
              });
}

void BlueZ::process(double monotonicNow) {
    now_ = monotonicNow;
    if (bus_ && !bus_->processIncoming(monotonicNow)) {
        reportOnce("bus-lost", "lost connection to system bus; reconnecting...");
        ++generation_;
        bus_->expireCalls(monotonicNow);
        bus_.reset();
        owner_.reset();
        bluezRunning_ = false;
        resetObjects("lost the system bus");
        nextConnect_ = monotonicNow + 1.0;
    }
    flushCompleted();
}

void BlueZ::tick(double monotonicNow) {
    now_ = monotonicNow;
    connectBus();
    if (bus_) {
        bus_->expireCalls(monotonicNow);
    }
    if (reloadAt_ > 0.0 && monotonicNow >= reloadAt_ && bus_ && !objectsLoaded_) {
        reloadAt_ = 0.0;
        loadObjects();
    }
    std::vector<std::pair<std::string, Stage>> expired;
    for (const auto& entry : operations_) {
        if (monotonicNow >= entry.second.deadline) {
            expired.emplace_back(entry.first, entry.second.stage);
        }
    }
    for (const auto& entry : expired) {
        switch (entry.second) {
            case Stage::Waiting: finishOperation(entry.first, linkFailed(LinkFailure::QueueTimeout)); break;
            case Stage::Connecting: finishOperation(entry.first, linkFailed(LinkFailure::ConnectTimeout)); break;
            case Stage::Resolving: finishOperation(entry.first, linkFailed(LinkFailure::ServicesTimeout)); break;
            case Stage::Writing: finishOperation(entry.first, linkFailed(LinkFailure::WriteTimeout)); break;
            case Stage::Reading: finishOperation(entry.first, linkSucceeded()); break;
        }
    }
    updateDiscovery();
    startConnections();
    flushCompleted();
}

void BlueZ::shutdown() {
    if (!bus_) {
        return;
    }
    for (const auto& entry : operations_) {
        if (bus_->alive()) {
            callBlueZ(entry.first, kDeviceInterface, "Disconnect", {}, 1.0, nullptr);
        }
    }
    operations_.clear();
    completed_.clear();
    if (discovering_ && bus_->alive() && !adapterPath_.empty()) {
        callBlueZ(adapterPath_, kAdapterInterface, "StopDiscovery", {}, 1.0, nullptr);
    }
    bus_.reset();
    owner_.reset();
    discovering_ = false;
    scanSession_ = false;
}

}

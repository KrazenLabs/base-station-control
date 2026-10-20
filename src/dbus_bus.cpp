#include "dbus_bus.hpp"

namespace bsc {

namespace {

constexpr double kGetNameOwnerTimeoutSeconds = 5.0;

}

NameOwnerWatch::NameOwnerWatch(std::string name) : name_(std::move(name)) {}

void NameOwnerWatch::start(Bus& bus) {
    reset();
    bus.addMatch(std::string("type='signal',sender='") + kBusDaemonName + "',interface='" + kBusDaemonName +
                 "',member='NameOwnerChanged',arg0='" + name_ + "'");
    const uint64_t generation = generation_;
    bus.call(dbusMethodCall(kBusDaemonName, kBusDaemonPath, kBusDaemonName, "GetNameOwner", {DBusValue::string(name_)}),
             kGetNameOwnerTimeoutSeconds, [this, generation](const DBusMessage& reply) {
                 if (generation != generation_ || reply.type != DBusMessageType::MethodReturn || reply.body.empty()) {
                     return;
                 }
                 if (const std::optional<std::string> owner = reply.body.front().asText()) {
                     owner_ = *owner;
                 }
             });
}

void NameOwnerWatch::reset() {
    owner_.clear();
    ++generation_;
}

void NameOwnerWatch::learnFrom(const DBusMessage& reply) {
    const bool isReply = reply.type == DBusMessageType::MethodReturn || reply.type == DBusMessageType::Error;
    if (isReply && !reply.sender.empty() && reply.sender != kBusDaemonName) {
        owner_ = reply.sender;
    }
}

OwnerChange NameOwnerWatch::update(const DBusMessage& message) {
    if (message.type != DBusMessageType::Signal || message.sender != kBusDaemonName ||
        message.interface != kBusDaemonName || message.member != "NameOwnerChanged" || message.body.size() < 3 ||
        message.body[0].text != name_) {
        return OwnerChange::None;
    }
    ++generation_;
    owner_ = message.body[2].text;
    return owner_.empty() ? OwnerChange::Vanished : OwnerChange::Appeared;
}

bool NameOwnerWatch::sentBy(const DBusMessage& message) const {
    return !owner_.empty() && message.sender == owner_;
}

}

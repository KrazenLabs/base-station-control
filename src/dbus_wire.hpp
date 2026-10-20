#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace bsc {

struct DBusValue {
    std::string signature;
    uint64_t bits = 0;
    double real = 0.0;
    std::string text;
    std::vector<DBusValue> items;

    static DBusValue byte(uint8_t value);
    static DBusValue boolean(bool value);
    static DBusValue int16(int16_t value);
    static DBusValue uint16(uint16_t value);
    static DBusValue int32(int32_t value);
    static DBusValue uint32(uint32_t value);
    static DBusValue int64(int64_t value);
    static DBusValue uint64(uint64_t value);
    static DBusValue number(double value);
    static DBusValue string(std::string value);
    static DBusValue objectPath(std::string value);
    static DBusValue signatureText(std::string value);
    static DBusValue variant(DBusValue inner);
    static DBusValue array(std::string elementSignature, std::vector<DBusValue> elements);
    static DBusValue structure(std::vector<DBusValue> fields);
    static DBusValue dictEntry(DBusValue key, DBusValue value);
    static DBusValue bytes(const std::vector<uint8_t>& data);
    static DBusValue stringVariantDict(std::vector<std::pair<std::string, DBusValue>> entries);

    char type() const { return signature.empty() ? '\0' : signature[0]; }
    bool isValid() const { return !signature.empty(); }
    bool isText() const;
    const DBusValue& unwrapped() const;
    std::optional<bool> asBool() const;
    std::optional<uint64_t> asUnsigned() const;
    std::optional<int64_t> asSigned() const;
    std::optional<std::string> asText() const;
    std::optional<std::vector<uint8_t>> asBytes() const;
    const DBusValue* find(std::string_view key) const;
};

size_t completeTypeLength(std::string_view signature, size_t position);
bool isValidSignature(std::string_view signature);
std::vector<std::string> splitSignature(std::string_view signature);
size_t alignmentOf(char type);
void marshalValue(std::string& out, const DBusValue& value);

enum class DBusMessageType : uint8_t { Invalid = 0, MethodCall = 1, MethodReturn = 2, Error = 3, Signal = 4 };

constexpr uint8_t kDBusNoReplyExpected = 0x1;

struct DBusMessage {
    DBusMessageType type = DBusMessageType::Invalid;
    uint8_t flags = 0;
    uint32_t serial = 0;
    uint32_t replySerial = 0;
    std::string path;
    std::string interface;
    std::string member;
    std::string errorName;
    std::string destination;
    std::string sender;
    std::string signature;
    uint32_t unixFds = 0;
    std::vector<int> fds;
    std::vector<DBusValue> body;

    std::string errorText() const;
};

DBusMessage dbusMethodCall(std::string destination, std::string path, std::string interface, std::string member,
                           std::vector<DBusValue> body = {});
DBusMessage dbusErrorReply(const DBusMessage& call, std::string errorName, const std::string& message);
std::string encodeMessage(const DBusMessage& message, uint32_t serial);

enum class FrameStatus { NeedMore, Invalid, Ready };

struct MessageFrame {
    FrameStatus status = FrameStatus::NeedMore;
    size_t length = 0;
};

MessageFrame measureMessage(std::string_view buffered);
std::optional<DBusMessage> decodeMessage(std::string_view bytes);

}

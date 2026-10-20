#include "dbus_wire.hpp"

#include <cstring>

namespace bsc {

namespace {

constexpr int kMaxDepth = 32;
constexpr uint32_t kMaxArrayBytes = 64u * 1024u * 1024u;
constexpr size_t kMaxMessageBytes = 128u * 1024u * 1024u;
constexpr size_t kFixedHeaderBytes = 16;

enum HeaderField : uint8_t {
    kFieldPath = 1,
    kFieldInterface = 2,
    kFieldMember = 3,
    kFieldErrorName = 4,
    kFieldReplySerial = 5,
    kFieldDestination = 6,
    kFieldSender = 7,
    kFieldSignature = 8,
    kFieldUnixFds = 9,
};

bool isBasicType(char type) {
    switch (type) {
        case 'y': case 'b': case 'n': case 'q': case 'i': case 'u': case 'x': case 't':
        case 'd': case 's': case 'o': case 'g': case 'h':
            return true;
        default:
            return false;
    }
}

size_t typeLength(std::string_view signature, size_t position, int depth) {
    if (position >= signature.size() || depth > kMaxDepth) {
        return 0;
    }
    const char type = signature[position];
    if (isBasicType(type) || type == 'v') {
        return 1;
    }
    if (type == 'a') {
        const size_t element = typeLength(signature, position + 1, depth + 1);
        return element == 0 ? 0 : element + 1;
    }
    if (type == '(') {
        size_t cursor = position + 1;
        size_t fields = 0;
        while (cursor < signature.size() && signature[cursor] != ')') {
            const size_t field = typeLength(signature, cursor, depth + 1);
            if (field == 0) {
                return 0;
            }
            cursor += field;
            ++fields;
        }
        if (cursor >= signature.size() || fields == 0) {
            return 0;
        }
        return cursor + 1 - position;
    }
    if (type == '{') {
        if (position == 0 || signature[position - 1] != 'a' || position + 1 >= signature.size() ||
            !isBasicType(signature[position + 1])) {
            return 0;
        }
        const size_t value = typeLength(signature, position + 2, depth + 1);
        if (value == 0) {
            return 0;
        }
        const size_t close = position + 2 + value;
        if (close >= signature.size() || signature[close] != '}') {
            return 0;
        }
        return close + 1 - position;
    }
    return 0;
}

void pad(std::string& out, size_t alignment) {
    while (out.size() % alignment != 0) {
        out.push_back('\0');
    }
}

void putLittle(std::string& out, uint64_t value, int bytes) {
    for (int i = 0; i < bytes; ++i) {
        out.push_back(static_cast<char>((value >> (8 * i)) & 0xFF));
    }
}

void putAt(std::string& out, size_t position, uint32_t value) {
    for (int i = 0; i < 4; ++i) {
        out[position + i] = static_cast<char>((value >> (8 * i)) & 0xFF);
    }
}

class Reader {
public:
    Reader(std::string_view data, size_t position, bool bigEndian)
        : data_(data), position_(position), bigEndian_(bigEndian) {}

    size_t position() const { return position_; }
    size_t size() const { return data_.size(); }

    bool align(size_t alignment) {
        const size_t aligned = (position_ + alignment - 1) / alignment * alignment;
        if (aligned > data_.size()) {
            return false;
        }
        position_ = aligned;
        return true;
    }

    bool read(int bytes, uint64_t* value) {
        if (position_ + static_cast<size_t>(bytes) > data_.size()) {
            return false;
        }
        uint64_t result = 0;
        for (int i = 0; i < bytes; ++i) {
            const uint64_t byte = static_cast<uint8_t>(data_[position_ + i]);
            result |= bigEndian_ ? byte << (8 * (bytes - 1 - i)) : byte << (8 * i);
        }
        position_ += static_cast<size_t>(bytes);
        *value = result;
        return true;
    }

    bool readText(size_t length, std::string* text) {
        if (position_ + length + 1 > data_.size() || data_[position_ + length] != '\0') {
            return false;
        }
        text->assign(data_.substr(position_, length));
        if (text->find('\0') != std::string::npos) {
            return false;
        }
        position_ += length + 1;
        return true;
    }

private:
    std::string_view data_;
    size_t position_;
    bool bigEndian_;
};

uint64_t signExtend(uint64_t value, int bytes) {
    const int shift = 64 - 8 * bytes;
    return static_cast<uint64_t>(static_cast<int64_t>(value << shift) >> shift);
}

std::optional<DBusValue> readValue(Reader& reader, std::string_view signature, int depth) {
    if (depth > kMaxDepth || signature.empty()) {
        return std::nullopt;
    }
    DBusValue value;
    value.signature = std::string(signature);
    const char type = signature[0];
    uint64_t raw = 0;
    switch (type) {
        case 'y':
            if (!reader.read(1, &raw)) return std::nullopt;
            value.bits = raw;
            return value;
        case 'b':
            if (!reader.align(4) || !reader.read(4, &raw) || raw > 1) return std::nullopt;
            value.bits = raw;
            return value;
        case 'n':
        case 'q':
            if (!reader.align(2) || !reader.read(2, &raw)) return std::nullopt;
            value.bits = type == 'n' ? signExtend(raw, 2) : raw;
            return value;
        case 'i':
        case 'u':
        case 'h':
            if (!reader.align(4) || !reader.read(4, &raw)) return std::nullopt;
            value.bits = type == 'i' ? signExtend(raw, 4) : raw;
            return value;
        case 'x':
        case 't':
            if (!reader.align(8) || !reader.read(8, &raw)) return std::nullopt;
            value.bits = raw;
            return value;
        case 'd':
            if (!reader.align(8) || !reader.read(8, &raw)) return std::nullopt;
            std::memcpy(&value.real, &raw, sizeof(value.real));
            value.bits = raw;
            return value;
        case 's':
        case 'o':
            if (!reader.align(4) || !reader.read(4, &raw) || !reader.readText(static_cast<size_t>(raw), &value.text)) {
                return std::nullopt;
            }
            return value;
        case 'g':
            if (!reader.read(1, &raw) || !reader.readText(static_cast<size_t>(raw), &value.text) ||
                (!value.text.empty() && !isValidSignature(value.text))) {
                return std::nullopt;
            }
            return value;
        case 'v': {
            uint64_t length = 0;
            std::string inner;
            if (!reader.read(1, &length) || !reader.readText(static_cast<size_t>(length), &inner) ||
                completeTypeLength(inner, 0) != inner.size()) {
                return std::nullopt;
            }
            std::optional<DBusValue> content = readValue(reader, inner, depth + 1);
            if (!content) return std::nullopt;
            value.items.push_back(std::move(*content));
            return value;
        }
        case 'a': {
            const std::string_view element = signature.substr(1);
            if (!reader.align(4) || !reader.read(4, &raw) || raw > kMaxArrayBytes ||
                !reader.align(alignmentOf(element[0]))) {
                return std::nullopt;
            }
            const size_t end = reader.position() + static_cast<size_t>(raw);
            if (end > reader.size()) return std::nullopt;
            while (reader.position() < end) {
                std::optional<DBusValue> item = readValue(reader, element, depth + 1);
                if (!item) return std::nullopt;
                value.items.push_back(std::move(*item));
            }
            if (reader.position() != end) return std::nullopt;
            return value;
        }
        case '(':
        case '{': {
            if (!reader.align(8)) return std::nullopt;
            const std::string_view inside = signature.substr(1, signature.size() - 2);
            size_t cursor = 0;
            while (cursor < inside.size()) {
                const size_t length = completeTypeLength(inside, cursor);
                if (length == 0) return std::nullopt;
                std::optional<DBusValue> field = readValue(reader, inside.substr(cursor, length), depth + 1);
                if (!field) return std::nullopt;
                value.items.push_back(std::move(*field));
                cursor += length;
            }
            return value;
        }
        default:
            return std::nullopt;
    }
}

DBusValue scalar(const char* signature, uint64_t bits) {
    DBusValue value;
    value.signature = signature;
    value.bits = bits;
    return value;
}

DBusValue textValue(const char* signature, std::string text) {
    DBusValue value;
    value.signature = signature;
    value.text = std::move(text);
    return value;
}

void addField(std::vector<DBusValue>& fields, uint8_t code, DBusValue value) {
    fields.push_back(DBusValue::structure({DBusValue::byte(code), DBusValue::variant(std::move(value))}));
}

}

DBusValue DBusValue::byte(uint8_t value) { return scalar("y", value); }
DBusValue DBusValue::boolean(bool value) { return scalar("b", value ? 1 : 0); }
DBusValue DBusValue::int16(int16_t value) { return scalar("n", static_cast<uint64_t>(static_cast<int64_t>(value))); }
DBusValue DBusValue::uint16(uint16_t value) { return scalar("q", value); }
DBusValue DBusValue::int32(int32_t value) { return scalar("i", static_cast<uint64_t>(static_cast<int64_t>(value))); }
DBusValue DBusValue::uint32(uint32_t value) { return scalar("u", value); }
DBusValue DBusValue::int64(int64_t value) { return scalar("x", static_cast<uint64_t>(value)); }
DBusValue DBusValue::uint64(uint64_t value) { return scalar("t", value); }

DBusValue DBusValue::number(double value) {
    DBusValue result;
    result.signature = "d";
    result.real = value;
    std::memcpy(&result.bits, &value, sizeof(value));
    return result;
}

DBusValue DBusValue::string(std::string value) { return textValue("s", std::move(value)); }
DBusValue DBusValue::objectPath(std::string value) { return textValue("o", std::move(value)); }
DBusValue DBusValue::signatureText(std::string value) { return textValue("g", std::move(value)); }

DBusValue DBusValue::variant(DBusValue inner) {
    DBusValue value;
    value.signature = "v";
    value.items.push_back(std::move(inner));
    return value;
}

DBusValue DBusValue::array(std::string elementSignature, std::vector<DBusValue> elements) {
    DBusValue value;
    value.signature = "a" + elementSignature;
    value.items = std::move(elements);
    return value;
}

DBusValue DBusValue::structure(std::vector<DBusValue> fields) {
    DBusValue value;
    value.signature = "(";
    for (const DBusValue& field : fields) {
        value.signature += field.signature;
    }
    value.signature += ")";
    value.items = std::move(fields);
    return value;
}

DBusValue DBusValue::dictEntry(DBusValue key, DBusValue entryValue) {
    DBusValue value;
    value.signature = "{" + key.signature + entryValue.signature + "}";
    value.items.push_back(std::move(key));
    value.items.push_back(std::move(entryValue));
    return value;
}

DBusValue DBusValue::bytes(const std::vector<uint8_t>& data) {
    std::vector<DBusValue> elements;
    elements.reserve(data.size());
    for (const uint8_t b : data) {
        elements.push_back(byte(b));
    }
    return array("y", std::move(elements));
}

DBusValue DBusValue::stringVariantDict(std::vector<std::pair<std::string, DBusValue>> entries) {
    std::vector<DBusValue> elements;
    for (auto& entry : entries) {
        elements.push_back(dictEntry(string(entry.first), variant(std::move(entry.second))));
    }
    return array("{sv}", std::move(elements));
}

bool DBusValue::isText() const {
    const char t = type();
    return t == 's' || t == 'o' || t == 'g';
}

const DBusValue& DBusValue::unwrapped() const {
    const DBusValue* current = this;
    while (current->type() == 'v' && !current->items.empty()) {
        current = &current->items.front();
    }
    return *current;
}

std::optional<bool> DBusValue::asBool() const {
    const DBusValue& value = unwrapped();
    if (value.type() != 'b') {
        return std::nullopt;
    }
    return value.bits != 0;
}

std::optional<uint64_t> DBusValue::asUnsigned() const {
    const DBusValue& value = unwrapped();
    switch (value.type()) {
        case 'y': case 'q': case 'u': case 't': case 'h':
            return value.bits;
        case 'n': case 'i': case 'x':
            if (static_cast<int64_t>(value.bits) < 0) return std::nullopt;
            return value.bits;
        default:
            return std::nullopt;
    }
}

std::optional<int64_t> DBusValue::asSigned() const {
    const DBusValue& value = unwrapped();
    switch (value.type()) {
        case 'y': case 'q': case 'u': case 'n': case 'i': case 'x':
            return static_cast<int64_t>(value.bits);
        case 't':
            if (value.bits > static_cast<uint64_t>(INT64_MAX)) return std::nullopt;
            return static_cast<int64_t>(value.bits);
        default:
            return std::nullopt;
    }
}

std::optional<std::string> DBusValue::asText() const {
    const DBusValue& value = unwrapped();
    if (!value.isText()) {
        return std::nullopt;
    }
    return value.text;
}

std::optional<std::vector<uint8_t>> DBusValue::asBytes() const {
    const DBusValue& value = unwrapped();
    if (value.signature != "ay") {
        return std::nullopt;
    }
    std::vector<uint8_t> data;
    data.reserve(value.items.size());
    for (const DBusValue& item : value.items) {
        data.push_back(static_cast<uint8_t>(item.bits));
    }
    return data;
}

const DBusValue* DBusValue::find(std::string_view key) const {
    const DBusValue& value = unwrapped();
    if (value.signature.size() < 4 || value.signature[0] != 'a' || value.signature[1] != '{') {
        return nullptr;
    }
    for (const DBusValue& entry : value.items) {
        if (entry.items.size() == 2 && entry.items[0].isText() && entry.items[0].text == key) {
            return &entry.items[1];
        }
    }
    return nullptr;
}

size_t completeTypeLength(std::string_view signature, size_t position) {
    return typeLength(signature, position, 0);
}

bool isValidSignature(std::string_view signature) {
    if (signature.size() > 255) {
        return false;
    }
    size_t cursor = 0;
    while (cursor < signature.size()) {
        const size_t length = completeTypeLength(signature, cursor);
        if (length == 0) {
            return false;
        }
        cursor += length;
    }
    return true;
}

std::vector<std::string> splitSignature(std::string_view signature) {
    std::vector<std::string> types;
    size_t cursor = 0;
    while (cursor < signature.size()) {
        const size_t length = completeTypeLength(signature, cursor);
        if (length == 0) {
            return {};
        }
        types.emplace_back(signature.substr(cursor, length));
        cursor += length;
    }
    return types;
}

size_t alignmentOf(char type) {
    switch (type) {
        case 'n': case 'q':
            return 2;
        case 'b': case 'i': case 'u': case 'h': case 's': case 'o': case 'a':
            return 4;
        case 'x': case 't': case 'd': case '(': case '{':
            return 8;
        default:
            return 1;
    }
}

void marshalValue(std::string& out, const DBusValue& value) {
    switch (value.type()) {
        case 'y':
            out.push_back(static_cast<char>(value.bits & 0xFF));
            break;
        case 'b':
            pad(out, 4);
            putLittle(out, value.bits != 0 ? 1 : 0, 4);
            break;
        case 'n':
        case 'q':
            pad(out, 2);
            putLittle(out, value.bits, 2);
            break;
        case 'i':
        case 'u':
        case 'h':
            pad(out, 4);
            putLittle(out, value.bits, 4);
            break;
        case 'x':
        case 't':
        case 'd':
            pad(out, 8);
            putLittle(out, value.bits, 8);
            break;
        case 's':
        case 'o':
            pad(out, 4);
            putLittle(out, value.text.size(), 4);
            out += value.text;
            out.push_back('\0');
            break;
        case 'g':
            out.push_back(static_cast<char>(value.text.size() & 0xFF));
            out += value.text;
            out.push_back('\0');
            break;
        case 'v': {
            if (value.items.empty()) {
                break;
            }
            const DBusValue& inner = value.items.front();
            out.push_back(static_cast<char>(inner.signature.size() & 0xFF));
            out += inner.signature;
            out.push_back('\0');
            marshalValue(out, inner);
            break;
        }
        case 'a': {
            pad(out, 4);
            const size_t lengthPosition = out.size();
            putLittle(out, 0, 4);
            pad(out, alignmentOf(value.signature.size() > 1 ? value.signature[1] : 'y'));
            const size_t start = out.size();
            for (const DBusValue& item : value.items) {
                marshalValue(out, item);
            }
            putAt(out, lengthPosition, static_cast<uint32_t>(out.size() - start));
            break;
        }
        case '(':
        case '{':
            pad(out, 8);
            for (const DBusValue& item : value.items) {
                marshalValue(out, item);
            }
            break;
        default:
            break;
    }
}

std::string DBusMessage::errorText() const {
    std::string text = errorName;
    if (!body.empty() && body.front().isText()) {
        text += (text.empty() ? "" : ": ") + body.front().text;
    }
    return text;
}

DBusMessage dbusMethodCall(std::string destination, std::string path, std::string interface, std::string member,
                           std::vector<DBusValue> body) {
    DBusMessage message;
    message.type = DBusMessageType::MethodCall;
    message.destination = std::move(destination);
    message.path = std::move(path);
    message.interface = std::move(interface);
    message.member = std::move(member);
    message.body = std::move(body);
    return message;
}

DBusMessage dbusErrorReply(const DBusMessage& call, std::string errorName, const std::string& text) {
    DBusMessage message;
    message.type = DBusMessageType::Error;
    message.flags = kDBusNoReplyExpected;
    message.replySerial = call.serial;
    message.destination = call.sender;
    message.errorName = std::move(errorName);
    message.body.push_back(DBusValue::string(text));
    return message;
}

std::string encodeMessage(const DBusMessage& message, uint32_t serial) {
    std::string body;
    std::string signature;
    for (const DBusValue& value : message.body) {
        marshalValue(body, value);
        signature += value.signature;
    }

    std::vector<DBusValue> fields;
    if (!message.path.empty()) addField(fields, kFieldPath, DBusValue::objectPath(message.path));
    if (!message.interface.empty()) addField(fields, kFieldInterface, DBusValue::string(message.interface));
    if (!message.member.empty()) addField(fields, kFieldMember, DBusValue::string(message.member));
    if (!message.errorName.empty()) addField(fields, kFieldErrorName, DBusValue::string(message.errorName));
    if (message.type == DBusMessageType::MethodReturn || message.type == DBusMessageType::Error) {
        addField(fields, kFieldReplySerial, DBusValue::uint32(message.replySerial));
    }
    if (!message.destination.empty()) addField(fields, kFieldDestination, DBusValue::string(message.destination));
    if (!signature.empty()) addField(fields, kFieldSignature, DBusValue::signatureText(signature));
    if (message.unixFds) addField(fields, kFieldUnixFds, DBusValue::uint32(message.unixFds));

    std::string out;
    out.push_back('l');
    out.push_back(static_cast<char>(message.type));
    out.push_back(static_cast<char>(message.flags));
    out.push_back('\1');
    putLittle(out, body.size(), 4);
    putLittle(out, serial, 4);
    marshalValue(out, DBusValue::array("(yv)", std::move(fields)));
    pad(out, 8);
    out += body;
    return out;
}

MessageFrame measureMessage(std::string_view buffered) {
    MessageFrame frame;
    if (buffered.size() < kFixedHeaderBytes) {
        return frame;
    }
    const char endian = buffered[0];
    if ((endian != 'l' && endian != 'B') || buffered[3] != '\1') {
        frame.status = FrameStatus::Invalid;
        return frame;
    }
    Reader reader(buffered, 4, endian == 'B');
    uint64_t bodyLength = 0;
    uint64_t serial = 0;
    uint64_t fieldsLength = 0;
    reader.read(4, &bodyLength);
    reader.read(4, &serial);
    reader.read(4, &fieldsLength);
    const uint64_t header = (kFixedHeaderBytes + fieldsLength + 7) / 8 * 8;
    const uint64_t total = header + bodyLength;
    if (fieldsLength > kMaxArrayBytes || total > kMaxMessageBytes) {
        frame.status = FrameStatus::Invalid;
        return frame;
    }
    frame.length = static_cast<size_t>(total);
    frame.status = buffered.size() >= frame.length ? FrameStatus::Ready : FrameStatus::NeedMore;
    return frame;
}

std::optional<DBusMessage> decodeMessage(std::string_view bytes) {
    const MessageFrame frame = measureMessage(bytes);
    if (frame.status != FrameStatus::Ready || frame.length != bytes.size()) {
        return std::nullopt;
    }
    const bool bigEndian = bytes[0] == 'B';
    DBusMessage message;
    message.type = static_cast<DBusMessageType>(static_cast<uint8_t>(bytes[1]));
    message.flags = static_cast<uint8_t>(bytes[2]);
    Reader reader(bytes, 4, bigEndian);
    uint64_t bodyLength = 0;
    uint64_t serial = 0;
    reader.read(4, &bodyLength);
    reader.read(4, &serial);
    message.serial = static_cast<uint32_t>(serial);

    const std::optional<DBusValue> fields = readValue(reader, "a(yv)", 0);
    if (!fields || !reader.align(8)) {
        return std::nullopt;
    }
    for (const DBusValue& field : fields->items) {
        if (field.items.size() != 2) {
            continue;
        }
        const DBusValue& content = field.items[1].unwrapped();
        switch (field.items[0].bits) {
            case kFieldPath: message.path = content.text; break;
            case kFieldInterface: message.interface = content.text; break;
            case kFieldMember: message.member = content.text; break;
            case kFieldErrorName: message.errorName = content.text; break;
            case kFieldReplySerial: message.replySerial = static_cast<uint32_t>(content.bits); break;
            case kFieldDestination: message.destination = content.text; break;
            case kFieldSender: message.sender = content.text; break;
            case kFieldSignature: message.signature = content.text; break;
            case kFieldUnixFds: message.unixFds = static_cast<uint32_t>(content.bits); break;
            default: break;
        }
    }
    if (reader.position() + bodyLength != bytes.size()) {
        return std::nullopt;
    }
    const std::vector<std::string> types = splitSignature(message.signature);
    if (types.empty() && !message.signature.empty()) {
        return std::nullopt;
    }
    for (const std::string& type : types) {
        std::optional<DBusValue> value = readValue(reader, type, 0);
        if (!value) {
            return std::nullopt;
        }
        message.body.push_back(std::move(*value));
    }
    if (reader.position() != bytes.size()) {
        return std::nullopt;
    }
    return message;
}

}

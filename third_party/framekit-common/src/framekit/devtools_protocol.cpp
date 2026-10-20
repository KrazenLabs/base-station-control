#include "framekit/devtools.hpp"

#include <json/json.h>

#include "framekit/json_read.hpp"
#include "framekit/text.hpp"

namespace framekit {

namespace {

std::string queryValue(const std::string& url, const std::string& name) {
    const size_t query = url.find('?');
    if (query == std::string::npos) {
        return "";
    }
    size_t position = query + 1;
    while (position < url.size()) {
        size_t end = url.find('&', position);
        if (end == std::string::npos) {
            end = url.size();
        }
        const std::string pair = url.substr(position, end - position);
        if (startsWith(pair, name + "=")) {
            return pair.substr(name.size() + 1);
        }
        position = end + 1;
    }
    return "";
}

}

std::string base64Encode(std::string_view data) {
    static const char alphabet[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string out;
    size_t i = 0;
    while (i + 2 < data.size()) {
        const uint32_t chunk = (static_cast<uint8_t>(data[i]) << 16) | (static_cast<uint8_t>(data[i + 1]) << 8) |
                               static_cast<uint8_t>(data[i + 2]);
        out += alphabet[(chunk >> 18) & 63];
        out += alphabet[(chunk >> 12) & 63];
        out += alphabet[(chunk >> 6) & 63];
        out += alphabet[chunk & 63];
        i += 3;
    }
    if (i + 1 == data.size()) {
        const uint32_t chunk = static_cast<uint8_t>(data[i]) << 16;
        out += alphabet[(chunk >> 18) & 63];
        out += alphabet[(chunk >> 12) & 63];
        out += "==";
    } else if (i + 2 == data.size()) {
        const uint32_t chunk = (static_cast<uint8_t>(data[i]) << 16) | (static_cast<uint8_t>(data[i + 1]) << 8);
        out += alphabet[(chunk >> 18) & 63];
        out += alphabet[(chunk >> 12) & 63];
        out += alphabet[(chunk >> 6) & 63];
        out += '=';
    }
    return out;
}

std::string encodeClientFrame(int opcode, std::string_view payload, const uint8_t mask[4]) {
    std::string frame;
    frame += static_cast<char>(0x80 | (opcode & 0x0F));
    const uint64_t length = payload.size();
    if (length < 126) {
        frame += static_cast<char>(0x80 | length);
    } else if (length < 65536) {
        frame += static_cast<char>(0x80 | 126);
        frame += static_cast<char>((length >> 8) & 0xFF);
        frame += static_cast<char>(length & 0xFF);
    } else {
        frame += static_cast<char>(0x80 | 127);
        for (int shift = 56; shift >= 0; shift -= 8) {
            frame += static_cast<char>((length >> shift) & 0xFF);
        }
    }
    for (int i = 0; i < 4; ++i) {
        frame += static_cast<char>(mask[i]);
    }
    for (size_t i = 0; i < payload.size(); ++i) {
        frame += static_cast<char>(static_cast<uint8_t>(payload[i]) ^ mask[i % 4]);
    }
    return frame;
}

std::optional<FrameHeader> parseFrameHeader(std::string_view bytes) {
    if (bytes.size() < 2) {
        return std::nullopt;
    }
    const uint8_t first = static_cast<uint8_t>(bytes[0]);
    const uint8_t second = static_cast<uint8_t>(bytes[1]);
    FrameHeader header;
    header.final = (first & 0x80) != 0;
    header.opcode = first & 0x0F;
    header.masked = (second & 0x80) != 0;
    size_t position = 2;
    const uint8_t shortLength = second & 0x7F;
    if (shortLength == 126) {
        if (bytes.size() < 4) {
            return std::nullopt;
        }
        header.payloadLength = (static_cast<uint64_t>(static_cast<uint8_t>(bytes[2])) << 8) |
                               static_cast<uint8_t>(bytes[3]);
        position = 4;
    } else if (shortLength == 127) {
        if (bytes.size() < 10) {
            return std::nullopt;
        }
        for (int i = 0; i < 8; ++i) {
            header.payloadLength = (header.payloadLength << 8) | static_cast<uint8_t>(bytes[2 + i]);
        }
        position = 10;
    } else {
        header.payloadLength = shortLength;
    }
    if (header.masked) {
        if (bytes.size() < position + 4) {
            return std::nullopt;
        }
        for (int i = 0; i < 4; ++i) {
            header.mask[i] = static_cast<uint8_t>(bytes[position + i]);
        }
        position += 4;
    }
    header.headerLength = position;
    return header;
}

std::optional<std::string> findOverlayPagePath(const std::string& targetsJson, const std::string& overlayKey,
                                               bool keyIsPrefix) {
    const std::optional<Json::Value> targets = parseJson(targetsJson);
    if (!targets || !targets->isArray()) {
        return std::nullopt;
    }
    for (const Json::Value& target : *targets) {
        if (textMember(target, "type") != "page") {
            continue;
        }
        const std::string key = queryValue(textMember(target, "url"), "vrOverlayKey");
        if (keyIsPrefix ? !startsWith(key, overlayKey) : key != overlayKey) {
            continue;
        }
        const std::string socketUrl = textMember(target, "webSocketDebuggerUrl");
        const size_t scheme = socketUrl.find("://");
        const size_t path = socketUrl.find('/', scheme == std::string::npos ? 0 : scheme + 3);
        if (path != std::string::npos) {
            return socketUrl.substr(path);
        }
    }
    return std::nullopt;
}

std::optional<std::string> httpBody(const std::string& response) {
    const size_t headerEnd = response.find("\r\n\r\n");
    if (headerEnd == std::string::npos) {
        return std::nullopt;
    }
    const std::string statusLine = response.substr(0, response.find("\r\n"));
    if (!contains(statusLine, " 200")) {
        return std::nullopt;
    }
    return response.substr(headerEnd + 4);
}

std::string evaluationRequest(int id, const std::string& expression) {
    Json::Value request(Json::objectValue);
    request["id"] = id;
    request["method"] = "Runtime.evaluate";
    request["params"]["expression"] = expression;
    request["params"]["returnByValue"] = true;
    Json::StreamWriterBuilder builder;
    builder["indentation"] = "";
    return Json::writeString(builder, request);
}

std::string addBindingRequest(int id, const std::string& name) {
    Json::Value request(Json::objectValue);
    request["id"] = id;
    request["method"] = "Runtime.addBinding";
    request["params"]["name"] = name;
    Json::StreamWriterBuilder builder;
    builder["indentation"] = "";
    return Json::writeString(builder, request);
}

std::optional<EvaluationResult> parseCommandReply(const std::string& message, int expectedId) {
    const std::optional<Json::Value> reply = parseJson(message);
    if (!reply || !reply->isObject() || !(*reply)["id"].isInt() || (*reply)["id"].asInt() != expectedId) {
        return std::nullopt;
    }
    if (reply->isMember("error")) {
        return EvaluationResult{false, textMember(member(*reply, "error"), "message")};
    }
    return EvaluationResult{true, std::string()};
}

std::optional<int> messageId(const std::string& message) {
    const std::optional<Json::Value> parsed = parseJson(message);
    if (!parsed || !parsed->isObject() || !(*parsed)["id"].isInt()) {
        return std::nullopt;
    }
    return (*parsed)["id"].asInt();
}

bool isErrorWithoutId(const std::string& message) {
    const std::optional<Json::Value> parsed = parseJson(message);
    return parsed && parsed->isObject() && parsed->isMember("error") && !parsed->isMember("id");
}

std::optional<std::string> bindingCallName(const std::string& message) {
    if (!contains(message, "Runtime.bindingCalled")) {
        return std::nullopt;
    }
    const std::optional<Json::Value> event = parseJson(message);
    if (!event || textMember(*event, "method") != "Runtime.bindingCalled") {
        return std::nullopt;
    }
    return textMember(member(*event, "params"), "name");
}

std::optional<EvaluationResult> parseEvaluationReply(const std::string& message, int expectedId) {
    const std::optional<Json::Value> reply = parseJson(message);
    if (!reply || !reply->isObject() || !(*reply)["id"].isInt() || (*reply)["id"].asInt() != expectedId) {
        return std::nullopt;
    }
    if (reply->isMember("error")) {
        return EvaluationResult{false, textMember(member(*reply, "error"), "message")};
    }
    const Json::Value& result = member(*reply, "result");
    if (result.isObject() && result.isMember("exceptionDetails")) {
        const Json::Value& details = member(result, "exceptionDetails");
        std::string text = textMember(member(details, "exception"), "description");
        return EvaluationResult{false, text.empty() ? textMember(details, "text") : text};
    }
    const Json::Value& value = member(member(result, "result"), "value");
    return EvaluationResult{true, value.isString() ? value.asString() : std::string()};
}

}

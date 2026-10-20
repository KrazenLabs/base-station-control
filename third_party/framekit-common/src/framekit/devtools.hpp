#pragma once

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <string_view>

namespace framekit {

std::string base64Encode(std::string_view data);
std::string encodeClientFrame(int opcode, std::string_view payload, const uint8_t mask[4]);

struct FrameHeader {
    bool final = false;
    int opcode = 0;
    bool masked = false;
    uint8_t mask[4] = {0, 0, 0, 0};
    uint64_t payloadLength = 0;
    size_t headerLength = 0;
};

std::optional<FrameHeader> parseFrameHeader(std::string_view bytes);
std::optional<std::string> findOverlayPagePath(const std::string& targetsJson, const std::string& overlayKey,
                                               bool keyIsPrefix = false);
std::optional<std::string> httpBody(const std::string& response);

struct EvaluationResult {
    bool ok = false;
    std::string value;
};

std::string evaluationRequest(int id, const std::string& expression);
std::string addBindingRequest(int id, const std::string& name);
std::optional<EvaluationResult> parseEvaluationReply(const std::string& message, int expectedId);
std::optional<EvaluationResult> parseCommandReply(const std::string& message, int expectedId);
std::optional<int> messageId(const std::string& message);
bool isErrorWithoutId(const std::string& message);
std::optional<std::string> bindingCallName(const std::string& message);

class DevToolsSession {
public:
    static std::unique_ptr<DevToolsSession> open(int port, const std::string& path, std::string* error);
    ~DevToolsSession();

    DevToolsSession(const DevToolsSession&) = delete;
    DevToolsSession& operator=(const DevToolsSession&) = delete;

    int fd() const { return socket_; }
    std::optional<EvaluationResult> evaluate(const std::string& expression);
    bool addBinding(const std::string& name);
    bool drainEvents();
    int takeBindingCalls();

private:
    explicit DevToolsSession(int socket) : socket_(socket) {}
    bool sendFrame(int opcode, std::string_view payload);
    bool waitReadable();
    bool fill(size_t count);
    bool frameBuffered() const;
    std::optional<std::string> receiveMessage();
    std::optional<std::string> exchange(const std::string& request, int id);
    void noteEvent(const std::string& message);

    int socket_ = -1;
    int nextId_ = 0;
    int bindingCalls_ = 0;
    double deadline_ = 0.0;
    std::string bindingName_;
    std::string buffered_;
};

std::optional<std::string> httpGetLocal(int port, const std::string& path, std::string* error);

}

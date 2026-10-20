#include "framekit/devtools.hpp"

#include <cerrno>
#include <cmath>
#include <cstring>
#include <random>

#include <arpa/inet.h>
#include <netinet/in.h>
#include <poll.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <unistd.h>

#include "framekit/clock.hpp"
#include "framekit/text.hpp"

namespace framekit {

namespace {

constexpr int kTimeoutSeconds = 2;
constexpr double kExchangeSeconds = 5.0;
constexpr double kDrainSeconds = 1.0;
constexpr size_t kMaxMessageBytes = 4 * 1024 * 1024;
constexpr size_t kMaxHttpBytes = 1024 * 1024;

int connectLocal(int port, std::string* error) {
    const int fd = ::socket(AF_INET, SOCK_STREAM | SOCK_CLOEXEC, 0);
    if (fd < 0) {
        if (error) {
            *error = std::strerror(errno);
        }
        return -1;
    }
    timeval timeout{};
    timeout.tv_sec = kTimeoutSeconds;
    ::setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));
    ::setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &timeout, sizeof(timeout));
    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_port = htons(static_cast<uint16_t>(port));
    address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    if (::connect(fd, reinterpret_cast<sockaddr*>(&address), sizeof(address)) != 0) {
        if (error) {
            *error = std::strerror(errno);
        }
        ::close(fd);
        return -1;
    }
    return fd;
}

bool sendAll(int fd, std::string_view data) {
    size_t sent = 0;
    while (sent < data.size()) {
        const ssize_t result = ::send(fd, data.data() + sent, data.size() - sent, MSG_NOSIGNAL);
        if (result < 0 && errno == EINTR) {
            continue;
        }
        if (result <= 0) {
            return false;
        }
        sent += static_cast<size_t>(result);
    }
    return true;
}

ssize_t receiveSome(int fd, std::string& into) {
    char chunk[16384];
    while (true) {
        const ssize_t result = ::recv(fd, chunk, sizeof(chunk), 0);
        if (result < 0 && errno == EINTR) {
            continue;
        }
        if (result > 0) {
            into.append(chunk, static_cast<size_t>(result));
        }
        return result;
    }
}

void randomBytes(uint8_t* out, size_t count) {
    static std::random_device device;
    for (size_t i = 0; i < count; ++i) {
        out[i] = static_cast<uint8_t>(device() & 0xFF);
    }
}

}

std::optional<std::string> httpGetLocal(int port, const std::string& path, std::string* error) {
    const int fd = connectLocal(port, error);
    if (fd < 0) {
        return std::nullopt;
    }
    const std::string request = "GET " + path + " HTTP/1.1\r\nHost: 127.0.0.1:" + std::to_string(port) +
                                "\r\nConnection: close\r\n\r\n";
    std::string response;
    if (sendAll(fd, request)) {
        while (response.size() < kMaxHttpBytes && receiveSome(fd, response) > 0) {
        }
    }
    ::close(fd);
    std::optional<std::string> body = httpBody(response);
    if (!body && error) {
        *error = response.empty() ? "no response" : "unexpected response: " + response.substr(0, response.find("\r\n"));
    }
    return body;
}

std::unique_ptr<DevToolsSession> DevToolsSession::open(int port, const std::string& path, std::string* error) {
    const int fd = connectLocal(port, error);
    if (fd < 0) {
        return nullptr;
    }
    uint8_t key[16];
    randomBytes(key, sizeof(key));
    const std::string request = "GET " + path + " HTTP/1.1\r\nHost: 127.0.0.1:" + std::to_string(port) +
                                "\r\nUpgrade: websocket\r\nConnection: Upgrade\r\nSec-WebSocket-Key: " +
                                base64Encode(std::string_view(reinterpret_cast<const char*>(key), sizeof(key))) +
                                "\r\nSec-WebSocket-Version: 13\r\n\r\n";
    std::string response;
    bool complete = sendAll(fd, request);
    while (complete && response.find("\r\n\r\n") == std::string::npos) {
        if (response.size() > 16384 || receiveSome(fd, response) <= 0) {
            complete = false;
        }
    }
    const std::string statusLine = response.substr(0, response.find("\r\n"));
    if (!complete || !contains(statusLine, " 101")) {
        if (error) {
            *error = complete ? "handshake refused: " + statusLine : "no handshake response";
        }
        ::close(fd);
        return nullptr;
    }
    std::unique_ptr<DevToolsSession> session(new DevToolsSession(fd));
    session->buffered_ = response.substr(response.find("\r\n\r\n") + 4);
    return session;
}

DevToolsSession::~DevToolsSession() {
    if (socket_ >= 0) {
        ::close(socket_);
    }
}

bool DevToolsSession::sendFrame(int opcode, std::string_view payload) {
    uint8_t mask[4];
    randomBytes(mask, sizeof(mask));
    return sendAll(socket_, encodeClientFrame(opcode, payload, mask));
}

bool DevToolsSession::waitReadable() {
    if (deadline_ <= 0.0) {
        return true;
    }
    while (true) {
        const double left = deadline_ - monotonicSeconds();
        if (left <= 0.0) {
            return false;
        }
        pollfd entry{socket_, POLLIN, 0};
        const int ready = ::poll(&entry, 1, static_cast<int>(std::ceil(left * 1000.0)));
        if (ready > 0) {
            return true;
        }
        if (ready < 0 && errno == EINTR) {
            continue;
        }
        return false;
    }
}

bool DevToolsSession::fill(size_t count) {
    while (buffered_.size() < count) {
        if (!waitReadable() || receiveSome(socket_, buffered_) <= 0) {
            return false;
        }
    }
    return true;
}

std::optional<std::string> DevToolsSession::receiveMessage() {
    std::string message;
    while (true) {
        std::optional<FrameHeader> header;
        while (!(header = parseFrameHeader(buffered_))) {
            if (!fill(buffered_.size() + 1)) {
                return std::nullopt;
            }
        }
        if (header->payloadLength > kMaxMessageBytes || message.size() + header->payloadLength > kMaxMessageBytes) {
            return std::nullopt;
        }
        const size_t total = header->headerLength + static_cast<size_t>(header->payloadLength);
        if (!fill(total)) {
            return std::nullopt;
        }
        std::string payload = buffered_.substr(header->headerLength, static_cast<size_t>(header->payloadLength));
        buffered_.erase(0, total);
        if (header->masked) {
            for (size_t i = 0; i < payload.size(); ++i) {
                payload[i] = static_cast<char>(static_cast<uint8_t>(payload[i]) ^ header->mask[i % 4]);
            }
        }
        switch (header->opcode) {
            case 0x0:
            case 0x1:
            case 0x2:
                message += payload;
                if (header->final) {
                    return message;
                }
                break;
            case 0x8:
                return std::nullopt;
            case 0x9:
                if (!sendFrame(0xA, payload)) {
                    return std::nullopt;
                }
                break;
            default:
                break;
        }
    }
}

void DevToolsSession::noteEvent(const std::string& message) {
    const std::optional<std::string> name = bindingCallName(message);
    if (name && !bindingName_.empty() && *name == bindingName_) {
        ++bindingCalls_;
    }
}

std::optional<std::string> DevToolsSession::exchange(const std::string& request, int id) {
    if (!sendFrame(0x1, request)) {
        return std::nullopt;
    }
    deadline_ = monotonicSeconds() + kExchangeSeconds;
    std::optional<std::string> reply;
    for (int skipped = 0; skipped < 1000; ++skipped) {
        std::optional<std::string> message = receiveMessage();
        if (!message || isErrorWithoutId(*message)) {
            break;
        }
        if (messageId(*message) == id) {
            reply = std::move(message);
            break;
        }
        noteEvent(*message);
    }
    deadline_ = 0.0;
    return reply;
}

std::optional<EvaluationResult> DevToolsSession::evaluate(const std::string& expression) {
    const int id = ++nextId_;
    const std::optional<std::string> message = exchange(evaluationRequest(id, expression), id);
    if (!message) {
        return std::nullopt;
    }
    return parseEvaluationReply(*message, id);
}

bool DevToolsSession::addBinding(const std::string& name) {
    bindingName_ = name;
    const int id = ++nextId_;
    const std::optional<std::string> message = exchange(addBindingRequest(id, name), id);
    if (!message) {
        return false;
    }
    const std::optional<EvaluationResult> result = parseCommandReply(*message, id);
    return result && result->ok;
}

bool DevToolsSession::frameBuffered() const {
    const std::optional<FrameHeader> header = parseFrameHeader(buffered_);
    return header && buffered_.size() >= header->headerLength + header->payloadLength;
}

bool DevToolsSession::drainEvents() {
    char chunk[16384];
    while (true) {
        const ssize_t result = ::recv(socket_, chunk, sizeof(chunk), MSG_DONTWAIT);
        if (result > 0) {
            buffered_.append(chunk, static_cast<size_t>(result));
            continue;
        }
        if (result == 0) {
            return false;
        }
        if (errno == EINTR) {
            continue;
        }
        if (errno == EAGAIN || errno == EWOULDBLOCK) {
            break;
        }
        return false;
    }
    deadline_ = monotonicSeconds() + kDrainSeconds;
    bool open = true;
    while (open && frameBuffered()) {
        const std::optional<std::string> message = receiveMessage();
        if (message) {
            noteEvent(*message);
        } else {
            open = false;
        }
    }
    deadline_ = 0.0;
    return open;
}

int DevToolsSession::takeBindingCalls() {
    const int calls = bindingCalls_;
    bindingCalls_ = 0;
    return calls;
}

}

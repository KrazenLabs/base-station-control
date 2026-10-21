#include "dbus_connection.hpp"

#include <cerrno>
#include <cstddef>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <optional>
#include <vector>

#include <sys/socket.h>
#include <sys/time.h>
#include <sys/un.h>
#include <unistd.h>

#include "text.hpp"

namespace bsc {

const char* const kDBusTimeoutError = "bsc.Error.Timeout";
const char* const kDBusDisconnectedError = "bsc.Error.Disconnected";

namespace {

constexpr const char* kDefaultSystemBus = "/run/dbus/system_bus_socket";
constexpr int kSocketTimeoutSeconds = 3;
constexpr size_t kMaxBufferedBytes = 160u * 1024u * 1024u;
constexpr size_t kMaxFdsPerRead = 16;
constexpr size_t kMaxQueuedFds = 64;

void closeFds(const std::vector<int>& fds) {
    for (const int fd : fds) {
        ::close(fd);
    }
}

std::string systemBusPath() {
    const char* address = std::getenv("DBUS_SYSTEM_BUS_ADDRESS");
    if (!address) {
        return kDefaultSystemBus;
    }
    std::string text(address);
    size_t start = 0;
    while (start <= text.size()) {
        size_t end = text.find(';', start);
        if (end == std::string::npos) {
            end = text.size();
        }
        const std::string entry = text.substr(start, end - start);
        if (startsWith(entry, "unix:")) {
            size_t keyStart = 5;
            while (keyStart < entry.size()) {
                size_t keyEnd = entry.find(',', keyStart);
                if (keyEnd == std::string::npos) {
                    keyEnd = entry.size();
                }
                const std::string pair = entry.substr(keyStart, keyEnd - keyStart);
                if (startsWith(pair, "path=") && pair.size() > 5) {
                    return pair.substr(5);
                }
                keyStart = keyEnd + 1;
            }
        }
        start = end + 1;
    }
    return kDefaultSystemBus;
}

DBusMessage syntheticError(uint32_t replySerial, const char* name, const std::string& text) {
    DBusMessage message;
    message.type = DBusMessageType::Error;
    message.replySerial = replySerial;
    message.errorName = name;
    if (!text.empty()) {
        message.body.push_back(DBusValue::string(text));
    }
    return message;
}

}

std::unique_ptr<Bus> DBusConnection::openSystemBus(double monotonicNow, std::string* error) {
    return connectTo(systemBusPath(), monotonicNow, error);
}

std::unique_ptr<DBusConnection> DBusConnection::connectTo(const std::string& path, double monotonicNow, std::string* error) {
    const int fd = ::socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0);
    if (fd < 0) {
        *error = std::strerror(errno);
        return nullptr;
    }
    timeval timeout{};
    timeout.tv_sec = kSocketTimeoutSeconds;
    ::setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));
    ::setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &timeout, sizeof(timeout));
    sockaddr_un address{};
    address.sun_family = AF_UNIX;
    if (path.size() >= sizeof(address.sun_path)) {
        ::close(fd);
        *error = "bus socket path too long: " + path;
        return nullptr;
    }
    std::memcpy(address.sun_path, path.c_str(), path.size() + 1);
    if (::connect(fd, reinterpret_cast<sockaddr*>(&address), sizeof(address)) != 0) {
        *error = path + ": " + std::strerror(errno);
        ::close(fd);
        return nullptr;
    }
    std::unique_ptr<DBusConnection> connection(new DBusConnection(fd));
    connection->now_ = monotonicNow;
    if (!connection->authenticate(error) || !connection->hello(error)) {
        return nullptr;
    }
    return connection;
}

DBusConnection::~DBusConnection() {
    close("connection closed");
}

void DBusConnection::close(const std::string& reason) {
    if (socket_ >= 0) {
        ::close(socket_);
        socket_ = -1;
    }
    closeReceivedFds();
    if (closeReason_.empty()) {
        closeReason_ = reason;
    }
}

void DBusConnection::closeReceivedFds() {
    for (const ReceivedFd& received : receivedFds_) {
        ::close(received.fd);
    }
    receivedFds_.clear();
}

void DBusConnection::consumeBuffered(size_t length) {
    buffered_.erase(0, length);
    bufferedStart_ += length;
}

std::vector<int> DBusConnection::takeFdsWithin(uint64_t streamStart, uint64_t streamEnd) {
    std::vector<int> fds;
    while (!receivedFds_.empty() && receivedFds_.front().streamEnd <= streamEnd) {
        const ReceivedFd received = receivedFds_.front();
        receivedFds_.pop_front();
        if (received.streamEnd > streamStart) {
            fds.push_back(received.fd);
        } else {
            ::close(received.fd);
        }
    }
    return fds;
}

void DBusConnection::receiveFds(const void* control, size_t length, uint64_t streamEnd) {
    msghdr header{};
    header.msg_control = const_cast<void*>(control);
    header.msg_controllen = length;
    for (cmsghdr* entry = CMSG_FIRSTHDR(&header); entry; entry = CMSG_NXTHDR(&header, entry)) {
        if (entry->cmsg_level != SOL_SOCKET || entry->cmsg_type != SCM_RIGHTS) {
            continue;
        }
        const size_t count = (entry->cmsg_len - CMSG_LEN(0)) / sizeof(int);
        for (size_t i = 0; i < count; ++i) {
            int fd = -1;
            std::memcpy(&fd, CMSG_DATA(entry) + i * sizeof(int), sizeof(int));
            if (fd < 0) {
                continue;
            }
            if (receivedFds_.size() >= kMaxQueuedFds) {
                ::close(fd);
                continue;
            }
            receivedFds_.push_back(ReceivedFd{fd, streamEnd});
        }
    }
}

bool DBusConnection::sendRaw(const std::string& bytes) {
    size_t sent = 0;
    while (sent < bytes.size()) {
        if (socket_ < 0) {
            return false;
        }
        const ssize_t result = ::send(socket_, bytes.data() + sent, bytes.size() - sent, MSG_NOSIGNAL);
        if (result < 0 && errno == EINTR) {
            continue;
        }
        if (result <= 0) {
            close(std::string("send failed: ") + std::strerror(errno));
            return false;
        }
        sent += static_cast<size_t>(result);
    }
    return true;
}

bool DBusConnection::readBlocking(std::string* into) {
    char chunk[16384];
    while (socket_ >= 0) {
        const ssize_t result = ::recv(socket_, chunk, sizeof(chunk), 0);
        if (result < 0 && errno == EINTR) {
            continue;
        }
        if (result <= 0) {
            return false;
        }
        into->append(chunk, static_cast<size_t>(result));
        return true;
    }
    return false;
}

bool DBusConnection::readAuthLine(std::string* line) {
    while (buffered_.find("\r\n") == std::string::npos) {
        if (buffered_.size() > 4096 || !readBlocking(&buffered_)) {
            return false;
        }
    }
    const size_t lineEnd = buffered_.find("\r\n");
    *line = buffered_.substr(0, lineEnd);
    consumeBuffered(lineEnd + 2);
    return true;
}

bool DBusConnection::authenticate(std::string* error) {
    const std::string uid = std::to_string(::getuid());
    std::string hex;
    for (const char c : uid) {
        char pair[3];
        std::snprintf(pair, sizeof(pair), "%02x", static_cast<unsigned>(static_cast<unsigned char>(c)));
        hex += pair;
    }
    std::string request(1, '\0');
    request += "AUTH EXTERNAL " + hex + "\r\n";
    if (!sendRaw(request)) {
        *error = "could not send authentication";
        return false;
    }
    std::string line;
    if (!readAuthLine(&line)) {
        *error = "no authentication answer from bus";
        return false;
    }
    if (!startsWith(line, "OK ")) {
        *error = "bus refused authentication: " + line;
        return false;
    }
    if (!sendRaw("NEGOTIATE_UNIX_FD\r\n")) {
        *error = "could not finish authentication";
        return false;
    }
    if (!readAuthLine(&line)) {
        *error = "no answer to NEGOTIATE_UNIX_FD from bus";
        return false;
    }
    passesFds_ = line == "AGREE_UNIX_FD";
    if (!sendRaw("BEGIN\r\n")) {
        *error = "could not finish authentication";
        return false;
    }
    return true;
}

bool DBusConnection::sendMessage(const DBusMessage& message, uint32_t serial) {
    return sendRaw(encodeMessage(message, serial));
}

bool DBusConnection::hello(std::string* error) {
    const uint32_t serial = nextSerial_++;
    if (!sendMessage(dbusMethodCall(kBusDaemonName, kBusDaemonPath, kBusDaemonName, "Hello"), serial)) {
        *error = "could not send Hello";
        return false;
    }
    while (true) {
        std::vector<DBusMessage> messages;
        decodeBuffered(&messages);
        std::optional<bool> accepted;
        for (const DBusMessage& message : messages) {
            const bool reply =
                message.type == DBusMessageType::MethodReturn || message.type == DBusMessageType::Error;
            if (!accepted && reply && message.replySerial == serial) {
                if (message.type == DBusMessageType::Error) {
                    *error = "Hello was refused: " + message.errorText();
                }
                accepted = message.type != DBusMessageType::Error;
            }
            closeFds(message.fds);
        }
        if (accepted) {
            return *accepted;
        }
        if (!readBlocking(&buffered_)) {
            *error = "no answer to Hello";
            return false;
        }
    }
}

bool DBusConnection::decodeBuffered(std::vector<DBusMessage>* messages) {
    while (true) {
        const MessageFrame frame = measureMessage(buffered_);
        if (frame.status == FrameStatus::NeedMore) {
            return true;
        }
        if (frame.status == FrameStatus::Invalid) {
            consumeBuffered(buffered_.size());
            return false;
        }
        const uint64_t start = bufferedStart_;
        std::optional<DBusMessage> message = decodeMessage(std::string_view(buffered_).substr(0, frame.length));
        consumeBuffered(frame.length);
        std::vector<int> fds = takeFdsWithin(start, bufferedStart_);
        const size_t wanted = message ? message->unixFds : 0;
        if (fds.size() > wanted) {
            closeFds(std::vector<int>(fds.begin() + static_cast<std::ptrdiff_t>(wanted), fds.end()));
            fds.resize(wanted);
        }
        if (!message) {
            continue;
        }
        message->fds = std::move(fds);
        messages->push_back(std::move(*message));
    }
}

void DBusConnection::call(DBusMessage message, double timeoutSeconds, ReplyHandler handler) {
    const uint32_t serial = nextSerial_++;
    if (nextSerial_ == 0) {
        nextSerial_ = 1;
    }
    if (alive()) {
        sendMessage(message, serial);
    }
    if (handler) {
        pending_[serial] = PendingCall{std::move(handler), now_ + timeoutSeconds};
    }
}

void DBusConnection::addMatch(const std::string& rule) {
    call(dbusMethodCall(kBusDaemonName, kBusDaemonPath, kBusDaemonName, "AddMatch", {DBusValue::string(rule)}), 5.0,
         nullptr);
}

bool DBusConnection::processIncoming(double monotonicNow) {
    now_ = monotonicNow;
    char chunk[65536];
    alignas(cmsghdr) char control[CMSG_SPACE(sizeof(int) * kMaxFdsPerRead)];
    while (socket_ >= 0) {
        iovec vector{chunk, sizeof(chunk)};
        msghdr header{};
        header.msg_iov = &vector;
        header.msg_iovlen = 1;
        header.msg_control = control;
        header.msg_controllen = sizeof(control);
        const ssize_t result = ::recvmsg(socket_, &header, MSG_DONTWAIT | MSG_CMSG_CLOEXEC);
        if (result >= 0 && header.msg_controllen > 0) {
            receiveFds(control, header.msg_controllen, bufferedStart_ + buffered_.size() + static_cast<size_t>(result));
        }
        if (result > 0) {
            buffered_.append(chunk, static_cast<size_t>(result));
            if (buffered_.size() > kMaxBufferedBytes) {
                close("too much unread data from bus");
            }
            continue;
        }
        if (result == 0) {
            close("bus closed connection");
            break;
        }
        if (errno == EINTR) {
            continue;
        }
        if (errno == EAGAIN || errno == EWOULDBLOCK) {
            break;
        }
        close(std::string("read failed: ") + std::strerror(errno));
    }
    std::vector<DBusMessage> messages;
    if (!decodeBuffered(&messages)) {
        close("bus sent data that isn't D-Bus");
    }
    for (const DBusMessage& message : messages) {
        dispatch(message);
        closeFds(message.fds);
    }
    return alive();
}

void DBusConnection::dispatch(const DBusMessage& message) {
    switch (message.type) {
        case DBusMessageType::MethodReturn:
        case DBusMessageType::Error: {
            const auto found = pending_.find(message.replySerial);
            if (found == pending_.end()) {
                return;
            }
            ReplyHandler handler = std::move(found->second.handler);
            pending_.erase(found);
            if (handler) {
                handler(message);
            }
            return;
        }
        case DBusMessageType::Signal:
            if (signalHandler_) {
                signalHandler_(message);
            }
            return;
        case DBusMessageType::MethodCall:
            answerMethodCall(message);
            return;
        default:
            return;
    }
}

void DBusConnection::answerMethodCall(const DBusMessage& call) {
    if ((call.flags & kDBusNoReplyExpected) != 0) {
        return;
    }
    if (call.interface == "org.freedesktop.DBus.Peer" && call.member == "Ping") {
        DBusMessage reply;
        reply.type = DBusMessageType::MethodReturn;
        reply.flags = kDBusNoReplyExpected;
        reply.replySerial = call.serial;
        reply.destination = call.sender;
        sendMessage(reply, nextSerial_++);
        return;
    }
    sendMessage(dbusErrorReply(call, "org.freedesktop.DBus.Error.UnknownMethod", "base-station-control exports nothing"),
                nextSerial_++);
}

void DBusConnection::expireCalls(double monotonicNow) {
    now_ = monotonicNow;
    std::vector<std::pair<uint32_t, PendingCall>> expired;
    for (auto it = pending_.begin(); it != pending_.end();) {
        if (!alive() || monotonicNow >= it->second.deadline) {
            expired.emplace_back(it->first, std::move(it->second));
            it = pending_.erase(it);
        } else {
            ++it;
        }
    }
    for (auto& entry : expired) {
        if (!entry.second.handler) {
            continue;
        }
        entry.second.handler(alive() ? syntheticError(entry.first, kDBusTimeoutError, "")
                                     : syntheticError(entry.first, kDBusDisconnectedError, closeReason_));
    }
}

}

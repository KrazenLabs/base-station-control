#pragma once

#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <vector>

namespace bsc {

enum class LinkFailure {
    None,
    Busy,
    Unreachable,
    QueueTimeout,
    ConnectFailed,
    ConnectTimeout,
    ServicesTimeout,
    WriteFailed,
    WriteTimeout,
    Disappeared,
    BluetoothReset
};

enum class LinkPhase { Idle, Connecting, Sending };

struct LinkResult {
    LinkFailure failure = LinkFailure::None;
    std::string detail;
    std::optional<uint8_t> readBack;

    bool ok() const { return failure == LinkFailure::None; }
};

LinkResult linkSucceeded(std::optional<uint8_t> readBack = std::nullopt);
LinkResult linkFailed(LinkFailure failure, std::string detail = "");
const char* linkFailureText(LinkFailure failure);
std::string describeLinkResult(const LinkResult& result);

using WriteDone = std::function<void(const LinkResult& result)>;

class PowerLink {
public:
    virtual ~PowerLink() = default;
    virtual bool canReach(const std::string& devicePath) const = 0;
    virtual void write(const std::string& devicePath, const std::string& characteristicUuid, std::vector<uint8_t> value,
                       bool readBack, WriteDone done) = 0;
    virtual LinkPhase phase(const std::string& devicePath) const = 0;
};

}

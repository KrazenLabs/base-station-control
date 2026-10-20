#include "power_link.hpp"

namespace bsc {

LinkResult linkSucceeded(std::optional<uint8_t> readBack) {
    LinkResult result;
    result.readBack = readBack;
    return result;
}

LinkResult linkFailed(LinkFailure failure, std::string detail) {
    LinkResult result;
    result.failure = failure;
    result.detail = std::move(detail);
    return result;
}

const char* linkFailureText(LinkFailure failure) {
    switch (failure) {
        case LinkFailure::None: return "done";
        case LinkFailure::Busy: return "busy";
        case LinkFailure::Unreachable: return "not reachable";
        case LinkFailure::QueueTimeout: return "command timed out";
        case LinkFailure::ConnectFailed: return "could not connect";
        case LinkFailure::ConnectTimeout: return "connection timed out";
        case LinkFailure::ServicesTimeout: return "no response from device";
        case LinkFailure::WriteFailed: return "failed to send command";
        case LinkFailure::WriteTimeout: return "command timed out";
        case LinkFailure::Disappeared: return "disappeared";
        case LinkFailure::BluetoothReset: return "Bluetooth was restarted";
    }
    return "failed";
}

std::string describeLinkResult(const LinkResult& result) {
    const std::string text = linkFailureText(result.failure);
    return result.detail.empty() ? text : text + " (" + result.detail + ")";
}

}

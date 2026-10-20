#pragma once

#include <filesystem>
#include <functional>
#include <optional>
#include <string>
#include <vector>

namespace framekit {

struct MailboxRequest {
    std::string id;
    std::vector<std::string> words;
    double ageSeconds = 0.0;
};

struct MailboxAnswer {
    bool ok = false;
    std::string message;
};

struct TakenRequests {
    std::vector<MailboxRequest> fresh;
    std::vector<MailboxRequest> expired;
};

class Mailbox {
public:
    explicit Mailbox(std::filesystem::path directory);

    const std::filesystem::path& directory() const { return directory_; }

    std::optional<std::string> submit(const std::vector<std::string>& words, std::string* error);
    std::optional<MailboxAnswer> takeAnswer(const std::string& id);
    void withdraw(const std::string& id);

    TakenRequests takeRequests(double maxAgeSeconds);
    bool answer(const std::string& id, const MailboxAnswer& answer, std::string* error = nullptr);
    void removeStale(double maxAgeSeconds);

private:
    std::filesystem::path pathFor(const std::string& id, const char* extension) const;

    std::filesystem::path directory_;
};

enum class WaitOutcome { Answered, TimedOut, Interrupted };

struct WaitResult {
    WaitOutcome outcome = WaitOutcome::TimedOut;
    MailboxAnswer answer;
};

WaitResult waitForAnswer(Mailbox& mailbox, const std::string& id, double timeoutSeconds,
                         const std::function<bool()>& interrupted);

}

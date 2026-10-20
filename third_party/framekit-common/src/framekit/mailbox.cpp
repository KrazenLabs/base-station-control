#include "framekit/mailbox.hpp"

#include <chrono>
#include <cstdio>
#include <random>
#include <system_error>
#include <thread>

#include <json/json.h>

#include "framekit/clock.hpp"
#include "framekit/files.hpp"
#include "framekit/json_read.hpp"
#include "framekit/json_write.hpp"
#include "framekit/text.hpp"

namespace framekit {

namespace {

constexpr const char* kRequestExtension = ".req";
constexpr const char* kAnswerExtension = ".ans";
constexpr const char* kTemporaryExtension = ".tmp";
constexpr double kWaitStepSeconds = 0.1;

std::string makeRequestId() {
    const auto nanoseconds =
        std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::system_clock::now().time_since_epoch());
    std::random_device random;
    char id[48];
    std::snprintf(id, sizeof(id), "%016llx-%08x", static_cast<unsigned long long>(nanoseconds.count()),
                  static_cast<unsigned>(random()));
    return id;
}

double ageSeconds(const std::filesystem::path& path) {
    std::error_code ec;
    const std::filesystem::file_time_type modified = std::filesystem::last_write_time(path, ec);
    if (ec) {
        return 0.0;
    }
    const auto age = std::filesystem::file_time_type::clock::now() - modified;
    return std::chrono::duration<double>(age).count();
}

bool hasLineBreak(const std::string& word) {
    return word.find('\n') != std::string::npos || word.find('\r') != std::string::npos;
}

}

Mailbox::Mailbox(std::filesystem::path directory) : directory_(std::move(directory)) {}

std::filesystem::path Mailbox::pathFor(const std::string& id, const char* extension) const {
    return directory_ / (id + extension);
}

std::optional<std::string> Mailbox::submit(const std::vector<std::string>& words, std::string* error) {
    Json::Value list(Json::arrayValue);
    for (const std::string& word : words) {
        if (hasLineBreak(word)) {
            if (error) {
                *error = "commands cannot contain line breaks";
            }
            return std::nullopt;
        }
        list.append(word);
    }
    Json::Value request(Json::objectValue);
    request["words"] = list;
    const std::string id = makeRequestId();
    if (!writeJsonFile(pathFor(id, kRequestExtension), request, error)) {
        return std::nullopt;
    }
    return id;
}

std::optional<MailboxAnswer> Mailbox::takeAnswer(const std::string& id) {
    const std::filesystem::path path = pathFor(id, kAnswerExtension);
    const std::optional<std::string> content = readFile(path);
    if (!content) {
        return std::nullopt;
    }
    std::error_code ec;
    std::filesystem::remove(path, ec);
    const std::optional<Json::Value> root = parseJson(*content);
    MailboxAnswer answer;
    if (!root) {
        answer.message = "failed to read service response";
        return answer;
    }
    answer.ok = boolMember(*root, "ok");
    answer.message = textMember(*root, "message");
    return answer;
}

void Mailbox::withdraw(const std::string& id) {
    std::error_code ec;
    std::filesystem::remove(pathFor(id, kRequestExtension), ec);
}

TakenRequests Mailbox::takeRequests(double maxAgeSeconds) {
    TakenRequests taken;
    for (const std::string& name : listDirectory(directory_)) {
        if (!endsWith(name, kRequestExtension)) {
            continue;
        }
        const std::filesystem::path path = directory_ / name;
        const double age = ageSeconds(path);
        const std::optional<std::string> content = readFile(path);
        std::error_code ec;
        if (!content || !std::filesystem::remove(path, ec)) {
            continue;
        }
        MailboxRequest request;
        request.id = name.substr(0, name.size() - std::char_traits<char>::length(kRequestExtension));
        request.ageSeconds = age;
        const Json::Value document = parseJson(*content).value_or(Json::Value());
        for (const Json::Value& word : member(document, "words")) {
            if (word.isString()) {
                request.words.push_back(word.asString());
            }
        }
        (age > maxAgeSeconds ? taken.expired : taken.fresh).push_back(std::move(request));
    }
    return taken;
}

bool Mailbox::answer(const std::string& id, const MailboxAnswer& answer, std::string* error) {
    Json::Value root(Json::objectValue);
    root["ok"] = answer.ok;
    root["message"] = answer.message;
    return writeJsonFile(pathFor(id, kAnswerExtension), root, error);
}

void Mailbox::removeStale(double maxAgeSeconds) {
    for (const std::string& name : listDirectory(directory_)) {
        if (!endsWith(name, kAnswerExtension) && !endsWith(name, kTemporaryExtension)) {
            continue;
        }
        const std::filesystem::path path = directory_ / name;
        if (ageSeconds(path) > maxAgeSeconds) {
            std::error_code ec;
            std::filesystem::remove(path, ec);
        }
    }
}

WaitResult waitForAnswer(Mailbox& mailbox, const std::string& id, double timeoutSeconds,
                         const std::function<bool()>& interrupted) {
    WaitResult result;
    const double deadline = monotonicSeconds() + timeoutSeconds;
    while (true) {
        if (std::optional<MailboxAnswer> answer = mailbox.takeAnswer(id)) {
            result.outcome = WaitOutcome::Answered;
            result.answer = std::move(*answer);
            return result;
        }
        if (interrupted && interrupted()) {
            result.outcome = WaitOutcome::Interrupted;
            break;
        }
        const double left = deadline - monotonicSeconds();
        if (left <= 0.0) {
            result.outcome = WaitOutcome::TimedOut;
            break;
        }
        std::this_thread::sleep_for(std::chrono::duration<double>(left < kWaitStepSeconds ? left : kWaitStepSeconds));
    }
    mailbox.withdraw(id);
    return result;
}

}

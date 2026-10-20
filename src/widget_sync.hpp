#pragma once

#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <vector>

#include "common.hpp"
#include "widget_bridge.hpp"
#include "widget_link.hpp"
#include "widget_schedule.hpp"

namespace bsc {

struct WidgetUpdate {
    std::vector<std::optional<WidgetReport>> results;
    std::vector<std::string> logs;
    WidgetState state = WidgetState::Waiting;
};

class WidgetSync {
public:
    using LinkFactory = std::function<std::unique_ptr<WidgetLink>(Logger log)>;

    explicit WidgetSync(LinkFactory factory, WidgetTiming timing = {});
    ~WidgetSync();

    WidgetSync(const WidgetSync&) = delete;
    WidgetSync& operator=(const WidgetSync&) = delete;

    int wakeFd() const { return mainWake_[0]; }
    void publish(const std::string& dataJson, bool active);
    WidgetUpdate take();
    void stop();

private:
    struct Shared {
        std::string data;
        bool active = false;
        bool stopping = false;
    };

    void run();
    Shared snapshot();
    void keepLog(const std::string& message);
    void deliver(std::optional<WidgetReport> result, WidgetState state);
    void waitForWork(int linkFd, double seconds);

    LinkFactory factory_;
    WidgetTiming timing_;
    std::mutex mutex_;
    Shared shared_;
    WidgetUpdate pending_;
    int workerWake_[2] = {-1, -1};
    int mainWake_[2] = {-1, -1};
    std::thread worker_;
};

}

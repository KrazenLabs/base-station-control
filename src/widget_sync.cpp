#include "widget_sync.hpp"

#include <cerrno>
#include <cmath>

#include <fcntl.h>
#include <poll.h>
#include <unistd.h>

namespace bsc {

namespace {

void signalPipe(int fd) {
    if (fd >= 0) {
        const char byte = 1;
        const ssize_t written = ::write(fd, &byte, 1);
        (void)written;
    }
}

void drainPipe(int fd) {
    char buffer[64];
    while (fd >= 0 && ::read(fd, buffer, sizeof(buffer)) > 0) {
    }
}

void closePipe(int (&fds)[2]) {
    for (int& fd : fds) {
        if (fd >= 0) {
            ::close(fd);
            fd = -1;
        }
    }
}

}

WidgetSync::WidgetSync(LinkFactory factory, WidgetTiming timing) : factory_(std::move(factory)), timing_(timing) {
    if (::pipe2(workerWake_, O_NONBLOCK | O_CLOEXEC) != 0) {
        workerWake_[0] = workerWake_[1] = -1;
    }
    if (::pipe2(mainWake_, O_NONBLOCK | O_CLOEXEC) != 0) {
        mainWake_[0] = mainWake_[1] = -1;
    }
    worker_ = std::thread([this]() { run(); });
}

WidgetSync::~WidgetSync() {
    stop();
    closePipe(workerWake_);
    closePipe(mainWake_);
}

void WidgetSync::publish(const std::string& dataJson, bool active) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (shared_.data == dataJson && shared_.active == active) {
        return;
    }
    shared_.data = dataJson;
    shared_.active = active;
    signalPipe(workerWake_[1]);
}

WidgetUpdate WidgetSync::take() {
    drainPipe(mainWake_[0]);
    std::lock_guard<std::mutex> lock(mutex_);
    WidgetUpdate update;
    update.results.swap(pending_.results);
    update.logs.swap(pending_.logs);
    update.state = pending_.state;
    return update;
}

void WidgetSync::stop() {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        shared_.stopping = true;
    }
    signalPipe(workerWake_[1]);
    if (worker_.joinable()) {
        worker_.join();
    }
}

WidgetSync::Shared WidgetSync::snapshot() {
    std::lock_guard<std::mutex> lock(mutex_);
    return shared_;
}

void WidgetSync::keepLog(const std::string& message) {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        pending_.logs.push_back(message);
    }
    signalPipe(mainWake_[1]);
}

void WidgetSync::deliver(std::optional<WidgetReport> result, WidgetState state) {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        pending_.results.push_back(std::move(result));
        pending_.state = state;
    }
    signalPipe(mainWake_[1]);
}

void WidgetSync::waitForWork(int linkFd, double seconds) {
    pollfd fds[2];
    nfds_t count = 0;
    if (workerWake_[0] >= 0) {
        fds[count++] = pollfd{workerWake_[0], POLLIN, 0};
    }
    if (linkFd >= 0) {
        fds[count++] = pollfd{linkFd, POLLIN, 0};
    }
    const int timeout = static_cast<int>(std::ceil(seconds * 1000.0));
    if (::poll(fds, count, timeout) < 0 && errno != EINTR) {
        ::usleep(100000);
    }
    drainPipe(workerWake_[0]);
}

void WidgetSync::run() {
    std::unique_ptr<WidgetLink> link = factory_([this](const std::string& message) { keepLog(message); });
    WidgetSchedule schedule(timing_);
    std::string sent;
    while (true) {
        Shared shared = snapshot();
        if (shared.stopping) {
            break;
        }
        waitForWork(link->fd(), schedule.waitSeconds(monotonicSeconds(), shared.data != sent, shared.active));
        shared = snapshot();
        if (shared.stopping) {
            break;
        }
        const bool ping = link->takePing();
        const double now = monotonicSeconds();
        const bool changed = shared.data != sent;
        if (!schedule.due(now, ping, changed, shared.active)) {
            continue;
        }
        const bool sendData = schedule.sendData(now, changed, link->connected());
        std::optional<WidgetReport> report = link->exchange(sendData ? shared.data : std::string(), now);
        schedule.exchanged(now, sendData && report.has_value());
        if (!report) {
            sent.clear();
        } else if (sendData) {
            sent = shared.data;
        }
        deliver(std::move(report), link->state());
    }
    link->remove();
    deliver(std::nullopt, link->state());
}

}

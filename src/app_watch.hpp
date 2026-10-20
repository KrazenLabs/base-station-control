#pragma once

#include <cstdint>
#include <map>
#include <vector>

namespace bsc {

struct AppReport {
    uint32_t id = 0;
    bool known = false;
    bool installed = false;
    bool running = false;
};

struct AppEvent {
    uint32_t id = 0;
    bool launched = false;
};

class AppWatcher {
public:
    explicit AppWatcher(double quitDelaySeconds = 4.0);

    std::vector<AppEvent> update(const std::vector<AppReport>& reports, double now);
    std::vector<AppEvent> tick(double now);
    bool installed(uint32_t id) const;
    bool running(uint32_t id) const;
    bool anyOtherRunning(uint32_t id) const;
    std::vector<uint32_t> installedApps() const;

private:
    struct State {
        bool seen = false;
        bool installed = false;
        bool running = false;
        double stoppedSince = -1.0;
    };

    double quitDelaySeconds_;
    std::map<uint32_t, State> states_;
};

}

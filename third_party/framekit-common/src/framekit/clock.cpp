#include "framekit/clock.hpp"

#include <chrono>

#if defined(__linux__)
#include <time.h>
#endif

namespace framekit {

double wallSeconds() {
    const auto now = std::chrono::system_clock::now().time_since_epoch();
    return std::chrono::duration<double>(now).count();
}

double monotonicSeconds() {
#if defined(__linux__)
    timespec boottime{};
    if (clock_gettime(CLOCK_BOOTTIME, &boottime) == 0) {
        return static_cast<double>(boottime.tv_sec) + static_cast<double>(boottime.tv_nsec) * 1e-9;
    }
#endif
    const auto steady = std::chrono::steady_clock::now().time_since_epoch();
    return std::chrono::duration<double>(steady).count();
}

}

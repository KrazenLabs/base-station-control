#pragma once

#include <cstdio>
#include <ctime>
#include <functional>
#include <string>

namespace framekit {

using Logger = std::function<void(const std::string&)>;

inline void log(const std::string& message) {
    const std::time_t now = std::time(nullptr);
    char stamp[16] = "";
    std::strftime(stamp, sizeof(stamp), "%H:%M:%S", std::localtime(&now));
    std::printf("%s %s\n", stamp, message.c_str());
    std::fflush(stdout);
}

}

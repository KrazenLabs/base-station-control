#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace bsc {

struct WatchedApp {
    uint32_t id = 0;
    std::string slug;
    std::string name;
};

using AppList = std::vector<WatchedApp>;

AppList builtInApps();
const WatchedApp* findApp(const AppList& apps, uint32_t id);
std::vector<uint32_t> appIds(const AppList& apps);

}

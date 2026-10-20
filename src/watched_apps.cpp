#include "watched_apps.hpp"

namespace bsc {

AppList builtInApps() {
    return {
        {438100, "vrchat", "VRChat"},
        {2519830, "resonite", "Resonite"},
    };
}

const WatchedApp* findApp(const AppList& apps, uint32_t id) {
    for (const WatchedApp& app : apps) {
        if (app.id == id) {
            return &app;
        }
    }
    return nullptr;
}

std::vector<uint32_t> appIds(const AppList& apps) {
    std::vector<uint32_t> ids;
    for (const WatchedApp& app : apps) {
        ids.push_back(app.id);
    }
    return ids;
}

}

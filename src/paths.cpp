#include "paths.hpp"

#include <cstdlib>
#include <string>
#include <system_error>

#include <pwd.h>
#include <unistd.h>

#include "app_info.hpp"

namespace bsc {

namespace {

std::filesystem::path homeDirectory() {
    const char* home = std::getenv("HOME");
    if (home && home[0] != '\0') {
        return home;
    }
    if (const passwd* entry = getpwuid(getuid())) {
        if (entry->pw_dir && entry->pw_dir[0] != '\0') {
            return entry->pw_dir;
        }
    }
    return "/home/steamos";
}

std::filesystem::path programDirectory(const std::filesystem::path& fallback) {
    std::error_code ec;
    const std::filesystem::path program = std::filesystem::read_symlink("/proc/self/exe", ec);
    if (ec || program.empty() || !program.has_parent_path()) {
        return fallback;
    }
    return program.parent_path();
}

}

Paths defaultPaths() {
    const std::filesystem::path home = homeDirectory();
    Paths paths;
    paths.installDir = programDirectory(home / ".local" / "share" / kAppId / "current");
    paths.config = paths.installDir / "config.json";
    paths.stateDir = home / ".local" / "state" / kAppId;
    paths.state = paths.stateDir / "state.json";
    paths.runtimeDir = std::filesystem::path("/run/user") / std::to_string(getuid()) / kAppId;
    paths.requests = paths.runtimeDir / "requests";
    paths.status = paths.runtimeDir / "status.json";
    paths.lock = paths.runtimeDir / "lock";
    return paths;
}

}

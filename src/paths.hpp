#pragma once

#include <filesystem>

namespace bsc {

struct Paths {
    std::filesystem::path installDir;
    std::filesystem::path config;
    std::filesystem::path stateDir;
    std::filesystem::path state;
    std::filesystem::path runtimeDir;
    std::filesystem::path requests;
    std::filesystem::path status;
    std::filesystem::path lock;
};

Paths defaultPaths();

}

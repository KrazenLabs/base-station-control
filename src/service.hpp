#pragma once

#include <filesystem>
#include <string>

#include "paths.hpp"

namespace bsc {

enum class LockResult { Acquired, AlreadyRunning, Failed };

LockResult acquireServiceLock(const std::filesystem::path& path, int* fd, std::string* error);
int runService(const Paths& paths, const std::string& version);

}

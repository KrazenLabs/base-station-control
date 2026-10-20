#pragma once

#include <string>
#include <vector>

#include "paths.hpp"

namespace bsc {

int runStatus(const Paths& paths);
int runServiceCommand(const Paths& paths, const std::vector<std::string>& words);
int runPanelStatus(const Paths& paths);
int runPanelAction(const Paths& paths, bool on);

}

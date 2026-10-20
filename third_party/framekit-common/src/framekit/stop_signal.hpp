#pragma once

namespace framekit {

void installStopHandlers();
bool stopRequested();
int stopSignalFd();

}

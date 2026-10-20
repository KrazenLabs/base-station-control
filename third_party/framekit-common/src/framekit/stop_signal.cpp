#include "framekit/stop_signal.hpp"

#include <cerrno>
#include <csignal>

#include <fcntl.h>
#include <unistd.h>

namespace framekit {

namespace {

volatile std::sig_atomic_t g_stopRequested = 0;
int g_wakePipe[2] = {-1, -1};

void handleStopSignal(int) {
    const int savedErrno = errno;
    g_stopRequested = 1;
    if (g_wakePipe[1] >= 0) {
        const char byte = 1;
        const ssize_t written = ::write(g_wakePipe[1], &byte, 1);
        (void)written;
    }
    errno = savedErrno;
}

}

void installStopHandlers() {
    if (::pipe2(g_wakePipe, O_NONBLOCK | O_CLOEXEC) != 0) {
        g_wakePipe[0] = -1;
        g_wakePipe[1] = -1;
    }
    std::signal(SIGINT, handleStopSignal);
    std::signal(SIGTERM, handleStopSignal);
}

bool stopRequested() {
    return g_stopRequested != 0;
}

int stopSignalFd() {
    return g_wakePipe[0];
}

}

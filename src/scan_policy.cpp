#include "scan_policy.hpp"

#include <algorithm>

namespace bsc {

ScanPolicy::ScanPolicy(ScanSettings settings, double start)
    : settings_(settings),
      scanUntil_(start + settings.startupSeconds),
      nextBackgroundScan_(start + settings.backgroundMinutes * 60.0) {}

void ScanPolicy::scanFor(double seconds, double now) {
    scanUntil_ = std::max(scanUntil_, now + seconds);
}

bool ScanPolicy::wanted(const ScanReasons& reasons, double now) {
    if (settings_.backgroundMinutes > 0.0 && now >= nextBackgroundScan_) {
        scanFor(settings_.backgroundSeconds, now);
        nextBackgroundScan_ = now + settings_.backgroundMinutes * 60.0;
    }
    const bool needed = reasons.menuOpen || reasons.switching || reasons.listening || now < scanUntil_;
    if (needed) {
        lingerUntil_ = now + settings_.lingerSeconds;
    }
    return needed || now < lingerUntil_;
}

void ScanPolicy::observe(bool scanning, double now) {
    if (scanning && !wasScanning_) {
        scanningSince_ = now;
    }
    wasScanning_ = scanning;
}

}

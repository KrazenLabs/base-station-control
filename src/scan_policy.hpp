#pragma once

namespace bsc {

struct ScanSettings {
    double lingerSeconds = 10.0;
    double startupSeconds = 15.0;
    double backgroundMinutes = 0.0;
    double backgroundSeconds = 8.0;
};

struct ScanReasons {
    bool menuOpen = false;
    bool switching = false;
    bool listening = false;
};

class ScanPolicy {
public:
    ScanPolicy(ScanSettings settings, double start);

    void scanFor(double seconds, double now);
    bool wanted(const ScanReasons& reasons, double now);
    void observe(bool scanning, double now);
    double scanningSince() const { return scanningSince_; }

private:
    ScanSettings settings_;
    double scanUntil_ = 0.0;
    double lingerUntil_ = 0.0;
    double nextBackgroundScan_ = 0.0;
    double scanningSince_ = 0.0;
    bool wasScanning_ = false;
};

}

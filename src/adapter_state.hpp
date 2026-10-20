#pragma once

namespace bsc {

enum class AdapterState { Unknown, Missing, Off, On };

inline const char* adapterStateName(AdapterState state) {
    switch (state) {
        case AdapterState::Missing: return "missing";
        case AdapterState::Off: return "off";
        case AdapterState::On: return "on";
        default: return "unknown";
    }
}

}

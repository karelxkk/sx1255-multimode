#pragma once

#include <cstddef>
#include <cstdint>
#include <string>

namespace sx1255 {

using Hertz = std::uint32_t;
using dBm   = std::int32_t;

// One complex IQ sample.  The SX1255 frontend is the only producer of raw
// samples; every plugin sees the exact same stream through the dispatcher.
struct ComplexFloat {
    float i;
    float q;
};

// Non-owning view of a contiguous block of IQ samples.
struct IQBlock {
    std::uint64_t     sequence     = 0;
    const ComplexFloat* samples    = nullptr;
    std::size_t       count        = 0;
    double            sampleRateHz = 125000.0;
};

// Supported air-interface modes.  This is intentionally a closed set in the
// first phase; the plugin system, not this enum, is the extension point.
enum class Mode : std::uint8_t {
    Unknown = 0,
    FM      = 1,
    DStar   = 2,
    M17     = 3,
};

inline const char* toString(Mode mode)
{
    switch (mode) {
        case Mode::FM:    return "FM";
        case Mode::DStar: return "D-Star";
        case Mode::M17:   return "M17";
        default:          return "Unknown";
    }
}

// Result of a plugin's detection pass over one IQ block.
struct Detection {
    bool  detected   = false;
    float confidence = 0.0F;  // 0.0 .. 1.0, advisory only
};

class ITransmit;

// Handed to a plugin when it is granted exclusive ownership of a session.
struct SessionContext {
    std::uint64_t sessionId = 0;
    Mode          mode      = Mode::Unknown;
    std::string   pluginId;
    ITransmit*    tx        = nullptr;   // owned by the frontend
};

}  // namespace sx1255

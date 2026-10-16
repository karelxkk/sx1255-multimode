#pragma once

#include "sx1255/Types.hpp"

#include <map>
#include <string>
#include <string_view>

namespace sx1255 {

// ---------------------------------------------------------------------------
// ITxArbiter
//
// Single authority for RF transmit.  Any transmit path (RF sessions and
// network-initiated traffic) asks here before keying, so only one transmitter
// is ever active.
// ---------------------------------------------------------------------------
class ITxArbiter {
public:
    virtual ~ITxArbiter() = default;

    // Try to reserve the transmitter for `mode`.  Returns false if another
    // session currently owns it.
    virtual bool acquireTx(Mode mode) = 0;

    // Try to reserve the transmitter for a data packet coming from the
    // network.  A packet is data for its own mode, so this must not depend on
    // the network audio source the way acquireTx() does: an APRS message for
    // a radio is not dropped just because SVXLink currently owns the network
    // audio.  The default keeps the stricter behaviour.
    virtual bool acquireTxPacket(Mode mode) { return acquireTx(mode); }

    // Release a previously acquired transmit reservation.
    virtual void releaseTx() = 0;
};

// Per-plugin runtime telemetry, surfaced by the web dashboard.  Keys are
// stable ASCII names (e.g. "confidence", "frame_count").
struct PluginTelemetry {
    Mode mode = Mode::Unknown;
    std::map<std::string, double> metrics;
    std::map<std::string, std::string> strings;
};

// ---------------------------------------------------------------------------
// IModePlugin
//
// Every air-interface mode (FM, D-Star, M17, ...) is implemented as a plugin.
// Plugins are completely decoupled from the RF frontend: they receive IQ
// samples, never touch SPI/ALSA/GPIO, and use the ITransmit handle from the
// SessionContext to send their own transmit pipeline.
//
// Lifecycle for one session:
//
//   monitoring:   detect(block) is called for every registered plugin
//   grant:        startSession(ctx) is called exactly once for the winner
//   exclusive:    process(block) is called only for the winning plugin
//   release:      stopSession() is called when the session ends
// ---------------------------------------------------------------------------
class IModePlugin {
public:
    virtual ~IModePlugin() = default;

    // Give plugins that transmit on their own initiative (e.g. a network
    // bridge) access to the shared RF transmitter.  Default: no-op.
    virtual void setTransmitter(ITransmit*) {}

    // Give plugins access to the global TX arbiter.  Default: no-op.
    virtual void setTxArbiter(ITxArbiter*) {}

    // Stable, human-readable identifier (e.g. "fm", "dstar", "m17").
    virtual std::string_view id() const noexcept = 0;

    // The air-interface mode this plugin implements.
    virtual Mode mode() const noexcept = 0;

    // Arbitration priority.  Higher wins; the spec orders M17 > D-Star > FM.
    virtual int priority() const noexcept = 0;

    // Inspect one IQ block and report whether a valid frame/sync/tone was
    // found.  Called concurrently for every plugin while no session is active.
    virtual Detection detect(const IQBlock& block) = 0;

    // The dispatcher granted exclusive channel ownership to this plugin.
    virtual void startSession(const SessionContext& ctx) = 0;

    // Process the next block.  Return false when the session is finished
    // (EOT / carrier lost); the session manager will then release ownership.
    virtual bool process(const IQBlock& block) = 0;

    // Keep the plugin's receive path alive while another plugin owns the
    // session.  Modes that can receive data outside their own session (M17
    // packet mode, which carries messages and not voice) override this so a
    // packet is not lost while another mode holds the channel.  The default
    // does nothing.
    virtual void monitor(const IQBlock&) {}

    // Ownership is being released (normal end, timeout or shutdown).
    virtual void stopSession() = 0;

    // Live diagnostic counters/confidence values for the web dashboard.
    // The default implementation reports an empty telemetry block.
    virtual PluginTelemetry telemetry() const { return {mode(), {}, {}}; }
};

}  // namespace sx1255

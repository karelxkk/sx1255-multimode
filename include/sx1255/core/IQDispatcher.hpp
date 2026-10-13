#pragma once

#include "sx1255/Types.hpp"

#include <functional>

namespace sx1255 {

class PluginRegistry;
class SessionManager;
class ITransmit;

// ---------------------------------------------------------------------------
// IQDispatcher
//
// The single fan-out point of the system.  The frontend calls dispatch() with
// every IQ block; the dispatcher either:
//
//   * routes the block exclusively to the active plugin (process()), or
//   * fans it out to every plugin (detect()) and lets the SessionManager
//     arbitrate ownership.
//
// The dispatcher contains no DSP and no mode knowledge.
// ---------------------------------------------------------------------------
class IQDispatcher {
public:
    IQDispatcher(PluginRegistry& registry, SessionManager& sessions, ITransmit* tx);

    // The transmit handle is owned by the frontend, which is constructed after
    // the dispatcher.  Wire it up before starting the capture loop.
    void setTransmitter(ITransmit* tx);

    // Optional diagnostic tap invoked with every IQ block before fan-out.
    // Used by the spectrum monitor and automatic capture; must not block.
    void setIqSink(std::function<void(const ComplexFloat*, std::size_t)> sink);

    // Process one IQ block.  Not thread-safe: call from the frontend thread.
    void dispatch(const IQBlock& block);

    std::uint64_t blocksProcessed() const;

private:
    PluginRegistry& m_registry;
    SessionManager& m_sessions;
    ITransmit*      m_tx;
    std::function<void(const ComplexFloat*, std::size_t)> m_iqSink;
    std::uint64_t   m_blocks = 0;
};

}  // namespace sx1255

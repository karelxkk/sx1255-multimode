#pragma once

#include "sx1255/Types.hpp"

#include <atomic>
#include <deque>
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

    // Pause plugin detection when the receiver is otherwise idle and the
    // measured IQ power is below the sleep threshold.  The frontend keeps
    // reading IQ, so FFT/waterfall continue and a later block can wake the
    // detectors immediately.
    void setIdlePowerSave(bool enabled, double sleepBelowDbfs, double wakeAboveDbfs,
                          double prebufferMs);
    bool idlePowerSaveActive() const { return m_idlePowerSave.load(); }

    // Process one IQ block.  Not thread-safe: call from the frontend thread.
    void dispatch(const IQBlock& block);

    std::uint64_t blocksProcessed() const;

private:
    void routeBlock(const IQBlock& block);
    void appendWakePrebuffer(const IQBlock& block);
    void flushWakePrebuffer(const IQBlock& liveBlock);

    PluginRegistry& m_registry;
    SessionManager& m_sessions;
    ITransmit*      m_tx;
    std::function<void(const ComplexFloat*, std::size_t)> m_iqSink;

    // Consecutive blocks a higher priority mode has to report a carrier before
    // the current owner is told to let go (~82 ms at the default 2048-sample
    // block, ~164 ms at 4096 samples).
    static constexpr int kPreemptBlocks = 5;
    int m_preemptBlocks = 0;
    std::uint64_t   m_blocks = 0;

    bool   m_powerSaveEnabled = false;
    double m_powerSaveSleepDb = -57.0;
    double m_powerSaveWakeDb = -55.0;
    std::size_t m_powerSavePrebufferMax = 31250;
    std::deque<ComplexFloat> m_wakePrebuffer;
    std::atomic<bool> m_idlePowerSave{false};
};

}  // namespace sx1255

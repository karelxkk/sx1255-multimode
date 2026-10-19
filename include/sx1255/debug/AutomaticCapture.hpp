#pragma once

#include "sx1255/Types.hpp"

#include <atomic>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace sx1255::debug {

enum class CaptureTrigger {
    FalsePositive = 0,
    UnknownSignal = 1,
    Crash         = 2,
    Health        = 3,
    M17NearMiss   = 4,
};

inline const char* toString(CaptureTrigger trigger)
{
    switch (trigger) {
        case CaptureTrigger::FalsePositive: return "false_positive";
        case CaptureTrigger::UnknownSignal: return "unknown_signal";
        case CaptureTrigger::Crash:         return "crash";
        case CaptureTrigger::Health:        return "health";
        case CaptureTrigger::M17NearMiss:   return "m17_near_miss";
        default:                            return "unknown";
    }
}

// ---------------------------------------------------------------------------
// AutomaticCapture
//
// Keeps a rolling IQ history in memory and, on a trigger, writes the
// pre-trigger window plus the post-trigger window to a timestamped .iq file.
// The producer side (dispatcher thread) only appends to a preallocated ring
// buffer under a short lock, so it does not perturb RX performance; all file
// I/O happens on the dedicated writer thread.
// ---------------------------------------------------------------------------
class AutomaticCapture {
public:
    struct Result {
        bool         ok = false;
        CaptureTrigger trigger = CaptureTrigger::FalsePositive;
        std::string  file;
        std::uint64_t samples = 0;
    };

    AutomaticCapture(std::size_t sampleRateHz,
                     double preSeconds,
                     double postSeconds,
                     std::string directory);
    ~AutomaticCapture();

    AutomaticCapture(const AutomaticCapture&) = delete;
    AutomaticCapture& operator=(const AutomaticCapture&) = delete;

    void start();
    void stop();

    // Producer side: append IQ samples to the rolling history.
    void push(const ComplexFloat* samples, std::size_t count);

    // Non-blocking trigger; the writer thread performs the dump.
    void trigger(CaptureTrigger reason);

    std::uint64_t completedCaptures() const;
    Result        lastResult() const;
    std::uint64_t bufferedSamples() const;

private:
    void run();
    bool dump(CaptureTrigger reason, std::uint64_t triggerWritten);
    std::uint64_t written() const;

    std::size_t  m_sampleRate;
    std::size_t  m_preSamples;
    std::size_t  m_postSamples;
    std::size_t  m_capacity;
    std::string  m_directory;

    std::vector<ComplexFloat> m_ring;
    std::uint64_t m_written = 0;   // total samples ever written

    mutable std::mutex m_mutex;
    std::condition_variable m_cv;
    bool m_triggered = false;
    CaptureTrigger m_reason = CaptureTrigger::FalsePositive;
    std::uint64_t m_triggerWritten = 0;

    std::atomic<bool> m_running{false};
    std::thread       m_thread;

    mutable std::mutex m_resultMutex;
    Result             m_lastResult;
    std::uint64_t      m_completed = 0;
};

}  // namespace sx1255::debug

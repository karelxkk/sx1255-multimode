#pragma once

#include "sx1255/Types.hpp"

#include <atomic>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace sx1255::spectrum {

// Latest published spectrum frame.
struct SpectrumSnapshot {
    std::size_t       fftSize = 0;
    double            sampleRateHz = 0.0;
    std::uint64_t     frameCount = 0;
    std::uint64_t     droppedSamples = 0;
    float             noiseFloorDb = 0.0F;
    std::vector<float> spectrumDb;   // one-sided PSD in dB (bins 0..fftSize/2)
    std::vector<float> peakHoldDb;   // max-hold of spectrumDb
};

// ---------------------------------------------------------------------------
// SpectrumAnalyzer
//
// Consumes the shared IQ stream on its own thread and publishes PSD frames at
// a fixed rate (default 5 Hz).  It performs FFT + PSD + peak-hold + noise
// floor estimation and keeps the latest frame available for the web
// dashboard.  The producer side is a short mutex-protected append so the RX
// path is never blocked on FFT work.
// ---------------------------------------------------------------------------
class SpectrumAnalyzer {
public:
    SpectrumAnalyzer(std::size_t fftSize = 4096,
                     double sampleRateHz = 125000.0,
                     int updatesPerSec = 5);
    ~SpectrumAnalyzer();

    SpectrumAnalyzer(const SpectrumAnalyzer&) = delete;
    SpectrumAnalyzer& operator=(const SpectrumAnalyzer&) = delete;

    void start();
    void stop();

    // Producer side (dispatcher thread): non-blocking enqueue of IQ samples.
    void push(const ComplexFloat* samples, std::size_t count);

    // Latest published frame (copy).
    SpectrumSnapshot snapshot() const;
    std::string      toJson() const;

    void resetPeakHold();

    // Optional observer invoked (on the analyzer thread) with every newly
    // published spectrum frame; used to feed the waterfall generator.
    void setFrameHandler(std::function<void(const std::vector<float>&)> handler);

    std::size_t fftSize() const { return m_fftSize; }
    double      sampleRateHz() const { return m_sampleRateHz; }

private:
    void run();
    void publish(const std::vector<float>& spectrumDb);

    std::size_t  m_fftSize;
    double       m_sampleRateHz;
    int          m_updatesPerSec;
    std::size_t  m_ringCapacity;

    std::vector<ComplexFloat> m_ring;
    std::size_t  m_readPos  = 0;   // next sample index to consume
    std::size_t  m_writePos = 0;   // next free sample index
    std::size_t  m_size     = 0;   // currently buffered samples
    std::uint64_t m_dropped = 0;

    mutable std::mutex m_mutex;
    std::condition_variable m_cv;

    std::atomic<bool> m_running{false};
    std::thread       m_thread;

    mutable std::mutex m_snapshotMutex;
    SpectrumSnapshot   m_snapshot;
    std::vector<float> m_peakHold;
    std::function<void(const std::vector<float>&)> m_frameHandler;
};

}  // namespace sx1255::spectrum

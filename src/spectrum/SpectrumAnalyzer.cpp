#include "sx1255/spectrum/SpectrumAnalyzer.hpp"

#include "sx1255/spectrum/FFT.hpp"

#include <algorithm>
#include <chrono>
#include <cstdio>

namespace sx1255::spectrum {

namespace {

std::size_t nextPowerOfTwo(std::size_t value)
{
    std::size_t n = 1;
    while (n < value)
        n <<= 1;
    return n;
}

float estimateNoiseFloor(const std::vector<float>& spectrumDb)
{
    if (spectrumDb.empty())
        return -120.0F;

    // Quietest quartile median is robust against signals and DC spikes.
    std::vector<float> copy(spectrumDb.begin() + 1, spectrumDb.end());  // skip DC
    if (copy.empty())
        copy = spectrumDb;
    std::sort(copy.begin(), copy.end());
    return copy[copy.size() / 4];
}

}  // namespace

SpectrumAnalyzer::SpectrumAnalyzer(std::size_t fftSize, double sampleRateHz, int updatesPerSec)
    : m_fftSize(nextPowerOfTwo(std::max<std::size_t>(fftSize, 1024)))
    , m_sampleRateHz(sampleRateHz)
    , m_updatesPerSec(std::max(1, updatesPerSec))
    , m_ringCapacity(m_fftSize)
{
    // The ring holds only the most recent FFT window; older samples are
    // dropped.  One FFT per published frame (not one per arriving window) is
    // enough for a 5 Hz diagnostic display.
    m_ring.resize(m_ringCapacity);

    const std::size_t bins = m_fftSize / 2 + 1;
    m_peakHold.assign(bins, -200.0F);
    m_snapshot.fftSize = m_fftSize;
    m_snapshot.sampleRateHz = m_sampleRateHz;
    m_snapshot.spectrumDb.assign(bins, -120.0F);
    m_snapshot.peakHoldDb.assign(bins, -120.0F);
    m_snapshot.noiseFloorDb = -120.0F;
}

SpectrumAnalyzer::~SpectrumAnalyzer()
{
    stop();
}

void SpectrumAnalyzer::start()
{
    if (m_running.load())
        return;
    m_running = true;
    m_thread = std::thread([this] { run(); });
}

void SpectrumAnalyzer::stop()
{
    if (!m_running.load())
        return;
    m_running = false;
    m_cv.notify_all();
    if (m_thread.joinable())
        m_thread.join();
}

void SpectrumAnalyzer::push(const ComplexFloat* samples, std::size_t count)
{
    if (count == 0)
        return;

    {
        std::lock_guard<std::mutex> lock(m_mutex);

        if (count >= m_ringCapacity) {
            // Keep only the newest window worth of samples.
            samples += count - m_ringCapacity;
            count = m_ringCapacity;
            for (std::size_t i = 0; i < count; ++i)
                m_ring[i] = samples[i];
            m_readPos = 0;
            m_writePos = 0;
            m_size = m_ringCapacity;
            m_dropped += static_cast<std::uint64_t>(count);  // conservative lower bound
        } else {
            if (m_size + count > m_ringCapacity) {
                const std::size_t overflow = m_size + count - m_ringCapacity;
                m_readPos = (m_readPos + overflow) % m_ringCapacity;
                m_size -= overflow;
                m_dropped += overflow;
            }
            for (std::size_t i = 0; i < count; ++i) {
                m_ring[m_writePos] = samples[i];
                m_writePos = (m_writePos + 1) % m_ringCapacity;
            }
            m_size += count;
        }
    }
    m_cv.notify_all();
}

SpectrumSnapshot SpectrumAnalyzer::snapshot() const
{
    std::lock_guard<std::mutex> lock(m_snapshotMutex);
    return m_snapshot;
}

std::string SpectrumAnalyzer::toJson() const
{
    const SpectrumSnapshot snap = snapshot();

    char head[192];
    std::snprintf(head, sizeof(head),
        "{\"fft_size\":%zu,\"sample_rate\":%.0f,\"frame\":%llu,\"dropped_samples\":%llu,"
        "\"noise_floor_db\":%.1f,\"spectrum\":[",
        snap.fftSize, snap.sampleRateHz,
        static_cast<unsigned long long>(snap.frameCount),
        static_cast<unsigned long long>(snap.droppedSamples),
        static_cast<double>(snap.noiseFloorDb));

    std::string out = head;
    char value[32];
    for (std::size_t i = 0; i < snap.spectrumDb.size(); ++i) {
        if (i)
            out += ',';
        std::snprintf(value, sizeof(value), "%.1f", static_cast<double>(snap.spectrumDb[i]));
        out += value;
    }
    out += "],\"peak_hold\":[";
    for (std::size_t i = 0; i < snap.peakHoldDb.size(); ++i) {
        if (i)
            out += ',';
        std::snprintf(value, sizeof(value), "%.1f", static_cast<double>(snap.peakHoldDb[i]));
        out += value;
    }
    out += "]}";
    return out;
}

void SpectrumAnalyzer::resetPeakHold()
{
    {
        std::lock_guard<std::mutex> lock(m_snapshotMutex);
        std::fill(m_snapshot.peakHoldDb.begin(), m_snapshot.peakHoldDb.end(), -120.0F);
    }
    std::lock_guard<std::mutex> lock(m_mutex);
    std::fill(m_peakHold.begin(), m_peakHold.end(), -200.0F);
}

void SpectrumAnalyzer::setFrameHandler(std::function<void(const std::vector<float>&)> handler)
{
    std::lock_guard<std::mutex> lock(m_snapshotMutex);
    m_frameHandler = std::move(handler);
}

void SpectrumAnalyzer::run()
{
    std::vector<ComplexFloat> window(m_fftSize);
    const auto period = std::chrono::microseconds(1000000 / m_updatesPerSec);
    auto next = std::chrono::steady_clock::now();

    while (m_running.load()) {
        std::this_thread::sleep_until(next);
        if (!m_running.load())
            break;

        bool haveWindow = false;
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            if (m_size >= m_ringCapacity) {
                // Snapshot the newest window in chronological order.  The ring
                // is a sliding buffer; the producer overwrites old slots after
                // we release the lock, so do not advance m_readPos here.
                for (std::size_t i = 0; i < m_fftSize; ++i)
                    window[i] = m_ring[(m_readPos + i) % m_ringCapacity];
                haveWindow = true;
            }
        }

        if (haveWindow) {
            const std::vector<float> psd = powerSpectrum(window.data(), m_fftSize, true);
            std::vector<float> spectrumDb(psd.size());
            for (std::size_t i = 0; i < psd.size(); ++i)
                spectrumDb[i] = powerToDb(psd[i]);
            publish(spectrumDb);
        }

        next = std::chrono::steady_clock::now() + period;
    }
}

void SpectrumAnalyzer::publish(const std::vector<float>& spectrumDb)
{
    const float noiseFloor = estimateNoiseFloor(spectrumDb);

    std::function<void(const std::vector<float>&)> handler;
    {
        std::lock_guard<std::mutex> lock(m_snapshotMutex);
        m_snapshot.spectrumDb = spectrumDb;
        m_snapshot.noiseFloorDb = noiseFloor;
        m_snapshot.frameCount++;
        m_snapshot.droppedSamples = m_dropped;

        for (std::size_t i = 0; i < spectrumDb.size(); ++i)
            m_peakHold[i] = std::max(m_peakHold[i], spectrumDb[i]);
        m_snapshot.peakHoldDb = m_peakHold;
        handler = m_frameHandler;
    }

    if (handler)
        handler(spectrumDb);
}

}  // namespace sx1255::spectrum

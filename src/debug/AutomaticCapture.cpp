#include "sx1255/debug/AutomaticCapture.hpp"

#include "sx1255/debug/IQFileFormat.hpp"

#include <chrono>
#include <cstdio>
#include <ctime>
#include <fstream>
#include <stdexcept>

namespace sx1255::debug {

namespace {

std::size_t nextPowerOfTwo(std::size_t value)
{
    std::size_t n = 1;
    while (n < value)
        n <<= 1;
    return n;
}

std::string timestampedFile(const std::string& directory)
{
    const std::time_t now = std::time(nullptr);
    std::tm tm{};
    localtime_r(&now, &tm);

    char stamp[32];
    std::strftime(stamp, sizeof(stamp), "%Y%m%d_%H%M%S", &tm);

    std::string path = directory;
    if (!path.empty() && path.back() != '/')
        path += '/';
    path += "capture_";
    path += stamp;
    path += ".iq";
    return path;
}

}  // namespace

AutomaticCapture::AutomaticCapture(std::size_t sampleRateHz,
                                   double preSeconds,
                                   double postSeconds,
                                   std::string directory)
    : m_sampleRate(sampleRateHz)
    , m_preSamples(static_cast<std::size_t>(sampleRateHz * preSeconds))
    , m_postSamples(static_cast<std::size_t>(sampleRateHz * postSeconds))
    , m_directory(std::move(directory))
{
    // One extra update period of margin keeps the pre-history intact while
    // the post-trigger window is still streaming in.
    const std::size_t margin = 4096;
    m_capacity = nextPowerOfTwo(m_preSamples + m_postSamples + margin);
    m_ring.assign(m_capacity, ComplexFloat{0.0F, 0.0F});
}

AutomaticCapture::~AutomaticCapture()
{
    stop();
}

void AutomaticCapture::start()
{
    if (m_running.load())
        return;
    m_running = true;
    m_thread = std::thread([this] { run(); });
}

void AutomaticCapture::stop()
{
    if (!m_running.load())
        return;
    m_running = false;
    m_cv.notify_all();
    if (m_thread.joinable())
        m_thread.join();
}

void AutomaticCapture::push(const ComplexFloat* samples, std::size_t count)
{
    if (count == 0)
        return;

    {
        std::lock_guard<std::mutex> lock(m_mutex);
        for (std::size_t i = 0; i < count; ++i) {
            m_ring[m_written % m_capacity] = samples[i];
            ++m_written;
        }
    }
    m_cv.notify_all();
}

void AutomaticCapture::trigger(CaptureTrigger reason)
{
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        if (m_triggered)
            return;  // one dump at a time
        m_triggered = true;
        m_reason = reason;
        m_triggerWritten = m_written;
    }
    m_cv.notify_all();
}

std::uint64_t AutomaticCapture::completedCaptures() const
{
    std::lock_guard<std::mutex> lock(m_resultMutex);
    return m_completed;
}

AutomaticCapture::Result AutomaticCapture::lastResult() const
{
    std::lock_guard<std::mutex> lock(m_resultMutex);
    return m_lastResult;
}

std::uint64_t AutomaticCapture::bufferedSamples() const
{
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_written;
}

void AutomaticCapture::run()
{
    while (m_running.load()) {
        CaptureTrigger reason{};
        std::uint64_t triggerWritten = 0;

        {
            std::unique_lock<std::mutex> lock(m_mutex);
            m_cv.wait(lock, [this] { return !m_running.load() || m_triggered; });
            if (!m_running.load())
                break;
            reason = m_reason;
            triggerWritten = m_triggerWritten;
        }

        const std::uint64_t postSamples =
            reason == CaptureTrigger::Crash ? 0 : m_postSamples;
        const std::uint64_t target = triggerWritten + postSamples;

        // Wait until the post-trigger window has streamed in.
        {
            std::unique_lock<std::mutex> lock(m_mutex);
            m_cv.wait(lock, [this, target] {
                return !m_running.load() || m_written >= target;
            });
        }

        dump(reason, triggerWritten);

        {
            std::lock_guard<std::mutex> lock(m_mutex);
            m_triggered = false;
        }
    }
}

bool AutomaticCapture::dump(CaptureTrigger reason, std::uint64_t triggerWritten)
{
    Result result;
    result.trigger = reason;
    result.file = timestampedFile(m_directory);

    const std::uint64_t postSamples =
        reason == CaptureTrigger::Crash ? 0 : m_postSamples;
    const std::uint64_t windowStart =
        triggerWritten >= m_preSamples ? triggerWritten - m_preSamples : 0;
    const std::uint64_t windowEnd = triggerWritten + postSamples;
    const std::uint64_t total = windowEnd - windowStart;

    std::vector<ComplexFloat> buffer(total);

    {
        std::lock_guard<std::mutex> lock(m_mutex);
        // A trigger can only happen after the pre-window exists.
        for (std::uint64_t i = 0; i < total; ++i)
            buffer[i] = m_ring[(windowStart + i) % m_capacity];
    }

    IQFileHeader header;
    header.sampleRateHz = static_cast<std::uint32_t>(m_sampleRate);
    header.frequencyHz  = 0;
    header.sampleCount  = total;
    header.timestampSec = static_cast<std::int64_t>(
        std::chrono::duration_cast<std::chrono::seconds>(
            std::chrono::system_clock::now().time_since_epoch()).count());

    std::ofstream out(result.file, std::ios::binary | std::ios::trunc);
    if (!out.is_open())
        return false;
    if (!writeIQHeader(out, header) || !writeIQSamples(out, buffer.data(), buffer.size())) {
        out.close();
        return false;
    }

    result.ok = true;
    result.samples = total;

    {
        std::lock_guard<std::mutex> lock(m_resultMutex);
        m_lastResult = result;
        ++m_completed;
    }
    return true;
}

}  // namespace sx1255::debug

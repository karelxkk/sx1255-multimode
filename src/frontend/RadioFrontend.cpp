#include "sx1255/frontend/RadioFrontend.hpp"

#include "sx1255/core/IQDispatcher.hpp"

#include <chrono>
#include <thread>

namespace sx1255 {

RadioFrontend::RadioFrontend(std::unique_ptr<ISX1255> device, IQDispatcher& dispatcher)
    : m_device(std::move(device))
    , m_dispatcher(dispatcher)
{
}

RadioFrontend::~RadioFrontend()
{
    stop();
}

bool RadioFrontend::start(const Settings& settings)
{
    if (m_running.load())
        return true;

    m_settings = settings;

    if (!m_device->open())
        return false;

    // Low-IF receive: place the SX1255 LO 25 kHz below the channel and undo
    // that shift with a fixed NCO before dispatch.  This is the configuration
    // the original sx1255-fm-repeater used; it keeps the wanted channel out of
    // the zero-IF DC/flicker region and is shared by every mode.
    m_device->setRxFrequency(settings.rxFrequency - 25000);
    m_device->setTxFrequency(settings.txFrequency);
    m_device->setPower(settings.power);
    m_device->setSquelch(settings.squelch);
    m_rxNcoI = 1.0;
    m_rxNcoQ = 0.0;
    m_rxNcoSamples = 0;

    m_running = true;
    m_thread = std::thread([this] { run(); });
    return true;
}

void RadioFrontend::stop()
{
    m_running = false;
    if (m_thread.joinable())
        m_thread.join();
    m_device->close();
}

bool RadioFrontend::running() const
{
    return m_running.load();
}

void RadioFrontend::run()
{
    while (m_running.load()) {
        m_rxBuffer.clear();

        if (!m_device->readIQ(m_rxBuffer, m_settings.blockSize) || m_rxBuffer.empty()) {
            if (m_device->eof()) {
                // A finite source (IQ replay) is exhausted; stop cleanly so
                // callers can observe the end of the capture.
                m_running = false;
                break;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(2));
            continue;
        }

        IQBlock block;
        block.sequence = m_sequence++;
        block.samples = m_rxBuffer.data();
        block.count = m_rxBuffer.size();
        block.sampleRateHz = 125000.0;

        applyRxIfRotation(m_rxBuffer);
        block.samples = m_rxBuffer.data();
        m_dispatcher.dispatch(block);
    }
}

void RadioFrontend::applyRxIfRotation(std::vector<ComplexFloat>& samples)
{
    const double inc = 2.0 * 3.14159265358979323846 * kRxIfShiftHz / kRxSampleRateHz;
    static const double stepC = std::cos(inc);
    static const double stepS = std::sin(inc);

    // The production low-IF shift is exactly -90 degrees at 125 kSa/s.  In
    // that case a complex sample is just (I,Q) -> (Q,-I), with no arithmetic
    // oscillator or per-sample normalisation needed.
    if (std::abs(stepC) < 1e-12 && stepS < 0.0) {
        for (ComplexFloat& sample : samples) {
            const float i = sample.i;
            sample.i = sample.q;
            sample.q = -i;
        }
        return;
    }

    for (ComplexFloat& sample : samples) {
        const double i = static_cast<double>(sample.i);
        const double q = static_cast<double>(sample.q);
        sample = {
            static_cast<float>(i * m_rxNcoI - q * m_rxNcoQ),
            static_cast<float>(i * m_rxNcoQ + q * m_rxNcoI)};

        const double nextI = m_rxNcoI * stepC - m_rxNcoQ * stepS;
        m_rxNcoQ = m_rxNcoI * stepS + m_rxNcoQ * stepC;
        m_rxNcoI = nextI;

        // The oscillator is advanced by recurrence rather than two sin/cos
        // calls per sample.  Periodic normalisation removes accumulated
        // floating-point amplitude drift.
        if (++m_rxNcoSamples >= 4096) {
            const double norm = 1.0 / std::hypot(m_rxNcoI, m_rxNcoQ);
            m_rxNcoI *= norm;
            m_rxNcoQ *= norm;
            m_rxNcoSamples = 0;
        }
    }
}

ISX1255& RadioFrontend::device()
{
    return *m_device;
}

double RadioFrontend::rssi()
{
    return m_device->readRssi();
}

bool RadioFrontend::setTxFrequency(Hertz hz)
{
    return m_device->setTxFrequency(hz);
}

bool RadioFrontend::setPtt(bool on)
{
    return m_device->setPtt(on);
}

bool RadioFrontend::writeIQ(const ComplexFloat* samples, std::size_t count)
{
    return m_device->writeIQ(samples, count);
}

bool RadioFrontend::drainTx()
{
    return m_device->drainTx();
}

bool RadioFrontend::isReady() const
{
    return m_running.load();
}

}  // namespace sx1255

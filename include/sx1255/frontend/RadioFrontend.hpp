#pragma once

#include "sx1255/Types.hpp"
#include "sx1255/frontend/ISX1255.hpp"
#include "sx1255/frontend/ITransmit.hpp"

#include <atomic>
#include <cmath>
#include <cstddef>
#include <memory>
#include <thread>
#include <vector>

namespace sx1255 {

class IQDispatcher;

// ---------------------------------------------------------------------------
// RadioFrontend
//
// The one and only RF frontend.  It owns the ISX1255 device, runs the capture
// loop, stamps sequence numbers on IQ blocks and hands them to the dispatcher.
// It also implements ITransmit so plugins can key PTT and send IQ without ever
// knowing the hardware details.
//
// The frontend contains zero mode logic: FM, D-Star, M17 and every future mode
// live entirely behind the dispatcher.
// ---------------------------------------------------------------------------
class RadioFrontend final : public ITransmit {
public:
    struct Settings {
        Hertz       rxFrequency = 430975000;
        Hertz       txFrequency = 438575000;
        dBm         power       = 10;
        dBm         squelch     = -120;
        std::size_t blockSize   = 2048;
    };

    RadioFrontend(std::unique_ptr<ISX1255> device, IQDispatcher& dispatcher);
    ~RadioFrontend() override;

    RadioFrontend(const RadioFrontend&) = delete;
    RadioFrontend& operator=(const RadioFrontend&) = delete;

    bool start(const Settings& settings);
    void stop();
    bool running() const;

    // Blocking capture loop; returns when stop() is called.
    void run();

    ISX1255& device();
    double   rssi();

    // ITransmit
    bool setTxFrequency(Hertz hz) override;
    bool setPtt(bool on) override;
    bool writeIQ(const ComplexFloat* samples, std::size_t count) override;
    bool drainTx() override;
    bool isReady() const override;

private:
    std::unique_ptr<ISX1255> m_device;
    IQDispatcher&            m_dispatcher;
    Settings                 m_settings;
    std::atomic<bool>        m_running{false};
    std::thread              m_thread;
    std::vector<ComplexFloat> m_rxBuffer;
    std::uint64_t            m_sequence = 0;
    double                   m_rxNcoPhase = 0.0;

    static constexpr double kRxIfShiftHz = -25000.0;  // low-IF NCO (LO = RX - 25 kHz)
    static constexpr double kRxSampleRateHz = 125000.0;

    void applyRxIfRotation(std::vector<ComplexFloat>& samples);
};

}  // namespace sx1255

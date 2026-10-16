#pragma once

#include "sx1255/fm/FMDsp.hpp"
#include "sx1255/fm/PtyPtt.hpp"
#include "sx1255/fm/Resampler.hpp"
#include "sx1255/frontend/ITransmit.hpp"
#include "sx1255/frontend/SPSCQueue.hpp"
#include "sx1255/plugin/IModePlugin.hpp"

#include <atomic>
#include <cstddef>
#include <mutex>
#include <string>
#include <thread>

namespace sx1255::fm {

struct SvxlinkBridgeConfig {
    std::string audioCapture = "plughw:Loopback,1,0";   // TX audio from SVXLink
    std::string audioPlayback = "plughw:Loopback,0,1";  // RX audio to SVXLink
    std::string ptyPath = "/run/svxlink/ptt";
    int    audioRate = 16000;
    double rxShiftHz   = 0.0;      // 0 = zero-IF; -25000 for a low-IF SX1255
    double rxMaxDevHz = 2500.0;   // discriminator -> full-scale audio
    // Deviation reference: 2500 = NFM 12.5 kHz baseline, 5000 = WFM 25 kHz.
    // tx_gain is a fine trim around this reference.
    double txDevHz    = 2500.0;
    double txGain     = 1.75;     // 1:1 with sx1255-fm-repeater TX_GAIN
    // Audio channel shaping.  Defaults are narrow-band FM (12.5 kHz) values;
    // widen `rxAudioLpHz`/`txAudioLpHz` (and the deviation) for 25 kHz wide FM.
    double deemphTau  = 75e-6;    // RX de-emphasis time constant
    double preemphTau = 75e-6;    // TX pre-emphasis time constant
    double rxAudioLpHz = 3000.0;  // RX audio low-pass corner
    double txAudioLpHz = 3000.0;  // TX audio low-pass corner
    // High-pass on the RX audio.  A real repeater removes the sub-audible
    // CTCSS tone (and the low harmonics a clipped tone generator makes) with a
    // high-pass around 300 Hz, so the received tone is not repeated on top of
    // the tone the repeater generates itself.  It cannot be combined with
    // SVXLink's SQL_DET=CTCSS, which needs the tone in the audio: with that
    // squelch, leave this at 0.  Only the TX tone from SVXLink must never be
    // filtered, so the high-pass sits on the RX side.
    double rxHighPassHz = 0.0;
    std::size_t queueSamples = 1u << 20;
};

// ---------------------------------------------------------------------------
// FmSvxlinkBridge
//
// RF <-> audio bridge for SVXLink, mirroring the original sx1255-fm-repeater:
//
//   RX: 125 k IQ -> discriminator -> de-emphasis -> 16 kHz -> snd-aloop
//   TX: snd-aloop 16 kHz -> pre-emphasis -> FM modulator -> 125 k IQ -> SX1255
//   PTT: SVXLink writes 'T'/'R' to a PTY; we key ITransmit directly.
// ---------------------------------------------------------------------------
class FmSvxlinkBridge {
public:
    explicit FmSvxlinkBridge(SvxlinkBridgeConfig config);
    ~FmSvxlinkBridge();

    FmSvxlinkBridge(const FmSvxlinkBridge&) = delete;
    FmSvxlinkBridge& operator=(const FmSvxlinkBridge&) = delete;

    // Start the bridge for one FM session.  tx is the SX1255 frontend used
    // for TX IQ output and PTT keying.
    bool start(ITransmit* tx);
    void stop();

    // Producer side (dispatcher IQ sink): non-blocking enqueue of RX IQ.
    void pushRxIq(const ComplexFloat* samples, std::size_t count);

    // True while SVXLink has the repeater keyed (PTY 'T' seen, 'R' not yet).
    bool transmitting() const { return m_transmitting.load(); }

    // Global TX arbiter (SessionManager).  May be set after start().
    void setArbiter(ITxArbiter* arbiter) { m_arbiter = arbiter; }

    // Tells the bridge whether the FM plugin currently owns an RF session.
    // While a session is active the channel is already reserved, so PTT keys
    // directly; otherwise it must request TX through the arbiter.
    void setSessionActive(bool active)
    {
        m_sessionActive = active;
        updatePtt();
    }

private:
    void rxThread();
    void txThread();
    void updatePtt();
    void onPtt(bool on);

    SvxlinkBridgeConfig m_config;
    ITransmit* m_tx = nullptr;

    SPSCQueue<ComplexFloat> m_rxQueue;

    std::atomic<bool> m_running{false};
    std::thread m_rxThread;
    std::thread m_txThread;
    PtyPtt m_ptt;
    std::atomic<bool> m_transmitting{false};
    std::atomic<std::uint64_t> m_rxDrops{0};
    std::atomic<ITxArbiter*> m_arbiter{nullptr};
    std::atomic<bool> m_sessionActive{false};

    std::mutex m_pttMutex;
    bool m_pttWant = false;
    std::atomic<bool> m_pttKeyed{false};
    bool m_pttReserved = false;
    std::atomic<bool> m_pttPending{false};

    void* m_capture = nullptr;   // snd_pcm_t* (opaque to avoid header deps)
    void* m_playback = nullptr;
};

}  // namespace sx1255::fm

#pragma once

#include "sx1255/Types.hpp"

#include <cstddef>
#include <cstdint>
#include <string>

namespace sx1255 {

struct FrontendConfig {
    std::string type       = "null";   // "null" | "sx1255"
    Hertz       rxFrequency = 430975000;
    Hertz       txFrequency = 438575000;
    dBm         power       = 10;
    // Reserved: RSSI-based squelch is not implemented yet.  Kept in the
    // frontend interface for a future implementation; the YAML key is
    // optional and no longer part of the example configuration.
    dBm         squelch     = -120;
    std::size_t blockSize   = 2048;

    std::string spiDevice  = "/dev/spidev0.0";
    std::uint32_t spiSpeed = 500000;

    std::string gpioChip     = "/dev/gpiochip0";
    int         gpioReset    = -1;
    int         gpioTxEnable = -1;
    int         gpioRxEnable = -1;
    int         gpioPtt      = -1;

    std::string alsaCapture;
    std::string alsaPlayback;

    int    lnaGain   = 24;
    int    pgaGain   = 0;
    int    dacGain   = -3;
    double mixerGain = -12.0;
};

struct FmSvxlinkConfig {
    std::string audioCapture  = "plughw:Loopback,1,0";   // TX audio from SVXLink
    std::string audioPlayback = "plughw:Loopback,0,1";   // RX audio to SVXLink
    std::string ptyPath       = "/run/svxlink/ptt";      // SVXLink PTT_TYPE=PTY
    std::string sqlPty        = "/run/svxlink/sql";      // SQL_DET=SERIAL (CTS)
    std::string sqlPtyOwner   = "";                      // "user" or "user:group"
    int    audioRate  = 16000;
    double rxShiftHz   = 0.0;     // 0 = zero-IF (SX1255 LO=RX); -25000 pro low-IF
    double rxMaxDevHz = 2500.0;
    double txDevHz    = 2500.0;
    double txGain     = 1.75;     // 1:1 s sx1255-fm-repeater (TX_GAIN)
    // Audio channel shaping (NFM 12.5 kHz defaults; raise for wideband/HiFi).
    double rxDeemphUs    = 75.0;   // RX de-emphasis time constant
    double txPreemphUs   = 75.0;   // TX pre-emphasis time constant
    double rxAudioLpfHz  = 3000.0; // RX audio low-pass corner
    double txAudioLpfHz  = 3000.0; // TX audio low-pass corner (pre-modulation)
    // RX audio high-pass.  It removes the sub-audible CTCSS tone and the low
    // harmonics a clipped tone generator produces from the audio that goes to
    // SVXLink - but SVXLink's own CTCSS squelch then has no tone left to
    // detect, so the two cannot be combined.  0 (default) keeps the tone in
    // the audio, which is what SQL_DET=CTCSS needs.
    double rxHighPassHz  = 0.0;
};

struct FmConfig {
    bool   enabled    = true;
    double ctcssTx    = 88.5;   // CTCSS regenerated on TX (0 disables)
    // Carrier detection is done by noise, not by a CTCSS tone.  The detector
    // high-passes the discriminator output above the voice band and compares
    // that energy with the total: open-channel noise sits around 1.0-1.3,
    // while a carrier (with or without speech) quiets the out-of-band noise
    // well below this.  Below this ratio the squelch opens.
    double squelchNoiseRatio = 0.65;
    int    hangTimeMs = 3000;   // local-repeat TX tail after carrier loss
    std::string backend = "local";  // "local" | "svxlink"
    FmSvxlinkConfig svxlink;
};

struct DStarConfig {
    bool        enabled = true;
    std::string callsign = "OK1LBC";
    unsigned int txPreambleBytes = 210;
    unsigned int txTailMs = 50;

    struct Gateway {
        bool         enabled      = false;
        std::string  address      = "127.0.0.1";
        unsigned int port         = 20010;
        std::string  localAddress = "127.0.0.1";
        unsigned int localPort    = 20011;
        std::string  name         = "sx1255-multimode";
    } gateway;
};

struct M17Config {
    bool        enabled = true;
    std::string callsign = "N0CALL";

    // --- RX detection and decoding -----------------------------------------
    // Thresholds are euclidean distances in the symbol domain (ideal symbols
    // are +-3), so a lower value is stricter.  The defaults match
    // m17-gateway; they can be loosened for marginal signals.
    double syncLsfThreshold    = 7.5;   // preamble + LSF sync word
    double syncPacketThreshold = 5.0;   // packet sync word + the next frame
    double syncStreamThreshold = 5.0;   // stream sync word + the next frame
    double syncEotThreshold    = 4.5;   // end-of-transmission marker
    // The LSF sync word alone (without the preamble).  A radio retrying a
    // packet sends its header without the preamble again; the frames were
    // heard, the header was not, and the message could not be routed.
    double syncLsfSyncThreshold = 4.5;
    double syncFollowThreshold = 4.5;   // sync word alone, inside a transmission
    // Sync bursts needed inside 500 ms before M17 is *declared* (a valid LSF
    // is always trusted immediately).  Detection only decides who owns the
    // channel; it never gates decoding, so a packet is never lost because it
    // was too short to raise the burst count.
    int    minSyncBurst = 5;
    // End the M17 session when nothing valid was received for this long.
    int    idleTimeoutMs = 1500;
    // Log every detected sync burst (type, distance, decoder state).  Verbose,
    // meant for diagnosing what a real radio actually transmits.
    bool   logSync = false;
    // Network -> RF packets wait in this queue instead of being dropped when
    // the channel is briefly busy; they expire after the TTL.
    int    networkPacketQueue = 8;
    int    networkPacketTtlMs = 2000;

    // Secondary M17 inet connection for m17-bridge (APRS messaging and GNSS
    // position).  The main `reflector` stays available for voice/stream.
    struct Bridge {
        bool        enabled = false;
        std::string host = "127.0.0.1";
        int         port = 17000;
        std::string module = "A";
        std::string callsign;   // empty = use the main M17 callsign
    } bridge;

    struct Reflector {
        bool        enabled = false;
        std::string host;
        int         port = 17000;
        std::string module = "A";
    } reflector;
};

// Preferred network source for network -> RF traffic.  "auto" keeps the
// current behaviour (the last locally used mode wins); any other value pins
// the source so a mode can be used without a local radio (e.g. D-Star
// gateway).
struct NetworkConfig {
    std::string mode = "auto";   // "auto" | "fm" | "dstar" | "m17"
};

struct RecordingConfig {
    bool        enabled = false;
    std::string file    = "capture.iq";
};

struct ReplayConfig {
    bool        enabled = false;
    std::string file    = "capture.iq";
};

struct AutomaticCaptureConfig {
    bool        enabled     = false;
    std::string directory   = ".";
    double      preSeconds  = 10.0;
    double      postSeconds = 10.0;
    bool        triggerFalsePositive = true;
    bool        triggerUnknownSignal = true;
    bool        triggerCrash         = true;
};

struct SpectrumConfig {
    bool        enabled      = true;
    int         fftSize      = 4096;
    int         updateHz     = 5;
    int         waterfallRows = 500;
    int         waterfallBins = 256;
};

struct Config {
    FrontendConfig frontend;
    FmConfig       fm;
    DStarConfig    dstar;
    M17Config      m17;
    NetworkConfig  network;
    RecordingConfig recording;
    ReplayConfig    replay;
    AutomaticCaptureConfig automaticCapture;
    SpectrumConfig         spectrum;
};

// Parse the YAML-subset configuration file.  Throws std::runtime_error with a
// line number on malformed input or missing/mistyped keys.
Config loadConfig(const std::string& path);

// Convert a network mode name to Mode.  Unknown is returned for "auto" and
// for invalid names (callers should validate explicitly if strict parsing is
// needed).
Mode parseNetworkMode(const std::string& mode);

// Render the configuration back to YAML (used by tests to round-trip values).
std::string serializeConfig(const Config& config);

}  // namespace sx1255

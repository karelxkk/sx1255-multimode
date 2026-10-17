#pragma once

#include "sx1255/Types.hpp"
#include "sx1255/plugin/IModePlugin.hpp"

#include <chrono>
#include <functional>
#include <memory>
#include <mutex>
#include <vector>

namespace sx1255 {

class SystemStatistics;

// A single plugin's verdict for one IQ block, gathered by the dispatcher.
struct DetectionReport {
    std::shared_ptr<IModePlugin> plugin;
    Detection                    detection;
};

// ---------------------------------------------------------------------------
// SessionManager
//
// Owns the channel state machine and enforces the single-owner rule:
//
//   IDLE -> DETECTING -> ACTIVE_<mode> -> RETURN_TO_IDLE -> IDLE
//                               |
//                               +--> TIMEOUT -> RETURN_TO_IDLE -> IDLE
//
// Exactly one plugin may be active at a time.  Arbitration is deterministic:
// the highest-priority plugin with a positive detection wins (M17 > D-Star >
// FM).  All calls are expected from the single frontend/dispatch thread, but
// the state is still guarded so a watchdog thread may call timeoutCheck().
// ---------------------------------------------------------------------------
class SessionManager : public ITxArbiter {
public:
    enum class State {
        Idle,
        Detecting,
        ActiveFm,
        ActiveDStar,
        ActiveM17,
        ActiveM17Network,
        ActiveSvxlink,
        Timeout,
        ReturnToIdle,
    };

    struct Settings {
        std::chrono::milliseconds sessionTimeout{300000};
        std::chrono::milliseconds returnGrace{20};
    };

    explicit SessionManager();
    explicit SessionManager(Settings settings);
    ~SessionManager();

    SessionManager(const SessionManager&) = delete;
    SessionManager& operator=(const SessionManager&) = delete;

    State state() const;
    bool  isActive() const;
    std::shared_ptr<IModePlugin> owner() const;
    std::uint64_t activeSessionId() const;

    // Idle -> Detecting.  Idempotent while a session is not active.
    void start();

    // Evaluate the gathered detections.  On success this calls
    // startSession() on the winning plugin and returns it; otherwise nullptr.
    IModePlugin* evaluateDetections(const std::vector<DetectionReport>& reports,
                                    ITransmit* tx);

    // Release the active session (normal end or timeout) and schedule the
    // return-to-idle grace period.
    void finishSession();

    // Advance time-based transitions (session timeout, return grace).
    void timeoutCheck();

    // Stop everything and return to Idle.
    void reset();

    // ITxArbiter
    bool acquireTx(Mode mode) override;
    bool acquireTxPacket(Mode mode) override;
    void releaseTx() override;

    // Mode the operator last used on a local (RF) reception.  A network source
    // is only allowed to transmit back to RF when it matches this mode, so the
    // operator keeps hearing what they last worked.  Defaults to FM/SVXLink.
    Mode lastLocalMode() const;

    // Effective network source mode: an explicitly configured preference wins,
    // otherwise the last local mode is used.
    Mode networkMode() const;

    // Pin the network source to one mode.  Mode::Unknown restores the
    // last-local-mode behaviour.
    void setPreferredNetworkMode(Mode mode);

    static State stateForMode(Mode mode);
    static const char* toString(State state);

    void setStatistics(SystemStatistics* stats) { m_stats = stats; }

    // Invoked (on the dispatch thread) when a positive detection arrives
    // while another plugin already owns the channel (a false positive).
    void setFalsePositiveHandler(std::function<void()> handler) { m_falsePositiveHandler = std::move(handler); }

private:
    mutable std::mutex      m_mutex;
    Settings                m_settings;
    State                   m_state = State::Idle;
    std::shared_ptr<IModePlugin> m_owner;
    std::uint64_t           m_sessionId = 0;
    std::uint64_t           m_sessionCounter = 0;
    Mode                    m_lastLocalMode = Mode::FM;
    Mode                    m_preferredNetworkMode = Mode::Unknown;
    std::chrono::steady_clock::time_point m_activeSince{};
    std::chrono::steady_clock::time_point m_returnSince{};
    SystemStatistics* m_stats = nullptr;
    std::function<void()> m_falsePositiveHandler;
};

}  // namespace sx1255

#include "sx1255/core/SessionManager.hpp"

#include "sx1255/diag/SystemStatistics.hpp"

#include <algorithm>
#include <utility>

namespace sx1255 {

using Clock = std::chrono::steady_clock;

namespace {

bool isActiveState(SessionManager::State state)
{
    switch (state) {
        case SessionManager::State::ActiveFm:
        case SessionManager::State::ActiveDStar:
        case SessionManager::State::ActiveM17:
        case SessionManager::State::ActiveM17Network:
        case SessionManager::State::ActiveSvxlink:
            return true;
        default:
            return false;
    }
}

}  // namespace

SessionManager::SessionManager()
    : SessionManager(Settings{})
{
}

SessionManager::SessionManager(Settings settings)
    : m_settings(settings)
{
}

SessionManager::~SessionManager()
{
    reset();
}

SessionManager::State SessionManager::state() const
{
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_state;
}

bool SessionManager::isActive() const
{
    std::lock_guard<std::mutex> lock(m_mutex);
    return isActiveState(m_state);
}

std::shared_ptr<IModePlugin> SessionManager::owner() const
{
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_owner;
}

std::uint64_t SessionManager::activeSessionId() const
{
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_sessionId;
}

void SessionManager::start()
{
    std::lock_guard<std::mutex> lock(m_mutex);
    if (m_state == State::Idle)
        m_state = State::Detecting;
}

SessionManager::State SessionManager::stateForMode(Mode mode)
{
    switch (mode) {
        case Mode::FM:    return State::ActiveFm;
        case Mode::DStar: return State::ActiveDStar;
        case Mode::M17:   return State::ActiveM17;
        default:          return State::Detecting;
    }
}

const char* SessionManager::toString(State state)
{
    switch (state) {
        case State::Idle:         return "IDLE";
        case State::Detecting:    return "DETECTING";
        case State::ActiveFm:     return "ACTIVE_FM";
        case State::ActiveDStar:  return "ACTIVE_DSTAR";
        case State::ActiveM17:    return "ACTIVE_M17";
        case State::ActiveM17Network: return "ACTIVE_M17_NETWORK";
        case State::ActiveSvxlink:    return "ACTIVE_SVXLINK";
        case State::Timeout:      return "TIMEOUT";
        case State::ReturnToIdle: return "RETURN_TO_IDLE";
        default:                  return "UNKNOWN";
    }
}

IModePlugin* SessionManager::evaluateDetections(const std::vector<DetectionReport>& reports,
                                                ITransmit* tx)
{
    std::shared_ptr<IModePlugin> granted;

    {
        std::lock_guard<std::mutex> lock(m_mutex);

        if (isActiveState(m_state)) {
            if (m_stats) {
                for (const auto& report : reports)
                    if (report.plugin && report.detection.detected) {
                        m_stats->recordFalsePositive();
                        if (m_falsePositiveHandler)
                            m_falsePositiveHandler();
                        break;
                    }
            }
            return nullptr;
        }

        if (m_state == State::Idle)
            m_state = State::Detecting;

        // Deterministic arbitration: highest priority first, stable order.
        std::vector<DetectionReport> sorted = reports;
        std::stable_sort(sorted.begin(), sorted.end(),
                         [](const DetectionReport& a, const DetectionReport& b) {
                             return a.plugin->priority() > b.plugin->priority();
                         });

        for (const auto& report : sorted) {
            if (!report.plugin || !report.detection.detected)
                continue;

            granted = report.plugin;
            m_owner = report.plugin;
            m_state = stateForMode(report.plugin->mode());
            m_sessionId = ++m_sessionCounter;
            m_activeSince = Clock::now();
            break;
        }
    }

    if (granted) {
        SessionContext ctx;
        ctx.sessionId = m_sessionId;
        ctx.mode = granted->mode();
        ctx.pluginId = std::string(granted->id());
        ctx.tx = tx;
        granted->startSession(ctx);
        return granted.get();
    }

    return nullptr;
}

bool SessionManager::acquireTx(Mode mode)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    if (isActiveState(m_state) || m_state == State::Timeout)
        return false;

    m_owner.reset();
    m_state = (mode == Mode::FM) ? State::ActiveSvxlink : State::ActiveM17Network;
    m_sessionId = ++m_sessionCounter;
    m_activeSince = Clock::now();
    (void)mode;
    return true;
}

void SessionManager::releaseTx()
{
    std::lock_guard<std::mutex> lock(m_mutex);
    if (m_state != State::ActiveM17Network && m_state != State::ActiveSvxlink)
        return;
    m_owner.reset();
    m_state = State::Idle;   // the dispatcher will move Idle -> Detecting
}

void SessionManager::finishSession()
{
    std::shared_ptr<IModePlugin> toStop;

    {
        std::lock_guard<std::mutex> lock(m_mutex);
        if (!isActiveState(m_state) && m_state != State::Timeout)
            return;

        toStop = std::move(m_owner);
        m_owner.reset();
        m_state = State::ReturnToIdle;
        m_returnSince = Clock::now();
    }

    if (toStop)
        toStop->stopSession();
}

void SessionManager::timeoutCheck()
{
    std::shared_ptr<IModePlugin> toStop;

    {
        std::lock_guard<std::mutex> lock(m_mutex);
        const auto now = Clock::now();

        switch (m_state) {
            case State::ReturnToIdle:
                if (now - m_returnSince >= m_settings.returnGrace)
                    m_state = State::Idle;
                break;

            case State::Timeout:
                m_state = State::ReturnToIdle;
                m_returnSince = now;
                break;

            default:
                if (isActiveState(m_state) && now - m_activeSince >= m_settings.sessionTimeout) {
                    m_state = State::Timeout;
                    toStop = std::move(m_owner);
                    m_owner.reset();
                    m_returnSince = now;
                }
                break;
        }
    }

    if (toStop)
        toStop->stopSession();
}

void SessionManager::reset()
{
    std::shared_ptr<IModePlugin> toStop;

    {
        std::lock_guard<std::mutex> lock(m_mutex);
        toStop = std::move(m_owner);
        m_owner.reset();
        m_state = State::Idle;
        m_sessionId = 0;
    }

    if (toStop)
        toStop->stopSession();
}

}  // namespace sx1255

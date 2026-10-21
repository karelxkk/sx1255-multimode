#pragma once

#include "sx1255/plugin/IModePlugin.hpp"

#include <functional>
#include <string>

namespace sx1255::test {

// Scriptable IModePlugin used by unit tests.
class MockPlugin final : public IModePlugin {
public:
    struct Log {
        int starts = 0;
        int processes = 0;
        int stops = 0;
        int detects = 0;
        int detectsWhileBlocked = 0;
    };

    using Detector = std::function<Detection(const IQBlock&)>;

    MockPlugin(std::string id, Mode mode, int priority, Detector detector = {})
        : m_id(std::move(id))
        , m_mode(mode)
        , m_priority(priority)
        , m_detector(std::move(detector))
    {
    }

    std::string_view id() const noexcept override { return m_id; }
    Mode mode() const noexcept override { return m_mode; }
    int priority() const noexcept override { return m_priority; }

    Detection detect(const IQBlock& block) override
    {
        ++log.detects;
        if (rxBlocked)
            ++log.detectsWhileBlocked;
        return m_detector ? m_detector(block) : Detection{false, 0.0F};
    }

    void startSession(const SessionContext& ctx) override
    {
        ++log.starts;
        lastContext = ctx;
        active = true;
    }

    bool process(const IQBlock&) override
    {
        ++log.processes;
        return processResult;
    }

    void stopSession() override
    {
        ++log.stops;
        active = false;
    }

    void setRxBlocked(bool blocked, std::string_view reason) override
    {
        rxBlocked = blocked;
        rxBlockReason = std::string(reason);
    }

    std::string m_id;
    Mode m_mode;
    int m_priority;
    Detector m_detector;
    bool processResult = true;
    bool active = false;
    bool rxBlocked = false;
    std::string rxBlockReason;
    SessionContext lastContext;
    Log log;
};

}  // namespace sx1255::test

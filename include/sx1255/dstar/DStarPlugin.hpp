#pragma once

#include "sx1255/dstar/DStarDetector.hpp"
#include "sx1255/plugin/IModePlugin.hpp"

#include <string_view>

namespace sx1255 {

struct DStarConfig;
class SystemStatistics;

// D-Star mode plugin.  Pure adapter: it owns no demodulation/decoding logic,
// only the DStarDetector and the IModePlugin glue for IQDispatcher /
// SessionManager.
class DStarPlugin final : public IModePlugin {
public:
    explicit DStarPlugin(DStarConfig config, SystemStatistics* stats = nullptr);

    std::string_view id() const noexcept override { return "dstar"; }
    Mode mode() const noexcept override { return Mode::DStar; }
    int priority() const noexcept override { return 20; }

    Detection detect(const IQBlock& block) override;
    void startSession(const SessionContext& ctx) override;
    bool process(const IQBlock& block) override;
    void stopSession() override;
    PluginTelemetry telemetry() const override;

private:
    dstar::DStarDetector m_detector;
    SystemStatistics* m_stats;
    bool m_active = false;
    float m_lastConfidence = 0.0F;
};

}  // namespace sx1255

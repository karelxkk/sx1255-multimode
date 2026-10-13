#include "sx1255/dstar/DStarPlugin.hpp"

#include "sx1255/diag/SystemStatistics.hpp"
#include "sx1255/plugins/PluginFactory.hpp"

#include <memory>

namespace sx1255 {

DStarPlugin::DStarPlugin(DStarConfig, SystemStatistics* stats)
    : m_stats(stats)
{
}

Detection DStarPlugin::detect(const IQBlock& block)
{
    const Detection d = m_detector.detect(block);
    m_lastConfidence = d.detected ? 1.0F : 0.0F;
    if (d.detected && m_stats)
        m_stats->recordDetection(Mode::DStar);
    return d;
}

void DStarPlugin::startSession(const SessionContext&)
{
    m_active = true;
    m_detector.reset();
    if (m_stats)
        m_stats->recordSession(Mode::DStar);
}

bool DStarPlugin::process(const IQBlock& block)
{
    // Keep feeding the detector so the session can observe EOT later; for now
    // ownership is released only by stopSession().
    m_detector.detect(block);
    return m_active;
}

void DStarPlugin::stopSession()
{
    m_active = false;
    m_detector.reset();
}

PluginTelemetry DStarPlugin::telemetry() const
{
    PluginTelemetry t;
    t.mode = Mode::DStar;
    t.metrics["sync_confidence"] = m_lastConfidence;
    t.metrics["frame_count"] = static_cast<double>(m_detector.frameSyncsSeen());
    return t;
}

std::shared_ptr<IModePlugin> createDStarPlugin(const DStarConfig& config, SystemStatistics* stats)
{
    return std::make_shared<DStarPlugin>(config, stats);
}

}  // namespace sx1255

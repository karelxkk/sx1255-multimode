#include "sx1255/core/IQDispatcher.hpp"

#include "sx1255/core/PluginRegistry.hpp"
#include "sx1255/core/SessionManager.hpp"
#include "sx1255/frontend/ITransmit.hpp"

#include <utility>
#include <vector>

namespace sx1255 {

IQDispatcher::IQDispatcher(PluginRegistry& registry, SessionManager& sessions, ITransmit* tx)
    : m_registry(registry)
    , m_sessions(sessions)
    , m_tx(tx)
{
}

void IQDispatcher::setTransmitter(ITransmit* tx)
{
    m_tx = tx;
}

void IQDispatcher::setIqSink(std::function<void(const ComplexFloat*, std::size_t)> sink)
{
    m_iqSink = std::move(sink);
}

void IQDispatcher::dispatch(const IQBlock& block)
{
    ++m_blocks;

    if (m_iqSink)
        m_iqSink(block.samples, block.count);

    m_sessions.timeoutCheck();

    // Exclusive phase: only the owning plugin sees the IQ stream.
    if (m_sessions.isActive()) {
        auto owner = m_sessions.owner();
        if (!owner) {
            // ACTIVE_SVXLINK / ACTIVE_M17_NETWORK: the arbiter holds TX but no
            // plugin owns a session.  The RX path must stay alive so the FM
            // plugin can keep feeding the SVXLink bridge (RF -> SVXLink).  Run
            // the monitoring fan-out, but do not grant a new session.
            for (const auto& plugin : m_registry.plugins())
                if (plugin)
                    plugin->detect(block);
            return;
        }
        if (!owner->process(block))
            m_sessions.finishSession();
        // The plugins that do not own the session keep monitoring it: M17
        // packet mode must not lose a packet because another mode (FM, D-Star
        // or their hang time) currently holds the channel.
        for (const auto& plugin : m_registry.plugins()) {
            if (plugin && plugin != owner)
                plugin->monitor(block);
        }
        return;
    }

    // Monitoring phase: fan the same block out to every plugin.
    std::vector<DetectionReport> reports;
    reports.reserve(m_registry.count());

    for (const auto& plugin : m_registry.plugins()) {
        if (!plugin)
            continue;
        reports.push_back({plugin, plugin->detect(block)});
    }

    m_sessions.evaluateDetections(reports, m_tx);
}

std::uint64_t IQDispatcher::blocksProcessed() const
{
    return m_blocks;
}

}  // namespace sx1255

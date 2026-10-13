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

    // Always-on RX hook, independent of who owns the transmitter: keeps the
    // FM/SVXLink receive path (and its squelch) alive in every state without
    // running each plugin's full detect() on the dispatch thread.
    for (const auto& plugin : m_registry.plugins())
        if (plugin)
            plugin->feedRx(block);

    m_sessions.timeoutCheck();

    // Exclusive phase: only the owning plugin sees the IQ stream.
    if (m_sessions.isActive()) {
        auto owner = m_sessions.owner();
        if (!owner) {
            // ACTIVE_SVXLINK / ACTIVE_M17_NETWORK: arbiter-only TX state.  The
            // RX path was already fed by feedRx() above; do not grant a new
            // session and do not run the (expensive) full detection fan-out.
            return;
        }
        if (!owner->process(block))
            m_sessions.finishSession();
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

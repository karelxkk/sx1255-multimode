#include "sx1255/core/IQDispatcher.hpp"

#include "sx1255/core/PluginRegistry.hpp"
#include "sx1255/core/SessionManager.hpp"
#include "sx1255/frontend/ITransmit.hpp"

#include <algorithm>
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
            for (const auto& plugin : m_registry.plugins()) {
                if (plugin) {
                    // A network source owns the RF transmitter here.  Keep
                    // decoding local modes, but do not feed local RX audio to
                    // SVXLink: an M17/D-Star transmission could otherwise be
                    // forwarded as FM while it waits for the network TX to end.
                    plugin->setRxBlocked(plugin->mode() == Mode::FM);
                    plugin->setDetectionCounting(true);
                    plugin->detect(block);
                }
            }
            return;
        }
        // The plugins that do not own the session keep monitoring it.  Running
        // their detect() keeps the receive path alive (an M17 packet must not be
        // lost because another mode holds the channel) and it gives the
        // arbitration a way out of a wrong owner: a noise-squelch FM detector
        // cannot tell an analogue voice carrier from a digital one, so a mode
        // with a higher priority that reports a carrier takes the channel over.
        // Dropping the session is enough; the next block grants it to the
        // winner again.
        bool higherWants = false;
        bool m17Wants = false;
        for (const auto& plugin : m_registry.plugins()) {
            if (!plugin || plugin == owner)
                continue;
            plugin->setRxBlocked(true);
            plugin->setDetectionCounting(false);
            const Detection d = plugin->detect(block);
            if (d.detected && plugin->priority() > owner->priority()) {
                higherWants = true;
                if (plugin->mode() == Mode::M17)
                    m17Wants = true;
            }
        }
        owner->setRxBlocked(false);
        owner->setDetectionCounting(true);
        // A valid M17 LSF is CRC-checked.  Release the old owner before it
        // processes this block, so no M17 audio is forwarded as FM.
        if (m17Wants) {
            m_preemptBlocks = 0;
            m_sessions.finishSession();
            return;
        }
        // The hand-over needs a confirmed detection: a single block of a stray
        // correlation from a digital detector must not cut an analogue session.
        m_preemptBlocks = higherWants ? m_preemptBlocks + 1 : 0;
        if (m_preemptBlocks >= kPreemptBlocks) {
            m_preemptBlocks = 0;
            m_sessions.finishSession();
            return;
        }
        if (!owner->process(block))
            m_sessions.finishSession();
        return;
    }

    // Monitoring phase: fan the same block out to every plugin.  Detection is
    // ordered by arbitration priority so a digital mode gets to claim the
    // block before FM forwards it to the SVXLink audio bridge.  Without this,
    // the first M17 LSF block was demodulated and sent to SVXLink as noise
    // simply because FM happened to be registered first.
    std::vector<DetectionReport> reports;
    reports.reserve(m_registry.count());

    auto plugins = m_registry.plugins();
    std::stable_sort(plugins.begin(), plugins.end(),
                     [](const auto& a, const auto& b) {
                         return a->priority() > b->priority();
                     });

    bool digitalDetected = false;
    for (const auto& plugin : plugins) {
        if (!plugin)
            continue;
        plugin->setRxBlocked(plugin->mode() == Mode::FM && digitalDetected);
        plugin->setDetectionCounting(true);
        const Detection detection = plugin->detect(block);
        if (detection.detected && plugin->mode() != Mode::FM)
            digitalDetected = true;
        reports.push_back({plugin, detection});
    }

    IModePlugin* owner = m_sessions.evaluateDetections(reports, m_tx);
    if (owner) {
        for (const auto& plugin : m_registry.plugins()) {
            if (plugin && plugin.get() != owner) {
                plugin->setRxBlocked(true);
                plugin->clearPendingDetection();
            }
        }
    }
}

std::uint64_t IQDispatcher::blocksProcessed() const
{
    return m_blocks;
}

}  // namespace sx1255

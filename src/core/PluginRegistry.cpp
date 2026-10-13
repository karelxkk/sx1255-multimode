#include "sx1255/core/PluginRegistry.hpp"

#include <algorithm>

namespace sx1255 {

void PluginRegistry::registerPlugin(std::shared_ptr<IModePlugin> plugin)
{
    if (!plugin)
        return;

    std::lock_guard<std::mutex> lock(m_mutex);
    m_plugins.push_back(std::move(plugin));
}

bool PluginRegistry::unregister(std::string_view id)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    const auto it = std::find_if(m_plugins.begin(), m_plugins.end(),
                                 [id](const auto& p) { return p && p->id() == id; });
    if (it == m_plugins.end())
        return false;
    m_plugins.erase(it);
    return true;
}

std::vector<std::shared_ptr<IModePlugin>> PluginRegistry::plugins() const
{
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_plugins;
}

std::vector<std::shared_ptr<IModePlugin>> PluginRegistry::byMode(Mode mode) const
{
    std::vector<std::shared_ptr<IModePlugin>> result;
    std::lock_guard<std::mutex> lock(m_mutex);
    for (const auto& plugin : m_plugins)
        if (plugin && plugin->mode() == mode)
            result.push_back(plugin);
    return result;
}

std::shared_ptr<IModePlugin> PluginRegistry::find(std::string_view id) const
{
    std::lock_guard<std::mutex> lock(m_mutex);
    const auto it = std::find_if(m_plugins.begin(), m_plugins.end(),
                                 [id](const auto& p) { return p && p->id() == id; });
    return it == m_plugins.end() ? std::shared_ptr<IModePlugin>{} : *it;
}

std::size_t PluginRegistry::count() const
{
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_plugins.size();
}

}  // namespace sx1255

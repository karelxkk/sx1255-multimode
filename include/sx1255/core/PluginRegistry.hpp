#pragma once

#include "sx1255/plugin/IModePlugin.hpp"

#include <memory>
#include <mutex>
#include <string_view>
#include <vector>

namespace sx1255 {

// ---------------------------------------------------------------------------
// PluginRegistry
//
// Holds the set of available mode plugins.  Registration happens at startup;
// the registry is read on every IQ block by the dispatcher, so reads must be
// cheap.  The mutex only protects the (rare) registration/unregistration path.
//
// Dynamic loading (.so plugins) is a planned extension: the registry only
// depends on IModePlugin, so a future dlopen-based loader can register
// plugins through the same registerPlugin() entry point.
// ---------------------------------------------------------------------------
class PluginRegistry {
public:
    void registerPlugin(std::shared_ptr<IModePlugin> plugin);
    bool unregister(std::string_view id);

    std::vector<std::shared_ptr<IModePlugin>> plugins() const;
    std::vector<std::shared_ptr<IModePlugin>> byMode(Mode mode) const;
    std::shared_ptr<IModePlugin> find(std::string_view id) const;

    std::size_t count() const;

private:
    mutable std::mutex m_mutex;
    std::vector<std::shared_ptr<IModePlugin>> m_plugins;
};

}  // namespace sx1255

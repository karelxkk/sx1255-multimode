#pragma once

#include "sx1255/config/Config.hpp"
#include "sx1255/plugin/IModePlugin.hpp"

#include <memory>

namespace sx1255 {

class SystemStatistics;

// Construct the built-in mode plugins.  These are the compile-time defaults;
// a future dlopen-based loader will register additional IModePlugin
// implementations through the same PluginRegistry interface.
std::shared_ptr<IModePlugin> createFmPlugin(const FmConfig& config, SystemStatistics* stats = nullptr);
std::shared_ptr<IModePlugin> createDStarPlugin(const DStarConfig& config, SystemStatistics* stats = nullptr);
std::shared_ptr<IModePlugin> createM17Plugin(const M17Config& config, SystemStatistics* stats = nullptr);

}  // namespace sx1255

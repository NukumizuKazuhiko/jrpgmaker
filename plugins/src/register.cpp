#include "jrpgmaker/plugins/register.hpp"

#include <memory>

#include "jrpgmaker/plugins/sample_instant/instant.hpp"
#include "jrpgmaker/plugins/sample_style/style.hpp"
#include "jrpgmaker/plugins/sample_turn_based/turn_based.hpp"
#include "jrpgmaker/plugins/sample_unlit/unlit.hpp"

namespace jrpgmaker::plugins {

std::optional<plugin::PluginError>
RegisterSamplePlugins(plugin::PluginRegistry& registry,
                      const plugin::PluginManifest& unlit_manifest,
                      const plugin::PluginManifest& style_manifest) {
    if (const auto error = registry.Register(
            unlit_manifest, [] { return std::make_unique<sample_unlit::Adapter>(); });
        error.has_value()) {
        return error;
    }
    return registry.Register(style_manifest,
                             [] { return std::make_unique<sample_style::Adapter>(); });
}

std::optional<plugin::PluginError>
RegisterSampleBattlePlugins(plugin::PluginRegistry& registry,
                            const plugin::PluginManifest& instant_manifest,
                            const plugin::PluginManifest& turn_based_manifest) {
    if (const auto error = registry.Register(
            instant_manifest, [] { return std::make_unique<sample_instant::Adapter>(); });
        error.has_value()) {
        return error;
    }
    return registry.Register(turn_based_manifest,
                             [] { return std::make_unique<sample_turn_based::Adapter>(); });
}

} // namespace jrpgmaker::plugins

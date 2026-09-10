#include "jrpgmaker/plugins/register.hpp"

#include <memory>

#include "jrpgmaker/plugins/sample_instant/instant.hpp"
#include "jrpgmaker/plugins/sample_style/style.hpp"
#include "jrpgmaker/plugins/sample_turn_based/turn_based.hpp"
#include "jrpgmaker/plugins/sample_unlit/unlit.hpp"

namespace jrpgmaker::plugins {

std::vector<plugin::CompiledPluginFactory> CompiledSamplePlugins() {
    return {
        {.id = "sample.unlit",
         .manifest_path = "plugins/sample_unlit/plugin.json",
         .factory = [] { return std::make_unique<sample_unlit::Adapter>(); }},
        {.id = "sample.style",
         .manifest_path = "plugins/sample_style/plugin.json",
         .factory = [] { return std::make_unique<sample_style::Adapter>(); }},
        {.id = "sample.instant",
         .manifest_path = "plugins/sample_instant/plugin.json",
         .factory = [] { return std::make_unique<sample_instant::Adapter>(); }},
        {.id = "sample.turn_based",
         .manifest_path = "plugins/sample_turn_based/plugin.json",
         .factory = [] { return std::make_unique<sample_turn_based::Adapter>(); }},
    };
}

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

#pragma once

#include "jrpgmaker/plugin/plugin.hpp"

namespace jrpgmaker::plugins {

// Factories compiled into the sample host, with their project-relative
// manifests. Hosts select entries by the plugin IDs declared by project.json.
[[nodiscard]] std::vector<plugin::CompiledPluginFactory> CompiledSamplePlugins();

// The application supplies manifests loaded from project/plugin data. This
// function owns only the build-time factory bindings for the shipped samples.
[[nodiscard]] std::optional<plugin::PluginError>
RegisterSamplePlugins(plugin::PluginRegistry& registry,
                      const plugin::PluginManifest& unlit_manifest,
                      const plugin::PluginManifest& style_manifest);

[[nodiscard]] std::optional<plugin::PluginError>
RegisterSampleBattlePlugins(plugin::PluginRegistry& registry,
                            const plugin::PluginManifest& instant_manifest,
                            const plugin::PluginManifest& turn_based_manifest);

} // namespace jrpgmaker::plugins

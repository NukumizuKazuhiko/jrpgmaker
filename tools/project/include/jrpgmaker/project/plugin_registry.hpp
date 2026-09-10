#pragma once

#include <filesystem>
#include <memory>
#include <span>
#include <string>
#include <vector>

#include "jrpgmaker/plugin/plugin.hpp"
#include "jrpgmaker/project/workspace.hpp"

namespace jrpgmaker::project {

struct PluginRegistryAssembly {
    std::shared_ptr<const plugin::PluginRegistry> registry;
    std::vector<std::filesystem::path> data_roots;
    std::vector<project::Diagnostic> diagnostics;
    explicit operator bool() const { return registry != nullptr && diagnostics.empty(); }
};

[[nodiscard]] PluginRegistryAssembly
AssembleProjectPluginRegistry(const std::filesystem::path& project_root,
                              std::span<const plugin::CompiledPluginFactory> compiled_factories);

} // namespace jrpgmaker::project

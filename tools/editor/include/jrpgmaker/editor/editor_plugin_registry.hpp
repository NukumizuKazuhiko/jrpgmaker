#pragma once

#include <filesystem>
#include <memory>
#include <span>
#include <string>
#include <vector>

#include "jrpgmaker/plugin/plugin.hpp"
#include "jrpgmaker/project/workspace.hpp"

namespace jrpgmaker::editor {

using EditorPluginFactoryBinding = plugin::CompiledPluginFactory;

struct EditorPluginRegistryAssembly {
    std::shared_ptr<const plugin::PluginRegistry> registry;
    std::vector<project::Diagnostic> diagnostics;
    explicit operator bool() const { return registry != nullptr && diagnostics.empty(); }
};

[[nodiscard]] EditorPluginRegistryAssembly
AssembleEditorPluginRegistry(const std::filesystem::path& project_root,
                             std::span<const EditorPluginFactoryBinding> compiled_factories);

} // namespace jrpgmaker::editor

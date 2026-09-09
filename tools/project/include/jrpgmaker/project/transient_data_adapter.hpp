#pragma once

#include <filesystem>
#include <optional>
#include <span>
#include <vector>

#include <nlohmann/json.hpp>

#include "jrpgmaker/project/workspace.hpp"

namespace jrpgmaker::project {

struct TransientDataAdapterResult {
    std::optional<DocumentAdapter> adapter;
    std::vector<Diagnostic> diagnostics;

    explicit operator bool() const { return adapter.has_value() && diagnostics.empty(); }
};

[[nodiscard]] TransientDataAdapterResult
CreateTransientDataAdapter(const std::filesystem::path& project_root,
                           const std::filesystem::path& relative_path,
                           const nlohmann::json& object_patch,
                           std::span<const std::filesystem::path> plugin_data_roots = {});

} // namespace jrpgmaker::project

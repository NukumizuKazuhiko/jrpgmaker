#include "jrpgmaker/project/transient_data_adapter.hpp"

#include <algorithm>
#include <fstream>
#include <string>

#include "jrpgmaker/core/calendar.hpp"
#include "jrpgmaker/core/input_actions.hpp"
#include "jrpgmaker/core/map_data.hpp"
#include "jrpgmaker/domain/event_script.hpp"
#include "jrpgmaker/domain/interaction.hpp"
#include "jrpgmaker/domain/localization.hpp"
#include "jrpgmaker/domain/schedule.hpp"
#include "jrpgmaker/domain/vertical_slice.hpp"

namespace {

bool IsSafeRelativePath(const std::filesystem::path& path) {
    if (path.empty() || path.is_absolute() || path.has_root_name())
        return false;
    for (const auto& part : path) {
        if (part == "..")
            return false;
    }
    return true;
}

bool IsDeclaredPluginDataPath(const std::filesystem::path& path,
                              std::span<const std::filesystem::path> roots) {
    const auto normalized_path = path.lexically_normal();
    return std::any_of(roots.begin(), roots.end(), [&normalized_path](const auto& root) {
        if (!IsSafeRelativePath(root))
            return false;
        const auto normalized_root = root.lexically_normal();
        auto path_part = normalized_path.begin();
        for (auto root_part = normalized_root.begin(); root_part != normalized_root.end();
             ++root_part, ++path_part) {
            if (path_part == normalized_path.end() || *path_part != *root_part)
                return false;
        }
        return path_part != normalized_path.end();
    });
}

std::string EscapeJsonPointerToken(std::string_view token) {
    std::string escaped;
    escaped.reserve(token.size());
    for (const char character : token) {
        if (character == '~')
            escaped += "~0";
        else if (character == '/')
            escaped += "~1";
        else
            escaped += character;
    }
    return escaped;
}

template <typename Validate>
jrpgmaker::project::DocumentValidator BuildValidator(std::string diagnostic_path,
                                                     Validate validate) {
    return [diagnostic_path = std::move(diagnostic_path),
            validate = std::move(validate)](const nlohmann::json& document) {
        try {
            if (validate(document))
                return std::vector<jrpgmaker::project::Diagnostic>{};
        } catch (const std::exception&) {
        }
        return std::vector<jrpgmaker::project::Diagnostic>{
            {"project.transient_data.invalid", diagnostic_path}};
    };
}

} // namespace

namespace jrpgmaker::project {

TransientDataAdapterResult CreateTransientDataAdapter(
    const std::filesystem::path& project_root, const std::filesystem::path& relative_path,
    const nlohmann::json& object_patch, std::span<const std::filesystem::path> plugin_data_roots) {
    const auto diagnostic = [&relative_path](std::string code) {
        return TransientDataAdapterResult{
            .adapter = std::nullopt,
            .diagnostics = {{std::move(code), relative_path.generic_string()}}};
    };
    if (!IsSafeRelativePath(relative_path))
        return diagnostic("project.transient_data.unsafe_path");
    if (!object_patch.is_object() || object_patch.empty() ||
        object_patch.size() > ProjectWorkspace::kMaxObjectPatchKeys)
        return diagnostic("project.transient_data.invalid_patch");
    DocumentAdapter adapter;
    adapter.type_id = "project.transient_data." + relative_path.filename().string();
    adapter.fields.reserve(object_patch.size());
    for (auto it = object_patch.begin(); it != object_patch.end(); ++it) {
        adapter.fields.push_back({.path = "/" + EscapeJsonPointerToken(it.key()),
                                  .value_type = "json",
                                  .label_key = {},
                                  .recipe = {},
                                  .required = true,
                                  .read_only = false,
                                  .choices = {}});
    }
    const auto diagnostic_path = relative_path.generic_string();
    const auto name = relative_path.filename().string();
    if (IsDeclaredPluginDataPath(relative_path, plugin_data_roots)) {
        adapter.validate = BuildValidator(
            diagnostic_path, [](const auto& document) { return document.is_object(); });
        return {.adapter = std::move(adapter), .diagnostics = {}};
    }
    if (name == "events_demo.json") {
        adapter.validate = BuildValidator(diagnostic_path, [](const auto& document) {
            (void) domain::ParseEventScript(document);
            return true;
        });
    } else if (name == "navigation_demo.json") {
        adapter.validate = BuildValidator(diagnostic_path, [](const auto& document) {
            (void) core::ParseNavigationGrid(document);
            return true;
        });
    } else if (name == "collision_demo.json") {
        adapter.validate = BuildValidator(diagnostic_path, [](const auto& document) {
            (void) core::ParseCollisionAabbs(document);
            return true;
        });
    } else if (name == "camera_demo.json") {
        adapter.validate = BuildValidator(diagnostic_path, [](const auto& document) {
            (void) core::ParseCameraRigData(document);
            return true;
        });
    } else if (name == "interaction_demo.json") {
        adapter.validate = BuildValidator(diagnostic_path, [](const auto& document) {
            (void) domain::ParseInteractionPoints(document);
            return true;
        });
    } else if (name == "input_actions.json") {
        adapter.validate = BuildValidator(diagnostic_path, [](const auto& document) {
            return static_cast<bool>(core::ParseInputActionMap(document));
        });
    } else if (name == "calendar_demo.json") {
        adapter.validate = BuildValidator(diagnostic_path, [](const auto& document) {
            return core::ParseCalendarDefinition(document).ok;
        });
    } else if (name == "localization_en.json") {
        adapter.validate = BuildValidator(diagnostic_path, [](const auto& document) {
            return static_cast<bool>(domain::ParseLocalizationTable(document));
        });
    } else if (name == "material_demo.json" || name == "material_accent.json") {
        adapter.validate = BuildValidator(diagnostic_path, [](const auto& document) {
            return document.is_object() && document.value("schema", 0) == 1 &&
                   document.contains("parameters");
        });
    } else if (name == "schedule_demo.json") {
        nlohmann::json calendar_document;
        try {
            std::ifstream input(project_root / "assets/data/calendar_demo.json");
            if (!input || !(input >> calendar_document))
                return diagnostic("project.transient_data.invalid");
        } catch (const std::exception&) {
            return diagnostic("project.transient_data.invalid");
        }
        const auto calendar = core::ParseCalendarDefinition(calendar_document);
        if (!calendar.ok)
            return diagnostic("project.transient_data.invalid");
        adapter.validate =
            BuildValidator(diagnostic_path, [calendar = calendar.calendar](const auto& document) {
                (void) domain::ParseScheduleTable(document, calendar);
                return true;
            });
    } else if (name == "vertical_slice_demo.json") {
        adapter.validate = BuildValidator(diagnostic_path, [](const auto& document) {
            (void) domain::ParseVerticalSliceDefinition(document);
            return true;
        });
    } else {
        return diagnostic("project.transient_data.unknown_file");
    }
    return {.adapter = std::move(adapter), .diagnostics = {}};
}

} // namespace jrpgmaker::project

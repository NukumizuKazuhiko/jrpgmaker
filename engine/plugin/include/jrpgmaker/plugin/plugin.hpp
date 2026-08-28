#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include <nlohmann/json.hpp>

namespace jrpgmaker::plugin {

enum class PluginType : std::uint8_t { kBattle, kRenderStyle };

// This is the source-level contract version consumed by the current runtime.
// A plugin must be rebuilt and revalidated when this value changes.
inline constexpr std::uint32_t kPluginEngineContract = 1;

struct PluginError {
    std::string code;
    std::string message;
    std::string path;
};

struct PluginManifest {
    std::uint32_t schema = 1;
    std::string id;
    PluginType type = PluginType::kBattle;
    std::uint32_t version = 1;
    std::uint32_t engine_contract = kPluginEngineContract;
    std::vector<std::string> data_roots;
    std::vector<std::string> capabilities;
};

struct ManifestParseResult {
    std::optional<PluginManifest> manifest;
    std::optional<PluginError> error;
    explicit operator bool() const { return manifest.has_value(); }
};

ManifestParseResult ParseManifest(const nlohmann::json& document);

[[nodiscard]] std::optional<PluginError> ValidatePluginManifest(const PluginManifest& manifest);

struct ProjectManifest {
    std::uint32_t schema = 1;
    std::string id;
    std::string render_style;
    std::string battle_plugin;
    std::vector<std::string> plugins;
    std::vector<std::string> data_roots;
    std::string material_document = "assets/data/material_demo.json";
    std::string input_actions = "assets/data/input_actions.json";
    std::string event_script = "assets/data/events_demo.json";
    std::string localization = "assets/data/localization_en.json";
    std::string resource_manifest = "assets/data/resources_demo.json";
};

struct ProjectManifestParseResult {
    std::optional<ProjectManifest> manifest;
    std::optional<PluginError> error;
    explicit operator bool() const { return manifest.has_value(); }
};

ProjectManifestParseResult ParseProjectManifest(const nlohmann::json& document);
[[nodiscard]] std::optional<PluginError>
ValidateProjectPlugins(const ProjectManifest& project, const class PluginRegistry& registry);
[[nodiscard]] std::optional<PluginError>
ValidateProjectDataRoots(const ProjectManifest& project, const std::filesystem::path& project_root);
[[nodiscard]] std::vector<PluginError>
ValidateProjectPluginData(const ProjectManifest& project, const class PluginRegistry& registry,
                          const std::filesystem::path& project_root);

struct PluginDataReadResult {
    std::vector<std::byte> bytes;
    std::optional<PluginError> error;
    explicit operator bool() const { return !error.has_value(); }
};

inline constexpr std::size_t kMaxPluginValidationFiles = 32;
inline constexpr std::size_t kMaxPluginValidationFileBytes = 256 * 1024;
inline constexpr std::size_t kMaxPluginValidationTotalBytes = 1024 * 1024;

struct PluginValidationContext {
    const PluginManifest& manifest;
    std::function<PluginDataReadResult(std::string_view relative_path)> read_file;
    std::size_t max_files = kMaxPluginValidationFiles;
    std::size_t max_total_bytes = kMaxPluginValidationTotalBytes;
};

struct PluginValidationResult {
    std::vector<PluginError> issues;
    explicit operator bool() const { return issues.empty(); }
};

class IPlugin {
public:
    virtual ~IPlugin() = default;
    IPlugin(const IPlugin&) = delete;
    IPlugin& operator=(const IPlugin&) = delete;

    [[nodiscard]] virtual PluginValidationResult
    ValidateData(const PluginValidationContext&) const {
        return {};
    }

protected:
    IPlugin() = default;
};

struct PluginCreateResult {
    std::unique_ptr<IPlugin> instance;
    std::optional<PluginError> error;
    explicit operator bool() const { return instance != nullptr; }
};

class PluginRegistry {
public:
    using Factory = std::function<std::unique_ptr<IPlugin>()>;
    static constexpr std::size_t kMaxPlugins = 32;

    [[nodiscard]] std::optional<PluginError> Register(PluginManifest manifest, Factory factory);
    [[nodiscard]] PluginCreateResult Create(const std::string& id, PluginType type) const;
    [[nodiscard]] std::optional<PluginManifest> FindManifest(const std::string& id) const;
    [[nodiscard]] std::size_t size() const { return entries_.size(); }

private:
    struct Entry {
        PluginManifest manifest;
        Factory factory;
    };
    std::vector<Entry> entries_;
};

[[nodiscard]] PluginCreateResult CreateProjectRenderStyle(const ProjectManifest& project,
                                                          const PluginRegistry& registry);
[[nodiscard]] PluginCreateResult CreateProjectBattlePlugin(const ProjectManifest& project,
                                                           const PluginRegistry& registry);

} // namespace jrpgmaker::plugin

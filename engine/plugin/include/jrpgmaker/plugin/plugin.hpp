#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

namespace jrpgmaker::plugin {

enum class PluginType : std::uint8_t { kBattle, kRenderStyle };

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
    std::uint32_t engine_contract = 1;
    std::vector<std::string> data_roots;
    std::vector<std::string> capabilities;
};

struct ManifestParseResult {
    std::optional<PluginManifest> manifest;
    std::optional<PluginError> error;
    explicit operator bool() const { return manifest.has_value(); }
};

ManifestParseResult ParseManifest(const nlohmann::json& document);

struct ProjectManifest {
    std::uint32_t schema = 1;
    std::string id;
    std::string render_style;
    std::string battle_plugin;
    std::vector<std::string> plugins;
    std::vector<std::string> data_roots;
    std::string material_document = "assets/data/material_demo.json";
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

class IPlugin {
public:
    virtual ~IPlugin() = default;
    IPlugin(const IPlugin&) = delete;
    IPlugin& operator=(const IPlugin&) = delete;

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

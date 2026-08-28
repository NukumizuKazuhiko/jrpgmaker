#pragma once

#include <cstddef>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "jrpgmaker/plugin/plugin.hpp"

namespace jrpgmaker::plugin {

inline constexpr std::size_t kMaxBattleActionsPerFrame = 32;
inline constexpr std::size_t kMaxBattlePayloadBytes = 64 * 1024;
inline constexpr std::size_t kMaxBattlePresentationCommands = 128;

struct BattleLaunchContext {
    std::string encounter_id;
    std::vector<std::byte> opaque_payload;
};

struct BattleFrameInput {
    double delta_seconds = 0.0;
    std::vector<std::string> action_ids;
    std::vector<std::byte> opaque_payload;
    bool cancel_requested = false;
};

struct PresentationCommand {
    std::string id;
    std::vector<std::byte> payload;
};

struct BattleFrameOutput {
    bool finished = false;
    std::string result_key;
    std::vector<PresentationCommand> presentation_commands;
    std::vector<std::byte> opaque_payload;
};

struct BattleSnapshot {
    bool active = true;
    bool finished = false;
    std::vector<std::byte> opaque_payload;
};

struct BattleSessionCreateResult {
    std::unique_ptr<class IBattleSession> session;
    std::optional<PluginError> error;
    explicit operator bool() const { return session != nullptr; }
};

struct BattleAdvanceResult {
    bool ok = true;
    BattleFrameOutput output;
    std::optional<PluginError> error;
    explicit operator bool() const { return ok; }
};

[[nodiscard]] std::optional<PluginError> ValidateBattleLaunch(const BattleLaunchContext& context);
[[nodiscard]] std::optional<PluginError> ValidateBattleInput(const BattleFrameInput& input);
[[nodiscard]] std::optional<PluginError> ValidateBattleOutput(const BattleFrameOutput& output);
[[nodiscard]] std::optional<PluginError> ValidateBattleSnapshot(const BattleSnapshot& snapshot);

class IBattleSession {
public:
    virtual ~IBattleSession() = default;
    IBattleSession(const IBattleSession&) = delete;
    IBattleSession& operator=(const IBattleSession&) = delete;

    [[nodiscard]] virtual BattleAdvanceResult Advance(const BattleFrameInput& input) = 0;
    [[nodiscard]] virtual BattleSnapshot Snapshot() const = 0;

protected:
    IBattleSession() = default;
};

class IBattlePlugin : public IPlugin {
public:
    ~IBattlePlugin() override = default;
    [[nodiscard]] virtual BattleSessionCreateResult
    CreateSession(const BattleLaunchContext& context) = 0;

protected:
    IBattlePlugin() = default;
};

} // namespace jrpgmaker::plugin

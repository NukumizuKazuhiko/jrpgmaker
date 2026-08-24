#pragma once

#include <cstddef>
#include <functional>
#include <vector>

namespace jrpgmaker::core {

// Explicit stage sequence (P1 contract): the main loop advances each stage in
// order every tick. Systems declare their owning stage and within-stage order
// via registration; cross-stage ordering is this enum's order.
enum class Stage {
    kInput,
    kDomainSim,
    kAnimation,
    kPresentationSync,
    kRenderSubmit,
};

constexpr std::size_t kStageCount = 5;

// System registration: every system must declare its owning stage and its
// within-stage ordering.
struct SystemRegistration {
    Stage stage;
    // Within a stage, systems run in ascending order. v0 uses fixed ordering;
    // an explicit before/after graph lands when the Stage contract matures.
    std::size_t order = 0;
};

// System callback signature: receives the tick delta in seconds.
using SystemCallback = std::function<void(double)>;

class StageRunner {
public:
    explicit StageRunner(std::size_t stage_count = kStageCount);

    StageRunner(const StageRunner&) = delete;
    StageRunner& operator=(const StageRunner&) = delete;

    // Register a system: owning stage, order, and callback. Duplicate
    // stage+order is rejected.
    void RegisterSystem(Stage stage, SystemRegistration registration, SystemCallback callback);

    // Advance all stages in sequence. v0 stages are empty placeholders:
    // callbacks run in ascending order within each stage.
    void Tick(double delta_seconds);

private:
    struct Entry {
        SystemRegistration registration;
        SystemCallback callback = nullptr;
    };

    std::vector<std::vector<Entry>> entries_;
};

} // namespace jrpgmaker::core
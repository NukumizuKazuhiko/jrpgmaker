#include "jrpgmaker/core/stage.hpp"

#include <algorithm>
#include <stdexcept>

namespace jrpgmaker::core {

StageRunner::StageRunner(std::size_t stage_count) : entries_(stage_count) {}

void StageRunner::RegisterSystem(Stage stage, SystemRegistration registration,
                                 SystemCallback callback) {
    auto& stage_entries = entries_[static_cast<std::size_t>(stage)];
    for (const Entry& entry : stage_entries) {
        if (entry.registration.order == registration.order) {
            throw std::runtime_error("core: duplicate system order within a stage");
        }
    }
    stage_entries.push_back({registration, callback});
}

void StageRunner::Tick(double delta_seconds) {
    for (auto& stage_entries : entries_) {
        std::sort(stage_entries.begin(), stage_entries.end(),
                  [](const Entry& left, const Entry& right) {
                      return left.registration.order < right.registration.order;
                  });
        for (Entry& entry : stage_entries) {
            if (entry.callback != nullptr) {
                entry.callback(delta_seconds);
            }
        }
    }
}

} // namespace jrpgmaker::core
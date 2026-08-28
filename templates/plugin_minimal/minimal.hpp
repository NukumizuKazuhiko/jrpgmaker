#pragma once

#include <memory>

#include "jrpgmaker/plugin/plugin.hpp"

namespace vendor::minimal {

class Plugin final : public jrpgmaker::plugin::IPlugin {};

[[nodiscard]] inline std::unique_ptr<jrpgmaker::plugin::IPlugin> Create() {
    return std::make_unique<Plugin>();
}

} // namespace vendor::minimal

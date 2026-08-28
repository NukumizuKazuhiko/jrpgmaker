#include <cassert>

#include "jrpgmaker/plugin/plugin.hpp"

int main() {
    const auto result = jrpgmaker::plugin::ParseManifest({
        {"schema", 1},
        {"id", "fixture.consumer"},
        {"type", "render_style"},
        {"version", 1},
        {"engine_contract", jrpgmaker::plugin::kPluginEngineContract},
        {"data_roots", nlohmann::json::array()},
        {"capabilities", nlohmann::json::array()},
    });
    assert(result);
    return 0;
}

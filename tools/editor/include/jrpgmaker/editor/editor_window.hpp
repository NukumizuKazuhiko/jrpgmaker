#pragma once

#include <cstdint>

namespace jrpgmaker::editor {

// Returns the SDL window flags required by the editor host. The high-density
// back buffer keeps the RmlUi surface in the same pixel space as the swapchain.
[[nodiscard]] std::uint64_t EditorWindowFlags(bool use_vulkan_surface);

} // namespace jrpgmaker::editor

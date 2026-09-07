#include "jrpgmaker/editor/editor_window.hpp"

#include <SDL3/SDL.h>

namespace jrpgmaker::editor {

std::uint64_t EditorWindowFlags(bool use_vulkan_surface) {
    auto flags = static_cast<std::uint64_t>(SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIGH_PIXEL_DENSITY);
#if !defined(_WIN32)
    if (use_vulkan_surface)
        flags |= SDL_WINDOW_VULKAN;
#else
    (void) use_vulkan_surface;
#endif
    return flags;
}

} // namespace jrpgmaker::editor

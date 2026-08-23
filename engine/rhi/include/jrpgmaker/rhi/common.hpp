#pragma once

#include <cstdint>

namespace jrpgmaker::rhi {

enum class Backend {
    kD3D12,
    kVulkan,
};

enum class Format {
    kB8G8R8A8Unorm,
    kR8G8B8A8Unorm,
};

enum class TextureUsage : std::uint32_t {
    kNone = 0u,
    kRenderTarget = 1u << 0,
    kSampled = 1u << 1,
    kReadBack = 1u << 2,
};

constexpr TextureUsage operator|(TextureUsage a, TextureUsage b) {
    return static_cast<TextureUsage>(static_cast<std::uint32_t>(a) | static_cast<std::uint32_t>(b));
}

constexpr TextureUsage operator&(TextureUsage a, TextureUsage b) {
    return static_cast<TextureUsage>(static_cast<std::uint32_t>(a) & static_cast<std::uint32_t>(b));
}

} // namespace jrpgmaker::rhi

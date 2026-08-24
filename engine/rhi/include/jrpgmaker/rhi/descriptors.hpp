#pragma once

#include <cstddef>
#include <cstdint>

#include "jrpgmaker/rhi/common.hpp"

namespace jrpgmaker::rhi {

struct ShaderBytecode {
    const void* data;
    std::size_t size;
};

struct TextureDesc {
    std::uint32_t width;
    std::uint32_t height;
    Format format;
    // At least one usage bit is required; kNone alone is rejected by both
    // backends (D3D12 and Vulkan) so the contract is backend-independent.
    TextureUsage usage;
};

struct ClearColor {
    float r;
    float g;
    float b;
    float a;
};

struct GraphicsPipelineDesc {
    ShaderBytecode vertex_shader;
    ShaderBytecode pixel_shader;
    Format color_format;
};

} // namespace jrpgmaker::rhi

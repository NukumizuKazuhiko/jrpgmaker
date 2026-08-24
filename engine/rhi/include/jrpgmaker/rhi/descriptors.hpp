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

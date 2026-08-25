#pragma once

#include <cstddef>
#include <cstdint>

#include "jrpgmaker/rhi/common.hpp"

namespace jrpgmaker::rhi {

struct ShaderBytecode {
    const void* data;
    std::size_t size;
};

struct BufferDesc {
    std::uint64_t size_bytes;
    // At least one usage bit is required; kNone alone is rejected by both
    // backends (D3D12 and Vulkan) so the contract is backend-independent.
    BufferUsage usage;
};

// Vertex attribute declaration bound to a shader input location. v0 supports
// interleaved float3 positions (the layout's stride covers one vertex). The
// attribute format maps to both D3D12 input elements and Vulkan vertex input
// attributes so a single layout drives both backends.
enum class VertexAttributeFormat : std::uint8_t {
    kFloat3,
    kFloat2,
};

struct VertexAttribute {
    std::uint32_t location;
    VertexAttributeFormat format;
    std::uint32_t offset_bytes;
    // D3D12 input-element semantic name (Vulkan matches purely by `location`,
    // so this is only consumed by the D3D12 backend). Defaults to "POSITION",
    // preserving the single-attribute call sites; multi-attribute layouts pass
    // e.g. "TEXCOORD" for the texture-coordinate attribute. The semantic index
    // is fixed at 0; the HLSL input struct must declare attributes in ascending
    // `location` order to keep the D3D12 and Vulkan bindings aligned.
    const char* semantic_name = "POSITION";
};

struct VertexInputLayout {
    // nullptr means "no vertex input" (geometry generated in the shader, P1
    // triangle baseline). Non-null requires attribute_count > 0 and a stride.
    const VertexAttribute* attributes;
    std::uint32_t attribute_count;
    std::uint32_t stride_bytes;
};

struct TextureDesc {
    std::uint32_t width;
    std::uint32_t height;
    Format format;
    // At least one usage bit is required; kNone alone is rejected by both
    // backends (D3D12 and Vulkan) so the contract is backend-independent.
    TextureUsage usage;
};

struct SamplerDesc {
    SamplerFilter filter = SamplerFilter::kNearest;
    SamplerAddress address = SamplerAddress::kClamp;
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
    // Vertex input binding. Default-initialized (attributes == nullptr) keeps
    // the P1 geometry-from-shader behavior; passing a layout enables vertex
    // buffers (P2, glTF mesh rendering).
    VertexInputLayout vertex_input{};
    // Size of the push-constant block bound to the vertex shader (v0: a single
    // 64-byte view-proj matrix). Zero means the pipeline declares no constants.
    std::uint32_t push_constants_size = 0;
    // 1 = the pixel shader samples a texture bound via ICommandList::
    // SetSampledTexture (slot 0). Zero (default) keeps the pipeline sampling-free
    // (existing behavior; no descriptor binding is required before drawing).
    std::uint32_t sample_slot = 0;
};

} // namespace jrpgmaker::rhi

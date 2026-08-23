#include <cstddef>
#include <cstdint>

#include <catch2/catch_test_macros.hpp>

#include "jrpgmaker/rhi/command_list.hpp"
#include "jrpgmaker/rhi/common.hpp"
#include "jrpgmaker/rhi/descriptors.hpp"
#include "jrpgmaker/rhi/device.hpp"
#include "jrpgmaker/rhi/device_factory.hpp"
#include "jrpgmaker/rhi/handles.hpp"
#include "jrpgmaker/rhi/swapchain.hpp"

namespace {

using jrpgmaker::rhi::BufferHandle;
using jrpgmaker::rhi::ClearColor;
using jrpgmaker::rhi::Format;
using jrpgmaker::rhi::GraphicsPipelineDesc;
using jrpgmaker::rhi::PipelineHandle;
using jrpgmaker::rhi::ShaderBytecode;
using jrpgmaker::rhi::TextureDesc;
using jrpgmaker::rhi::TextureHandle;
using jrpgmaker::rhi::TextureUsage;

TEST_CASE("rhi handles are strongly typed and invalid by default", "[rhi][contract]") {
    constexpr BufferHandle buffer{};
    constexpr TextureHandle texture{};
    constexpr PipelineHandle pipeline{};

    REQUIRE(buffer == BufferHandle::kInvalid);
    REQUIRE(texture == TextureHandle::kInvalid);
    REQUIRE(pipeline == PipelineHandle::kInvalid);
}

TEST_CASE("rhi texture usage flags compose", "[rhi][contract]") {
    const auto combined = TextureUsage::kRenderTarget | TextureUsage::kReadBack;
    REQUIRE((combined & TextureUsage::kRenderTarget) == TextureUsage::kRenderTarget);
    REQUIRE((combined & TextureUsage::kSampled) == TextureUsage::kNone);
}

TEST_CASE("rhi descriptor aggregates are trivially constructible", "[rhi][contract]") {
    static const std::byte vertex_bytes[1] = {};
    static const std::byte pixel_bytes[1] = {};

    const GraphicsPipelineDesc desc{
        .vertex_shader = ShaderBytecode{vertex_bytes, sizeof(vertex_bytes)},
        .pixel_shader = ShaderBytecode{pixel_bytes, sizeof(pixel_bytes)},
        .color_format = Format::kB8G8R8A8Unorm,
    };
    const TextureDesc texture_desc{
        .width = 64,
        .height = 64,
        .format = Format::kB8G8R8A8Unorm,
        .usage = TextureUsage::kRenderTarget | TextureUsage::kReadBack,
    };
    const ClearColor clear{0.0F, 0.0F, 0.0F, 1.0F};

    REQUIRE(desc.vertex_shader.size == sizeof(vertex_bytes));
    REQUIRE(texture_desc.width == 64);
    REQUIRE(clear.a == 1.0F);
}

} // namespace

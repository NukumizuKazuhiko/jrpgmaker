#include <catch2/catch_test_macros.hpp>

#include "jrpgmaker/rhi/command_list.hpp"
#include "jrpgmaker/rhi/device.hpp"
#include "jrpgmaker/rhi/device_factory.hpp"
#include "jrpgmaker/rhi/handles.hpp"

namespace {

using namespace jrpgmaker::rhi;

TEST_CASE("d3d12 backend creates a device on hardware or warp", "[rhi][d3d12]") {
    const std::unique_ptr<IDevice> device = CreateDevice(Backend::kD3D12);
    REQUIRE(device != nullptr);
    device->WaitForGpuIdle();
}

TEST_CASE("d3d12 backend submits an empty command stream", "[rhi][d3d12]") {
    const std::unique_ptr<IDevice> device = CreateDevice(Backend::kD3D12);
    REQUIRE(device != nullptr);

    ICommandList* command_list = device->CreateCommandList();
    REQUIRE(command_list != nullptr);

    command_list->Begin();
    command_list->End();
    device->Submit(*command_list);
    device->WaitForGpuIdle();
    device->DestroyCommandList(command_list);
}

TEST_CASE("d3d12 backend unimplemented resource surface returns invalid handles", "[rhi][d3d12]") {
    const std::unique_ptr<IDevice> device = CreateDevice(Backend::kD3D12);
    REQUIRE(device != nullptr);

    CHECK(device->CreateBuffer(BufferDesc{.size_bytes = 16}) == BufferHandle::kInvalid);
    CHECK(device->CreateTexture(
              TextureDesc{.width = 4,
                          .height = 4,
                          .format = Format::kB8G8R8A8Unorm,
                          .usage = TextureUsage::kRenderTarget | TextureUsage::kReadBack}) ==
          TextureHandle::kInvalid);
    CHECK(device->CreatePipeline(GraphicsPipelineDesc{
              .vertex_shader = ShaderBytecode{nullptr, 0},
              .pixel_shader = ShaderBytecode{nullptr, 0},
              .color_format = Format::kB8G8R8A8Unorm,
          }) == PipelineHandle::kInvalid);
    CHECK(device->CreateSwapchain(nullptr, 1, 1, Format::kB8G8R8A8Unorm) == nullptr);
    CHECK(device->MapReadBack(TextureHandle::kInvalid) == nullptr);
}

} // namespace

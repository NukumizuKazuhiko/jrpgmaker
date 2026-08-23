#include <catch2/catch_test_macros.hpp>

#include "jrpgmaker/rhi/command_list.hpp"
#include "jrpgmaker/rhi/device.hpp"
#include "jrpgmaker/rhi/device_factory.hpp"
#include "jrpgmaker/rhi/handles.hpp"

namespace {

using namespace jrpgmaker::rhi;

TEST_CASE("vulkan backend creates a device on any physical device", "[rhi][vulkan]") {
    const std::unique_ptr<IDevice> device = CreateDevice(Backend::kVulkan);
    REQUIRE(device != nullptr);
    device->WaitForGpuIdle();
}

TEST_CASE("vulkan backend submits an empty command stream", "[rhi][vulkan]") {
    const std::unique_ptr<IDevice> device = CreateDevice(Backend::kVulkan);
    REQUIRE(device != nullptr);

    ICommandList* command_list = device->CreateCommandList();
    REQUIRE(command_list != nullptr);

    command_list->Begin();
    command_list->End();
    device->Submit(*command_list);
    device->WaitForGpuIdle();
    device->DestroyCommandList(command_list);
}

TEST_CASE("vulkan backend survives repeated submit and wait cycles", "[rhi][vulkan]") {
    const std::unique_ptr<IDevice> device = CreateDevice(Backend::kVulkan);
    REQUIRE(device != nullptr);

    for (int cycle = 0; cycle < 4; ++cycle) {
        ICommandList* command_list = device->CreateCommandList();
        REQUIRE(command_list != nullptr);
        command_list->Begin();
        command_list->End();
        device->Submit(*command_list);
        device->WaitForGpuIdle();
        device->DestroyCommandList(command_list);
    }
}

TEST_CASE("vulkan backend unimplemented resource surface returns invalid handles",
          "[rhi][vulkan]") {
    const std::unique_ptr<IDevice> device = CreateDevice(Backend::kVulkan);
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

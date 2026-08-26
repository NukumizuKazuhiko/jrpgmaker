#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers.hpp>

#include <memory>
#include <stdexcept>

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
    ICommandList* command_list = device->CreateCommandList();
    REQUIRE(command_list != nullptr);

    for (int cycle = 0; cycle < 4; ++cycle) {
        command_list->Begin();
        command_list->End();
        device->Submit(*command_list);
        device->WaitForGpuIdle();
    }
    device->DestroyCommandList(command_list);
}

TEST_CASE("vulkan backend rejects draws without a bound pipeline", "[rhi][vulkan][draw-contract]") {
    const std::unique_ptr<IDevice> device = CreateDevice(Backend::kVulkan);
    REQUIRE(device != nullptr);

    ICommandList* command_list = device->CreateCommandList();
    REQUIRE(command_list != nullptr);
    command_list->Begin();

    REQUIRE_THROWS_WITH(command_list->Draw(3, 1), "vulkan backend: draw requires a bound pipeline");
    REQUIRE_THROWS_WITH(command_list->DrawIndexed(3, 1),
                        "vulkan backend: indexed draw requires a bound pipeline");

    command_list->End();
    device->DestroyCommandList(command_list);
}

TEST_CASE("vulkan backend rejects buffer usage mismatches", "[rhi][vulkan][buffer-contract]") {
    const std::unique_ptr<IDevice> device = CreateDevice(Backend::kVulkan);
    REQUIRE(device != nullptr);
    const BufferHandle vertex_buffer = device->CreateBuffer({64, BufferUsage::kVertex});
    const BufferHandle index_buffer = device->CreateBuffer({64, BufferUsage::kIndex});
    ICommandList* command_list = device->CreateCommandList();
    REQUIRE(command_list != nullptr);
    command_list->Begin();

    REQUIRE_THROWS_WITH(command_list->SetVertexBuffer(index_buffer, 16),
                        "vulkan backend: vertex binding requires a kVertex buffer");
    REQUIRE_THROWS_WITH(command_list->SetIndexBuffer(vertex_buffer, false),
                        "vulkan backend: index binding requires a kIndex buffer");

    command_list->End();
    device->DestroyCommandList(command_list);
    device->DestroyBuffer(index_buffer);
    device->DestroyBuffer(vertex_buffer);
}

TEST_CASE("vulkan backend rejects invalid swapchain window and pipeline handles", "[rhi][vulkan]") {
    const std::unique_ptr<IDevice> device = CreateDevice(Backend::kVulkan);
    REQUIRE(device != nullptr);

    REQUIRE_THROWS_AS(device->CreatePipeline(GraphicsPipelineDesc{
                          .vertex_shader = ShaderBytecode{nullptr, 0},
                          .pixel_shader = ShaderBytecode{nullptr, 0},
                          .color_format = Format::kB8G8R8A8Unorm,
                      }),
                      std::runtime_error);
    REQUIRE_THROWS_WITH(device->CreatePipeline(GraphicsPipelineDesc{
                            .vertex_shader = ShaderBytecode{nullptr, 0},
                            .pixel_shader = ShaderBytecode{nullptr, 0},
                            .color_format = Format::kB8G8R8A8Unorm,
                            .push_constants_size = 65,
                        }),
                        "vulkan backend: pipeline push constants must be a multiple of 4 bytes and "
                        "at most 64 bytes");
    REQUIRE_THROWS_AS(device->CreateSwapchain(nullptr, 1, 1, Format::kB8G8R8A8Unorm),
                      std::runtime_error);
    REQUIRE_THROWS_AS(device->MapReadBack(TextureHandle::kInvalid), std::runtime_error);
}

} // namespace

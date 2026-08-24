#include <catch2/catch_test_macros.hpp>

#include <memory>
#include <stdexcept>

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

TEST_CASE("d3d12 backend survives repeated submit and wait cycles", "[rhi][d3d12]") {
    const std::unique_ptr<IDevice> device = CreateDevice(Backend::kD3D12);
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

TEST_CASE("d3d12 backend rejects invalid swapchain window and pipeline handles", "[rhi][d3d12]") {
    const std::unique_ptr<IDevice> device = CreateDevice(Backend::kD3D12);
    REQUIRE(device != nullptr);

    REQUIRE_THROWS_AS(device->CreatePipeline(GraphicsPipelineDesc{
                          .vertex_shader = ShaderBytecode{nullptr, 0},
                          .pixel_shader = ShaderBytecode{nullptr, 0},
                          .color_format = Format::kB8G8R8A8Unorm,
                      }),
                      std::runtime_error);
    REQUIRE_THROWS_AS(device->CreateSwapchain(nullptr, 1, 1, Format::kB8G8R8A8Unorm),
                      std::runtime_error);
    REQUIRE_THROWS_AS(device->MapReadBack(TextureHandle::kInvalid), std::runtime_error);
}

} // namespace

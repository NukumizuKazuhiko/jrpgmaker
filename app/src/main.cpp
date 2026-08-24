#include <SDL3/SDL.h>

#include <iostream>
#include <memory>
#include <stdexcept>

#include "jrpgmaker/core/stage.hpp"
#include "jrpgmaker/core/version.hpp"
#include "jrpgmaker/rhi/command_list.hpp"
#include "jrpgmaker/rhi/device.hpp"
#include "jrpgmaker/rhi/device_factory.hpp"
#include "jrpgmaker/rhi/swapchain.hpp"
#include "shaders_generated.hpp"

namespace {

constexpr std::uint32_t kWindowWidth = 800;
constexpr std::uint32_t kWindowHeight = 600;

jrpgmaker::rhi::Backend SelectBackend() {
#if defined(_WIN32)
    return jrpgmaker::rhi::Backend::kD3D12;
#else
    return jrpgmaker::rhi::Backend::kVulkan;
#endif
}

void* NativeWindowHandle(SDL_Window* window) {
#if defined(_WIN32)
    return SDL_GetPointerProperty(SDL_GetWindowProperties(window),
                                  SDL_PROP_WINDOW_WIN32_HWND_POINTER, nullptr);
#else
    return window;
#endif
}

void RunMainLoop(jrpgmaker::rhi::ISwapchain* swapchain, jrpgmaker::core::StageRunner& stages) {
    constexpr double kFixedDelta = 1.0 / 60.0;
    double accumulator = 0.0;
    std::uint64_t last_counter = SDL_GetPerformanceCounter();

    bool running = true;
    while (running) {
        SDL_Event event;
        while (SDL_PollEvent(&event)) {
            // SDL3 delivers the window close button as
            // SDL_EVENT_WINDOW_CLOSE_REQUESTED; SDL_EVENT_QUIT covers explicit
            // quit requests. Both must stop the loop so teardown runs.
            if (event.type == SDL_EVENT_QUIT || event.type == SDL_EVENT_WINDOW_CLOSE_REQUESTED) {
                running = false;
            } else if (event.type == SDL_EVENT_WINDOW_RESIZED) {
                swapchain->Resize(event.window.data1, event.window.data2);
            }
        }
        if (!running) {
            break;
        }

        const std::uint64_t current_counter = SDL_GetPerformanceCounter();
        const double frame_seconds =
            static_cast<double>(current_counter - last_counter) / SDL_GetPerformanceFrequency();
        last_counter = current_counter;
        accumulator += frame_seconds;
        if (accumulator > 0.25) {
            accumulator = 0.25;
        }
        while (accumulator >= kFixedDelta) {
            stages.Tick(kFixedDelta);
            accumulator -= kFixedDelta;
        }
    }
}

} // namespace

auto main() -> int {
    try {
        if (!SDL_Init(SDL_INIT_VIDEO)) {
            throw std::runtime_error(std::string("SDL_Init failed: ") + SDL_GetError());
        }

        SDL_Window* window = SDL_CreateWindow("jrpgmaker", kWindowWidth, kWindowHeight, 0);
        if (window == nullptr) {
            throw std::runtime_error(std::string("SDL_CreateWindow failed: ") + SDL_GetError());
        }

        std::unique_ptr<jrpgmaker::rhi::IDevice> device =
            jrpgmaker::rhi::CreateDevice(SelectBackend());

        std::unique_ptr<jrpgmaker::rhi::ISwapchain> swapchain(
            device->CreateSwapchain(NativeWindowHandle(window), kWindowWidth, kWindowHeight,
                                    jrpgmaker::rhi::Format::kB8G8R8A8Unorm));

        const jrpgmaker::rhi::ShaderBytecode vs = {
#if defined(_WIN32)
            jrpgmaker::shaders::kTriangleVsDxil, jrpgmaker::shaders::kTriangleVsDxil_size
#else
            jrpgmaker::shaders::kTriangleVsSpv, jrpgmaker::shaders::kTriangleVsSpv_size
#endif
        };
        const jrpgmaker::rhi::ShaderBytecode ps = {
#if defined(_WIN32)
            jrpgmaker::shaders::kTrianglePsDxil, jrpgmaker::shaders::kTrianglePsDxil_size
#else
            jrpgmaker::shaders::kTrianglePsSpv, jrpgmaker::shaders::kTrianglePsSpv_size
#endif
        };
        jrpgmaker::rhi::GraphicsPipelineDesc pipeline_desc{};
        pipeline_desc.vertex_shader = vs;
        pipeline_desc.pixel_shader = ps;
        pipeline_desc.color_format = jrpgmaker::rhi::Format::kB8G8R8A8Unorm;
        const jrpgmaker::rhi::VertexAttribute position_attribute{
            .location = 0,
            .format = jrpgmaker::rhi::VertexAttributeFormat::kFloat3,
            .offset_bytes = 0,
        };
        pipeline_desc.vertex_input = jrpgmaker::rhi::VertexInputLayout{
            .attributes = &position_attribute,
            .attribute_count = 1,
            .stride_bytes = 3u * sizeof(float),
        };
        const jrpgmaker::rhi::PipelineHandle pipeline = device->CreatePipeline(pipeline_desc);

        // The demo triangle, now supplied through the RHI vertex-input path
        // (matches shaders/triangle.hlsl).
        const float triangle_vertices[] = {
            -0.5f, -0.5f, 0.0f, //
            0.5f,  -0.5f, 0.0f, //
            0.0f,  0.5f,  0.0f, //
        };
        const jrpgmaker::rhi::BufferHandle vertex_buffer = device->CreateBuffer(
            jrpgmaker::rhi::BufferDesc{.size_bytes = sizeof(triangle_vertices),
                                       .usage = jrpgmaker::rhi::BufferUsage::kVertex});
        device->MapWrite(vertex_buffer, triangle_vertices, sizeof(triangle_vertices));

        jrpgmaker::rhi::ICommandList* command_list = device->CreateCommandList();

        jrpgmaker::core::StageRunner stages;
        // The render submit stage owns the presentation: it acquires the back
        // buffer, records the draw, submits and presents (docs/01 Stage
        // contract: RenderSubmit drives the render submit).
        stages.RegisterSystem(
            jrpgmaker::core::Stage::kRenderSubmit, {jrpgmaker::core::Stage::kRenderSubmit, 0},
            [device = device.get(), swapchain = swapchain.get(), command_list, pipeline,
             vertex_buffer](double) {
                const jrpgmaker::rhi::TextureHandle back_buffer = swapchain->AcquireTexture();
                command_list->Begin();
                command_list->BeginRendering(back_buffer, {0.10f, 0.11f, 0.12f, 1.0f});
                command_list->SetPipeline(pipeline);
                command_list->SetVertexBuffer(vertex_buffer, 3u * sizeof(float));
                command_list->Draw(3, 1);
                command_list->EndRendering();
                command_list->End();
                device->Submit(*command_list);
                swapchain->Present();
            });

        std::cout << "jrpgmaker " << jrpgmaker::core::version() << " running\n";
        RunMainLoop(swapchain.get(), stages);

        // DestroyXxx requires the GPU to be idle (docs/01 lifecycle contract):
        // the last submitted command list and the shared allocator must not be
        // touched while the GPU may still reference them.
        device->WaitForGpuIdle();
        device->DestroyCommandList(command_list);
        device->DestroyBuffer(vertex_buffer);

        // Teardown order matters: destroy the swapchain before the window so
        // the Vulkan surface is destroyed while its window still exists, and
        // destroy the pipeline while the GPU is idle.
        device->DestroyPipeline(pipeline);
        swapchain.reset();
        SDL_DestroyWindow(window);
        SDL_Quit();
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "jrpgmaker fatal: " << error.what() << '\n';
        return 1;
    }
}
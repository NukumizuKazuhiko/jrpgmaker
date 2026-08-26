#include <SDL3/SDL.h>

#include <deque>
#include <fstream>
#include <iostream>
#include <memory>
#include <optional>
#include <stdexcept>
#include <vector>

#include <glm/glm.hpp>
#include <nlohmann/json.hpp>

#include "jrpgmaker/assetimport/asset_import.hpp"
#include "jrpgmaker/core/animation.hpp"
#include "jrpgmaker/core/camera_rig.hpp"
#include "jrpgmaker/core/character_controller.hpp"
#include "jrpgmaker/core/map_data.hpp"
#include "jrpgmaker/core/stage.hpp"
#include "jrpgmaker/core/version.hpp"
#include "jrpgmaker/domain/event_runner.hpp"
#include "jrpgmaker/domain/event_script.hpp"
#include "jrpgmaker/domain/interaction.hpp"
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

struct InputState {
    glm::vec3 movement{0.0f};
    bool confirm_pressed = false;
    bool confirm_requested = false;
    bool forward = false;
    bool backward = false;
    bool left = false;
    bool right = false;

    void UpdateMovement() {
        glm::vec2 axes{static_cast<float>(right) - static_cast<float>(left),
                       static_cast<float>(backward) - static_cast<float>(forward)};
        if (glm::dot(axes, axes) > 1.0f) {
            axes = glm::normalize(axes);
        }
        movement = {axes.x, 0.0f, axes.y};
    }
};

struct SkinnedVertex {
    float position[3];
    std::uint16_t joints[4];
    float weights[4];
};

std::vector<SkinnedVertex> BuildSkinnedVertices(const jrpgmaker::core::MeshData& mesh) {
    if (!mesh.skinned()) {
        throw std::runtime_error("character mesh has no skin attributes");
    }
    std::vector<SkinnedVertex> vertices(mesh.vertex_count());
    for (std::size_t vertex = 0; vertex < mesh.vertex_count(); ++vertex) {
        SkinnedVertex& output = vertices[vertex];
        for (std::uint32_t component = 0; component < 3u; ++component) {
            output.position[component] = mesh.positions[vertex * 3u + component];
        }
        for (std::uint32_t component = 0; component < 4u; ++component) {
            output.joints[component] = mesh.joints[vertex * 4u + component];
            output.weights[component] = mesh.weights[vertex * 4u + component];
        }
    }
    return vertices;
}

void RunMainLoop(jrpgmaker::rhi::ISwapchain* swapchain, jrpgmaker::core::StageRunner& stages,
                 InputState& input) {
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
            } else if (event.type == SDL_EVENT_KEY_DOWN || event.type == SDL_EVENT_KEY_UP) {
                const bool is_down = event.type == SDL_EVENT_KEY_DOWN;
                switch (event.key.key) {
                case SDLK_W:
                    input.forward = is_down;
                    break;
                case SDLK_S:
                    input.backward = is_down;
                    break;
                case SDLK_A:
                    input.left = is_down;
                    break;
                case SDLK_D:
                    input.right = is_down;
                    break;
                case SDLK_E:
                    input.confirm_pressed = is_down;
                    if (is_down && !event.key.repeat) {
                        input.confirm_requested = true;
                    }
                    break;
                default:
                    break;
                }
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

        std::optional<jrpgmaker::assetimport::SceneLoad> character_load =
            jrpgmaker::assetimport::LoadGltfScene("assets/art/meshes/arm_skinned.gltf");
        if (!character_load.has_value() || character_load->skeletons.empty() ||
            character_load->animations.empty()) {
            throw std::runtime_error("failed to load animated character asset");
        }
        auto& character = *character_load;
        const auto character_view =
            character.scene.Registry()
                .view<jrpgmaker::assetimport::MeshRef, jrpgmaker::assetimport::SkinRef>();
        if (character_view.begin() == character_view.end()) {
            throw std::runtime_error("animated character asset has no skinned mesh node");
        }
        const jrpgmaker::core::Entity character_entity = *character_view.begin();
        const auto& character_mesh_ref =
            character_view.get<jrpgmaker::assetimport::MeshRef>(character_entity);
        const auto& character_skin_ref =
            character_view.get<jrpgmaker::assetimport::SkinRef>(character_entity);
        const jrpgmaker::core::MeshData* character_mesh =
            character.assets.FindMesh(character_mesh_ref.handle);
        if (character_mesh == nullptr) {
            throw std::runtime_error("animated character asset mesh reference is invalid");
        }
        const std::vector<SkinnedVertex> character_vertices = BuildSkinnedVertices(*character_mesh);

        const jrpgmaker::rhi::ShaderBytecode vs = {
#if defined(_WIN32)
            jrpgmaker::shaders::kSkinnedVsDxil, jrpgmaker::shaders::kSkinnedVsDxil_size
#else
            jrpgmaker::shaders::kSkinnedVsSpv, jrpgmaker::shaders::kSkinnedVsSpv_size
#endif
        };
        const jrpgmaker::rhi::ShaderBytecode ps = {
#if defined(_WIN32)
            jrpgmaker::shaders::kSkinnedPsDxil, jrpgmaker::shaders::kSkinnedPsDxil_size
#else
            jrpgmaker::shaders::kSkinnedPsSpv, jrpgmaker::shaders::kSkinnedPsSpv_size
#endif
        };
        jrpgmaker::rhi::GraphicsPipelineDesc pipeline_desc{};
        pipeline_desc.vertex_shader = vs;
        pipeline_desc.pixel_shader = ps;
        pipeline_desc.color_format = jrpgmaker::rhi::Format::kB8G8R8A8Unorm;
        const jrpgmaker::rhi::VertexAttribute skinned_attributes[] = {
            {.location = 0,
             .format = jrpgmaker::rhi::VertexAttributeFormat::kFloat3,
             .offset_bytes = 0,
             .semantic_name = "POSITION"},
            {.location = 1,
             .format = jrpgmaker::rhi::VertexAttributeFormat::kUint16x4,
             .offset_bytes = 12,
             .semantic_name = "JOINTS"},
            {.location = 2,
             .format = jrpgmaker::rhi::VertexAttributeFormat::kFloat4,
             .offset_bytes = 20,
             .semantic_name = "WEIGHTS"},
        };
        pipeline_desc.vertex_input = jrpgmaker::rhi::VertexInputLayout{
            .attributes = skinned_attributes,
            .attribute_count = static_cast<std::uint32_t>(std::size(skinned_attributes)),
            .stride_bytes = static_cast<std::uint32_t>(sizeof(SkinnedVertex)),
        };
        pipeline_desc.vertex_uniform_size = 32u * 16u * sizeof(float);
        const jrpgmaker::rhi::PipelineHandle pipeline = device->CreatePipeline(pipeline_desc);

        const jrpgmaker::rhi::BufferHandle vertex_buffer =
            device->CreateBuffer(jrpgmaker::rhi::BufferDesc{
                .size_bytes = character_vertices.size() * sizeof(SkinnedVertex),
                .usage = jrpgmaker::rhi::BufferUsage::kVertex});
        device->MapWrite(vertex_buffer, character_vertices.data(),
                         character_vertices.size() * sizeof(SkinnedVertex));
        const jrpgmaker::rhi::BufferHandle index_buffer =
            device->CreateBuffer(jrpgmaker::rhi::BufferDesc{
                .size_bytes = character_mesh->indices.size() * sizeof(std::uint32_t),
                .usage = jrpgmaker::rhi::BufferUsage::kIndex});
        device->MapWrite(index_buffer, character_mesh->indices.data(),
                         character_mesh->indices.size() * sizeof(std::uint32_t));
        const jrpgmaker::rhi::BufferHandle uniform_buffer = device->CreateBuffer(
            jrpgmaker::rhi::BufferDesc{.size_bytes = 32u * sizeof(glm::mat4),
                                       .usage = jrpgmaker::rhi::BufferUsage::kUniform});

        jrpgmaker::rhi::ICommandList* command_list = device->CreateCommandList();

        jrpgmaker::core::StageRunner stages;
        float animation_time = 0.0f;
        std::vector<jrpgmaker::core::Aabb> obstacles;
        {
            std::ifstream file("assets/data/collision_demo.json");
            if (!file) {
                throw std::runtime_error("failed to open assets/data/collision_demo.json");
            }
            obstacles = jrpgmaker::core::ParseCollisionAabbs(nlohmann::json::parse(file));
        }
        jrpgmaker::core::NavigationGrid navigation_grid(1, 1, glm::vec2(0.0f), 1.0f, {true});
        {
            std::ifstream file("assets/data/navigation_demo.json");
            if (!file) {
                throw std::runtime_error("failed to open assets/data/navigation_demo.json");
            }
            navigation_grid = jrpgmaker::core::ParseNavigationGrid(nlohmann::json::parse(file));
        }
        std::cout << "navigation grid " << navigation_grid.width() << "x"
                  << navigation_grid.height() << " loaded\n";
        jrpgmaker::core::CameraRigData camera_data;
        {
            std::ifstream file("assets/data/camera_demo.json");
            if (!file) {
                throw std::runtime_error("failed to open assets/data/camera_demo.json");
            }
            camera_data = jrpgmaker::core::ParseCameraRigData(nlohmann::json::parse(file));
        }
        std::vector<jrpgmaker::domain::InteractionPoint> interactions;
        {
            std::ifstream file("assets/data/interaction_demo.json");
            if (!file) {
                throw std::runtime_error("failed to open assets/data/interaction_demo.json");
            }
            interactions = jrpgmaker::domain::ParseInteractionPoints(nlohmann::json::parse(file));
        }
        jrpgmaker::domain::EventScript event_script;
        {
            std::ifstream file("assets/data/events_demo.json");
            if (!file) {
                throw std::runtime_error("failed to open assets/data/events_demo.json");
            }
            event_script = jrpgmaker::domain::ParseEventScript(nlohmann::json::parse(file));
        }
        jrpgmaker::domain::ValidateInteractionTargets(interactions, event_script);
        jrpgmaker::core::EventBus event_bus;
        jrpgmaker::domain::FlagStore flags;
        jrpgmaker::domain::EventRunner event_runner(event_script, flags, event_bus);
        jrpgmaker::domain::InteractionSystem interaction_system(interactions, event_bus);
        jrpgmaker::core::CharacterController controller(
            {.position = {0.0f, 1.0f, 0.0f}, .radius = 0.35f, .half_height = 0.9f});
        jrpgmaker::core::CameraRig camera_rig(camera_data.third_person);
        jrpgmaker::core::LocomotionState locomotion = jrpgmaker::core::LocomotionState::kIdle;
        std::deque<std::string> pending_events;
        std::string prompt_projection;
        std::string dialog_projection;
        event_bus.Subscribe<jrpgmaker::domain::InteractionPromptShown>(
            [&prompt_projection](const auto& prompt) {
                prompt_projection = prompt.prompt_text_key;
            });
        event_bus.Subscribe<jrpgmaker::domain::InteractionPromptHidden>(
            [&prompt_projection](const auto&) { prompt_projection.clear(); });
        event_bus.Subscribe<jrpgmaker::domain::DialogRequested>(
            [&dialog_projection](const auto& dialog) { dialog_projection = dialog.text_key; });
        InputState input;
        stages.RegisterSystem(jrpgmaker::core::Stage::kInput, {jrpgmaker::core::Stage::kInput, 0},
                              [&input](double) { input.UpdateMovement(); });
        stages.RegisterSystem(
            jrpgmaker::core::Stage::kDomainSim, {jrpgmaker::core::Stage::kDomainSim, 0},
            [&controller, &input, &obstacles, &interaction_system, &event_runner, &character,
             character_entity, &pending_events](double delta) {
                controller.Move(input.movement * 3.0f, static_cast<float>(delta), obstacles);
                character.scene.Registry()
                    .get<jrpgmaker::core::Transform>(character_entity)
                    .translation = controller.state().position;
                interaction_system.Update(controller.state().position, input.confirm_pressed);
                for (const std::string& event_id : interaction_system.DrainConfirmedEvents()) {
                    pending_events.push_back(event_id);
                }
                const auto start_next_event = [&]() {
                    if (!event_runner.IsActive() && !pending_events.empty()) {
                        const std::string event_id = std::move(pending_events.front());
                        pending_events.pop_front();
                        if (!event_runner.Start(event_id)) {
                            throw std::runtime_error("interaction target event is missing: " +
                                                     event_id);
                        }
                    }
                };
                start_next_event();
                if (input.confirm_requested && event_runner.IsDialogPending() &&
                    event_runner.pending_options().empty()) {
                    event_runner.AdvanceDialog();
                }
                event_runner.Tick(delta);
                start_next_event();
                input.confirm_requested = false;
            });
        stages.RegisterSystem(jrpgmaker::core::Stage::kPresentationSync,
                              {jrpgmaker::core::Stage::kPresentationSync, 0},
                              [&prompt_projection, &dialog_projection](double) {
                                  static std::string last_prompt;
                                  static std::string last_dialog;
                                  if (prompt_projection != last_prompt) {
                                      std::cout
                                          << (prompt_projection.empty()
                                                  ? "prompt hidden\n"
                                                  : "prompt shown: " + prompt_projection + "\n");
                                      last_prompt = prompt_projection;
                                  }
                                  if (dialog_projection != last_dialog) {
                                      std::cout << "dialog requested: " << dialog_projection
                                                << '\n';
                                      last_dialog = dialog_projection;
                                  }
                              });
        stages.RegisterSystem(
            jrpgmaker::core::Stage::kAnimation, {jrpgmaker::core::Stage::kAnimation, 0},
            [&controller, &camera_rig, &camera_data, &locomotion, &animation_time](double delta) {
                const glm::vec3 horizontal_velocity =
                    controller.state().velocity * glm::vec3(1.0f, 0.0f, 1.0f);
                locomotion =
                    jrpgmaker::core::SelectLocomotionState(glm::length(horizontal_velocity));
                animation_time += static_cast<float>(delta);
                camera_rig.Update(controller.state().position, static_cast<float>(delta),
                                  camera_data.fixed_regions);
            });
        // The render submit stage owns the presentation: it acquires the back
        // buffer, records the draw, submits and presents (docs/01 Stage
        // contract: RenderSubmit drives the render submit).
        stages.RegisterSystem(
            jrpgmaker::core::Stage::kRenderSubmit, {jrpgmaker::core::Stage::kRenderSubmit, 0},
            [device = device.get(), swapchain = swapchain.get(), command_list, pipeline,
             vertex_buffer, index_buffer, uniform_buffer, &character, character_entity,
             character_skin_ref, character_mesh, &camera_rig, &animation_time,
             &locomotion](double) mutable {
                const auto& skeleton =
                    character.skeletons[character_skin_ref.skeleton_index].skeleton;
                const std::size_t clip_index =
                    locomotion == jrpgmaker::core::LocomotionState::kIdle ? 0u
                    : locomotion == jrpgmaker::core::LocomotionState::kWalk
                        ? std::min<std::size_t>(1u, character.animations.size() - 1u)
                        : std::min<std::size_t>(2u, character.animations.size() - 1u);
                const auto& clip = character.animations[clip_index].clip;
                const auto pose = jrpgmaker::core::SamplePose(skeleton, clip, animation_time);
                const auto local_bones = jrpgmaker::core::BoneMatrices(skeleton, pose);
                std::vector<glm::mat4> render_bones(32u, glm::mat4(1.0f));
                const glm::mat4 world = character.scene.WorldMatrix(character_entity);
                const glm::mat4 view_projection = camera_rig.camera().ViewProjection();
                for (std::size_t bone = 0; bone < local_bones.size(); ++bone) {
                    render_bones[bone] = view_projection * world * local_bones[bone];
                }
                device->MapWrite(uniform_buffer, render_bones.data(),
                                 render_bones.size() * sizeof(glm::mat4));
                const jrpgmaker::rhi::TextureHandle back_buffer = swapchain->AcquireTexture();
                command_list->Begin();
                command_list->BeginRendering(back_buffer, {0.10f, 0.11f, 0.12f, 1.0f});
                command_list->SetPipeline(pipeline);
                command_list->SetVertexUniformBuffer(uniform_buffer, 32u * sizeof(glm::mat4));
                command_list->SetVertexBuffer(vertex_buffer, sizeof(SkinnedVertex));
                command_list->SetIndexBuffer(index_buffer, true);
                command_list->DrawIndexed(
                    static_cast<std::uint32_t>(character_mesh->indices.size()), 1);
                command_list->EndRendering();
                command_list->End();
                device->Submit(*command_list);
                swapchain->Present();
            });

        std::cout << "jrpgmaker " << jrpgmaker::core::version() << " running\n";
        RunMainLoop(swapchain.get(), stages, input);

        // DestroyXxx requires the GPU to be idle (docs/01 lifecycle contract):
        // the last submitted command list and the shared allocator must not be
        // touched while the GPU may still reference them.
        device->WaitForGpuIdle();
        device->DestroyCommandList(command_list);
        device->DestroyBuffer(vertex_buffer);
        device->DestroyBuffer(index_buffer);
        device->DestroyBuffer(uniform_buffer);

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

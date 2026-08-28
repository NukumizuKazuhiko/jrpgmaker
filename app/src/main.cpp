#include <SDL3/SDL.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <deque>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string_view>
#include <unordered_map>
#include <vector>

#include <glm/glm.hpp>
#include <nlohmann/json.hpp>

#include "jrpgmaker/assetimport/asset_import.hpp"
#include "jrpgmaker/assetimport/async_loader.hpp"
#include "jrpgmaker/audio/audio.hpp"
#include "jrpgmaker/core/animation.hpp"
#include "jrpgmaker/core/calendar.hpp"
#include "jrpgmaker/core/camera_rig.hpp"
#include "jrpgmaker/core/character_controller.hpp"
#include "jrpgmaker/core/cutscene.hpp"
#include "jrpgmaker/core/input_actions.hpp"
#include "jrpgmaker/core/map_data.hpp"
#include "jrpgmaker/core/stage.hpp"
#include "jrpgmaker/core/version.hpp"
#include "jrpgmaker/domain/encounter.hpp"
#include "jrpgmaker/domain/event_lint.hpp"
#include "jrpgmaker/domain/event_runner.hpp"
#include "jrpgmaker/domain/event_script.hpp"
#include "jrpgmaker/domain/interaction.hpp"
#include "jrpgmaker/domain/save.hpp"
#include "jrpgmaker/domain/schedule.hpp"
#include "jrpgmaker/domain/vertical_slice.hpp"
#include "jrpgmaker/plugin/battle.hpp"
#include "jrpgmaker/plugin/plugin.hpp"
#include "jrpgmaker/plugins/register.hpp"
#include "jrpgmaker/render/style.hpp"
#include "jrpgmaker/render/texture_resource.hpp"
#include "jrpgmaker/rhi/command_list.hpp"
#include "jrpgmaker/rhi/device.hpp"
#include "jrpgmaker/rhi/device_factory.hpp"
#include "jrpgmaker/rhi/swapchain.hpp"
#include "jrpgmaker/ui/theme.hpp"
#include "shaders_generated.hpp"

namespace {

class SdlAudioOutput {
public:
    explicit SdlAudioOutput(jrpgmaker::audio::MixerBus& mixer) : mixer_(mixer) {
        const SDL_AudioSpec spec{SDL_AUDIO_F32, 1, 48000};
        stream_ =
            SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &spec, nullptr, nullptr);
        if (stream_ == nullptr) {
            SDL_Log("audio output unavailable: %s", SDL_GetError());
            return;
        }
        if (!SDL_ResumeAudioStreamDevice(stream_)) {
            SDL_Log("audio output could not resume: %s", SDL_GetError());
            SDL_DestroyAudioStream(stream_);
            stream_ = nullptr;
        }
    }

    SdlAudioOutput(const SdlAudioOutput&) = delete;
    SdlAudioOutput& operator=(const SdlAudioOutput&) = delete;

    ~SdlAudioOutput() {
        if (stream_ != nullptr) {
            SDL_DestroyAudioStream(stream_);
        }
    }

    void Tick() {
        if (stream_ == nullptr) {
            return;
        }

        constexpr int kFramesPerChunk = 480;
        constexpr int kBytesPerFrame = sizeof(float);
        constexpr int kMaxQueuedBytes = kFramesPerChunk * kBytesPerFrame * 4;
        if (SDL_GetAudioStreamQueued(stream_) > kMaxQueuedBytes) {
            return;
        }

        std::array<float, kFramesPerChunk> frames{};
        mixer_.Mix(frames);
        if (!SDL_PutAudioStreamData(stream_, frames.data(),
                                    static_cast<int>(frames.size() * sizeof(float)))) {
            SDL_Log("audio output rejected PCM buffer: %s", SDL_GetError());
        }
    }

private:
    jrpgmaker::audio::MixerBus& mixer_;
    SDL_AudioStream* stream_ = nullptr;
};

std::vector<float> MakeEventCue() {
    constexpr int kSampleRate = 48000;
    constexpr int kFrames = kSampleRate / 20;
    constexpr float kPi = 3.14159265358979323846f;
    std::vector<float> samples(kFrames);
    for (int frame = 0; frame < kFrames; ++frame) {
        const float envelope = 1.0f - static_cast<float>(frame) / kFrames;
        samples[frame] = 0.08f * envelope *
                         std::sin(2.0f * kPi * 660.0f * static_cast<float>(frame) / kSampleRate);
    }
    return samples;
}

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
    bool save_requested = false;
    bool load_requested = false;

    void UpdateAction(std::string_view action_id, bool is_down, bool is_repeat) {
        int& pressed_count = pressed_actions[std::string(action_id)];
        if (is_down) {
            if (is_repeat)
                return;
            ++pressed_count;
        } else {
            pressed_count = std::max(0, pressed_count - 1);
        }
        const bool active = pressed_count > 0;
        if (action_id == "move.forward")
            forward = active;
        else if (action_id == "move.backward")
            backward = active;
        else if (action_id == "move.left")
            left = active;
        else if (action_id == "move.right")
            right = active;
        else if (action_id == "extension.confirm") {
            confirm_pressed = active;
            if (is_down && !is_repeat)
                confirm_requested = true;
        } else if (action_id == "project.save" && is_down && !is_repeat) {
            save_requested = true;
        } else if (action_id == "project.load" && is_down && !is_repeat) {
            load_requested = true;
        }
    }

    void UpdateMovement() {
        glm::vec2 axes{static_cast<float>(right) - static_cast<float>(left),
                       static_cast<float>(backward) - static_cast<float>(forward)};
        if (glm::dot(axes, axes) > 1.0f) {
            axes = glm::normalize(axes);
        }
        movement = {axes.x, 0.0f, axes.y};
    }

private:
    std::unordered_map<std::string, int> pressed_actions;
};

struct SkinnedVertex {
    float position[3];
    std::uint16_t joints[4];
    float weights[4];
    float uv[2];
};

struct UiVertex {
    float position[3];
    float color[4];
};

nlohmann::json ReadJsonFile(const std::filesystem::path& path) {
    std::ifstream file(path);
    if (!file) {
        throw std::runtime_error("failed to open " + path.string());
    }
    return nlohmann::json::parse(file);
}

jrpgmaker::render::OpaqueMaterialParameters
EncodeMaterialParameters(const nlohmann::json& parameters) {
    const std::vector<std::uint8_t> encoded = nlohmann::json::to_cbor(parameters);
    jrpgmaker::render::OpaqueMaterialParameters result;
    result.reserve(encoded.size());
    for (const std::uint8_t byte : encoded) {
        result.push_back(static_cast<std::byte>(byte));
    }
    return result;
}

jrpgmaker::plugin::PluginManifest ReadPluginManifest(const std::filesystem::path& path) {
    const auto result = jrpgmaker::plugin::ParseManifest(ReadJsonFile(path));
    if (!result) {
        throw std::runtime_error("invalid plugin manifest " + path.string() + ": " +
                                 result.error->message);
    }
    return *result.manifest;
}

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
        if (mesh.texcoords.size() != mesh.vertex_count() * 2u) {
            throw std::runtime_error("textured character mesh has no TEXCOORD_0 data");
        }
        output.uv[0] = mesh.texcoords[vertex * 2u];
        output.uv[1] = mesh.texcoords[vertex * 2u + 1u];
    }
    return vertices;
}

using InputBindings = std::unordered_map<SDL_Keycode, std::vector<std::string>>;

InputBindings BuildInputBindings(const jrpgmaker::core::InputActionMap& action_map) {
    InputBindings bindings;
    for (const auto& action : action_map.actions) {
        for (const std::string& key_name : action.keys) {
            const SDL_Keycode key = SDL_GetKeyFromName(key_name.c_str());
            if (key == SDLK_UNKNOWN) {
                throw std::runtime_error("input action uses unknown SDL key: " + key_name);
            }
            bindings[key].push_back(action.id);
        }
    }
    return bindings;
}

void RunMainLoop(jrpgmaker::rhi::ISwapchain* swapchain, jrpgmaker::core::StageRunner& stages,
                 InputState& input, SdlAudioOutput& audio_output,
                 const InputBindings& input_bindings) {
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
                const auto binding = input_bindings.find(event.key.key);
                if (binding != input_bindings.end()) {
                    for (const std::string& action_id : binding->second) {
                        input.UpdateAction(action_id, is_down, event.key.repeat);
                    }
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
            audio_output.Tick();
            accumulator -= kFixedDelta;
        }
    }
}

} // namespace

auto main() -> int {
    try {
        if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO)) {
            throw std::runtime_error(std::string("SDL_Init failed: ") + SDL_GetError());
        }
        jrpgmaker::audio::MixerBus mixer;
        SdlAudioOutput audio_output(mixer);

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
        // The service owns all GPU objects. The loader is declared after it so
        // its worker is joined before the service is destroyed.
        jrpgmaker::render::TextureResourceService texture_resources(*device);
        jrpgmaker::assetimport::AsyncLoader texture_loader;
        std::vector<std::string> character_texture_ids;
        std::vector<bool> character_texture_acquired;
        std::deque<std::string> texture_errors;
        const std::filesystem::path character_asset_path = "assets/art/meshes/arm_skinned.gltf";
        for (const auto& texture : character.textures) {
            const std::string resource_id =
                !texture.name.empty() ? texture.name : texture.source_uri;
            if (resource_id.empty()) {
                throw std::runtime_error("character texture has no stable id");
            }
            character_texture_ids.push_back(resource_id);
            character_texture_acquired.push_back(false);
            const auto sampler =
                jrpgmaker::rhi::SamplerDesc{.filter = jrpgmaker::rhi::SamplerFilter::kLinear,
                                            .address = jrpgmaker::rhi::SamplerAddress::kRepeat};
            if (texture.source_uri.empty() || texture.source_uri.starts_with("data:")) {
                const auto queued = texture_resources.QueueUpload({.id = resource_id,
                                                                   .width = texture.width,
                                                                   .height = texture.height,
                                                                   .rgba8 = texture.rgba8,
                                                                   .sampler = sampler});
                if (!queued) {
                    (void) texture_resources.RecordFailure(resource_id, queued.error);
                }
                continue;
            }
            const std::filesystem::path source_path =
                character_asset_path.parent_path() / std::filesystem::path(texture.source_uri);
            const bool submitted = texture_loader.SubmitTexture(
                source_path,
                [&texture_resources, &texture_errors, resource_id,
                 sampler](std::filesystem::path, jrpgmaker::assetimport::TextureLoadResult result) {
                    if (!result.texture.has_value()) {
                        const std::string error =
                            result.error.empty() ? "texture decode failed" : result.error;
                        (void) texture_resources.RecordFailure(resource_id, error);
                        texture_errors.push_back(resource_id + ": " + error);
                        return;
                    }
                    const auto& texture = *result.texture;
                    const auto queued =
                        texture_resources.QueueUpload({.id = resource_id,
                                                       .width = texture.width,
                                                       .height = texture.height,
                                                       .rgba8 = std::move(result.texture->rgba8),
                                                       .sampler = sampler});
                    if (!queued) {
                        (void) texture_resources.RecordFailure(resource_id, queued.error);
                        texture_errors.push_back(resource_id + ": " + queued.error);
                    }
                });
            if (!submitted) {
                (void) texture_resources.RecordFailure(resource_id,
                                                       "texture decode request queue exhausted");
                texture_errors.push_back(resource_id + ": texture decode request queue exhausted");
            }
        }

        const auto project_result =
            jrpgmaker::plugin::ParseProjectManifest(ReadJsonFile("assets/data/project_demo.json"));
        if (!project_result) {
            throw std::runtime_error("invalid project manifest: " + project_result.error->message);
        }
        const auto input_action_map = jrpgmaker::core::ParseInputActionMap(
            ReadJsonFile(project_result.manifest->input_actions));
        if (!input_action_map) {
            throw std::runtime_error("invalid input action map: " + input_action_map.error);
        }
        const InputBindings input_bindings = BuildInputBindings(*input_action_map.map);
        jrpgmaker::plugin::PluginRegistry plugin_registry;
        const auto registration_error = jrpgmaker::plugins::RegisterSamplePlugins(
            plugin_registry, ReadPluginManifest("plugins/sample_unlit/plugin.json"),
            ReadPluginManifest("plugins/sample_style/plugin.json"));
        if (registration_error.has_value()) {
            throw std::runtime_error("failed to register plugins: " + registration_error->message);
        }
        const auto battle_registration_error = jrpgmaker::plugins::RegisterSampleBattlePlugins(
            plugin_registry, ReadPluginManifest("plugins/sample_instant/plugin.json"),
            ReadPluginManifest("plugins/sample_turn_based/plugin.json"));
        if (battle_registration_error.has_value()) {
            throw std::runtime_error("failed to register battle plugins: " +
                                     battle_registration_error->message);
        }
        if (const auto error = jrpgmaker::plugin::ValidateProjectPlugins(*project_result.manifest,
                                                                         plugin_registry);
            error.has_value()) {
            throw std::runtime_error("invalid project plugin selection: " + error->message);
        }
        if (const auto error = jrpgmaker::plugin::ValidateProjectDataRoots(
                *project_result.manifest, std::filesystem::current_path());
            error.has_value()) {
            throw std::runtime_error("invalid project data root: " + error->message);
        }
        auto battle_result =
            jrpgmaker::plugin::CreateProjectBattlePlugin(*project_result.manifest, plugin_registry);
        if (!battle_result) {
            throw std::runtime_error("failed to create project battle plugin: " +
                                     battle_result.error->message);
        }
        auto battle_plugin = std::move(battle_result.instance);
        const auto style_result =
            jrpgmaker::plugin::CreateProjectRenderStyle(*project_result.manifest, plugin_registry);
        if (!style_result) {
            throw std::runtime_error("failed to create project render style: " +
                                     style_result.error->message);
        }
        auto* style_adapter =
            dynamic_cast<jrpgmaker::render::IRenderStyleAdapter*>(style_result.instance.get());
        if (style_adapter == nullptr) {
            throw std::runtime_error("project render style has an invalid adapter type");
        }
        const auto material_document = ReadJsonFile(project_result.manifest->material_document);
        if (!material_document.is_object() || material_document.value("schema", 0) != 1 ||
            material_document.value("id", std::string{}) != "character" ||
            material_document.value("style_plugin_id", std::string{}) !=
                project_result.manifest->render_style ||
            !material_document.contains("parameters") ||
            (material_document.contains("sampled_texture") &&
             (!material_document["sampled_texture"].is_string() ||
              material_document["sampled_texture"].get<std::string>().empty()))) {
            throw std::runtime_error("invalid character material instance");
        }
        const auto material_validation =
            style_adapter->ValidateMaterial(material_document["parameters"]);
        if (!material_validation.ok) {
            throw std::runtime_error("invalid character material parameters: " +
                                     material_validation.error);
        }
        const auto material_parameters = EncodeMaterialParameters(material_document["parameters"]);
        const auto theme = jrpgmaker::ui::ParseTheme(ReadJsonFile("assets/data/theme_demo.json"));
        if (!theme) {
            throw std::runtime_error("invalid project UI theme: " + theme.error);
        }
        const auto resource_catalog = jrpgmaker::render::ParseRenderResourceCatalog(
            ReadJsonFile("assets/data/render_resources_demo.json"));
        if (!resource_catalog) {
            throw std::runtime_error("invalid render resource catalog: " + resource_catalog.error);
        }
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
        jrpgmaker::rhi::GraphicsPipelineDesc textured_pipeline_desc = pipeline_desc;
        const jrpgmaker::rhi::ShaderBytecode textured_vs = {
#if defined(_WIN32)
            jrpgmaker::shaders::kSkinnedTexturedVsDxil,
            jrpgmaker::shaders::kSkinnedTexturedVsDxil_size
#else
            jrpgmaker::shaders::kSkinnedTexturedVsSpv,
            jrpgmaker::shaders::kSkinnedTexturedVsSpv_size
#endif
        };
        const jrpgmaker::rhi::ShaderBytecode textured_ps = {
#if defined(_WIN32)
            jrpgmaker::shaders::kSkinnedTexturedPsDxil,
            jrpgmaker::shaders::kSkinnedTexturedPsDxil_size
#else
            jrpgmaker::shaders::kSkinnedTexturedPsSpv,
            jrpgmaker::shaders::kSkinnedTexturedPsSpv_size
#endif
        };
        const jrpgmaker::rhi::VertexAttribute textured_skinned_attributes[] = {
            skinned_attributes[0],
            skinned_attributes[1],
            skinned_attributes[2],
            {.location = 3,
             .format = jrpgmaker::rhi::VertexAttributeFormat::kFloat2,
             .offset_bytes = 36,
             .semantic_name = "TEXCOORD"},
        };
        textured_pipeline_desc.vertex_shader = textured_vs;
        textured_pipeline_desc.pixel_shader = textured_ps;
        textured_pipeline_desc.vertex_input = jrpgmaker::rhi::VertexInputLayout{
            .attributes = textured_skinned_attributes,
            .attribute_count = static_cast<std::uint32_t>(std::size(textured_skinned_attributes)),
            .stride_bytes = static_cast<std::uint32_t>(sizeof(SkinnedVertex)),
        };
        textured_pipeline_desc.sample_slot = 1;
        const jrpgmaker::rhi::PipelineHandle textured_pipeline =
            device->CreatePipeline(textured_pipeline_desc);
        jrpgmaker::rhi::GraphicsPipelineDesc accent_pipeline_desc = pipeline_desc;
        const jrpgmaker::rhi::ShaderBytecode accent_ps = {
#if defined(_WIN32)
            jrpgmaker::shaders::kCameraPsDxil, jrpgmaker::shaders::kCameraPsDxil_size
#else
            jrpgmaker::shaders::kCameraPsSpv, jrpgmaker::shaders::kCameraPsSpv_size
#endif
        };
        accent_pipeline_desc.pixel_shader = accent_ps;
        const jrpgmaker::rhi::PipelineHandle accent_pipeline =
            device->CreatePipeline(accent_pipeline_desc);

        const jrpgmaker::rhi::VertexAttribute ui_attributes[] = {
            {.location = 0,
             .format = jrpgmaker::rhi::VertexAttributeFormat::kFloat3,
             .offset_bytes = 0,
             .semantic_name = "POSITION"},
            {.location = 1,
             .format = jrpgmaker::rhi::VertexAttributeFormat::kFloat4,
             .offset_bytes = 12,
             .semantic_name = "COLOR"},
        };
        jrpgmaker::rhi::GraphicsPipelineDesc ui_pipeline_desc{};
        ui_pipeline_desc.color_format = jrpgmaker::rhi::Format::kB8G8R8A8Unorm;
        ui_pipeline_desc.vertex_input = jrpgmaker::rhi::VertexInputLayout{
            .attributes = ui_attributes,
            .attribute_count = static_cast<std::uint32_t>(std::size(ui_attributes)),
            .stride_bytes = sizeof(UiVertex),
        };
#if defined(_WIN32)
        ui_pipeline_desc.vertex_shader = {jrpgmaker::shaders::kUiVsDxil,
                                          jrpgmaker::shaders::kUiVsDxil_size};
        ui_pipeline_desc.pixel_shader = {jrpgmaker::shaders::kUiPsDxil,
                                         jrpgmaker::shaders::kUiPsDxil_size};
#else
        ui_pipeline_desc.vertex_shader = {jrpgmaker::shaders::kUiVsSpv,
                                          jrpgmaker::shaders::kUiVsSpv_size};
        ui_pipeline_desc.pixel_shader = {jrpgmaker::shaders::kUiPsSpv,
                                         jrpgmaker::shaders::kUiPsSpv_size};
#endif
        const jrpgmaker::rhi::PipelineHandle ui_pipeline = device->CreatePipeline(ui_pipeline_desc);
        const auto& ui_theme = *theme.theme;
        const std::array<UiVertex, 4> ui_vertices = {{
            {{-0.95f, -0.88f, 0.0f},
             {ui_theme.accent.r, ui_theme.accent.g, ui_theme.accent.b, 0.88f}},
            {{0.95f, -0.88f, 0.0f},
             {ui_theme.accent.r, ui_theme.accent.g, ui_theme.accent.b, 0.88f}},
            {{0.95f, -0.62f, 0.0f},
             {ui_theme.accent.r, ui_theme.accent.g, ui_theme.accent.b, 0.88f}},
            {{-0.95f, -0.62f, 0.0f},
             {ui_theme.accent.r, ui_theme.accent.g, ui_theme.accent.b, 0.88f}},
        }};
        constexpr std::array<std::uint32_t, 6> kUiIndices = {0, 1, 2, 0, 2, 3};
        const auto ui_vertex_buffer = device->CreateBuffer(jrpgmaker::rhi::BufferDesc{
            .size_bytes = sizeof(ui_vertices), .usage = jrpgmaker::rhi::BufferUsage::kVertex});
        device->MapWrite(ui_vertex_buffer, ui_vertices.data(), sizeof(ui_vertices));
        const auto ui_index_buffer = device->CreateBuffer(jrpgmaker::rhi::BufferDesc{
            .size_bytes = sizeof(kUiIndices), .usage = jrpgmaker::rhi::BufferUsage::kIndex});
        device->MapWrite(ui_index_buffer, kUiIndices.data(), sizeof(kUiIndices));

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
        const auto encounters = jrpgmaker::domain::ParseEncounterPoints(
            ReadJsonFile("assets/data/encounter_demo.json"));
        jrpgmaker::domain::ValidateEncounterTargets(encounters, event_script);
        const auto cutscene_timeline =
            jrpgmaker::core::ParseCutsceneTimeline(ReadJsonFile("assets/data/cutscene_demo.json"));
        jrpgmaker::domain::ValidateCutsceneTargets(cutscene_timeline, event_script);
        const auto calendar_result = jrpgmaker::core::ParseCalendarDefinition(
            ReadJsonFile("assets/data/calendar_demo.json"));
        if (!calendar_result.ok) {
            throw std::runtime_error("calendar data error: " + calendar_result.error);
        }
        const auto calendar = calendar_result.calendar;
        const auto schedule = jrpgmaker::domain::ParseScheduleTable(
            ReadJsonFile("assets/data/schedule_demo.json"), calendar);
        jrpgmaker::domain::ValidateScheduleTargets(schedule, event_script);
        const auto vertical_slice = jrpgmaker::domain::ParseVerticalSliceDefinition(
            ReadJsonFile("assets/data/vertical_slice_demo.json"));
        jrpgmaker::domain::ValidateVerticalSliceTargets(vertical_slice, event_script);
        std::cout << "vertical slice " << vertical_slice.id << " loaded ("
                  << vertical_slice.total_duration_seconds << " seconds)\n";
        jrpgmaker::core::GameClock game_clock(calendar);
        jrpgmaker::domain::ScheduleSystem schedule_system(schedule, calendar);
        jrpgmaker::core::CutscenePlayer cutscene_player(cutscene_timeline);
        jrpgmaker::core::EventBus event_bus;
        jrpgmaker::domain::FlagStore flags;
        jrpgmaker::domain::EventRunner event_runner(event_script, flags, event_bus);
        jrpgmaker::domain::InteractionSystem interaction_system(interactions, event_bus);
        jrpgmaker::domain::EncounterSystem encounter_system(encounters, event_bus);
        jrpgmaker::core::CharacterController controller(
            {.position = {0.0f, 1.0f, 0.0f}, .radius = 0.35f, .half_height = 0.9f});
        jrpgmaker::core::CameraRig camera_rig(camera_data.third_person);
        jrpgmaker::core::LocomotionState locomotion = jrpgmaker::core::LocomotionState::kIdle;
        std::deque<std::string> pending_events;
        std::deque<jrpgmaker::domain::EncounterRequested> pending_encounters;
        std::unique_ptr<jrpgmaker::plugin::IBattleSession> battle_session;
        std::map<std::string, std::string> battle_result_events;
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
        event_bus.Subscribe<jrpgmaker::domain::EventStarted>(
            [&mixer](const auto&) { mixer.Play("event.cue", MakeEventCue(), 0.8f); });
        event_bus.Subscribe<jrpgmaker::domain::EncounterRequested>(
            [&pending_encounters](const auto& request) { pending_encounters.push_back(request); });
        InputState input;
        stages.RegisterSystem(jrpgmaker::core::Stage::kInput, {jrpgmaker::core::Stage::kInput, 0},
                              [&input](double) { input.UpdateMovement(); });
        stages.RegisterSystem(
            jrpgmaker::core::Stage::kDomainSim, {jrpgmaker::core::Stage::kDomainSim, 0},
            [&controller, &input, &obstacles, &interaction_system, &encounter_system, &event_runner,
             &character, &battle_plugin, &plugin_registry, &battle_session, &battle_result_events,
             character_entity, &pending_events, &cutscene_player, &game_clock, &schedule_system,
             &pending_encounters, &flags, project_result, &texture_loader](double delta) {
                texture_loader.Poll();
                const bool in_battle = battle_session != nullptr;
                controller.Move(in_battle ? glm::vec3(0.0f) : input.movement * 3.0f,
                                static_cast<float>(delta), obstacles);
                character.scene.Registry()
                    .get<jrpgmaker::core::Transform>(character_entity)
                    .translation = controller.state().position;
                interaction_system.Update(controller.state().position, input.confirm_pressed);
                if (!in_battle)
                    encounter_system.Update(controller.state().position);
                game_clock.AdvanceMinutes(1);
                for (const auto& event_id : schedule_system.Poll(game_clock)) {
                    pending_events.push_back(event_id);
                }
                cutscene_player.Advance(delta);
                for (const auto& event_id : cutscene_player.DrainTriggeredEvents()) {
                    pending_events.push_back(event_id);
                }
                for (const std::string& event_id : interaction_system.DrainConfirmedEvents()) {
                    pending_events.push_back(event_id);
                }
                if (battle_session != nullptr) {
                    jrpgmaker::plugin::BattleFrameInput battle_input{
                        .delta_seconds = delta,
                        .action_ids = input.confirm_requested
                                          ? std::vector<std::string>{"extension.confirm"}
                                          : std::vector<std::string>{},
                        .opaque_payload = {},
                        .cancel_requested = false};
                    const auto advanced = battle_session->Advance(battle_input);
                    if (!advanced) {
                        throw std::runtime_error("battle plugin advance failed: " +
                                                 advanced.error->message);
                    }
                    if (const auto error = jrpgmaker::plugin::ValidateBattleOutput(advanced.output);
                        error.has_value()) {
                        throw std::runtime_error("battle plugin returned invalid output: " +
                                                 error->message);
                    }
                    if (advanced.output.finished) {
                        const auto result = battle_result_events.find(advanced.output.result_key);
                        if (result == battle_result_events.end()) {
                            throw std::runtime_error("battle result has no mapped project event: " +
                                                     advanced.output.result_key);
                        }
                        pending_events.push_back(result->second);
                        battle_session.reset();
                        battle_result_events.clear();
                    }
                } else if (!pending_encounters.empty() && !event_runner.IsActive() &&
                           pending_events.empty()) {
                    const auto request = std::move(pending_encounters.front());
                    pending_encounters.pop_front();
                    const std::string plugin_id = request.plugin_id.empty()
                                                      ? project_result.manifest->battle_plugin
                                                      : request.plugin_id;
                    if (plugin_id != project_result.manifest->battle_plugin) {
                        auto selected = plugin_registry.Create(
                            plugin_id, jrpgmaker::plugin::PluginType::kBattle);
                        if (!selected) {
                            throw std::runtime_error("encounter selected unknown battle plugin: " +
                                                     plugin_id);
                        }
                        battle_plugin = std::move(selected.instance);
                    }
                    auto* battle =
                        dynamic_cast<jrpgmaker::plugin::IBattlePlugin*>(battle_plugin.get());
                    if (battle == nullptr) {
                        throw std::runtime_error(
                            "selected battle plugin has no battle interface: " + plugin_id);
                    }
                    auto created = battle->CreateSession(
                        {.encounter_id = request.encounter_id, .opaque_payload = {}});
                    if (!created) {
                        throw std::runtime_error("battle session creation failed: " +
                                                 created.error->message);
                    }
                    battle_session = std::move(created.session);
                    battle_result_events = request.result_event_ids;
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
                if (input.save_requested) {
                    if (event_runner.IsActive() || !pending_events.empty()) {
                        throw std::runtime_error("save is only available at an event boundary");
                    }
                    std::string error;
                    if (!jrpgmaker::domain::WriteSaveFile(
                            "save_slot_0.json", jrpgmaker::domain::CaptureSave(game_clock, flags),
                            error)) {
                        throw std::runtime_error("save failed: " + error);
                    }
                    std::cout << "save written at minute " << game_clock.absolute_minutes() << '\n';
                    input.save_requested = false;
                }
                if (input.load_requested) {
                    if (event_runner.IsActive() || !pending_events.empty()) {
                        throw std::runtime_error("load is only available at an event boundary");
                    }
                    const auto loaded = jrpgmaker::domain::ReadSaveFile("save_slot_0.json");
                    if (!loaded.ok) {
                        throw std::runtime_error("load failed: " + loaded.error);
                    }
                    jrpgmaker::domain::RestoreSave(loaded.state, game_clock, flags);
                    schedule_system.Reset(game_clock);
                    std::cout << "save restored at minute " << game_clock.absolute_minutes()
                              << '\n';
                    input.load_requested = false;
                }
                input.confirm_requested = false;
            });
        stages.RegisterSystem(
            jrpgmaker::core::Stage::kPresentationSync,
            {jrpgmaker::core::Stage::kPresentationSync, 0},
            [&prompt_projection, &dialog_projection, &texture_errors](double) {
                static std::string last_prompt;
                static std::string last_dialog;
                while (!texture_errors.empty()) {
                    std::cerr << "texture load failed: " << texture_errors.front() << '\n';
                    texture_errors.pop_front();
                }
                if (prompt_projection != last_prompt) {
                    std::cout << (prompt_projection.empty()
                                      ? "prompt hidden\n"
                                      : "prompt shown: " + prompt_projection + "\n");
                    last_prompt = prompt_projection;
                }
                if (dialog_projection != last_dialog) {
                    std::cout << "dialog requested: " << dialog_projection << '\n';
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
            [device = device.get(), swapchain = swapchain.get(), command_list, vertex_buffer,
             index_buffer, uniform_buffer, pipeline, textured_pipeline, accent_pipeline,
             ui_pipeline, ui_vertex_buffer, ui_index_buffer, &character, character_entity,
             character_skin_ref, character_mesh, &camera_rig, &animation_time, &locomotion,
             style = style_adapter, material_document, material_parameters, resource_catalog,
             &texture_resources, &character_texture_ids,
             &character_texture_acquired](double) mutable {
                texture_resources.PumpUploads(2);
                for (std::size_t i = 0; i < character_texture_ids.size(); ++i) {
                    if (!character_texture_acquired[i] &&
                        texture_resources.Acquire(character_texture_ids[i])) {
                        character_texture_acquired[i] = true;
                    }
                }
                const bool textures_ready = std::all_of(
                    character_texture_ids.begin(), character_texture_ids.end(),
                    [&texture_resources](const std::string& id) {
                        const auto status = texture_resources.Status(id);
                        return status.has_value() &&
                               status->state == jrpgmaker::render::TextureResourceState::kReady;
                    });
                if (!textures_ready) {
                    const jrpgmaker::rhi::TextureHandle back_buffer = swapchain->AcquireTexture();
                    (void) back_buffer;
                    command_list->Begin();
                    command_list->End();
                    device->Submit(*command_list);
                    swapchain->Present();
                    return;
                }
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
                jrpgmaker::render::SceneSnapshot snapshot{
                    .view_projection = view_projection,
                    .renderables = {{.mesh = "character",
                                     .material = "character",
                                     .world = world,
                                     .material_parameters = material_parameters,
                                     .sampled_texture = material_document.value("sampled_texture",
                                                                                std::string{})}}};
                auto render_plan = style->BuildPlan(snapshot);
                if (render_plan.passes.empty()) {
                    throw std::runtime_error("render style produced an empty render plan");
                }
                render_plan.passes.push_back(
                    jrpgmaker::render::RenderPass{.id = "ui.overlay",
                                                  .clear_color = glm::vec4(0.0f),
                                                  .clear_target = false,
                                                  .pipeline = "ui",
                                                  .draws = {{.mesh = "ui",
                                                             .material = "ui",
                                                             .world = glm::mat4(1.0f),
                                                             .material_parameters = {},
                                                             .sampled_texture = {}}}});
                const auto validation =
                    jrpgmaker::render::ValidateRenderPlan(render_plan, style->Descriptor().budget);
                if (!validation.ok) {
                    throw std::runtime_error("render style produced an invalid render plan: " +
                                             validation.error);
                }
                const auto& catalog = *resource_catalog.catalog;
                for (const auto& pass : render_plan.passes) {
                    if (std::find(catalog.pipeline_ids.begin(), catalog.pipeline_ids.end(),
                                  pass.pipeline) == catalog.pipeline_ids.end()) {
                        throw std::runtime_error("render plan references unknown pipeline: " +
                                                 pass.pipeline);
                    }
                    for (const auto& draw : pass.draws) {
                        if (std::find(catalog.mesh_ids.begin(), catalog.mesh_ids.end(),
                                      draw.mesh) == catalog.mesh_ids.end()) {
                            throw std::runtime_error("render plan references unknown mesh: " +
                                                     draw.mesh);
                        }
                        if (!draw.sampled_texture.empty()) {
                            if (std::find(catalog.texture_ids.begin(), catalog.texture_ids.end(),
                                          draw.sampled_texture) == catalog.texture_ids.end()) {
                                throw std::runtime_error(
                                    "render plan references unknown texture: " +
                                    draw.sampled_texture);
                            }
                            if (!texture_resources.Find(draw.sampled_texture).has_value()) {
                                throw std::runtime_error("render plan texture is not loaded: " +
                                                         draw.sampled_texture);
                            }
                        }
                    }
                }
                for (std::size_t bone = 0; bone < local_bones.size(); ++bone) {
                    render_bones[bone] = view_projection * world * local_bones[bone];
                }
                device->MapWrite(uniform_buffer, render_bones.data(),
                                 render_bones.size() * sizeof(glm::mat4));
                const jrpgmaker::rhi::TextureHandle back_buffer = swapchain->AcquireTexture();
                command_list->Begin();
                const jrpgmaker::render::RenderPlanResolver resolver{
                    .resolve_pipeline =
                        [pipeline, textured_pipeline, accent_pipeline, ui_pipeline](
                            const auto& pass) -> std::optional<jrpgmaker::rhi::PipelineHandle> {
                        if (pass.pipeline == "unlit") {
                            return pipeline;
                        }
                        if (pass.pipeline == "textured") {
                            return textured_pipeline;
                        }
                        if (pass.pipeline == "accent_textured") {
                            return textured_pipeline;
                        }
                        if (pass.pipeline == "accent") {
                            return accent_pipeline;
                        }
                        if (pass.pipeline == "ui") {
                            return ui_pipeline;
                        }
                        return std::nullopt;
                    },
                    .resolve_mesh =
                        [vertex_buffer, index_buffer, character_mesh, ui_vertex_buffer,
                         ui_index_buffer](const auto& draw) {
                            if (draw.mesh == "ui") {
                                return std::optional<jrpgmaker::render::RenderMeshBinding>{
                                    jrpgmaker::render::RenderMeshBinding{
                                        .vertex_buffer = ui_vertex_buffer,
                                        .index_buffer = ui_index_buffer,
                                        .stride_bytes = sizeof(UiVertex),
                                        .index_count = 6,
                                        .indices_are_32_bit = true}};
                            }
                            return std::optional{jrpgmaker::render::RenderMeshBinding{
                                .vertex_buffer = vertex_buffer,
                                .index_buffer = index_buffer,
                                .stride_bytes = sizeof(SkinnedVertex),
                                .index_count =
                                    static_cast<std::uint32_t>(character_mesh->indices.size()),
                                .indices_are_32_bit = true}};
                        },
                    .resolve_sampled_texture =
                        [&texture_resources](const auto& draw) {
                            return texture_resources.Find(draw.sampled_texture);
                        },
                    .validate_material = [style, material_document](const auto& draw)
                        -> jrpgmaker::render::RenderPlanValidation {
                        if (draw.material == "ui") {
                            return {};
                        }
                        if (draw.material != material_document.value("id", std::string{})) {
                            return jrpgmaker::render::RenderPlanValidation{
                                .ok = false, .error = "render draw references unknown material"};
                        }
                        const auto result =
                            style->ValidateMaterial(material_document["parameters"]);
                        return jrpgmaker::render::RenderPlanValidation{.ok = result.ok,
                                                                       .error = result.error};
                    },
                    .bind_draw_resources =
                        [uniform_buffer](jrpgmaker::rhi::ICommandList& list, const auto& draw,
                                         const auto&) {
                            if (draw.mesh == "character") {
                                list.SetVertexUniformBuffer(uniform_buffer,
                                                            32u * sizeof(glm::mat4));
                            }
                            return jrpgmaker::render::RenderPlanValidation{};
                        }};
                const auto recorded = jrpgmaker::render::RenderPlanExecutor::Record(
                    render_plan, back_buffer, *command_list, resolver, style->Descriptor().budget);
                if (!recorded.ok) {
                    throw std::runtime_error("failed to record render plan: " + recorded.error);
                }
                command_list->End();
                device->Submit(*command_list);
                swapchain->Present();
            });

        std::cout << "jrpgmaker " << jrpgmaker::core::version() << " running\n";
        RunMainLoop(swapchain.get(), stages, input, audio_output, input_bindings);

        // DestroyXxx requires the GPU to be idle (docs/01 lifecycle contract):
        // the last submitted command list and the shared allocator must not be
        // touched while the GPU may still reference them.
        device->WaitForGpuIdle();
        texture_loader.Poll();
        texture_resources.PumpUploads(character_texture_ids.size());
        for (std::size_t i = 0; i < character_texture_ids.size(); ++i) {
            if (character_texture_acquired[i]) {
                (void) texture_resources.Release(character_texture_ids[i]);
                character_texture_acquired[i] = false;
            }
            (void) texture_resources.Unload(character_texture_ids[i]);
        }
        device->DestroyCommandList(command_list);
        device->DestroyBuffer(vertex_buffer);
        device->DestroyBuffer(index_buffer);
        device->DestroyBuffer(uniform_buffer);
        device->DestroyPipeline(pipeline);
        device->DestroyPipeline(textured_pipeline);
        device->DestroyPipeline(accent_pipeline);
        device->DestroyPipeline(ui_pipeline);
        device->DestroyBuffer(ui_vertex_buffer);
        device->DestroyBuffer(ui_index_buffer);

        // Teardown order matters: destroy the swapchain before the window so
        // the Vulkan surface is destroyed while its window still exists.
        swapchain.reset();
        SDL_DestroyWindow(window);
        SDL_Quit();
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "jrpgmaker fatal: " << error.what() << '\n';
        return 1;
    }
}

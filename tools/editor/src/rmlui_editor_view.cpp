#include "jrpgmaker/editor/rmlui_editor_view.hpp"

#include <RmlUi/Core.h>
#include <RmlUi/Core/Elements/ElementFormControlInput.h>
#include <SDL3/SDL.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <format>
#include <initializer_list>
#include <iostream>
#include <limits>
#include <memory>
#include <string>
#include <string_view>
#include <tuple>
#include <unordered_map>
#include <utility>
#include <vector>

#include <glm/vec2.hpp>
#include <glm/vec3.hpp>
#include <glm/vec4.hpp>

#include "jrpgmaker/editor/rmlui_input.hpp"
#include "jrpgmaker/render/ui_draw_adapter.hpp"

namespace jrpgmaker::editor {
namespace {

constexpr std::uint32_t kMaxTextureDimension = 4096;

constexpr std::string_view kLeftCenterSplitter = kSplitterLeftCenterId;
constexpr std::string_view kSceneInspectorSplitter = kSplitterSceneInspectorId;
constexpr std::string_view kDiagnosticsSplitter = kSplitterDiagnosticsId;
constexpr std::string_view kProjectPanelId = "workspace.project";
constexpr std::string_view kHierarchyPanelId = "workspace.hierarchy";
constexpr std::string_view kScenePanelId = "workspace.scene";
constexpr std::string_view kInspectorPanelId = kPanelInspectorId;
constexpr std::string_view kDiagnosticsPanelId = "workspace.diagnostics";
constexpr std::string_view kLeftDockId = "left-dock";
constexpr std::string_view kLeftDockTabsId = "left-dock-tabs";

bool IsWorkspacePanelId(std::string_view id) {
    return id == kProjectPanelId || id == kHierarchyPanelId || id == kScenePanelId ||
           id == kInspectorPanelId || id == kDiagnosticsPanelId;
}

std::string_view PanelElementId(std::string_view panel_id) {
    if (panel_id == kProjectPanelId)
        return "project-panel";
    if (panel_id == kHierarchyPanelId)
        return "hierarchy-panel";
    if (panel_id == kScenePanelId)
        return "scene-panel";
    if (panel_id == kInspectorPanelId)
        return "inspector-panel";
    if (panel_id == "workspace.form")
        return "inspector-panel";
    if (panel_id == kDiagnosticsPanelId)
        return "diagnostics-panel";
    return {};
}

bool IsWorkspacePanelElementId(std::string_view element_id) {
    return element_id == "project-panel" || element_id == "hierarchy-panel" ||
           element_id == "scene-panel" || element_id == "inspector-panel" ||
           element_id == "diagnostics-panel";
}

bool IsSplitterId(std::string_view id) {
    return id == kLeftCenterSplitter || id == kSceneInspectorSplitter || id == kDiagnosticsSplitter;
}

std::string RatioPercent(float ratio) {
    return std::format("{:.6f}%", ratio * 100.0f);
}

std::string RatioAttribute(float ratio) {
    return std::format("{:.4f}", ratio);
}

Rml::Element* FindSplitterAncestor(Rml::Element* element) {
    for (auto* current = element; current != nullptr; current = current->GetParentNode()) {
        if (IsSplitterId(current->GetId().c_str()))
            return current;
    }
    return nullptr;
}

Rml::Element* FindSplitterAtPoint(Rml::Context& context, Rml::ElementDocument& document,
                                  const RmlUiInputPoint& point) {
    const Rml::Vector2f position(static_cast<float>(point.x), static_cast<float>(point.y));
    if (auto* hit = FindSplitterAncestor(context.GetElementAtPoint(position)); hit != nullptr)
        return hit;
    for (const auto id : {kLeftCenterSplitter, kSceneInspectorSplitter, kDiagnosticsSplitter}) {
        auto* splitter = document.GetElementById(Rml::String(id));
        if (splitter == nullptr)
            continue;
        if (splitter->IsPointWithinElement(position))
            return splitter;
        const auto offset = splitter->GetAbsoluteOffset(Rml::BoxArea::Border);
        const auto size = splitter->GetBox().GetSize(Rml::BoxArea::Border);
        if (position.x >= offset.x && position.x <= offset.x + size.x && position.y >= offset.y &&
            position.y <= offset.y + size.y)
            return splitter;
        const bool horizontal = id == kDiagnosticsSplitter;
        const float line = horizontal ? position.y : position.x;
        const float expected = horizontal ? offset.y : offset.x;
        const float slop = std::max(2.0f, horizontal ? size.y : size.x);
        if (std::abs(line - expected) <= slop)
            return splitter;
    }
    return nullptr;
}

Rml::Element* FindWorkspacePanelAtPoint(Rml::Context& context, const RmlUiInputPoint& point) {
    for (auto* current =
             context.GetElementAtPoint({static_cast<float>(point.x), static_cast<float>(point.y)});
         current != nullptr; current = current->GetParentNode()) {
        if (IsWorkspacePanelElementId(current->GetId().c_str()))
            return current;
    }
    return nullptr;
}

int ToRmlModifiers(const RmlUiInputModifiers& modifiers) {
    int result = 0;
    if (modifiers.control)
        result |= Rml::Input::KM_CTRL;
    if (modifiers.shift)
        result |= Rml::Input::KM_SHIFT;
    if (modifiers.alt)
        result |= Rml::Input::KM_ALT;
    if (modifiers.caps_lock)
        result |= Rml::Input::KM_CAPSLOCK;
    if (modifiers.num_lock)
        result |= Rml::Input::KM_NUMLOCK;
    return result;
}

std::optional<Rml::Input::KeyIdentifier> ToRmlKey(std::string_view key) {
    if (key.size() == 1) {
        const char value = key.front();
        if (value >= '0' && value <= '9')
            return static_cast<Rml::Input::KeyIdentifier>(Rml::Input::KI_0 + value - '0');
        if (value >= 'A' && value <= 'Z')
            return static_cast<Rml::Input::KeyIdentifier>(Rml::Input::KI_A + value - 'A');
        if (value >= 'a' && value <= 'z')
            return static_cast<Rml::Input::KeyIdentifier>(Rml::Input::KI_A + value - 'a');
    }
    if (key == "Space")
        return Rml::Input::KI_SPACE;
    if (key == "Backspace")
        return Rml::Input::KI_BACK;
    if (key == "Tab")
        return Rml::Input::KI_TAB;
    if (key == "Enter" || key == "Return")
        return Rml::Input::KI_RETURN;
    if (key == "Escape")
        return Rml::Input::KI_ESCAPE;
    if (key == "Left")
        return Rml::Input::KI_LEFT;
    if (key == "Right")
        return Rml::Input::KI_RIGHT;
    if (key == "Up")
        return Rml::Input::KI_UP;
    if (key == "Down")
        return Rml::Input::KI_DOWN;
    if (key == "Delete")
        return Rml::Input::KI_DELETE;
    if (key == "Home")
        return Rml::Input::KI_HOME;
    if (key == "End")
        return Rml::Input::KI_END;
    if (key.size() >= 2 && key.front() == 'F') {
        int number = 0;
        for (const char digit : key.substr(1)) {
            if (digit < '0' || digit > '9')
                return std::nullopt;
            number = number * 10 + digit - '0';
        }
        if (number >= 1 && number <= 24)
            return static_cast<Rml::Input::KeyIdentifier>(Rml::Input::KI_F1 + number - 1);
    }
    return std::nullopt;
}

glm::vec4 ToColor(const Rml::ColourbPremultiplied& color) {
    constexpr float scale = 1.0f / 255.0f;
    return {static_cast<float>(color.red) * scale, static_cast<float>(color.green) * scale,
            static_cast<float>(color.blue) * scale, static_cast<float>(color.alpha) * scale};
}

glm::vec3 ToNdc(const Rml::Vector2f& position, const Rml::Vector2f& translation,
                std::uint32_t width, std::uint32_t height) {
    const float x = position.x + translation.x;
    const float y = position.y + translation.y;
    return {2.0f * x / static_cast<float>(width) - 1.0f,
            1.0f - 2.0f * y / static_cast<float>(height), 0.0f};
}

Rml::Vector2f SnapTextPositionToPixel(const Rml::Vector2f& position,
                                      const Rml::Vector2f& translation) {
    return {std::round(position.x + translation.x), std::round(position.y + translation.y)};
}

std::string CapturePersistentFocusId(Rml::Context& context, Rml::ElementDocument& document) {
    auto* focused = context.GetFocusElement();
    if (focused == nullptr || focused->GetOwnerDocument() != &document)
        return {};

    for (auto* element = focused; element != nullptr && element->GetOwnerDocument() == &document;
         element = element->GetParentNode()) {
        if (element->IsClassSet("menu-root") || element->IsClassSet("menu-item"))
            return {};
        const auto id = element->GetId();
        if (!id.empty())
            return id.c_str();
    }
    return {};
}

class SystemInterface final : public Rml::SystemInterface {
public:
    double GetElapsedTime() override {
        const auto now = std::chrono::steady_clock::now();
        return std::chrono::duration<double>(now - start_).count();
    }

    bool LogMessage(Rml::Log::Type type, const Rml::String& message) override {
        if (type == Rml::Log::LT_ERROR || type == Rml::Log::LT_ASSERT)
            std::cerr << "rmlui.error\t" << message << '\n';
        return true;
    }

    void SetClipboardText(const Rml::String& text) override {
        (void) SDL_SetClipboardText(text.c_str());
    }

    void GetClipboardText(Rml::String& text) override {
        const char* clipboard = SDL_GetClipboardText();
        text = clipboard == nullptr ? Rml::String{} : Rml::String(clipboard);
        SDL_free(const_cast<char*>(clipboard));
    }

private:
    std::chrono::steady_clock::time_point start_ = std::chrono::steady_clock::now();
};

struct Geometry {
    std::vector<Rml::Vertex> vertices;
    std::vector<std::uint32_t> indices;
};

class RenderInterface final : public Rml::RenderInterface {
public:
    RenderInterface(rhi::IDevice& device, const RmlUiEditorConfig& config, std::uint32_t width,
                    std::uint32_t height)
        : device_(device), config_(config), width_(width), height_(height) {}

    ~RenderInterface() override { ReleaseAll(); }

    void SetDimensions(std::uint32_t width, std::uint32_t height) {
        width_ = width;
        height_ = height;
    }

    [[nodiscard]] bool CreateSampler() {
        // RmlUi generates antialiased glyph atlases. Linear sampling preserves the
        // coverage ramp at glyph edges instead of turning text into enlarged pixels.
        sampler_ =
            device_.CreateSampler({rhi::SamplerFilter::kLinear, rhi::SamplerAddress::kClamp});
        return sampler_ != rhi::SamplerHandle::kInvalid;
    }

    Rml::CompiledGeometryHandle CompileGeometry(Rml::Span<const Rml::Vertex> vertices,
                                                Rml::Span<const int> indices) override {
        if (vertices.empty() || indices.empty() || geometries_ >= config_.max_geometries ||
            vertices.size() > config_.max_vertices_per_geometry ||
            indices.size() > config_.max_indices_per_geometry)
            return 0;
        for (const int index : indices)
            if (index < 0 || static_cast<std::size_t>(index) >= vertices.size())
                return 0;
        auto geometry = std::make_unique<Geometry>();
        geometry->vertices.assign(vertices.begin(), vertices.end());
        geometry->indices.reserve(indices.size());
        for (const int index : indices)
            geometry->indices.push_back(static_cast<std::uint32_t>(index));
        ++geometries_;
        return reinterpret_cast<Rml::CompiledGeometryHandle>(geometry.release());
    }

    void RenderGeometry(Rml::CompiledGeometryHandle handle, Rml::Vector2f translation,
                        Rml::TextureHandle texture) override {
        if (handle == 0 || calls_.size() >= config_.max_geometries)
            return;
        calls_.push_back({reinterpret_cast<Geometry*>(handle), translation, texture});
    }

    void ReleaseGeometry(Rml::CompiledGeometryHandle handle) override {
        if (handle == 0)
            return;
        auto* geometry = reinterpret_cast<Geometry*>(handle);
        DestroyGeometry(*geometry);
        delete geometry;
        if (geometries_ > 0)
            --geometries_;
    }

    Rml::TextureHandle LoadTexture(Rml::Vector2i& texture_dimensions,
                                   const Rml::String& source) override {
        (void) texture_dimensions;
        std::cerr << "rmlui.texture_unsupported\t" << source << '\n';
        return 0;
    }

    Rml::TextureHandle GenerateTexture(Rml::Span<const Rml::byte> source,
                                       Rml::Vector2i dimensions) override {
        if (dimensions.x <= 0 || dimensions.y <= 0 ||
            static_cast<std::uint32_t>(dimensions.x) > kMaxTextureDimension ||
            static_cast<std::uint32_t>(dimensions.y) > kMaxTextureDimension ||
            source.size() < static_cast<std::size_t>(dimensions.x) *
                                static_cast<std::size_t>(dimensions.y) * 4u)
            return 0;
        try {
            const auto texture = device_.CreateTexture(
                {static_cast<std::uint32_t>(dimensions.x), static_cast<std::uint32_t>(dimensions.y),
                 rhi::Format::kR8G8B8A8Unorm, rhi::TextureUsage::kSampled});
            if (texture == rhi::TextureHandle::kInvalid)
                return 0;
            device_.UploadTexture(texture, source.data(),
                                  static_cast<std::uint64_t>(dimensions.x) * 4u);
            textures_.push_back(texture);
            return static_cast<Rml::TextureHandle>(texture);
        } catch (...) {
            return 0;
        }
    }

    void ReleaseTexture(Rml::TextureHandle texture) override {
        if (texture == 0)
            return;
        const auto handle = static_cast<rhi::TextureHandle>(texture);
        const auto it = std::find(textures_.begin(), textures_.end(), handle);
        if (it == textures_.end())
            return;
        device_.DestroyTexture(handle);
        textures_.erase(it);
    }

    void EnableScissorRegion(bool enable) override { scissor_enabled_ = enable; }
    void SetScissorRegion(Rml::Rectanglei region) override { scissor_ = region; }

    void SetTransform(const Rml::Matrix4f* transform) override {
        has_transform_ = transform != nullptr;
    }

    void BeginFrame() {
        for (const auto& buffer : transient_buffers_)
            device_.DestroyBuffer(buffer);
        transient_buffers_.clear();
        calls_.clear();
    }
    [[nodiscard]] std::size_t call_count() const { return calls_.size(); }

    [[nodiscard]] bool Record(rhi::ICommandList& command_list, rhi::PipelineHandle solid_pipeline,
                              rhi::PipelineHandle textured_pipeline) {
        if (width_ == 0 || height_ == 0 || has_transform_)
            return !has_transform_;
        for (const auto& call : calls_) {
            if (call.geometry == nullptr || call.geometry->indices.empty())
                continue;
            std::vector<render::UiVertex> solid;
            std::vector<render::UiTextVertex> textured;
            solid.reserve(call.geometry->vertices.size());
            textured.reserve(call.geometry->vertices.size());
            for (const auto& vertex : call.geometry->vertices) {
                const auto position =
                    call.texture != 0
                        ? ToNdc(SnapTextPositionToPixel(vertex.position, call.translation), {},
                                width_, height_)
                        : ToNdc(vertex.position, call.translation, width_, height_);
                const auto color = ToColor(vertex.colour);
                solid.push_back({position, color});
                textured.push_back({position, {vertex.tex_coord.x, vertex.tex_coord.y}, color});
            }
            const auto vertex_size = call.texture != 0
                                         ? textured.size() * sizeof(render::UiTextVertex)
                                         : solid.size() * sizeof(render::UiVertex);
            const auto vertex_buffer =
                device_.CreateBuffer({vertex_size, rhi::BufferUsage::kVertex});
            const auto index_buffer = device_.CreateBuffer(
                {call.geometry->indices.size() * sizeof(std::uint32_t), rhi::BufferUsage::kIndex});
            if (vertex_buffer == rhi::BufferHandle::kInvalid ||
                index_buffer == rhi::BufferHandle::kInvalid) {
                if (vertex_buffer != rhi::BufferHandle::kInvalid)
                    device_.DestroyBuffer(vertex_buffer);
                if (index_buffer != rhi::BufferHandle::kInvalid)
                    device_.DestroyBuffer(index_buffer);
                return false;
            }
            if (call.texture != 0)
                device_.MapWrite(vertex_buffer, textured.data(), vertex_size);
            else
                device_.MapWrite(vertex_buffer, solid.data(), vertex_size);
            device_.MapWrite(index_buffer, call.geometry->indices.data(),
                             call.geometry->indices.size() * sizeof(std::uint32_t));
            transient_buffers_.push_back(vertex_buffer);
            transient_buffers_.push_back(index_buffer);
            if (call.texture != 0) {
                if (textured_pipeline == rhi::PipelineHandle::kInvalid ||
                    sampler_ == rhi::SamplerHandle::kInvalid)
                    return false;
                command_list.SetPipeline(textured_pipeline);
                command_list.SetSampledTexture(static_cast<rhi::TextureHandle>(call.texture),
                                               sampler_);
                command_list.SetVertexBuffer(vertex_buffer, sizeof(render::UiTextVertex));
            } else {
                if (solid_pipeline == rhi::PipelineHandle::kInvalid)
                    return false;
                command_list.SetPipeline(solid_pipeline);
                command_list.SetVertexBuffer(vertex_buffer, sizeof(render::UiVertex));
            }
            command_list.SetIndexBuffer(index_buffer, true);
            command_list.DrawIndexed(static_cast<std::uint32_t>(call.geometry->indices.size()), 1);
        }
        return true;
    }

private:
    struct RenderCall {
        Geometry* geometry = nullptr;
        Rml::Vector2f translation;
        Rml::TextureHandle texture = 0;
    };

    void DestroyGeometry(Geometry& geometry) {
        geometry.vertices.clear();
        geometry.indices.clear();
    }

    void ReleaseAll() {
        for (const auto buffer : transient_buffers_)
            device_.DestroyBuffer(buffer);
        transient_buffers_.clear();
        for (const auto texture : textures_)
            device_.DestroyTexture(texture);
        textures_.clear();
        if (sampler_ != rhi::SamplerHandle::kInvalid) {
            device_.DestroySampler(sampler_);
            sampler_ = rhi::SamplerHandle::kInvalid;
        }
    }

    rhi::IDevice& device_;
    const RmlUiEditorConfig& config_;
    std::uint32_t width_ = 0;
    std::uint32_t height_ = 0;
    rhi::SamplerHandle sampler_ = rhi::SamplerHandle::kInvalid;
    std::vector<rhi::TextureHandle> textures_;
    std::vector<RenderCall> calls_;
    std::vector<rhi::BufferHandle> transient_buffers_;
    std::uint32_t geometries_ = 0;
    bool scissor_enabled_ = false;
    bool has_transform_ = false;
    Rml::Rectanglei scissor_;
};

class CommandListener final : public Rml::EventListener {
public:
    explicit CommandListener(RmlUiCommandHandler& handler) : handler_(handler) {}

    void ProcessEvent(Rml::Event& event) override {
        const bool click = event.GetId() == Rml::EventId::Click;
        const bool change = event.GetId() == Rml::EventId::Change;
        const bool keydown = event.GetId() == Rml::EventId::Keydown;
        if (keydown && (HandleMenuKey(event) || HandleCommandKey(event)))
            return;
        if (!click && !change)
            return;
        auto* element = event.GetTargetElement();
        if (click && HandleMenuClick(event, element))
            return;
        while (element != nullptr) {
            const auto command = element->GetAttribute<Rml::String>("data-command", {});
            if (!command.empty()) {
                (void) element->Focus(false);
                if (element->GetAttribute<Rml::String>("data-enabled", "true") == "false") {
                    event.StopPropagation();
                    return;
                }
                auto argument = element->GetAttribute<Rml::String>("data-argument", {});
                if (change && command == "project.filter") {
                    if (const auto* input = dynamic_cast<Rml::ElementFormControlInput*>(element))
                        argument = input->GetValue();
                }
                if (handler_)
                    handler_({command, argument});
                event.StopPropagation();
                return;
            }
            element = element->GetParentNode();
        }
    }

private:
    static Rml::Element* FindMenuElement(Rml::Element* target, std::string_view role,
                                         bool require_direct_child) {
        for (auto* element = target; element != nullptr; element = element->GetParentNode()) {
            if (element->GetAttribute<Rml::String>("data-menu-role", {}) != role)
                continue;
            if (!require_direct_child || target == element)
                return element;
            for (auto* child = target; child != nullptr && child != element;
                 child = child->GetParentNode()) {
                if (child->GetParentNode() == element && !child->IsClassSet("menu-popup"))
                    return element;
            }
        }
        return nullptr;
    }

    static void ClearMenuState(Rml::ElementDocument& document) {
        Rml::ElementList roots;
        document.GetElementsByClassName(roots, "menu-root");
        for (auto* root : roots)
            root->SetClass("menu-open", false);

        Rml::ElementList submenus;
        document.GetElementsByClassName(submenus, "submenu-item");
        for (auto* submenu : submenus)
            submenu->SetClass("menu-open", false);
    }

    static bool HandleMenuClick(Rml::Event& event, Rml::Element* target) {
        if (target == nullptr)
            return false;
        auto* document = target->GetOwnerDocument();
        if (document == nullptr)
            return false;

        auto* submenu = FindMenuElement(target, "submenu", true);
        auto* root = FindMenuElement(target, "root", true);
        const bool was_open = (submenu != nullptr && submenu->IsClassSet("menu-open")) ||
                              (root != nullptr && root->IsClassSet("menu-open"));
        ClearMenuState(*document);
        if (submenu != nullptr) {
            (void) submenu->Focus(false);
            if (!was_open)
                submenu->SetClass("menu-open", true);
            for (auto* parent = submenu->GetParentNode(); parent != nullptr;
                 parent = parent->GetParentNode()) {
                if (parent->GetAttribute<Rml::String>("data-menu-role", {}) == "root") {
                    parent->SetClass("menu-open", true);
                    break;
                }
            }
            event.StopPropagation();
            return true;
        }
        if (root != nullptr) {
            (void) target->Focus(false);
            if (!was_open)
                root->SetClass("menu-open", true);
            event.StopPropagation();
            return true;
        }
        return false;
    }

    static bool HandleMenuKey(Rml::Event& event) {
        auto* target = event.GetTargetElement();
        if (target == nullptr)
            return false;
        auto* document = target->GetOwnerDocument();
        if (document == nullptr)
            return false;
        const auto key = event.GetParameter<int>("key_identifier", Rml::Input::KI_UNKNOWN);
        if (key == Rml::Input::KI_ESCAPE) {
            Rml::ElementList roots;
            document->GetElementsByClassName(roots, "menu-root");
            Rml::ElementList submenus;
            document->GetElementsByClassName(submenus, "submenu-item");
            bool open = false;
            for (auto* root : roots)
                open = open || root->IsClassSet("menu-open");
            for (auto* submenu : submenus)
                open = open || submenu->IsClassSet("menu-open");
            if (!open)
                return false;
            ClearMenuState(*document);
            event.StopPropagation();
            return true;
        }
        const auto menu_role = target->GetAttribute<Rml::String>("data-menu-role", {});
        const bool is_root_trigger =
            menu_role == "root" || FindMenuElement(target, "root", true) != nullptr;
        const bool is_submenu_trigger =
            menu_role == "submenu" || FindMenuElement(target, "submenu", true) != nullptr;
        const bool is_menu_item = target->IsClassSet("menu-item");
        if (!is_root_trigger && !is_submenu_trigger && !is_menu_item)
            return false;

        if (key == Rml::Input::KI_RETURN || key == Rml::Input::KI_NUMPADENTER ||
            key == Rml::Input::KI_SPACE) {
            target->Click();
            event.StopPropagation();
            return true;
        }

        if (key != Rml::Input::KI_UP && key != Rml::Input::KI_DOWN && key != Rml::Input::KI_LEFT &&
            key != Rml::Input::KI_RIGHT)
            return false;

        const auto menu_item_enabled = [](Rml::Element* element) {
            return element != nullptr && element->IsClassSet("menu-item") &&
                   element->GetAttribute<Rml::String>("data-enabled", "true") != "false";
        };
        const auto direct_popup = [](Rml::Element* owner) -> Rml::Element* {
            if (owner == nullptr)
                return nullptr;
            for (int index = 0; index < owner->GetNumChildren(); ++index) {
                auto* child = owner->GetChild(index);
                if (child != nullptr && child->IsClassSet("menu-popup"))
                    return child;
            }
            return nullptr;
        };
        const auto focus_popup_item = [menu_item_enabled](Rml::Element* popup, bool last) {
            if (popup == nullptr)
                return false;
            const int count = popup->GetNumChildren();
            if (last) {
                for (int index = count - 1; index >= 0; --index) {
                    auto* item = popup->GetChild(index);
                    if (menu_item_enabled(item))
                        return item->Focus(false);
                }
            } else {
                for (int index = 0; index < count; ++index) {
                    auto* item = popup->GetChild(index);
                    if (menu_item_enabled(item))
                        return item->Focus(false);
                }
            }
            return false;
        };
        const auto move_popup_item = [menu_item_enabled](Rml::Element* item, int direction) {
            auto* popup = item == nullptr ? nullptr : item->GetParentNode();
            if (popup == nullptr)
                return false;
            std::vector<Rml::Element*> items;
            for (int index = 0; index < popup->GetNumChildren(); ++index) {
                auto* candidate = popup->GetChild(index);
                if (menu_item_enabled(candidate))
                    items.push_back(candidate);
            }
            const auto current = std::find(items.begin(), items.end(), item);
            if (current == items.end() || items.empty())
                return false;
            const auto index = static_cast<std::ptrdiff_t>(current - items.begin());
            const auto count = static_cast<std::ptrdiff_t>(items.size());
            const auto next = (index + direction + count) % count;
            return items[static_cast<std::size_t>(next)]->Focus(false);
        };
        const auto root_for = [](Rml::Element* element) -> Rml::Element* {
            for (auto* current = element; current != nullptr; current = current->GetParentNode()) {
                if (current->GetAttribute<Rml::String>("data-menu-role", {}) == "root")
                    return current;
            }
            return nullptr;
        };
        const auto submenu_for = [](Rml::Element* element) -> Rml::Element* {
            for (auto* current = element; current != nullptr; current = current->GetParentNode()) {
                if (current->GetAttribute<Rml::String>("data-menu-role", {}) == "submenu")
                    return current;
            }
            return nullptr;
        };
        const auto focus_root = [&document, &focus_popup_item, &direct_popup](Rml::Element* root,
                                                                              int direction) {
            if (root == nullptr)
                return false;
            Rml::ElementList roots;
            document->GetElementsByClassName(roots, "menu-root");
            const auto current = std::find(roots.begin(), roots.end(), root);
            if (current == roots.end() || roots.empty())
                return false;
            const auto index = static_cast<std::ptrdiff_t>(current - roots.begin());
            const auto count = static_cast<std::ptrdiff_t>(roots.size());
            const auto next = (index + direction + count) % count;
            auto* next_root = roots[static_cast<std::size_t>(next)];
            ClearMenuState(*document);
            next_root->SetClass("menu-open", true);
            return focus_popup_item(direct_popup(next_root), false);
        };

        if (is_root_trigger) {
            auto* root = root_for(target);
            if (key == Rml::Input::KI_LEFT)
                return focus_root(root, -1);
            if (key == Rml::Input::KI_RIGHT)
                return focus_root(root, 1);
            ClearMenuState(*document);
            if (root != nullptr)
                root->SetClass("menu-open", true);
            const bool focused = focus_popup_item(direct_popup(root), key == Rml::Input::KI_UP);
            if (focused)
                event.StopPropagation();
            return focused;
        }

        if (is_submenu_trigger) {
            auto* submenu = submenu_for(target);
            if (key == Rml::Input::KI_LEFT) {
                ClearMenuState(*document);
                auto* root = root_for(submenu);
                if (root != nullptr)
                    root->SetClass("menu-open", true);
                const bool focused = submenu != nullptr && submenu->Focus(false);
                if (focused)
                    event.StopPropagation();
                return focused;
            }
            if (key == Rml::Input::KI_RIGHT || key == Rml::Input::KI_DOWN ||
                key == Rml::Input::KI_UP) {
                ClearMenuState(*document);
                if (auto* root = root_for(submenu); root != nullptr)
                    root->SetClass("menu-open", true);
                if (submenu != nullptr)
                    submenu->SetClass("menu-open", true);
                const bool focused =
                    focus_popup_item(direct_popup(submenu), key == Rml::Input::KI_UP);
                if (focused)
                    event.StopPropagation();
                return focused;
            }
        }

        if (is_menu_item) {
            if (key == Rml::Input::KI_UP || key == Rml::Input::KI_DOWN) {
                const bool focused = move_popup_item(target, key == Rml::Input::KI_DOWN ? 1 : -1);
                if (focused)
                    event.StopPropagation();
                return focused;
            }
            auto* submenu = submenu_for(target);
            if (key == Rml::Input::KI_LEFT && submenu != nullptr) {
                ClearMenuState(*document);
                if (auto* root = root_for(submenu); root != nullptr)
                    root->SetClass("menu-open", true);
                submenu->Focus(false);
                event.StopPropagation();
                return true;
            }
            if (key == Rml::Input::KI_RIGHT && submenu == nullptr) {
                if (auto* root = root_for(target); root != nullptr) {
                    const bool focused = focus_root(root, 1);
                    if (focused)
                        event.StopPropagation();
                    return focused;
                }
            }
        }
        return false;
    }

    static bool HandleCommandKey(Rml::Event& event) {
        auto* target = event.GetTargetElement();
        if (target == nullptr)
            return false;
        const auto key = event.GetParameter<int>("key_identifier", Rml::Input::KI_UNKNOWN);
        const auto target_role = target->GetAttribute<Rml::String>("role", {});
        if (target_role == "tab" && (key == Rml::Input::KI_LEFT || key == Rml::Input::KI_RIGHT)) {
            auto* parent = target->GetParentNode();
            if (parent == nullptr)
                return false;
            std::vector<Rml::Element*> tabs;
            for (int index = 0; index < parent->GetNumChildren(); ++index) {
                auto* candidate = parent->GetChild(index);
                if (candidate != nullptr &&
                    candidate->GetAttribute<Rml::String>("role", {}) == "tab")
                    tabs.push_back(candidate);
            }
            const auto current = std::find(tabs.begin(), tabs.end(), target);
            if (current == tabs.end() || tabs.empty())
                return false;
            const auto index = static_cast<std::ptrdiff_t>(current - tabs.begin());
            const auto count = static_cast<std::ptrdiff_t>(tabs.size());
            const auto next = (index + (key == Rml::Input::KI_RIGHT ? 1 : -1) + count) % count;
            tabs[static_cast<std::size_t>(next)]->Focus(false);
            event.StopPropagation();
            return true;
        }
        if (key != Rml::Input::KI_RETURN && key != Rml::Input::KI_NUMPADENTER &&
            key != Rml::Input::KI_SPACE)
            return false;
        for (auto* element = target; element != nullptr; element = element->GetParentNode()) {
            const auto role = element->GetAttribute<Rml::String>("role", {});
            if (role != "button" && role != "checkbox" && role != "tab" && role != "treeitem")
                continue;
            const auto command = element->GetAttribute<Rml::String>("data-command", {});
            if (command.empty())
                return false;
            if (element->GetAttribute<Rml::String>("data-enabled", "true") == "false") {
                event.StopPropagation();
                return true;
            }
            element->Click();
            event.StopPropagation();
            return true;
        }
        return false;
    }

    RmlUiCommandHandler& handler_;
};

} // namespace

class RmlUiEditorView::Impl final {
public:
    ~Impl() {
        if (rml_initialized && Rml::GetTextInputHandler() == text_input_handler.get())
            Rml::SetTextInputHandler(nullptr);
        if (context != nullptr)
            Rml::RemoveContext(context->GetName());
        if (rml_initialized)
            Rml::Shutdown();
    }

    rhi::IDevice* device = nullptr;
    RmlUiEditorConfig config;
    RmlUiCommandHandler command_handler;
    std::unique_ptr<SystemInterface> system_interface;
    std::unique_ptr<RenderInterface> render_interface;
    std::unique_ptr<RmlUiTextInputHandler> text_input_handler;
    Rml::Context* context = nullptr;
    Rml::ElementDocument* document = nullptr;
    std::unique_ptr<CommandListener> command_listener;
    bool rml_initialized = false;
    bool ready = false;
    std::vector<std::string> diagnostics;
    float density_independent_pixel_ratio = 1.0f;
    std::uint32_t width = 0;
    std::uint32_t height = 0;
    std::unordered_map<std::string, float> splitter_ratios = [] {
        const EditorUserSettings defaults{};
        return std::unordered_map<std::string, float>{
            {std::string(kLeftCenterSplitter), defaults.splitter_left_center},
            {std::string(kSceneInspectorSplitter), defaults.splitter_scene_inspector},
            {std::string(kDiagnosticsSplitter), defaults.splitter_diagnostics},
        };
    }();
    bool project_visible = true;
    bool hierarchy_visible = true;
    bool hierarchy_expanded = true;
    bool inspector_visible = true;
    bool diagnostics_visible = true;
    std::string active_left_panel = kPanelProjectId;
    std::string focused_panel;
    std::string maximized_panel;
    bool maximize_shortcut_down = false;
    std::string active_splitter;
    RmlUiInputPoint last_pointer;
    RmlUiInputPoint drag_start_point;
    float drag_start_ratio = 0.0f;

    bool ActivateLeftPanel(std::string_view panel_id) {
        if (panel_id != kProjectPanelId && panel_id != kHierarchyPanelId)
            return false;
        if (panel_id == kProjectPanelId && !project_visible)
            return false;
        if (panel_id == kHierarchyPanelId && !hierarchy_visible)
            return false;
        active_left_panel = panel_id;
        focused_panel = active_left_panel;
        if (document == nullptr)
            return true;
        (void) ApplySplitterLayout();
        if (auto* tab = document->GetElementById(Rml::String(
                panel_id == kProjectPanelId ? "left-dock-tab-project" : "left-dock-tab-hierarchy")))
            (void) tab->Focus(false);
        return context->Update();
    }

    void NormalizeActiveLeftPanel() {
        if (active_left_panel == kProjectPanelId && project_visible)
            return;
        if (active_left_panel == kHierarchyPanelId && hierarchy_visible)
            return;
        if (project_visible)
            active_left_panel = kProjectPanelId;
        else if (hierarchy_visible)
            active_left_panel = kHierarchyPanelId;
    }

    float Ratio(std::string_view id) const {
        const auto it = splitter_ratios.find(std::string(id));
        return it == splitter_ratios.end() ? 0.0f : it->second;
    }

    float ClampRatio(std::string_view id, float value) const {
        float minimum = 0.0f;
        float maximum = 1.0f;
        if (id == kLeftCenterSplitter) {
            minimum = 0.12f;
            maximum = std::min(0.40f, Ratio(kSceneInspectorSplitter) - 0.20f);
        } else if (id == kSceneInspectorSplitter) {
            minimum = std::max(0.55f, Ratio(kLeftCenterSplitter) + 0.20f);
            maximum = 0.92f;
        } else if (id == kDiagnosticsSplitter) {
            minimum = 0.60f;
            maximum = 0.92f;
        } else {
            return 0.0f;
        }
        return std::clamp(value, minimum, maximum);
    }

    void UpdateFocusedPanelAtPoint() {
        if (document == nullptr)
            return;
        if (auto* panel = FindWorkspacePanelAtPoint(*context, last_pointer); panel != nullptr) {
            const std::string_view id = panel->GetId().c_str();
            if (id == "project-panel")
                focused_panel = kProjectPanelId;
            else if (id == "hierarchy-panel")
                focused_panel = kHierarchyPanelId;
            else if (id == "scene-panel")
                focused_panel = kScenePanelId;
            else if (id == "inspector-panel")
                focused_panel = kInspectorPanelId;
            else if (id == "diagnostics-panel")
                focused_panel = kDiagnosticsPanelId;
        }
    }

    bool ApplyHierarchyExpandedState() {
        if (document == nullptr)
            return false;
        auto* root = document->GetElementById("hierarchy-map");
        auto* children = document->GetElementById("hierarchy-children");
        if (root == nullptr || children == nullptr)
            return true;

        root->SetAttribute("aria-expanded", hierarchy_expanded ? "true" : "false");
        root->SetClass("collapsed", !hierarchy_expanded);
        (void) children->SetProperty("display", Rml::String(hierarchy_expanded ? "block" : "none"));
        bool has_selected_cell = false;
        for (int index = 0; index < children->GetNumChildren(); ++index) {
            auto* child = children->GetChild(index);
            if (child == nullptr || child->GetAttribute<Rml::String>("role", {}) != "treeitem")
                continue;
            const bool selected =
                child->GetAttribute<Rml::String>("aria-selected", "false") == "true";
            has_selected_cell = has_selected_cell || selected;
            child->SetAttribute("tabindex", hierarchy_expanded && selected ? "0" : "-1");
        }
        root->SetAttribute("tabindex", hierarchy_expanded && has_selected_cell ? "-1" : "0");
        return true;
    }

    bool ToggleHierarchy() {
        hierarchy_expanded = !hierarchy_expanded;
        if (!ApplyHierarchyExpandedState())
            return false;
        return context == nullptr || context->Update();
    }

    void ClearMaximizedPanel() { maximized_panel.clear(); }

    bool ApplySplitterLayout() {
        if (document == nullptr)
            return false;
        auto* root = document->GetElementById("editor-root");
        if (root == nullptr) {
            ClearMaximizedPanel();
            return true;
        }
        auto* project = document->GetElementById("project-panel");
        auto* hierarchy = document->GetElementById("hierarchy-panel");
        auto* scene = document->GetElementById("scene-panel");
        auto* inspector = document->GetElementById("inspector-panel");
        auto* diagnostics_panel = document->GetElementById("diagnostics-panel");
        auto* statusbar = document->GetElementById("statusbar");
        if (!maximized_panel.empty() &&
            (PanelElementId(maximized_panel).empty() ||
             document->GetElementById(Rml::String(PanelElementId(maximized_panel))) == nullptr))
            ClearMaximizedPanel();
        if (project == nullptr || hierarchy == nullptr || scene == nullptr ||
            inspector == nullptr || diagnostics_panel == nullptr || statusbar == nullptr) {
            ClearMaximizedPanel();
            return true;
        }

        const auto saved_left_ratio = Ratio(kLeftCenterSplitter);
        const auto saved_inspector_ratio = Ratio(kSceneInspectorSplitter);
        const auto saved_diagnostics_ratio = Ratio(kDiagnosticsSplitter);
        const bool left_visible = project_visible || hierarchy_visible;
        NormalizeActiveLeftPanel();
        const auto left_ratio = left_visible ? saved_left_ratio : 0.0f;
        const auto inspector_ratio = inspector_visible ? saved_inspector_ratio : 1.0f;
        const auto diagnostics_ratio = diagnostics_visible ? saved_diagnostics_ratio : 1.0f;
        const auto set_display = [](Rml::Element& element, bool visible) {
            (void) element.SetProperty("display", Rml::String(visible ? "block" : "none"));
        };
        const bool project_active = project_visible && active_left_panel == kProjectPanelId;
        const bool hierarchy_active = hierarchy_visible && active_left_panel == kHierarchyPanelId;
        set_display(*project, project_active);
        set_display(*hierarchy, hierarchy_active);
        set_display(*inspector, inspector_visible);
        set_display(*diagnostics_panel, diagnostics_visible);
        if (auto* dock = document->GetElementById(Rml::String(kLeftDockId)))
            set_display(*dock, left_visible);
        if (auto* tabs = document->GetElementById(Rml::String(kLeftDockTabsId)))
            set_display(*tabs, left_visible);
        for (const auto& [id, visible, active] :
             std::initializer_list<std::tuple<const char*, bool, bool>>{
                 {"left-dock-tab-project", project_visible, project_active},
                 {"left-dock-tab-hierarchy", hierarchy_visible, hierarchy_active}}) {
            if (auto* tab = document->GetElementById(Rml::String(id)); tab != nullptr) {
                set_display(*tab, visible);
                tab->SetAttribute("aria-selected", active ? "true" : "false");
                tab->SetAttribute("tabindex", active ? "0" : "-1");
                tab->SetClass("active", active);
            }
        }
        const auto set_ratio = [](Rml::Element& element, std::string_view property, float ratio) {
            (void) element.SetProperty(Rml::String(property), Rml::String(RatioPercent(ratio)));
        };
        const auto set_pixels = [](Rml::Element& element, std::string_view property, float pixels) {
            (void) element.SetProperty(Rml::String(property),
                                       Rml::String(std::format("{:.6f}px", pixels)));
        };
        if (!maximized_panel.empty()) {
            auto* target = document->GetElementById(Rml::String(PanelElementId(maximized_panel)));
            if (target == nullptr) {
                ClearMaximizedPanel();
            } else {
                for (auto* panel : {project, hierarchy, scene, inspector, diagnostics_panel})
                    set_display(*panel, panel == target);
                if (auto* dock = document->GetElementById(Rml::String(kLeftDockId)))
                    set_display(*dock, false);
                const auto scene_top = 68.0f;
                const auto status_height = statusbar->GetOffsetHeight();
                const auto content_bottom =
                    std::max(scene_top, static_cast<float>(height) - status_height);
                set_pixels(*target, "left", 0.0f);
                set_pixels(*target, "top", scene_top);
                set_pixels(*target, "width", static_cast<float>(width));
                set_pixels(*target, "height", content_bottom - scene_top);
                for (const auto id :
                     {kLeftCenterSplitter, kSceneInspectorSplitter, kDiagnosticsSplitter}) {
                    if (auto* splitter = document->GetElementById(Rml::String(id)); splitter) {
                        set_display(*splitter, false);
                        splitter->SetClass("dragging", false);
                    }
                }
                return true;
            }
        }
        set_ratio(*project, "left", 0.0f);
        set_ratio(*project, "width", left_ratio);
        set_ratio(*hierarchy, "left", 0.0f);
        set_ratio(*hierarchy, "width", left_ratio);
        set_ratio(*scene, "left", left_ratio);
        set_ratio(*scene, "width", inspector_ratio - left_ratio);
        set_ratio(*inspector, "left", inspector_ratio);
        set_ratio(*inspector, "width", 1.0f - inspector_ratio);
        set_ratio(*diagnostics_panel, "left", left_ratio);
        set_ratio(*diagnostics_panel, "width", 1.0f - left_ratio);
        set_ratio(*diagnostics_panel, "top", diagnostics_ratio);

        const auto scene_top = 68.0f;
        const auto status_height = statusbar->GetOffsetHeight();
        const auto content_bottom = std::max(scene_top, static_cast<float>(height) - status_height);
        const auto diagnostics_top =
            diagnostics_visible ? std::clamp(diagnostics_ratio * static_cast<float>(height),
                                             scene_top, content_bottom)
                                : content_bottom;
        const auto main_height = std::max(0.0f, diagnostics_top - scene_top);
        const auto diagnostics_height =
            std::max(0.0f, static_cast<float>(height) - status_height - diagnostics_top);
        set_pixels(*scene, "height", main_height);
        set_pixels(*inspector, "height", main_height);
        set_pixels(*diagnostics_panel, "height", diagnostics_height);
        const auto tab_height = left_visible ? 30.0f : 0.0f;
        const auto left_height =
            std::max(0.0f, static_cast<float>(height) - scene_top - status_height - tab_height);
        if (left_visible) {
            if (auto* tabs = document->GetElementById(Rml::String(kLeftDockTabsId))) {
                set_pixels(*tabs, "left", 0.0f);
                set_pixels(*tabs, "top", scene_top);
                set_pixels(*tabs, "width", left_ratio * static_cast<float>(width));
                set_pixels(*tabs, "height", tab_height);
                int tab_index = 0;
                const auto visible_tab_count = static_cast<float>(
                    static_cast<int>(project_visible) + static_cast<int>(hierarchy_visible));
                const auto tab_width =
                    left_ratio * static_cast<float>(width) / std::max(1.0f, visible_tab_count);
                for (int index = 0; index < tabs->GetNumChildren(); ++index) {
                    auto* tab = tabs->GetChild(index);
                    if (tab == nullptr || tab->GetAttribute<Rml::String>("role", {}) != "tab")
                        continue;
                    set_pixels(*tab, "top", 0.0f);
                    set_pixels(*tab, "left", tab_width * static_cast<float>(tab_index++));
                    set_pixels(*tab, "width", tab_width);
                }
            }
            auto& active = active_left_panel == kHierarchyPanelId ? *hierarchy : *project;
            set_pixels(active, "top", scene_top + tab_height);
            set_pixels(active, "height", left_height);
        }
        for (const auto id : {kLeftCenterSplitter, kSceneInspectorSplitter, kDiagnosticsSplitter}) {
            const auto ratio = id == kLeftCenterSplitter       ? left_ratio
                               : id == kSceneInspectorSplitter ? inspector_ratio
                                                               : diagnostics_ratio;
            const auto property_name = std::string("--") + std::string(id);
            (void) root->SetProperty(Rml::String(property_name), Rml::String(RatioPercent(ratio)));
            if (auto* splitter = document->GetElementById(Rml::String(id)); splitter != nullptr) {
                const bool splitter_visible = id == kLeftCenterSplitter       ? left_visible
                                              : id == kSceneInspectorSplitter ? inspector_visible
                                                                              : diagnostics_visible;
                set_display(*splitter, splitter_visible);
                if (id == kDiagnosticsSplitter) {
                    set_ratio(*splitter, "left", left_ratio);
                    set_ratio(*splitter, "top", ratio);
                } else {
                    set_ratio(*splitter, "left", ratio);
                }
                splitter->SetAttribute("aria-valuenow", RatioAttribute(ratio));
                splitter->SetClass("dragging", active_splitter == id);
            }
        }
        return true;
    }

    bool UpdateActiveSplitter(const RmlUiInputPoint& point) {
        if (active_splitter.empty() || width == 0 || height == 0)
            return false;
        const float extent = active_splitter == kDiagnosticsSplitter ? static_cast<float>(height)
                                                                     : static_cast<float>(width);
        const float delta = active_splitter == kDiagnosticsSplitter
                                ? static_cast<float>(point.y - drag_start_point.y) / extent
                                : static_cast<float>(point.x - drag_start_point.x) / extent;
        const float next = ClampRatio(active_splitter, drag_start_ratio + delta);
        splitter_ratios[active_splitter] = next;
        (void) ApplySplitterLayout();
        return context->Update();
    }
};

RmlUiEditorView::RmlUiEditorView() : impl_(std::make_unique<Impl>()) {}
RmlUiEditorView::~RmlUiEditorView() = default;

bool RmlUiEditorView::Initialize(rhi::IDevice& device, RmlUiEditorConfig config,
                                 RmlUiCommandHandler command_handler, std::uint32_t width,
                                 std::uint32_t height) {
    impl_->device = &device;
    impl_->width = width;
    impl_->height = height;
    impl_->config = std::move(config);
    impl_->command_handler = std::move(command_handler);
    impl_->system_interface = std::make_unique<SystemInterface>();
    impl_->render_interface =
        std::make_unique<RenderInterface>(device, impl_->config, width, height);
    impl_->text_input_handler = std::make_unique<RmlUiTextInputHandler>();
    Rml::SetSystemInterface(impl_->system_interface.get());
    if (!Rml::Initialise()) {
        impl_->diagnostics.push_back("editor.rmlui.initialise_failed");
        return false;
    }
    impl_->rml_initialized = true;
    Rml::SetTextInputHandler(impl_->text_input_handler.get());
    for (const auto& font_path : impl_->config.font_paths) {
        if (!Rml::LoadFontFace(font_path.generic_string(), "EditorFont",
                               Rml::Style::FontStyle::Normal, Rml::Style::FontWeight::Auto, true))
            impl_->diagnostics.push_back("editor.rmlui.font_load_failed:" + font_path.string());
    }
    if (!impl_->render_interface->CreateSampler()) {
        impl_->diagnostics.push_back("editor.rmlui.sampler_creation_failed");
        return false;
    }
    impl_->context =
        Rml::CreateContext("editor", {static_cast<int>(width), static_cast<int>(height)},
                           impl_->render_interface.get());
    if (impl_->context == nullptr) {
        impl_->diagnostics.push_back("editor.rmlui.context_creation_failed");
        return false;
    }
    impl_->context->SetDefaultScrollBehavior(Rml::ScrollBehavior::Instant, 1.0f);
    impl_->command_handler =
        [this, handler = std::move(impl_->command_handler)](const RmlUiCommand& command) {
            if (command.name == "dock.activate")
                (void) impl_->ActivateLeftPanel(command.argument);
            if (command.name == "hierarchy.toggle_navigation") {
                (void) impl_->ToggleHierarchy();
                return;
            }
            if (handler)
                handler(command);
        };
    impl_->command_listener = std::make_unique<CommandListener>(impl_->command_handler);
    // Listen during capture so dynamically loaded document elements and native
    // form controls share the same command route without relying on bubbling.
    impl_->context->AddEventListener("click", impl_->command_listener.get(), true);
    impl_->context->AddEventListener("change", impl_->command_listener.get(), true);
    impl_->context->AddEventListener("keydown", impl_->command_listener.get(), true);
    impl_->ready = true;
    return true;
}

bool RmlUiEditorView::SetMarkup(std::string_view markup, std::string_view source_url) {
    if (!ready() || markup.empty())
        return false;
    std::string focused_id;
    if (impl_->document != nullptr) {
        focused_id = CapturePersistentFocusId(*impl_->context, *impl_->document);
        impl_->context->UnloadDocument(impl_->document);
        impl_->document = nullptr;
        (void) impl_->context->Update();
    }
    impl_->active_splitter.clear();
    impl_->document =
        impl_->context->LoadDocumentFromMemory(Rml::String(markup), Rml::String(source_url));
    if (impl_->document == nullptr) {
        impl_->ClearMaximizedPanel();
        impl_->diagnostics.push_back("editor.rmlui.document_load_failed");
        return false;
    }
    impl_->document->Show();
    if (!impl_->context->Update())
        return false;
    if (!impl_->ApplySplitterLayout())
        return false;
    if (!impl_->ApplyHierarchyExpandedState())
        return false;
    if (!focused_id.empty()) {
        if (auto* focused = impl_->document->GetElementById(Rml::String(focused_id));
            focused != nullptr)
            (void) focused->Focus(false);
    }
    return impl_->context->Update();
}

void RmlUiEditorView::ImportSettings(const EditorUserSettings& settings) {
    constexpr float kLeftMinimum = 0.12f;
    constexpr float kLeftMaximum = 0.40f;
    constexpr float kSceneMinimum = 0.55f;
    constexpr float kSceneMaximum = 0.92f;
    constexpr float kDiagnosticsMinimum = 0.60f;
    constexpr float kDiagnosticsMaximum = 0.92f;
    EditorUserSettings sanitized;
    if (std::isfinite(settings.splitter_left_center))
        sanitized.splitter_left_center =
            std::clamp(settings.splitter_left_center, kLeftMinimum, kLeftMaximum);
    if (std::isfinite(settings.splitter_scene_inspector))
        sanitized.splitter_scene_inspector =
            std::clamp(settings.splitter_scene_inspector, kSceneMinimum, kSceneMaximum);
    if (std::isfinite(settings.splitter_diagnostics))
        sanitized.splitter_diagnostics =
            std::clamp(settings.splitter_diagnostics, kDiagnosticsMinimum, kDiagnosticsMaximum);
    sanitized.splitter_left_center =
        std::min(sanitized.splitter_left_center, sanitized.splitter_scene_inspector - 0.20f);
    impl_->ClearMaximizedPanel();
    impl_->maximize_shortcut_down = false;
    impl_->splitter_ratios = {
        {std::string(kLeftCenterSplitter), sanitized.splitter_left_center},
        {std::string(kSceneInspectorSplitter), sanitized.splitter_scene_inspector},
        {std::string(kDiagnosticsSplitter), sanitized.splitter_diagnostics},
    };
    impl_->project_visible = settings.project_visible;
    impl_->hierarchy_visible = settings.hierarchy_visible;
    impl_->inspector_visible = settings.inspector_visible;
    impl_->diagnostics_visible = settings.diagnostics_visible;
    impl_->active_left_panel = settings.active_left_panel;
    impl_->NormalizeActiveLeftPanel();
    impl_->active_splitter.clear();
    if (impl_->document != nullptr) {
        (void) impl_->ApplySplitterLayout();
        (void) impl_->context->Update();
    }
}

EditorUserSettings RmlUiEditorView::ExportSettings() const {
    return {.splitter_left_center = impl_->Ratio(kLeftCenterSplitter),
            .splitter_scene_inspector = impl_->Ratio(kSceneInspectorSplitter),
            .splitter_diagnostics = impl_->Ratio(kDiagnosticsSplitter),
            .project_visible = impl_->project_visible,
            .hierarchy_visible = impl_->hierarchy_visible,
            .inspector_visible = impl_->inspector_visible,
            .diagnostics_visible = impl_->diagnostics_visible,
            .active_left_panel = impl_->active_left_panel};
}

bool RmlUiEditorView::TogglePanel(std::string_view panel_id) {
    bool* visibility = nullptr;
    if (panel_id == kPanelProjectId)
        visibility = &impl_->project_visible;
    else if (panel_id == kPanelHierarchyId)
        visibility = &impl_->hierarchy_visible;
    else if (panel_id == kPanelInspectorId)
        visibility = &impl_->inspector_visible;
    else if (panel_id == kPanelDiagnosticsId)
        visibility = &impl_->diagnostics_visible;
    if (visibility == nullptr)
        return false;
    const bool visible_before = *visibility;
    if (impl_->maximized_panel == panel_id)
        impl_->ClearMaximizedPanel();
    *visibility = !*visibility;
    if (!visible_before)
        (void) impl_->ActivateLeftPanel(panel_id);
    else if (panel_id == impl_->active_left_panel) {
        const auto other = panel_id == kPanelProjectId ? kPanelHierarchyId : kPanelProjectId;
        if ((other == kPanelProjectId && impl_->project_visible) ||
            (other == kPanelHierarchyId && impl_->hierarchy_visible))
            impl_->active_left_panel = other;
    }
    impl_->NormalizeActiveLeftPanel();
    if (impl_->document != nullptr) {
        (void) impl_->ApplySplitterLayout();
        (void) impl_->context->Update();
    }
    return true;
}

std::string_view RmlUiEditorView::focused_panel() const {
    return impl_->focused_panel;
}

std::string_view RmlUiEditorView::maximized_panel() const {
    return impl_->maximized_panel;
}

bool RmlUiEditorView::panel_maximized() const {
    return !impl_->maximized_panel.empty();
}

bool RmlUiEditorView::ToggleFocusedPanelMaximize() {
    if (!ready() || impl_->document == nullptr)
        return false;
    if (!impl_->maximized_panel.empty()) {
        impl_->ClearMaximizedPanel();
        (void) impl_->ApplySplitterLayout();
        return impl_->context->Update();
    }
    if (!IsWorkspacePanelId(impl_->focused_panel))
        return false;
    bool visible = impl_->focused_panel == kScenePanelId;
    if (impl_->focused_panel == kProjectPanelId)
        visible = impl_->project_visible;
    else if (impl_->focused_panel == kHierarchyPanelId)
        visible = impl_->hierarchy_visible;
    else if (impl_->focused_panel == kInspectorPanelId)
        visible = impl_->inspector_visible;
    else if (impl_->focused_panel == kDiagnosticsPanelId)
        visible = impl_->diagnostics_visible;
    if (!visible || impl_->document->GetElementById(
                        Rml::String(PanelElementId(impl_->focused_panel))) == nullptr)
        return false;
    impl_->maximized_panel = impl_->focused_panel;
    (void) impl_->ApplySplitterLayout();
    return impl_->context->Update();
}

std::optional<RmlUiElementLayout> RmlUiEditorView::panel_layout(std::string_view id) const {
    if (!ready() || impl_->document == nullptr)
        return std::nullopt;
    const auto element_id = PanelElementId(id);
    auto* element =
        impl_->document->GetElementById(Rml::String(element_id.empty() ? id : element_id));
    if (element == nullptr)
        return std::nullopt;
    const auto offset = element->GetAbsoluteOffset(Rml::BoxArea::Border);
    const auto size = element->GetBox().GetSize(Rml::BoxArea::Border);
    return RmlUiElementLayout{offset.x, offset.y, size.x, size.y, element->IsVisible(true)};
}

bool RmlUiEditorView::panel_visible(std::string_view panel_id) const {
    if (panel_id == kPanelProjectId)
        return impl_->project_visible;
    if (panel_id == kPanelHierarchyId)
        return impl_->hierarchy_visible;
    if (panel_id == kPanelInspectorId)
        return impl_->inspector_visible;
    if (panel_id == kPanelDiagnosticsId)
        return impl_->diagnostics_visible;
    return panel_id == "workspace.scene";
}

void RmlUiEditorView::ImportSplitterRatios(const EditorUserSettings& settings) {
    ImportSettings(settings);
}

EditorUserSettings RmlUiEditorView::ExportSplitterRatios() const {
    return ExportSettings();
}

bool RmlUiEditorView::Resize(std::uint32_t width, std::uint32_t height) {
    if (!ready() || width == 0 || height == 0)
        return false;
    impl_->width = width;
    impl_->height = height;
    impl_->active_splitter.clear();
    impl_->context->SetDimensions({static_cast<int>(width), static_cast<int>(height)});
    impl_->render_interface->SetDimensions(width, height);
    if (!impl_->context->Update())
        return false;
    (void) impl_->ApplySplitterLayout();
    return impl_->context->Update();
}

bool RmlUiEditorView::SetDensityIndependentPixelRatio(float ratio) {
    if (!ready() || !(ratio > 0.0f) || !std::isfinite(ratio))
        return false;
    impl_->density_independent_pixel_ratio = ratio;
    impl_->context->SetDensityIndependentPixelRatio(ratio);
    return impl_->context->Update();
}

bool RmlUiEditorView::Update() {
    return ready() && impl_->context->Update();
}

bool RmlUiEditorView::hierarchy_expanded() const {
    return impl_->hierarchy_expanded;
}

bool RmlUiEditorView::ToggleHierarchy() {
    return ready() && impl_->ToggleHierarchy();
}

bool RmlUiEditorView::Render() {
    if (!ready())
        return false;
    impl_->render_interface->BeginFrame();
    return impl_->context->Render();
}

bool RmlUiEditorView::Record(rhi::ICommandList& command_list, rhi::PipelineHandle solid_pipeline,
                             rhi::PipelineHandle textured_pipeline) {
    return ready() &&
           impl_->render_interface->Record(command_list, solid_pipeline, textured_pipeline);
}

bool RmlUiEditorView::ProcessMouseMove(float x, float y, const RmlUiInputModifiers& modifiers) {
    const auto point = ToRmlUiInputPoint(x, y, impl_->density_independent_pixel_ratio);
    impl_->last_pointer = point;
    if (!impl_->active_splitter.empty())
        return impl_->UpdateActiveSplitter(point);
    return !ready() ||
           impl_->context->ProcessMouseMove(point.x, point.y, ToRmlModifiers(modifiers));
}

bool RmlUiEditorView::ProcessMouseButtonDown(int button, const RmlUiInputModifiers& modifiers) {
    if (ready() && button == 0) {
        (void) impl_->context->ProcessMouseMove(impl_->last_pointer.x, impl_->last_pointer.y,
                                                ToRmlModifiers(modifiers));
        impl_->UpdateFocusedPanelAtPoint();
        auto* splitter =
            FindSplitterAtPoint(*impl_->context, *impl_->document, impl_->last_pointer);
        if (splitter != nullptr) {
            impl_->active_splitter = splitter->GetId().c_str();
            impl_->drag_start_point = impl_->last_pointer;
            impl_->drag_start_ratio = impl_->Ratio(impl_->active_splitter);
            splitter->SetClass("dragging", true);
            (void) splitter->Focus(false);
            return true;
        }
    }
    return !ready() || impl_->context->ProcessMouseButtonDown(button, ToRmlModifiers(modifiers));
}

bool RmlUiEditorView::ProcessMouseButtonDown(float x, float y, int button,
                                             const RmlUiInputModifiers& modifiers) {
    impl_->last_pointer = ToRmlUiInputPoint(x, y, impl_->density_independent_pixel_ratio);
    return ProcessMouseButtonDown(button, modifiers);
}

bool RmlUiEditorView::ProcessMouseButtonUp(int button, const RmlUiInputModifiers& modifiers) {
    if (button == 0 && !impl_->active_splitter.empty()) {
        impl_->active_splitter.clear();
        (void) impl_->ApplySplitterLayout();
        return true;
    }
    return !ready() || impl_->context->ProcessMouseButtonUp(button, ToRmlModifiers(modifiers));
}

bool RmlUiEditorView::ProcessMouseWheel(float x, float y, const RmlUiInputModifiers& modifiers) {
    return !ready() || impl_->context->ProcessMouseWheel({x, y}, ToRmlModifiers(modifiers));
}

bool RmlUiEditorView::ProcessMouseLeave() {
    return !ready() || impl_->context->ProcessMouseLeave();
}

bool RmlUiEditorView::ProcessKeyDown(std::string_view key, const RmlUiInputModifiers& modifiers) {
    if (key == "Space" && modifiers.shift && !modifiers.control && !modifiers.alt) {
        if (!impl_->maximize_shortcut_down) {
            impl_->maximize_shortcut_down = true;
            (void) ToggleFocusedPanelMaximize();
        }
        return false;
    }
    const auto identifier = ToRmlKey(key);
    return !ready() || !identifier ||
           impl_->context->ProcessKeyDown(*identifier, ToRmlModifiers(modifiers));
}

bool RmlUiEditorView::ProcessKeyUp(std::string_view key, const RmlUiInputModifiers& modifiers) {
    (void) modifiers;
    if (key == "Space" && impl_->maximize_shortcut_down) {
        impl_->maximize_shortcut_down = false;
        return false;
    }
    const auto identifier = ToRmlKey(key);
    return !ready() || !identifier ||
           impl_->context->ProcessKeyUp(*identifier, ToRmlModifiers(modifiers));
}

bool RmlUiEditorView::ProcessTextInput(std::string_view text) {
    return !ready() || impl_->context->ProcessTextInput(Rml::String(text));
}

bool RmlUiEditorView::ProcessTextEditing(std::string_view text, int start, int length) {
    if (!ready())
        return true;
    impl_->text_input_handler->HandleEdit(text, start, length);
    return true;
}

bool RmlUiEditorView::ready() const {
    return impl_->ready;
}
float RmlUiEditorView::splitter_ratio(std::string_view id) const {
    return impl_->Ratio(id);
}
const std::vector<std::string>& RmlUiEditorView::diagnostics() const {
    return impl_->diagnostics;
}

} // namespace jrpgmaker::editor

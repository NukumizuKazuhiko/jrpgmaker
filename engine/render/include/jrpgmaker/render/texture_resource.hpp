#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <unordered_map>

#include "jrpgmaker/render/style.hpp"
#include "jrpgmaker/rhi/device.hpp"

namespace jrpgmaker::render {

struct TextureResourceRegistration {
    std::string id;
    std::uint32_t width = 0;
    std::uint32_t height = 0;
    std::span<const std::uint8_t> rgba8;
    rhi::SamplerDesc sampler;
};

struct TextureResourceRegistrationResult {
    bool ok = false;
    std::string error;
    explicit operator bool() const { return ok; }
};

// Owns sampled GPU resources created from tightly packed RGBA8 pixels. The
// caller must keep the device alive and idle before this object is destroyed.
class TextureResourceService {
public:
    explicit TextureResourceService(rhi::IDevice& device) : device_(device) {}
    ~TextureResourceService();

    TextureResourceService(const TextureResourceService&) = delete;
    TextureResourceService& operator=(const TextureResourceService&) = delete;

    [[nodiscard]] TextureResourceRegistrationResult
    Register(const TextureResourceRegistration& registration);
    [[nodiscard]] std::optional<RenderSampledTextureBinding> Find(std::string_view id) const;
    [[nodiscard]] std::size_t size() const { return resources_.size(); }

private:
    struct Resource {
        rhi::TextureHandle texture = rhi::TextureHandle::kInvalid;
        rhi::SamplerHandle sampler = rhi::SamplerHandle::kInvalid;
    };

    rhi::IDevice& device_;
    std::unordered_map<std::string, Resource> resources_;
};

} // namespace jrpgmaker::render

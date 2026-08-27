#include "jrpgmaker/render/texture_resource.hpp"

#include <exception>
#include <limits>

namespace jrpgmaker::render {

TextureResourceService::~TextureResourceService() {
    for (const auto& [id, resource] : resources_) {
        (void) id;
        if (resource.sampler != rhi::SamplerHandle::kInvalid) {
            device_.DestroySampler(resource.sampler);
        }
        if (resource.texture != rhi::TextureHandle::kInvalid) {
            device_.DestroyTexture(resource.texture);
        }
    }
}

TextureResourceRegistrationResult
TextureResourceService::Register(const TextureResourceRegistration& registration) {
    if (registration.id.empty()) {
        return {.ok = false, .error = "texture resource id must not be empty"};
    }
    if (registration.width == 0 || registration.height == 0) {
        return {.ok = false, .error = "texture resource dimensions must be positive"};
    }
    if (resources_.contains(registration.id)) {
        return {.ok = false, .error = "texture resource id is already registered"};
    }
    constexpr std::size_t kBytesPerPixel = 4;
    if (registration.width >
        std::numeric_limits<std::size_t>::max() / kBytesPerPixel / registration.height) {
        return {.ok = false, .error = "texture resource dimensions overflow"};
    }
    const std::size_t expected_bytes = static_cast<std::size_t>(registration.width) *
                                       static_cast<std::size_t>(registration.height) *
                                       kBytesPerPixel;
    if (registration.rgba8.size() != expected_bytes) {
        return {.ok = false, .error = "texture resource must contain tightly packed RGBA8 pixels"};
    }

    Resource resource;
    try {
        resource.texture =
            device_.CreateTexture(rhi::TextureDesc{.width = registration.width,
                                                   .height = registration.height,
                                                   .format = rhi::Format::kR8G8B8A8Unorm,
                                                   .usage = rhi::TextureUsage::kSampled});
        if (resource.texture == rhi::TextureHandle::kInvalid) {
            return {.ok = false, .error = "failed to create sampled texture"};
        }
        device_.UploadTexture(resource.texture, registration.rgba8.data(),
                              static_cast<std::uint64_t>(registration.width) * kBytesPerPixel);
        resource.sampler = device_.CreateSampler(registration.sampler);
        if (resource.sampler == rhi::SamplerHandle::kInvalid) {
            device_.DestroyTexture(resource.texture);
            return {.ok = false, .error = "failed to create texture sampler"};
        }
    } catch (const std::exception& exception) {
        if (resource.sampler != rhi::SamplerHandle::kInvalid) {
            device_.DestroySampler(resource.sampler);
        }
        if (resource.texture != rhi::TextureHandle::kInvalid) {
            device_.DestroyTexture(resource.texture);
        }
        return {.ok = false,
                .error = std::string("texture resource upload failed: ") + exception.what()};
    }

    resources_.emplace(registration.id, resource);
    return {.ok = true, .error = {}};
}

std::optional<RenderSampledTextureBinding> TextureResourceService::Find(std::string_view id) const {
    const auto it = resources_.find(std::string(id));
    if (it == resources_.end()) {
        return std::nullopt;
    }
    return RenderSampledTextureBinding{.texture = it->second.texture,
                                       .sampler = it->second.sampler};
}

} // namespace jrpgmaker::render

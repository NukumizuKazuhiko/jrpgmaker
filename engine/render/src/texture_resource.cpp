#include "jrpgmaker/render/texture_resource.hpp"

#include <exception>
#include <limits>
#include <utility>

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

TextureResourceRegistrationResult TextureResourceService::Validate(std::string_view id,
                                                                   std::uint32_t width,
                                                                   std::uint32_t height,
                                                                   std::size_t rgba8_size) const {
    if (id.empty()) {
        return {.ok = false, .error = "texture resource id must not be empty"};
    }
    if (width == 0 || height == 0) {
        return {.ok = false, .error = "texture resource dimensions must be positive"};
    }
    constexpr std::size_t kBytesPerPixel = 4;
    if (width > std::numeric_limits<std::size_t>::max() / kBytesPerPixel / height) {
        return {.ok = false, .error = "texture resource dimensions overflow"};
    }
    const std::size_t expected_bytes = static_cast<std::size_t>(width) * height * kBytesPerPixel;
    if (rgba8_size != expected_bytes) {
        return {.ok = false, .error = "texture resource must contain tightly packed RGBA8 pixels"};
    }
    if (expected_bytes > budget_.max_resident_bytes) {
        return {.ok = false, .error = "texture resource exceeds resident byte budget"};
    }
    return {.ok = true, .error = {}};
}

void TextureResourceService::DestroyResource(Resource& resource) {
    if (resource.sampler != rhi::SamplerHandle::kInvalid) {
        device_.DestroySampler(resource.sampler);
        resource.sampler = rhi::SamplerHandle::kInvalid;
    }
    if (resource.texture != rhi::TextureHandle::kInvalid) {
        device_.DestroyTexture(resource.texture);
        resource.texture = rhi::TextureHandle::kInvalid;
    }
}

TextureResourceRegistrationResult
TextureResourceService::UploadNow(std::string_view id, std::uint32_t width, std::uint32_t height,
                                  std::span<const std::uint8_t> rgba8,
                                  const rhi::SamplerDesc& sampler) {
    const auto validation = Validate(id, width, height, rgba8.size());
    if (!validation) {
        return validation;
    }
    const std::size_t bytes = rgba8.size();
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (resident_bytes_ > budget_.max_resident_bytes - bytes) {
            return {.ok = false, .error = "texture resource resident byte budget exhausted"};
        }
    }

    Resource resource;
    try {
        resource.texture = device_.CreateTexture(rhi::TextureDesc{
            .width = width,
            .height = height,
            .format = rhi::Format::kR8G8B8A8Unorm,
            .usage = rhi::TextureUsage::kSampled,
        });
        if (resource.texture == rhi::TextureHandle::kInvalid) {
            return {.ok = false, .error = "failed to create sampled texture"};
        }
        device_.UploadTexture(resource.texture, rgba8.data(),
                              static_cast<std::uint64_t>(width) * 4u);
        resource.sampler = device_.CreateSampler(sampler);
        if (resource.sampler == rhi::SamplerHandle::kInvalid) {
            DestroyResource(resource);
            return {.ok = false, .error = "failed to create texture sampler"};
        }
    } catch (const std::exception& exception) {
        DestroyResource(resource);
        return {.ok = false,
                .error = std::string("texture resource upload failed: ") + exception.what()};
    }

    std::lock_guard<std::mutex> lock(mutex_);
    auto [it, inserted] = resources_.try_emplace(std::string(id));
    if (!inserted && it->second.state == TextureResourceState::kReady) {
        DestroyResource(resource);
        return {.ok = false, .error = "texture resource id is already registered"};
    }
    if (!inserted) {
        resident_bytes_ -= it->second.resident_bytes;
        DestroyResource(it->second);
    }
    it->second = std::move(resource);
    it->second.state = TextureResourceState::kReady;
    it->second.resident_bytes = bytes;
    it->second.error.clear();
    resident_bytes_ += bytes;
    return {.ok = true, .error = {}};
}

TextureResourceRegistrationResult
TextureResourceService::Register(const TextureResourceRegistration& registration) {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (resources_.contains(registration.id)) {
            return {.ok = false, .error = "texture resource id is already registered"};
        }
    }
    return UploadNow(registration.id, registration.width, registration.height, registration.rgba8,
                     registration.sampler);
}

TextureResourceQueueResult TextureResourceService::QueueUpload(TextureResourceUpload upload) {
    const auto validation = Validate(upload.id, upload.width, upload.height, upload.rgba8.size());
    if (!validation) {
        return {.accepted = false, .merged = false, .error = validation.error};
    }
    std::lock_guard<std::mutex> lock(mutex_);
    const auto it = resources_.find(upload.id);
    if (it != resources_.end() && it->second.state == TextureResourceState::kReady) {
        return {.accepted = true, .merged = true, .error = {}};
    }
    if (it != resources_.end() && it->second.state == TextureResourceState::kLoading) {
        return {.accepted = true, .merged = true, .error = {}};
    }
    Resource& resource = resources_[upload.id];
    resource.state = TextureResourceState::kLoading;
    resource.error.clear();
    pending_uploads_.push_back(std::move(upload));
    return {.accepted = true, .merged = false, .error = {}};
}

TextureResourceQueueResult TextureResourceService::RecordFailure(std::string id,
                                                                 std::string error) {
    if (id.empty()) {
        return {
            .accepted = false, .merged = false, .error = "texture resource id must not be empty"};
    }
    if (error.empty()) {
        error = "texture resource failed without a diagnostic";
    }
    std::lock_guard<std::mutex> lock(mutex_);
    Resource& resource = resources_[std::move(id)];
    if (resource.state == TextureResourceState::kReady) {
        return {
            .accepted = false, .merged = false, .error = "cannot fail a ready texture resource"};
    }
    resource.state = TextureResourceState::kFailed;
    resource.error = std::move(error);
    return {.accepted = true, .merged = false, .error = {}};
}

std::size_t TextureResourceService::PumpUploads(std::size_t max_uploads) {
    std::size_t uploaded = 0;
    while (uploaded < max_uploads) {
        TextureResourceUpload upload;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            if (pending_uploads_.empty()) {
                break;
            }
            upload = std::move(pending_uploads_.front());
            pending_uploads_.pop_front();
        }
        const auto result =
            UploadNow(upload.id, upload.width, upload.height, upload.rgba8, upload.sampler);
        if (!result) {
            std::lock_guard<std::mutex> lock(mutex_);
            auto it = resources_.find(upload.id);
            if (it != resources_.end()) {
                it->second.state = TextureResourceState::kFailed;
                it->second.error = result.error;
            }
        }
        ++uploaded;
    }
    return uploaded;
}

std::optional<RenderSampledTextureBinding> TextureResourceService::Find(std::string_view id) const {
    std::lock_guard<std::mutex> lock(mutex_);
    const auto it = resources_.find(std::string(id));
    if (it == resources_.end() || it->second.state != TextureResourceState::kReady) {
        return std::nullopt;
    }
    return RenderSampledTextureBinding{.texture = it->second.texture,
                                       .sampler = it->second.sampler};
}

std::optional<TextureResourceStatus> TextureResourceService::Status(std::string_view id) const {
    std::lock_guard<std::mutex> lock(mutex_);
    const auto it = resources_.find(std::string(id));
    if (it == resources_.end()) {
        return std::nullopt;
    }
    return TextureResourceStatus{.state = it->second.state,
                                 .resident_bytes = it->second.resident_bytes,
                                 .reference_count = it->second.reference_count,
                                 .error = it->second.error};
}

bool TextureResourceService::Acquire(std::string_view id) {
    std::lock_guard<std::mutex> lock(mutex_);
    const auto it = resources_.find(std::string(id));
    if (it == resources_.end() || it->second.state != TextureResourceState::kReady) {
        return false;
    }
    ++it->second.reference_count;
    return true;
}

bool TextureResourceService::Release(std::string_view id) {
    std::lock_guard<std::mutex> lock(mutex_);
    const auto it = resources_.find(std::string(id));
    if (it == resources_.end() || it->second.reference_count == 0) {
        return false;
    }
    --it->second.reference_count;
    return true;
}

bool TextureResourceService::Unload(std::string_view id) {
    std::lock_guard<std::mutex> lock(mutex_);
    const auto it = resources_.find(std::string(id));
    if (it == resources_.end() || it->second.reference_count != 0 ||
        it->second.state == TextureResourceState::kLoading) {
        return false;
    }
    resident_bytes_ -= it->second.resident_bytes;
    DestroyResource(it->second);
    it->second = Resource{};
    return true;
}

std::size_t TextureResourceService::pending_count() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return pending_uploads_.size();
}

std::size_t TextureResourceService::size() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return resources_.size();
}

std::size_t TextureResourceService::resident_bytes() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return resident_bytes_;
}

} // namespace jrpgmaker::render

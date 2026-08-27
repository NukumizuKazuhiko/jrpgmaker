#pragma once

#include <cstddef>
#include <cstdint>
#include <deque>
#include <mutex>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

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

enum class TextureResourceState : std::uint8_t {
    kUnloaded,
    kLoading,
    kReady,
    kFailed,
};

struct TextureResourceBudget {
    // A finite default prevents an unbounded stream of decoded pixels from
    // becoming resident. Zero means no resource may be registered.
    std::size_t max_resident_bytes = 64u * 1024u * 1024u;
};

struct TextureResourceUpload {
    std::string id;
    std::uint32_t width = 0;
    std::uint32_t height = 0;
    std::vector<std::uint8_t> rgba8;
    rhi::SamplerDesc sampler;
};

struct TextureResourceQueueResult {
    bool accepted = false;
    bool merged = false;
    std::string error;
    explicit operator bool() const { return accepted; }
};

struct TextureResourceStatus {
    TextureResourceState state = TextureResourceState::kUnloaded;
    std::size_t resident_bytes = 0;
    std::size_t reference_count = 0;
    std::string error;
};

// Owns sampled GPU resources created from tightly packed RGBA8 pixels. The
// caller must keep the device alive and idle before this object is destroyed.
class TextureResourceService {
public:
    explicit TextureResourceService(rhi::IDevice& device, TextureResourceBudget budget = {})
        : device_(device), budget_(budget) {}
    ~TextureResourceService();

    TextureResourceService(const TextureResourceService&) = delete;
    TextureResourceService& operator=(const TextureResourceService&) = delete;

    [[nodiscard]] TextureResourceRegistrationResult
    Register(const TextureResourceRegistration& registration);

    // Thread-safe CPU-side enqueue. The upload owns its decoded pixels after
    // this call. GPU creation and upload happen only in PumpUploads, which
    // must run on the thread that owns the RHI device.
    [[nodiscard]] TextureResourceQueueResult QueueUpload(TextureResourceUpload upload);

    // Commits at most max_uploads queued uploads on the calling (GPU-owning)
    // thread. A zero limit means no work; callers must use an explicit finite
    // limit to preserve frame-time backpressure.
    std::size_t PumpUploads(std::size_t max_uploads);

    [[nodiscard]] std::optional<RenderSampledTextureBinding> Find(std::string_view id) const;
    [[nodiscard]] std::optional<TextureResourceStatus> Status(std::string_view id) const;
    [[nodiscard]] bool Acquire(std::string_view id);
    [[nodiscard]] bool Release(std::string_view id);
    [[nodiscard]] bool Unload(std::string_view id);
    [[nodiscard]] std::size_t size() const;
    [[nodiscard]] std::size_t pending_count() const;
    [[nodiscard]] std::size_t resident_bytes() const;

private:
    struct Resource {
        rhi::TextureHandle texture = rhi::TextureHandle::kInvalid;
        rhi::SamplerHandle sampler = rhi::SamplerHandle::kInvalid;
        TextureResourceState state = TextureResourceState::kUnloaded;
        std::size_t resident_bytes = 0;
        std::size_t reference_count = 0;
        std::string error;
    };

    [[nodiscard]] TextureResourceRegistrationResult Validate(std::string_view id,
                                                             std::uint32_t width,
                                                             std::uint32_t height,
                                                             std::size_t rgba8_size) const;
    [[nodiscard]] TextureResourceRegistrationResult
    UploadNow(std::string_view id, std::uint32_t width, std::uint32_t height,
              std::span<const std::uint8_t> rgba8, const rhi::SamplerDesc& sampler);
    void DestroyResource(Resource& resource);

    rhi::IDevice& device_;
    TextureResourceBudget budget_;
    mutable std::mutex mutex_;
    std::deque<TextureResourceUpload> pending_uploads_;
    std::unordered_map<std::string, Resource> resources_;
    std::size_t resident_bytes_ = 0;
};

} // namespace jrpgmaker::render

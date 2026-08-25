#pragma once

#include <cstdint>
#include <memory>
#include <unordered_map>
#include <vector>

#include <volk.h>

#include "jrpgmaker/rhi/command_list.hpp"
#include "jrpgmaker/rhi/device.hpp"

namespace jrpgmaker::rhi::vulkan {

VkFormat ToNativeFormat(Format format);

class VulkanDevice;
class VulkanSwapchain;

class VulkanCommandList final : public ICommandList {
public:
    explicit VulkanCommandList(VulkanDevice* owner);
    ~VulkanCommandList() override = default;

    void Begin() override;
    void End() override;
    void BeginRendering(TextureHandle color_target, const ClearColor& clear_color) override;
    void EndRendering() override;
    void SetPipeline(PipelineHandle handle) override;
    void Draw(std::uint32_t vertex_count, std::uint32_t instance_count) override;
    void SetVertexBuffer(BufferHandle handle, std::uint32_t stride_bytes) override;
    void SetIndexBuffer(BufferHandle handle, bool indices_are_32_bit) override;
    void DrawIndexed(std::uint32_t index_count, std::uint32_t instance_count) override;
    void SetPushConstants(const void* data, std::uint32_t size_bytes) override;
    void SetSampledTexture(TextureHandle texture, SamplerHandle sampler) override;

    VkCommandBuffer Native() { return command_buffer_; }

    void CopyTextureToReadBack(VkImage source, VkBuffer staging, VkExtent3D extent);
    void CopyBufferToTexture(VkBuffer staging, VkImage destination, VkExtent3D extent);

private:
    VulkanDevice* owner_ = nullptr;
    VkImage rendering_image_ = VK_NULL_HANDLE;
    VkDevice device_ = VK_NULL_HANDLE;
    VkCommandBuffer command_buffer_ = VK_NULL_HANDLE;
    PipelineHandle bound_pipeline_ = PipelineHandle::kInvalid;
};

class VulkanDevice final : public IDevice {
public:
    static std::unique_ptr<IDevice> Create();

    ~VulkanDevice() override;

    TextureHandle CreateTexture(const TextureDesc& desc) override;
    void DestroyTexture(TextureHandle handle) override;
    SamplerHandle CreateSampler(const SamplerDesc& desc) override;
    void DestroySampler(SamplerHandle handle) override;
    void UploadTexture(TextureHandle handle, const void* data,
                       std::uint64_t row_pitch_bytes) override;
    BufferHandle CreateBuffer(const BufferDesc& desc) override;
    void DestroyBuffer(BufferHandle handle) override;
    void MapWrite(BufferHandle handle, const void* data, std::uint64_t size_bytes) override;
    PipelineHandle CreatePipeline(const GraphicsPipelineDesc& desc) override;
    void DestroyPipeline(PipelineHandle handle) override;

    ICommandList* CreateCommandList() override;
    void DestroyCommandList(ICommandList* command_list) override;

    ISwapchain* CreateSwapchain(void* native_window_handle, std::uint32_t width,
                                std::uint32_t height, Format format) override;
    void DestroySwapchain(ISwapchain* swapchain) override;

    void Submit(ICommandList& command_list) override;
    void WaitForGpuIdle() override;

    MappedTexture MapReadBack(TextureHandle handle) override;

private:
    friend class VulkanCommandList;
    friend class VulkanSwapchain;

    struct TextureEntry {
        VkImage image = VK_NULL_HANDLE;
        VkDeviceMemory memory = VK_NULL_HANDLE;
        VkImageView view = VK_NULL_HANDLE;
        VkFormat format = VK_FORMAT_UNDEFINED;
        VkImageLayout layout = VK_IMAGE_LAYOUT_UNDEFINED;
        std::uint32_t width = 0;
        std::uint32_t height = 0;
        bool is_swapchain = false;
    };

    struct SamplerEntry {
        VkSampler sampler = VK_NULL_HANDLE;
    };

    struct ReadBackEntry {
        VkBuffer buffer = VK_NULL_HANDLE;
        VkDeviceMemory memory = VK_NULL_HANDLE;
        std::byte* mapped = nullptr;
        VkDeviceSize size = 0;
    };

    struct BufferEntry {
        VkBuffer buffer = VK_NULL_HANDLE;
        VkDeviceMemory memory = VK_NULL_HANDLE;
        std::byte* mapped = nullptr;
        VkDeviceSize size = 0;
    };

    struct PipelineEntry {
        VkPipeline pipeline = VK_NULL_HANDLE;
        std::uint32_t sample_slot = 0;
    };

    VulkanDevice() = default;

    VkDevice Native() { return device_; }
    VkCommandPool CommandPool() { return command_pool_; }
    const TextureEntry* LookupTexture(TextureHandle handle);
    const BufferEntry& BufferResource(BufferHandle handle);
    TextureHandle RegisterSwapchainTexture(VkImage image, VkFormat format, std::uint32_t width,
                                           std::uint32_t height, VkImageView view);
    void UnregisterSwapchainTexture(TextureHandle handle);
    VkBuffer EnsureReadBackBuffer(TextureHandle handle);
    VkPipeline PipelineState(PipelineHandle handle);
    std::uint32_t PipelineSampleSlot(PipelineHandle handle);
    VkPipelineLayout PipelineLayout();
    VkDescriptorSet SampleDescriptorSet();
    std::uint32_t FindMemoryType(std::uint32_t type_bits, VkMemoryPropertyFlags properties) const;

    VkInstance instance_ = VK_NULL_HANDLE;
    VkPhysicalDevice physical_device_ = VK_NULL_HANDLE;
    VkDevice device_ = VK_NULL_HANDLE;
    std::uint32_t queue_family_ = 0;
    VkQueue queue_ = VK_NULL_HANDLE;
    VkCommandPool command_pool_ = VK_NULL_HANDLE;
    VkFence fence_ = VK_NULL_HANDLE;
    VkDescriptorSetLayout sample_set_layout_ = VK_NULL_HANDLE;
    VkDescriptorPool sample_pool_ = VK_NULL_HANDLE;
    VkDescriptorSet sample_set_ = VK_NULL_HANDLE;
    VkPipelineLayout pipeline_layout_ = VK_NULL_HANDLE;
    bool swapchain_supported_ = false;

    std::unordered_map<std::uint64_t, TextureEntry> textures_;
    std::unordered_map<std::uint64_t, ReadBackEntry> read_backs_;
    std::unordered_map<std::uint64_t, BufferEntry> buffers_;
    std::unordered_map<std::uint64_t, PipelineEntry> pipelines_;
    std::unordered_map<std::uint64_t, SamplerEntry> samplers_;
    std::uint64_t next_handle_ = 1;
};

} // namespace jrpgmaker::rhi::vulkan

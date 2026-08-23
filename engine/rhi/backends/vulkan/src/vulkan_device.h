#pragma once

#include <cstdint>
#include <memory>

#include <volk.h>

#include "jrpgmaker/rhi/command_list.hpp"
#include "jrpgmaker/rhi/device.hpp"

namespace jrpgmaker::rhi::vulkan {

class VulkanCommandList final : public ICommandList {
public:
    VulkanCommandList(VkDevice device, VkCommandPool pool);
    ~VulkanCommandList() override = default;

    void Begin() override;
    void End() override;
    void BeginRendering(TextureHandle color_target, const ClearColor& clear_color) override;
    void EndRendering() override;
    void SetPipeline(PipelineHandle handle) override;
    void Draw(std::uint32_t vertex_count, std::uint32_t instance_count) override;
    void CopyTexture(TextureHandle source, TextureHandle destination) override;

    VkCommandBuffer Native() { return command_buffer_; }

private:
    VkDevice device_ = VK_NULL_HANDLE;
    VkCommandBuffer command_buffer_ = VK_NULL_HANDLE;
};

class VulkanDevice final : public IDevice {
public:
    static std::unique_ptr<IDevice> Create();

    ~VulkanDevice() override;

    BufferHandle CreateBuffer(const BufferDesc& desc) override;
    void DestroyBuffer(BufferHandle handle) override;
    TextureHandle CreateTexture(const TextureDesc& desc) override;
    void DestroyTexture(TextureHandle handle) override;
    PipelineHandle CreatePipeline(const GraphicsPipelineDesc& desc) override;
    void DestroyPipeline(PipelineHandle handle) override;

    ICommandList* CreateCommandList() override;
    void DestroyCommandList(ICommandList* command_list) override;

    ISwapchain* CreateSwapchain(void* native_window_handle, std::uint32_t width,
                                std::uint32_t height, Format format) override;
    void DestroySwapchain(ISwapchain* swapchain) override;

    void Submit(ICommandList& command_list) override;
    void WaitForGpuIdle() override;

    const std::byte* MapReadBack(TextureHandle handle) override;

private:
    VulkanDevice() = default;

    VkInstance instance_ = VK_NULL_HANDLE;
    VkPhysicalDevice physical_device_ = VK_NULL_HANDLE;
    VkDevice device_ = VK_NULL_HANDLE;
    VkQueue queue_ = VK_NULL_HANDLE;
    VkCommandPool command_pool_ = VK_NULL_HANDLE;
    VkFence fence_ = VK_NULL_HANDLE;
};

} // namespace jrpgmaker::rhi::vulkan

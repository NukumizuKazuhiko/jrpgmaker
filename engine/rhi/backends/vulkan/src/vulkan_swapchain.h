#pragma once

#include <cstdint>
#include <vector>

#include <volk.h>

#include "jrpgmaker/rhi/common.hpp"
#include "jrpgmaker/rhi/swapchain.hpp"

namespace jrpgmaker::rhi::vulkan {

class VulkanDevice;

class VulkanSwapchain final : public ISwapchain {
public:
    VulkanSwapchain(VulkanDevice* owner, void* native_window_handle, std::uint32_t width,
                    std::uint32_t height, Format format);
    ~VulkanSwapchain() override;

    TextureHandle AcquireTexture() override;
    void Present() override;
    void Resize(std::uint32_t width, std::uint32_t height) override;

private:
    void CreateSwapchainKHR(std::uint32_t width, std::uint32_t height);
    void DestroySwapchainKHR();

    VulkanDevice* owner_ = nullptr;
    VkSurfaceKHR surface_ = VK_NULL_HANDLE;
    VkSwapchainKHR swapchain_ = VK_NULL_HANDLE;
    VkFormat format_ = VK_FORMAT_UNDEFINED;
    std::vector<TextureHandle> image_handles_;
    std::uint32_t current_image_ = 0;
};

} // namespace jrpgmaker::rhi::vulkan
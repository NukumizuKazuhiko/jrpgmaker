#include "vulkan_swapchain.h"

#include <SDL3/SDL_vulkan.h>

#include <format>
#include <stdexcept>
#include <vector>

#include "vulkan_device.h"

namespace jrpgmaker::rhi::vulkan {
namespace {

void ThrowIfFailed(VkResult result, const char* context) {
    if (result != VK_SUCCESS) {
        throw std::runtime_error(std::format("vulkan swapchain failed in {}: result={}", context,
                                             static_cast<int>(result)));
    }
}

VkSurfaceKHR CreateSurface(VkInstance instance, void* native_window_handle) {
    VkSurfaceKHR surface = VK_NULL_HANDLE;
    if (!SDL_Vulkan_CreateSurface(static_cast<SDL_Window*>(native_window_handle), instance, nullptr,
                                  &surface)) {
        throw std::runtime_error(
            std::format("vulkan swapchain failed to create surface: {}", SDL_GetError()));
    }
    return surface;
}

} // namespace

VulkanSwapchain::VulkanSwapchain(VulkanDevice* owner, void* native_window_handle,
                                 std::uint32_t width, std::uint32_t height, Format format)
    : owner_(owner) {
    surface_ = CreateSurface(owner->instance_, native_window_handle);

    VkBool32 present_supported = VK_FALSE;
    ThrowIfFailed(vkGetPhysicalDeviceSurfaceSupportKHR(
                      owner->physical_device_, owner->queue_family_, surface_, &present_supported),
                  "vkGetPhysicalDeviceSurfaceSupportKHR");
    if (present_supported == VK_FALSE) {
        throw std::runtime_error("vulkan swapchain: graphics queue family cannot present");
    }

    std::uint32_t format_count = 0;
    ThrowIfFailed(vkGetPhysicalDeviceSurfaceFormatsKHR(owner->physical_device_, surface_,
                                                       &format_count, nullptr),
                  "vkGetPhysicalDeviceSurfaceFormatsKHR(count)");
    std::vector<VkSurfaceFormatKHR> formats(format_count);
    ThrowIfFailed(vkGetPhysicalDeviceSurfaceFormatsKHR(owner->physical_device_, surface_,
                                                       &format_count, formats.data()),
                  "vkGetPhysicalDeviceSurfaceFormatsKHR");

    const VkFormat requested_format = ToNativeFormat(format);
    bool found_format = false;
    VkSurfaceFormatKHR selected_format{};
    for (const VkSurfaceFormatKHR& candidate : formats) {
        if (candidate.format == requested_format &&
            candidate.colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR) {
            selected_format = candidate;
            found_format = true;
            break;
        }
    }
    if (!found_format) {
        throw std::runtime_error("vulkan swapchain: requested format is not supported by surface");
    }
    format_ = selected_format.format;

    CreateSwapchainKHR(width, height);
}

VulkanSwapchain::~VulkanSwapchain() {
    DestroySwapchainKHR();
    if (surface_ != VK_NULL_HANDLE) {
        vkDestroySurfaceKHR(owner_->instance_, surface_, nullptr);
    }
}

void VulkanSwapchain::CreateSwapchainKHR(std::uint32_t width, std::uint32_t height) {
    for (TextureHandle handle : image_handles_) {
        owner_->UnregisterSwapchainTexture(handle);
    }
    image_handles_.clear();
    DestroySwapchainKHR();

    VkSurfaceCapabilitiesKHR capabilities{};
    ThrowIfFailed(vkGetPhysicalDeviceSurfaceCapabilitiesKHR(owner_->physical_device_, surface_,
                                                            &capabilities),
                  "vkGetPhysicalDeviceSurfaceCapabilitiesKHR");

    std::uint32_t image_count = capabilities.minImageCount + 1;
    if (capabilities.maxImageCount > 0 && image_count > capabilities.maxImageCount) {
        image_count = capabilities.maxImageCount;
    }

    VkSwapchainCreateInfoKHR create_info{};
    create_info.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
    create_info.surface = surface_;
    create_info.minImageCount = image_count;
    create_info.imageFormat = format_;
    create_info.imageColorSpace = VK_COLOR_SPACE_SRGB_NONLINEAR_KHR;
    create_info.imageExtent = VkExtent2D{width, height};
    create_info.imageArrayLayers = 1;
    create_info.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
    create_info.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
    create_info.preTransform = capabilities.currentTransform;
    create_info.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
    create_info.presentMode = VK_PRESENT_MODE_FIFO_KHR;
    create_info.clipped = VK_TRUE;

    ThrowIfFailed(vkCreateSwapchainKHR(owner_->device_, &create_info, nullptr, &swapchain_),
                  "vkCreateSwapchainKHR");

    std::uint32_t swapchain_image_count = 0;
    ThrowIfFailed(
        vkGetSwapchainImagesKHR(owner_->device_, swapchain_, &swapchain_image_count, nullptr),
        "vkGetSwapchainImagesKHR(count)");
    std::vector<VkImage> images(swapchain_image_count);
    ThrowIfFailed(
        vkGetSwapchainImagesKHR(owner_->device_, swapchain_, &swapchain_image_count, images.data()),
        "vkGetSwapchainImagesKHR");

    for (VkImage image : images) {
        VkImageViewCreateInfo view_info{};
        view_info.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        view_info.image = image;
        view_info.viewType = VK_IMAGE_VIEW_TYPE_2D;
        view_info.format = format_;
        view_info.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        view_info.subresourceRange.levelCount = 1;
        view_info.subresourceRange.layerCount = 1;
        VkImageView view = VK_NULL_HANDLE;
        ThrowIfFailed(vkCreateImageView(owner_->device_, &view_info, nullptr, &view),
                      "vkCreateImageView(swapchain)");

        image_handles_.push_back(
            owner_->RegisterSwapchainTexture(image, format_, width, height, view));
    }
}

void VulkanSwapchain::DestroySwapchainKHR() {
    if (swapchain_ != VK_NULL_HANDLE) {
        vkDestroySwapchainKHR(owner_->device_, swapchain_, nullptr);
        swapchain_ = VK_NULL_HANDLE;
    }
}

TextureHandle VulkanSwapchain::AcquireTexture() {
    ThrowIfFailed(vkAcquireNextImageKHR(owner_->device_, swapchain_, UINT64_MAX, VK_NULL_HANDLE,
                                        VK_NULL_HANDLE, &current_image_),
                  "vkAcquireNextImageKHR");
    return image_handles_[current_image_];
}

void VulkanSwapchain::Present() {
    VkPresentInfoKHR present_info{};
    present_info.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
    present_info.swapchainCount = 1;
    present_info.pSwapchains = &swapchain_;
    present_info.pImageIndices = &current_image_;
    ThrowIfFailed(vkQueuePresentKHR(owner_->queue_, &present_info), "vkQueuePresentKHR");
}

void VulkanSwapchain::Resize(std::uint32_t width, std::uint32_t height) {
    owner_->WaitForGpuIdle();
    CreateSwapchainKHR(width, height);
}

} // namespace jrpgmaker::rhi::vulkan
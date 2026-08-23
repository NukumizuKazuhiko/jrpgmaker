#include "vulkan_device.h"

#include <format>
#include <stdexcept>
#include <utility>
#include <vector>

namespace jrpgmaker::rhi::vulkan {
namespace {

void ThrowIfFailed(VkResult result, const char* context) {
    if (result != VK_SUCCESS) {
        throw std::runtime_error(std::format("vulkan backend failed in {}: result={}", context,
                                             static_cast<int>(result)));
    }
}

VkInstance CreateInstance() {
    VkApplicationInfo app_info{};
    app_info.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
    app_info.apiVersion = VK_API_VERSION_1_3;

    VkInstanceCreateInfo create_info{};
    create_info.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
    create_info.pApplicationInfo = &app_info;

    VkInstance instance = VK_NULL_HANDLE;
    ThrowIfFailed(vkCreateInstance(&create_info, nullptr, &instance), "vkCreateInstance");
    return instance;
}

std::uint32_t FindGraphicsQueueFamily(VkPhysicalDevice physical_device) {
    std::uint32_t family_count = 0;
    vkGetPhysicalDeviceQueueFamilyProperties(physical_device, &family_count, nullptr);

    std::vector<VkQueueFamilyProperties> families(family_count);
    vkGetPhysicalDeviceQueueFamilyProperties(physical_device, &family_count, families.data());

    for (std::uint32_t i = 0; i < family_count; ++i) {
        if ((families[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) != 0u) {
            return i;
        }
    }
    throw std::runtime_error(
        "vulkan backend: no graphics-capable queue family on the selected device");
}

VkPhysicalDevice SelectPhysicalDevice(VkInstance instance) {
    std::uint32_t device_count = 0;
    ThrowIfFailed(vkEnumeratePhysicalDevices(instance, &device_count, nullptr),
                  "vkEnumeratePhysicalDevices");
    if (device_count == 0) {
        throw std::runtime_error("vulkan backend: no vulkan-capable physical device found");
    }

    std::vector<VkPhysicalDevice> devices(device_count);
    ThrowIfFailed(vkEnumeratePhysicalDevices(instance, &device_count, devices.data()),
                  "vkEnumeratePhysicalDevices");

    VkPhysicalDevice best = VK_NULL_HANDLE;
    VkPhysicalDeviceType best_type = VK_PHYSICAL_DEVICE_TYPE_CPU;
    for (VkPhysicalDevice candidate : devices) {
        VkPhysicalDeviceProperties properties{};
        vkGetPhysicalDeviceProperties(candidate, &properties);
        if (best == VK_NULL_HANDLE ||
            static_cast<int>(properties.deviceType) > static_cast<int>(best_type)) {
            best = candidate;
            best_type = properties.deviceType;
        }
    }
    return best;
}

} // namespace

std::unique_ptr<IDevice> VulkanDevice::Create() {
    ThrowIfFailed(volkInitialize(), "volkInitialize");

    auto instance = std::unique_ptr<VulkanDevice>(new VulkanDevice());
    instance->instance_ = CreateInstance();
    instance->physical_device_ = SelectPhysicalDevice(instance->instance_);
    instance->queue_family_ = FindGraphicsQueueFamily(instance->physical_device_);

    const float queue_priority = 1.0f;
    VkDeviceQueueCreateInfo queue_info{};
    queue_info.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
    queue_info.queueFamilyIndex = instance->queue_family_;
    queue_info.queueCount = 1;
    queue_info.pQueuePriorities = &queue_priority;

    VkDeviceCreateInfo device_info{};
    device_info.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
    device_info.queueCreateInfoCount = 1;
    device_info.pQueueCreateInfos = &queue_info;

    ThrowIfFailed(
        vkCreateDevice(instance->physical_device_, &device_info, nullptr, &instance->device_),
        "vkCreateDevice");

    vkGetDeviceQueue(instance->device_, instance->queue_family_, 0, &instance->queue_);

    VkCommandPoolCreateInfo pool_info{};
    pool_info.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    pool_info.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    pool_info.queueFamilyIndex = instance->queue_family_;
    ThrowIfFailed(
        vkCreateCommandPool(instance->device_, &pool_info, nullptr, &instance->command_pool_),
        "vkCreateCommandPool");

    VkFenceCreateInfo fence_info{};
    fence_info.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
    fence_info.flags = VK_FENCE_CREATE_SIGNALED_BIT;
    ThrowIfFailed(vkCreateFence(instance->device_, &fence_info, nullptr, &instance->fence_),
                  "vkCreateFence");

    return instance;
}

VulkanDevice::~VulkanDevice() {
    if (device_ != VK_NULL_HANDLE) {
        vkDestroyCommandPool(device_, command_pool_, nullptr);
        vkDestroyFence(device_, fence_, nullptr);
        vkDestroyDevice(device_, nullptr);
    }
    if (instance_ != VK_NULL_HANDLE) {
        vkDestroyInstance(instance_, nullptr);
    }
}

BufferHandle VulkanDevice::CreateBuffer(const BufferDesc&) {
    return BufferHandle::kInvalid;
}

void VulkanDevice::DestroyBuffer(BufferHandle) {}

TextureHandle VulkanDevice::CreateTexture(const TextureDesc&) {
    return TextureHandle::kInvalid;
}

void VulkanDevice::DestroyTexture(TextureHandle) {}

PipelineHandle VulkanDevice::CreatePipeline(const GraphicsPipelineDesc&) {
    return PipelineHandle::kInvalid;
}

void VulkanDevice::DestroyPipeline(PipelineHandle) {}

ICommandList* VulkanDevice::CreateCommandList() {
    return new VulkanCommandList(device_, command_pool_);
}

void VulkanDevice::DestroyCommandList(ICommandList* command_list) {
    delete command_list;
}

ISwapchain* VulkanDevice::CreateSwapchain(void*, std::uint32_t, std::uint32_t, Format) {
    return nullptr;
}

void VulkanDevice::DestroySwapchain(ISwapchain*) {}

void VulkanDevice::Submit(ICommandList& command_list) {
    auto& vulkan_list = static_cast<VulkanCommandList&>(command_list);
    ThrowIfFailed(vkWaitForFences(device_, 1, &fence_, VK_TRUE, UINT64_MAX),
                  "vkWaitForFences(pre-submit)");
    ThrowIfFailed(vkResetFences(device_, 1, &fence_), "vkResetFences");

    VkCommandBuffer command_buffer = vulkan_list.Native();
    VkSubmitInfo submit_info{};
    submit_info.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submit_info.commandBufferCount = 1;
    submit_info.pCommandBuffers = &command_buffer;

    ThrowIfFailed(vkQueueSubmit(queue_, 1, &submit_info, fence_), "vkQueueSubmit");
}

void VulkanDevice::WaitForGpuIdle() {
    ThrowIfFailed(vkWaitForFences(device_, 1, &fence_, VK_TRUE, UINT64_MAX), "vkWaitForFences");
}

const std::byte* VulkanDevice::MapReadBack(TextureHandle) {
    return nullptr;
}

VulkanCommandList::VulkanCommandList(VkDevice device, VkCommandPool pool) {
    VkCommandBufferAllocateInfo allocate_info{};
    allocate_info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    allocate_info.commandPool = pool;
    allocate_info.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    allocate_info.commandBufferCount = 1;
    ThrowIfFailed(vkAllocateCommandBuffers(device, &allocate_info, &command_buffer_),
                  "vkAllocateCommandBuffers");
}

void VulkanCommandList::Begin() {
    VkCommandBufferBeginInfo begin_info{};
    begin_info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    begin_info.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    ThrowIfFailed(vkBeginCommandBuffer(command_buffer_, &begin_info), "vkBeginCommandBuffer");
}

void VulkanCommandList::End() {
    ThrowIfFailed(vkEndCommandBuffer(command_buffer_), "vkEndCommandBuffer");
}

void VulkanCommandList::BeginRendering(TextureHandle, const ClearColor&) {
    throw std::runtime_error("vulkan backend: BeginRendering is not implemented yet");
}

void VulkanCommandList::EndRendering() {
    throw std::runtime_error("vulkan backend: EndRendering is not implemented yet");
}

void VulkanCommandList::SetPipeline(PipelineHandle) {
    throw std::runtime_error("vulkan backend: SetPipeline is not implemented yet");
}

void VulkanCommandList::Draw(std::uint32_t, std::uint32_t) {
    throw std::runtime_error("vulkan backend: Draw is not implemented yet");
}

void VulkanCommandList::CopyTexture(TextureHandle, TextureHandle) {
    throw std::runtime_error("vulkan backend: CopyTexture is not implemented yet");
}

} // namespace jrpgmaker::rhi::vulkan

namespace jrpgmaker::rhi {

std::unique_ptr<IDevice> CreateDevice(Backend backend) {
    if (backend == Backend::kVulkan) {
        return vulkan::VulkanDevice::Create();
    }
    throw std::runtime_error("no rhi backend available for the requested graphics api");
}

} // namespace jrpgmaker::rhi

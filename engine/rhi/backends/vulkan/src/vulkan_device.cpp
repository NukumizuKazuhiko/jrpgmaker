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

VkFormat ToNativeFormat(Format format) {
    switch (format) {
    case Format::kB8G8R8A8Unorm:
        return VK_FORMAT_B8G8R8A8_UNORM;
    case Format::kR8G8B8A8Unorm:
        return VK_FORMAT_R8G8B8A8_UNORM;
    }
    throw std::runtime_error("vulkan backend: unsupported texture format");
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
    volkLoadInstance(instance->instance_);
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

    VkPhysicalDeviceDynamicRenderingFeatures dynamic_rendering_features{};
    dynamic_rendering_features.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DYNAMIC_RENDERING_FEATURES;
    dynamic_rendering_features.dynamicRendering = VK_TRUE;
    device_info.pNext = &dynamic_rendering_features;

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

TextureHandle VulkanDevice::CreateTexture(const TextureDesc& desc) {
    const VkFormat native_format = ToNativeFormat(desc.format);

    VkImageCreateInfo image_info{};
    image_info.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    image_info.imageType = VK_IMAGE_TYPE_2D;
    image_info.format = native_format;
    image_info.extent = VkExtent3D{desc.width, desc.height, 1};
    image_info.mipLevels = 1;
    image_info.arrayLayers = 1;
    image_info.samples = VK_SAMPLE_COUNT_1_BIT;
    image_info.tiling = VK_IMAGE_TILING_OPTIMAL;
    image_info.usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT |
                       VK_IMAGE_USAGE_TRANSFER_DST_BIT;
    image_info.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;

    TextureEntry entry{};
    entry.format = native_format;
    entry.width = desc.width;
    entry.height = desc.height;
    ThrowIfFailed(vkCreateImage(device_, &image_info, nullptr, &entry.image), "vkCreateImage");

    VkMemoryRequirements requirements{};
    vkGetImageMemoryRequirements(device_, entry.image, &requirements);
    const std::uint32_t memory_type =
        FindMemoryType(requirements.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);

    VkMemoryAllocateInfo allocate_info{};
    allocate_info.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    allocate_info.allocationSize = requirements.size;
    allocate_info.memoryTypeIndex = memory_type;
    ThrowIfFailed(vkAllocateMemory(device_, &allocate_info, nullptr, &entry.memory),
                  "vkAllocateMemory(image)");
    ThrowIfFailed(vkBindImageMemory(device_, entry.image, entry.memory, 0), "vkBindImageMemory");

    VkImageViewCreateInfo view_info{};
    view_info.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    view_info.image = entry.image;
    view_info.viewType = VK_IMAGE_VIEW_TYPE_2D;
    view_info.format = native_format;
    view_info.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    view_info.subresourceRange.levelCount = 1;
    view_info.subresourceRange.layerCount = 1;
    ThrowIfFailed(vkCreateImageView(device_, &view_info, nullptr, &entry.view),
                  "vkCreateImageView");

    const std::uint64_t handle_value = next_handle_++;
    textures_.emplace(handle_value, entry);
    return static_cast<TextureHandle>(handle_value);
}

void VulkanDevice::DestroyTexture(TextureHandle handle) {
    const std::uint64_t key = static_cast<std::uint64_t>(handle);
    WaitForGpuIdle();
    const auto staged = read_backs_.find(key);
    if (staged != read_backs_.end()) {
        vkDestroyBuffer(device_, staged->second.buffer, nullptr);
        vkFreeMemory(device_, staged->second.memory, nullptr);
        read_backs_.erase(staged);
    }
    const auto it = textures_.find(key);
    if (it == textures_.end()) {
        return;
    }
    vkDestroyImageView(device_, it->second.view, nullptr);
    vkDestroyImage(device_, it->second.image, nullptr);
    vkFreeMemory(device_, it->second.memory, nullptr);
    textures_.erase(it);
}

PipelineHandle VulkanDevice::CreatePipeline(const GraphicsPipelineDesc&) {
    return PipelineHandle::kInvalid;
}

void VulkanDevice::DestroyPipeline(PipelineHandle) {}

const VulkanDevice::TextureEntry* VulkanDevice::LookupTexture(TextureHandle handle) {
    const auto it = textures_.find(static_cast<std::uint64_t>(handle));
    if (it == textures_.end()) {
        throw std::runtime_error("vulkan backend: unknown texture handle");
    }
    return &it->second;
}

std::uint32_t VulkanDevice::FindMemoryType(std::uint32_t type_bits,
                                           VkMemoryPropertyFlags properties) const {
    VkPhysicalDeviceMemoryProperties memory_properties{};
    vkGetPhysicalDeviceMemoryProperties(physical_device_, &memory_properties);
    for (std::uint32_t i = 0; i < memory_properties.memoryTypeCount; ++i) {
        const bool type_allowed = (type_bits & (1u << i)) != 0u;
        const bool properties_met =
            (memory_properties.memoryTypes[i].propertyFlags & properties) == properties;
        if (type_allowed && properties_met) {
            return i;
        }
    }
    throw std::runtime_error("vulkan backend: no memory type satisfies the requested properties");
}

VkBuffer VulkanDevice::EnsureReadBackBuffer(TextureHandle handle) {
    const std::uint64_t key = static_cast<std::uint64_t>(handle);
    const auto existing = read_backs_.find(key);
    if (existing != read_backs_.end()) {
        return existing->second.buffer;
    }

    const TextureEntry* source = LookupTexture(handle);
    const VkDeviceSize size = static_cast<VkDeviceSize>(source->width) * source->height * 4;

    ReadBackEntry entry{};
    entry.size = size;
    VkBufferCreateInfo buffer_info{};
    buffer_info.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    buffer_info.size = size;
    buffer_info.usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT;
    buffer_info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    ThrowIfFailed(vkCreateBuffer(device_, &buffer_info, nullptr, &entry.buffer),
                  "vkCreateBuffer(read-back)");

    VkMemoryRequirements requirements{};
    vkGetBufferMemoryRequirements(device_, entry.buffer, &requirements);
    const std::uint32_t memory_type =
        FindMemoryType(requirements.memoryTypeBits,
                       VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);

    VkMemoryAllocateInfo allocate_info{};
    allocate_info.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    allocate_info.allocationSize = requirements.size;
    allocate_info.memoryTypeIndex = memory_type;
    ThrowIfFailed(vkAllocateMemory(device_, &allocate_info, nullptr, &entry.memory),
                  "vkAllocateMemory(read-back)");
    ThrowIfFailed(vkBindBufferMemory(device_, entry.buffer, entry.memory, 0), "vkBindBufferMemory");
    ThrowIfFailed(
        vkMapMemory(device_, entry.memory, 0, size, 0, reinterpret_cast<void**>(&entry.mapped)),
        "vkMapMemory(read-back)");

    auto [inserted, ok] = read_backs_.emplace(key, entry);
    if (!ok) {
        throw std::runtime_error("vulkan backend: duplicate read-back registration");
    }
    return inserted->second.buffer;
}

ICommandList* VulkanDevice::CreateCommandList() {
    return new VulkanCommandList(this);
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

MappedTexture VulkanDevice::MapReadBack(TextureHandle handle) {
    const TextureEntry* source = LookupTexture(handle);
    VkBuffer staging = EnsureReadBackBuffer(handle);

    ICommandList* copy_list = CreateCommandList();
    copy_list->Begin();
    static_cast<VulkanCommandList*>(copy_list)->CopyTextureToReadBack(
        source->image, staging, VkExtent3D{source->width, source->height, 1});
    copy_list->End();
    Submit(*copy_list);
    DestroyCommandList(copy_list);
    WaitForGpuIdle();

    const auto staged = read_backs_.find(static_cast<std::uint64_t>(handle));
    const std::uint64_t row_pitch = static_cast<std::uint64_t>(source->width) * 4;
    return MappedTexture{staged->second.mapped, row_pitch};
}

VulkanCommandList::VulkanCommandList(VulkanDevice* owner)
    : owner_(owner), device_(owner->Native()) {
    VkCommandBufferAllocateInfo allocate_info{};
    allocate_info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    allocate_info.commandPool = owner->CommandPool();
    allocate_info.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    allocate_info.commandBufferCount = 1;
    ThrowIfFailed(vkAllocateCommandBuffers(device_, &allocate_info, &command_buffer_),
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

void VulkanCommandList::BeginRendering(TextureHandle color_target, const ClearColor& clear_color) {
    const VulkanDevice::TextureEntry* entry = owner_->LookupTexture(color_target);
    rendering_image_ = entry->image;

    VkImageMemoryBarrier to_attachment{};
    to_attachment.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    to_attachment.srcAccessMask = 0;
    to_attachment.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
    to_attachment.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    to_attachment.newLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    to_attachment.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    to_attachment.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    to_attachment.image = entry->image;
    to_attachment.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    to_attachment.subresourceRange.levelCount = 1;
    to_attachment.subresourceRange.layerCount = 1;
    vkCmdPipelineBarrier(command_buffer_, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
                         VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, 0, 0, nullptr, 0, nullptr,
                         1, &to_attachment);

    VkRenderingAttachmentInfo attachment{};
    attachment.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
    attachment.imageView = entry->view;
    attachment.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    attachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    attachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    attachment.clearValue.color = {{clear_color.r, clear_color.g, clear_color.b, clear_color.a}};

    VkRenderingInfo rendering_info{};
    rendering_info.sType = VK_STRUCTURE_TYPE_RENDERING_INFO;
    rendering_info.renderArea.extent = VkExtent2D{entry->width, entry->height};
    rendering_info.layerCount = 1;
    rendering_info.colorAttachmentCount = 1;
    rendering_info.pColorAttachments = &attachment;
    vkCmdBeginRendering(command_buffer_, &rendering_info);
}

void VulkanCommandList::EndRendering() {
    vkCmdEndRendering(command_buffer_);

    VkImageMemoryBarrier to_transfer_src{};
    to_transfer_src.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    to_transfer_src.srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
    to_transfer_src.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
    to_transfer_src.oldLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    to_transfer_src.newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
    to_transfer_src.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    to_transfer_src.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    to_transfer_src.image = rendering_image_;
    to_transfer_src.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    to_transfer_src.subresourceRange.levelCount = 1;
    to_transfer_src.subresourceRange.layerCount = 1;
    vkCmdPipelineBarrier(command_buffer_, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
                         VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 0, nullptr, 1,
                         &to_transfer_src);
    rendering_image_ = VK_NULL_HANDLE;
}

void VulkanCommandList::CopyTextureToReadBack(VkImage source, VkBuffer staging, VkExtent3D extent) {
    VkBufferImageCopy region{};
    region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    region.imageSubresource.layerCount = 1;
    region.imageExtent = extent;
    vkCmdCopyImageToBuffer(command_buffer_, source, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, staging,
                           1, &region);
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

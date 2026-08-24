#include "vulkan_device.h"

#include <algorithm>
#include <cstring>
#include <format>
#include <iterator>
#include <stdexcept>
#include <utility>
#include <vector>

#include "vulkan_swapchain.h"

namespace jrpgmaker::rhi::vulkan {
namespace {

void ThrowIfFailed(VkResult result, const char* context) {
    if (result != VK_SUCCESS) {
        throw std::runtime_error(std::format("vulkan backend failed in {}: result={}", context,
                                             static_cast<int>(result)));
    }
}

std::vector<const char*> EnabledInstanceExtensions() {
    std::vector<VkExtensionProperties> available;
    std::uint32_t extension_count = 0;
    ThrowIfFailed(vkEnumerateInstanceExtensionProperties(nullptr, &extension_count, nullptr),
                  "vkEnumerateInstanceExtensionProperties");
    available.resize(extension_count);
    ThrowIfFailed(
        vkEnumerateInstanceExtensionProperties(nullptr, &extension_count, available.data()),
        "vkEnumerateInstanceExtensionProperties");

    constexpr const char* kRequired[] = {
        "VK_KHR_surface",
#if defined(_WIN32)
        "VK_KHR_win32_surface",
#elif defined(__linux__)
        "VK_KHR_xcb_surface",
        "VK_KHR_wayland_surface",
#elif defined(__APPLE__)
        "VK_EXT_metal_surface",
#endif
    };

    std::vector<const char*> enabled;
    for (const char* required : kRequired) {
        const bool supported = std::any_of(
            available.begin(), available.end(), [required](const VkExtensionProperties& candidate) {
                return std::strcmp(candidate.extensionName, required) == 0;
            });
        if (supported) {
            enabled.push_back(required);
        }
    }
    return enabled;
}

VkInstance CreateInstance() {
    VkApplicationInfo app_info{};
    app_info.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
    app_info.apiVersion = VK_API_VERSION_1_3;

    const std::vector<const char*> extensions = EnabledInstanceExtensions();

    VkInstanceCreateInfo create_info{};
    create_info.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
    create_info.pApplicationInfo = &app_info;
    create_info.enabledExtensionCount = static_cast<std::uint32_t>(extensions.size());
    create_info.ppEnabledExtensionNames = extensions.data();

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

VkFormat ToNativeFormat(Format format) {
    switch (format) {
    case Format::kB8G8R8A8Unorm:
        return VK_FORMAT_B8G8R8A8_UNORM;
    case Format::kR8G8B8A8Unorm:
        return VK_FORMAT_R8G8B8A8_UNORM;
    }
    throw std::runtime_error("vulkan backend: unsupported texture format");
}

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

    const char* device_extensions[] = {"VK_KHR_swapchain"};
    device_info.enabledExtensionCount = static_cast<std::uint32_t>(std::size(device_extensions));
    device_info.ppEnabledExtensionNames = device_extensions;

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
        for (const auto& [key, entry] : pipelines_) {
            vkDestroyPipeline(device_, entry.pipeline, nullptr);
        }
        if (pipeline_layout_ != VK_NULL_HANDLE) {
            vkDestroyPipelineLayout(device_, pipeline_layout_, nullptr);
        }
        for (const auto& [key, entry] : read_backs_) {
            vkFreeMemory(device_, entry.memory, nullptr);
            vkDestroyBuffer(device_, entry.buffer, nullptr);
        }
        for (const auto& [key, entry] : textures_) {
            if (entry.is_swapchain) {
                continue;
            }
            vkDestroyImageView(device_, entry.view, nullptr);
            vkFreeMemory(device_, entry.memory, nullptr);
            vkDestroyImage(device_, entry.image, nullptr);
        }
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

    VkImageUsageFlags usage_flags = 0;
    if ((desc.usage & TextureUsage::kRenderTarget) != TextureUsage::kNone) {
        usage_flags |= VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
    }
    if ((desc.usage & TextureUsage::kReadBack) != TextureUsage::kNone) {
        usage_flags |= VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
    }
    if (usage_flags == 0) {
        throw std::runtime_error(
            "vulkan backend: texture creation requires at least one usage bit");
    }

    VkImageCreateInfo image_info{};
    image_info.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    image_info.imageType = VK_IMAGE_TYPE_2D;
    image_info.format = native_format;
    image_info.extent = VkExtent3D{desc.width, desc.height, 1};
    image_info.mipLevels = 1;
    image_info.arrayLayers = 1;
    image_info.samples = VK_SAMPLE_COUNT_1_BIT;
    image_info.tiling = VK_IMAGE_TILING_OPTIMAL;
    image_info.usage = usage_flags;
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
    const auto it = textures_.find(key);
    if (it != textures_.end() && it->second.is_swapchain) {
        throw std::runtime_error(
            "vulkan backend: swapchain back buffers are owned by the swapchain");
    }
    WaitForGpuIdle();
    const auto staged = read_backs_.find(key);
    if (staged != read_backs_.end()) {
        vkDestroyBuffer(device_, staged->second.buffer, nullptr);
        vkFreeMemory(device_, staged->second.memory, nullptr);
        read_backs_.erase(staged);
    }
    const auto destroy_it = textures_.find(key);
    if (destroy_it == textures_.end()) {
        return;
    }
    vkDestroyImageView(device_, destroy_it->second.view, nullptr);
    vkDestroyImage(device_, destroy_it->second.image, nullptr);
    vkFreeMemory(device_, destroy_it->second.memory, nullptr);
    textures_.erase(destroy_it);
}

VkPipelineLayout VulkanDevice::PipelineLayout() {
    if (pipeline_layout_ == VK_NULL_HANDLE) {
        VkPipelineLayoutCreateInfo layout_info{};
        layout_info.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
        ThrowIfFailed(vkCreatePipelineLayout(device_, &layout_info, nullptr, &pipeline_layout_),
                      "vkCreatePipelineLayout");
    }
    return pipeline_layout_;
}

PipelineHandle VulkanDevice::CreatePipeline(const GraphicsPipelineDesc& desc) {
    struct ShaderModuleGuard {
        VkDevice device;
        VkShaderModule& vs;
        VkShaderModule& ps;
        ~ShaderModuleGuard() {
            if (vs != VK_NULL_HANDLE) {
                vkDestroyShaderModule(device, vs, nullptr);
            }
            if (ps != VK_NULL_HANDLE) {
                vkDestroyShaderModule(device, ps, nullptr);
            }
        }
    };

    VkShaderModuleCreateInfo vs_module_info{};
    vs_module_info.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
    vs_module_info.codeSize = desc.vertex_shader.size;
    vs_module_info.pCode = reinterpret_cast<const std::uint32_t*>(desc.vertex_shader.data);
    VkShaderModule vs_module = VK_NULL_HANDLE;
    ThrowIfFailed(vkCreateShaderModule(device_, &vs_module_info, nullptr, &vs_module),
                  "vkCreateShaderModule(vertex)");

    VkShaderModuleCreateInfo ps_module_info{};
    ps_module_info.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
    ps_module_info.codeSize = desc.pixel_shader.size;
    ps_module_info.pCode = reinterpret_cast<const std::uint32_t*>(desc.pixel_shader.data);
    VkShaderModule ps_module = VK_NULL_HANDLE;
    ThrowIfFailed(vkCreateShaderModule(device_, &ps_module_info, nullptr, &ps_module),
                  "vkCreateShaderModule(pixel)");

    const ShaderModuleGuard module_guard{device_, vs_module, ps_module};

    VkPipelineShaderStageCreateInfo stages[2]{};
    stages[0].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
    stages[0].module = vs_module;
    stages[0].pName = "vs_main";
    stages[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
    stages[1].module = ps_module;
    stages[1].pName = "ps_main";

    VkPipelineVertexInputStateCreateInfo vertex_input_info{};
    vertex_input_info.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;

    VkPipelineInputAssemblyStateCreateInfo input_assembly_info{};
    input_assembly_info.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
    input_assembly_info.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;

    const VkDynamicState dynamic_states[] = {VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};
    VkPipelineDynamicStateCreateInfo dynamic_state_info{};
    dynamic_state_info.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
    dynamic_state_info.dynamicStateCount = 2;
    dynamic_state_info.pDynamicStates = dynamic_states;

    VkPipelineViewportStateCreateInfo viewport_state_info{};
    viewport_state_info.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
    viewport_state_info.viewportCount = 1;
    viewport_state_info.scissorCount = 1;

    VkPipelineRasterizationStateCreateInfo rasterization_info{};
    rasterization_info.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
    rasterization_info.polygonMode = VK_POLYGON_MODE_FILL;
    rasterization_info.cullMode = VK_CULL_MODE_NONE;
    rasterization_info.lineWidth = 1.0f;

    VkPipelineMultisampleStateCreateInfo multisample_info{};
    multisample_info.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
    multisample_info.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

    VkPipelineColorBlendAttachmentState blend_attachment{};
    blend_attachment.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
                                      VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;

    VkPipelineColorBlendStateCreateInfo color_blend_info{};
    color_blend_info.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
    color_blend_info.attachmentCount = 1;
    color_blend_info.pAttachments = &blend_attachment;

    const VkFormat color_format = ToNativeFormat(desc.color_format);
    VkPipelineRenderingCreateInfo rendering_info{};
    rendering_info.sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO;
    rendering_info.colorAttachmentCount = 1;
    rendering_info.pColorAttachmentFormats = &color_format;

    VkGraphicsPipelineCreateInfo pipeline_info{};
    pipeline_info.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
    pipeline_info.pNext = &rendering_info;
    pipeline_info.stageCount = 2;
    pipeline_info.pStages = stages;
    pipeline_info.pVertexInputState = &vertex_input_info;
    pipeline_info.pInputAssemblyState = &input_assembly_info;
    pipeline_info.pViewportState = &viewport_state_info;
    pipeline_info.pRasterizationState = &rasterization_info;
    pipeline_info.pMultisampleState = &multisample_info;
    pipeline_info.pColorBlendState = &color_blend_info;
    pipeline_info.pDynamicState = &dynamic_state_info;
    pipeline_info.layout = PipelineLayout();

    PipelineEntry entry{};
    ThrowIfFailed(vkCreateGraphicsPipelines(device_, VK_NULL_HANDLE, 1, &pipeline_info, nullptr,
                                            &entry.pipeline),
                  "vkCreateGraphicsPipelines");

    const std::uint64_t handle_value = next_handle_++;
    pipelines_.emplace(handle_value, std::move(entry));
    return static_cast<PipelineHandle>(handle_value);
}

void VulkanDevice::DestroyPipeline(PipelineHandle handle) {
    const auto it = pipelines_.find(static_cast<std::uint64_t>(handle));
    if (it != pipelines_.end()) {
        vkDestroyPipeline(device_, it->second.pipeline, nullptr);
        pipelines_.erase(it);
    }
}

VkPipeline VulkanDevice::PipelineState(PipelineHandle handle) {
    const auto it = pipelines_.find(static_cast<std::uint64_t>(handle));
    if (it == pipelines_.end()) {
        throw std::runtime_error("vulkan backend: unknown pipeline handle");
    }
    return it->second.pipeline;
}

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

ISwapchain* VulkanDevice::CreateSwapchain(void* native_window_handle, std::uint32_t width,
                                          std::uint32_t height, Format format) {
    return new VulkanSwapchain(this, native_window_handle, width, height, format);
}

void VulkanDevice::DestroySwapchain(ISwapchain* swapchain) {
    delete static_cast<VulkanSwapchain*>(swapchain);
}

TextureHandle VulkanDevice::RegisterSwapchainTexture(VkImage image, VkFormat format,
                                                     std::uint32_t width, std::uint32_t height,
                                                     VkImageView view) {
    TextureEntry entry{};
    entry.image = image;
    entry.format = format;
    entry.width = width;
    entry.height = height;
    entry.view = view;
    entry.is_swapchain = true;

    const std::uint64_t handle_value = next_handle_++;
    textures_.emplace(handle_value, entry);
    return static_cast<TextureHandle>(handle_value);
}

void VulkanDevice::UnregisterSwapchainTexture(TextureHandle handle) {
    const std::uint64_t key = static_cast<std::uint64_t>(handle);
    const auto it = textures_.find(key);
    if (it == textures_.end()) {
        return;
    }
    if (it->second.view != VK_NULL_HANDLE) {
        vkDestroyImageView(device_, it->second.view, nullptr);
    }
    textures_.erase(it);
}

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

    VkViewport viewport{};
    viewport.x = 0.0f;
    viewport.y = static_cast<float>(entry->height);
    viewport.width = static_cast<float>(entry->width);
    viewport.height = -static_cast<float>(entry->height);
    viewport.minDepth = 0.0f;
    viewport.maxDepth = 1.0f;
    vkCmdSetViewport(command_buffer_, 0, 1, &viewport);

    VkRect2D scissor{{0, 0}, {entry->width, entry->height}};
    vkCmdSetScissor(command_buffer_, 0, 1, &scissor);

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

void VulkanCommandList::SetPipeline(PipelineHandle handle) {
    vkCmdBindPipeline(command_buffer_, VK_PIPELINE_BIND_POINT_GRAPHICS,
                      owner_->PipelineState(handle));
}

void VulkanCommandList::Draw(std::uint32_t vertex_count, std::uint32_t instance_count) {
    vkCmdDraw(command_buffer_, vertex_count, instance_count, 0, 0);
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

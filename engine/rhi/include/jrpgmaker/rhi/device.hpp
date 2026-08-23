#pragma once

#include <cstddef>
#include <cstdint>

#include "jrpgmaker/rhi/descriptors.hpp"
#include "jrpgmaker/rhi/handles.hpp"

namespace jrpgmaker::rhi {

class ICommandList;
class ISwapchain;

class IDevice {
public:
    virtual ~IDevice() = default;

    IDevice(const IDevice&) = delete;
    IDevice& operator=(const IDevice&) = delete;

    virtual BufferHandle CreateBuffer(const BufferDesc& desc) = 0;
    virtual void DestroyBuffer(BufferHandle handle) = 0;

    virtual TextureHandle CreateTexture(const TextureDesc& desc) = 0;
    virtual void DestroyTexture(TextureHandle handle) = 0;

    virtual PipelineHandle CreatePipeline(const GraphicsPipelineDesc& desc) = 0;
    virtual void DestroyPipeline(PipelineHandle handle) = 0;

    virtual ICommandList* CreateCommandList() = 0;
    virtual void DestroyCommandList(ICommandList* command_list) = 0;

    virtual ISwapchain* CreateSwapchain(void* native_window_handle, std::uint32_t width,
                                        std::uint32_t height, Format format) = 0;
    virtual void DestroySwapchain(ISwapchain* swapchain) = 0;

    virtual void Submit(ICommandList& command_list) = 0;
    virtual void WaitForGpuIdle() = 0;

    virtual const std::byte* MapReadBack(TextureHandle handle) = 0;

protected:
    IDevice() = default;
};

} // namespace jrpgmaker::rhi

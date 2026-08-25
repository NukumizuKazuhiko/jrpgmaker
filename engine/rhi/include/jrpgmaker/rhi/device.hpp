#pragma once

#include <cstddef>
#include <cstdint>

#include "jrpgmaker/rhi/descriptors.hpp"
#include "jrpgmaker/rhi/handles.hpp"

namespace jrpgmaker::rhi {

class ICommandList;
class ISwapchain;

struct MappedTexture {
    const std::byte* data;
    std::uint64_t row_pitch_bytes;
};

class IDevice {
public:
    virtual ~IDevice() = default;

    IDevice(const IDevice&) = delete;
    IDevice& operator=(const IDevice&) = delete;

    virtual TextureHandle CreateTexture(const TextureDesc& desc) = 0;
    virtual void DestroyTexture(TextureHandle handle) = 0;

    virtual SamplerHandle CreateSampler(const SamplerDesc& desc) = 0;
    virtual void DestroySampler(SamplerHandle handle) = 0;

    // Uploads a single level of host pixel data into a texture created with
    // TextureUsage::kSampled. `data` must contain at least
    // row_pitch_bytes * height bytes. The texture is transitioned to the
    // shader-visible state and stays there; call once before first use.
    virtual void UploadTexture(TextureHandle handle, const void* data,
                               std::uint64_t row_pitch_bytes) = 0;

    virtual BufferHandle CreateBuffer(const BufferDesc& desc) = 0;
    virtual void DestroyBuffer(BufferHandle handle) = 0;
    // Copies the given data into a buffer that is writable from the host (v0:
    // host-visible memory, no staging). `size_bytes` must be <= the buffer's
    // desc.size_bytes. Buffer contents are stable once the buffer is bound for
    // drawing; write before first use, then destroy after WaitForGpuIdle.
    virtual void MapWrite(BufferHandle handle, const void* data, std::uint64_t size_bytes) = 0;

    virtual PipelineHandle CreatePipeline(const GraphicsPipelineDesc& desc) = 0;
    virtual void DestroyPipeline(PipelineHandle handle) = 0;

    virtual ICommandList* CreateCommandList() = 0;
    virtual void DestroyCommandList(ICommandList* command_list) = 0;

    virtual ISwapchain* CreateSwapchain(void* native_window_handle, std::uint32_t width,
                                        std::uint32_t height, Format format) = 0;
    virtual void DestroySwapchain(ISwapchain* swapchain) = 0;

    virtual void Submit(ICommandList& command_list) = 0;
    virtual void WaitForGpuIdle() = 0;

    virtual MappedTexture MapReadBack(TextureHandle handle) = 0;

protected:
    IDevice() = default;
};

} // namespace jrpgmaker::rhi

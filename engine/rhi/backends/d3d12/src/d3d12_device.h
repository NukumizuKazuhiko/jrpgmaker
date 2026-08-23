#pragma once

#include <cstdint>
#include <memory>

#include <d3d12.h>
#include <wrl/client.h>

#include "jrpgmaker/rhi/command_list.hpp"
#include "jrpgmaker/rhi/device.hpp"

namespace jrpgmaker::rhi::d3d12 {

class D3D12CommandList final : public ICommandList {
public:
    D3D12CommandList(ID3D12Device* device, ID3D12CommandAllocator* allocator);
    ~D3D12CommandList() override = default;

    void Begin() override;
    void End() override;
    void BeginRendering(TextureHandle color_target, const ClearColor& clear_color) override;
    void EndRendering() override;
    void SetPipeline(PipelineHandle handle) override;
    void Draw(std::uint32_t vertex_count, std::uint32_t instance_count) override;
    void CopyTexture(TextureHandle source, TextureHandle destination) override;

    ID3D12CommandList* Native() { return command_list_.Get(); }

private:
    Microsoft::WRL::ComPtr<ID3D12CommandAllocator> allocator_;
    Microsoft::WRL::ComPtr<ID3D12GraphicsCommandList> command_list_;
};

class D3D12Device final : public IDevice {
public:
    static std::unique_ptr<IDevice> Create();

    ~D3D12Device() override;

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
    D3D12Device() = default;

    Microsoft::WRL::ComPtr<ID3D12Device> device_;
    Microsoft::WRL::ComPtr<ID3D12CommandQueue> queue_;
    Microsoft::WRL::ComPtr<ID3D12CommandAllocator> allocator_;
    Microsoft::WRL::ComPtr<ID3D12Fence> fence_;
    void* fence_event_ = nullptr;
    std::uint64_t fence_value_ = 0;
};

} // namespace jrpgmaker::rhi::d3d12

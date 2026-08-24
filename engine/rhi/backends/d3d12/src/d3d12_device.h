#pragma once

#include <cstdint>
#include <memory>
#include <unordered_map>

#include <d3d12.h>
#include <wrl/client.h>

#include "jrpgmaker/rhi/command_list.hpp"
#include "jrpgmaker/rhi/device.hpp"

namespace jrpgmaker::rhi::d3d12 {

class D3D12Device;

class D3D12CommandList final : public ICommandList {
public:
    explicit D3D12CommandList(D3D12Device* owner);
    ~D3D12CommandList() override = default;

    void Begin() override;
    void End() override;
    void BeginRendering(TextureHandle color_target, const ClearColor& clear_color) override;
    void EndRendering() override;
    void SetPipeline(PipelineHandle handle) override;
    void Draw(std::uint32_t vertex_count, std::uint32_t instance_count) override;
    void CopyTexture(TextureHandle source, TextureHandle destination) override;

    ID3D12CommandList* Native() { return command_list_.Get(); }

    void CopyTextureToReadBack(ID3D12Resource* source, ID3D12Resource* staging);
    void SetAllocator(ID3D12CommandAllocator* allocator) { allocator_ = allocator; }

private:
    D3D12Device* owner_ = nullptr;
    Microsoft::WRL::ComPtr<ID3D12CommandAllocator> allocator_;
    Microsoft::WRL::ComPtr<ID3D12GraphicsCommandList> command_list_;
    ID3D12Resource* rendering_target_ = nullptr;
    D3D12_CPU_DESCRIPTOR_HANDLE rendering_rtv_{};
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

    MappedTexture MapReadBack(TextureHandle handle) override;

private:
    friend class D3D12CommandList;

    struct TextureEntry {
        Microsoft::WRL::ComPtr<ID3D12Resource> resource;
        D3D12_RESOURCE_DESC desc{};
        D3D12_CPU_DESCRIPTOR_HANDLE rtv{};
        bool has_rtv = false;
    };

    struct ReadBackEntry {
        Microsoft::WRL::ComPtr<ID3D12Resource> resource;
        std::byte* mapped = nullptr;
        std::uint64_t row_pitch_bytes = 0;
    };

    struct PipelineEntry {
        Microsoft::WRL::ComPtr<ID3D12PipelineState> pipeline;
    };

    D3D12Device() = default;

    ID3D12Device* Native() { return device_.Get(); }
    ID3D12CommandAllocator* Allocator() { return allocator_.Get(); }
    D3D12_CPU_DESCRIPTOR_HANDLE RtvCpuHandle(TextureHandle handle);
    ID3D12Resource* TextureResource(TextureHandle handle);
    ID3D12Resource* EnsureReadBack(TextureHandle handle);
    ID3D12PipelineState* PipelineState(PipelineHandle handle);
    ID3D12RootSignature* RootSignature() { return root_signature_.Get(); }
    void CheckGpuErrors();

    Microsoft::WRL::ComPtr<ID3D12Device> device_;
    Microsoft::WRL::ComPtr<ID3D12InfoQueue> info_queue_;
    Microsoft::WRL::ComPtr<ID3D12CommandQueue> queue_;
    Microsoft::WRL::ComPtr<ID3D12CommandAllocator> allocator_;
    Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> rtv_heap_;
    UINT rtv_descriptor_size_ = 0;
    UINT rtv_allocated_ = 0;
    Microsoft::WRL::ComPtr<ID3D12RootSignature> root_signature_;

    std::unordered_map<std::uint64_t, TextureEntry> textures_;
    std::unordered_map<std::uint64_t, ReadBackEntry> read_backs_;
    std::unordered_map<std::uint64_t, PipelineEntry> pipelines_;
    std::uint64_t next_handle_ = 1;

    Microsoft::WRL::ComPtr<ID3D12Fence> fence_;
    void* fence_event_ = nullptr;
    std::uint64_t fence_value_ = 0;
};

} // namespace jrpgmaker::rhi::d3d12

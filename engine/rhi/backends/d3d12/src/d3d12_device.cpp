#include "d3d12_device.h"

#include <dxgi1_4.h>

#include <format>
#include <memory>
#include <stdexcept>
#include <utility>

namespace jrpgmaker::rhi::d3d12 {
namespace {

using Microsoft::WRL::ComPtr;

constexpr UINT kRtvHeapCapacity = 64;

void ThrowIfFailed(HRESULT hr, const char* context) {
    if (FAILED(hr)) {
        throw std::runtime_error(std::format("d3d12 backend failed in {}: hr=0x{:08X}", context,
                                             static_cast<unsigned int>(hr)));
    }
}

DXGI_FORMAT ToNativeFormat(Format format) {
    switch (format) {
    case Format::kB8G8R8A8Unorm:
        return DXGI_FORMAT_B8G8R8A8_UNORM;
    case Format::kR8G8B8A8Unorm:
        return DXGI_FORMAT_R8G8B8A8_UNORM;
    }
    throw std::runtime_error("d3d12 backend: unsupported texture format");
}

ComPtr<ID3D12Device> CreateNativeDevice() {
#ifndef NDEBUG
    ComPtr<ID3D12Debug> debug;
    if (SUCCEEDED(D3D12GetDebugInterface(IID_PPV_ARGS(debug.GetAddressOf())))) {
        debug->EnableDebugLayer();
    }
#endif

    ComPtr<IDXGIFactory4> factory;
    ThrowIfFailed(CreateDXGIFactory1(IID_PPV_ARGS(factory.GetAddressOf())), "CreateDXGIFactory1");

    ComPtr<IDXGIAdapter1> hardware_adapter;
    for (UINT i = 0;; ++i) {
        ComPtr<IDXGIAdapter1> candidate;
        const HRESULT enum_hr = factory->EnumAdapters1(i, candidate.ReleaseAndGetAddressOf());
        if (enum_hr == DXGI_ERROR_NOT_FOUND) {
            break;
        }
        ThrowIfFailed(enum_hr, "EnumAdapters1");

        DXGI_ADAPTER_DESC1 desc{};
        ThrowIfFailed(candidate->GetDesc1(&desc), "GetDesc1");
        const bool is_software = (desc.Flags & DXGI_ADAPTER_FLAG_SOFTWARE) != 0u;
        const bool supports_d3d12 = SUCCEEDED(D3D12CreateDevice(
            candidate.Get(), D3D_FEATURE_LEVEL_11_0, __uuidof(ID3D12Device), nullptr));
        if (!is_software && supports_d3d12) {
            hardware_adapter = std::move(candidate);
            break;
        }
    }

    ComPtr<ID3D12Device> device;
    HRESULT hr = E_FAIL;
    if (hardware_adapter) {
        hr = D3D12CreateDevice(hardware_adapter.Get(), D3D_FEATURE_LEVEL_11_0,
                               IID_PPV_ARGS(device.GetAddressOf()));
    }
    if (FAILED(hr)) {
        ComPtr<IDXGIAdapter1> warp_adapter;
        ThrowIfFailed(factory->EnumWarpAdapter(IID_PPV_ARGS(warp_adapter.GetAddressOf())),
                      "EnumWarpAdapter");
        ThrowIfFailed(D3D12CreateDevice(warp_adapter.Get(), D3D_FEATURE_LEVEL_11_0,
                                        IID_PPV_ARGS(device.GetAddressOf())),
                      "D3D12CreateDevice(WARP)");
    }
    return device;
}

} // namespace

std::unique_ptr<IDevice> D3D12Device::Create() {
    auto instance = std::unique_ptr<D3D12Device>(new D3D12Device());
    instance->device_ = CreateNativeDevice();

    D3D12_COMMAND_QUEUE_DESC queue_desc{};
    queue_desc.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
    ThrowIfFailed(instance->device_->CreateCommandQueue(
                      &queue_desc, IID_PPV_ARGS(instance->queue_.GetAddressOf())),
                  "CreateCommandQueue");

    ThrowIfFailed(
        instance->device_->CreateCommandAllocator(
            D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(instance->allocator_.GetAddressOf())),
        "CreateCommandAllocator");

    ThrowIfFailed(instance->device_->CreateFence(0, D3D12_FENCE_FLAG_NONE,
                                                 IID_PPV_ARGS(instance->fence_.GetAddressOf())),
                  "CreateFence");

    D3D12_DESCRIPTOR_HEAP_DESC rtv_heap_desc{};
    rtv_heap_desc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
    rtv_heap_desc.NumDescriptors = kRtvHeapCapacity;
    ThrowIfFailed(instance->device_->CreateDescriptorHeap(
                      &rtv_heap_desc, IID_PPV_ARGS(instance->rtv_heap_.GetAddressOf())),
                  "CreateDescriptorHeap(RTV)");
    instance->rtv_descriptor_size_ =
        instance->device_->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);

    instance->fence_event_ = CreateEventW(nullptr, FALSE, FALSE, nullptr);
    if (instance->fence_event_ == nullptr) {
        throw std::runtime_error("d3d12 backend failed to create fence event");
    }
    return instance;
}

D3D12Device::~D3D12Device() {
    if (fence_event_ != nullptr) {
        CloseHandle(fence_event_);
    }
}

BufferHandle D3D12Device::CreateBuffer(const BufferDesc&) {
    return BufferHandle::kInvalid;
}

void D3D12Device::DestroyBuffer(BufferHandle) {}

TextureHandle D3D12Device::CreateTexture(const TextureDesc& desc) {
    const DXGI_FORMAT native_format = ToNativeFormat(desc.format);

    D3D12_RESOURCE_DESC resource_desc{};
    resource_desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    resource_desc.Width = desc.width;
    resource_desc.Height = desc.height;
    resource_desc.DepthOrArraySize = 1;
    resource_desc.MipLevels = 1;
    resource_desc.Format = native_format;
    resource_desc.SampleDesc.Count = 1;
    if ((desc.usage & TextureUsage::kRenderTarget) != TextureUsage::kNone) {
        resource_desc.Flags |= D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;
    }

    TextureEntry entry{};
    entry.desc = resource_desc;
    D3D12_HEAP_PROPERTIES default_heap{};
    default_heap.Type = D3D12_HEAP_TYPE_DEFAULT;
    ThrowIfFailed(device_->CreateCommittedResource(&default_heap, D3D12_HEAP_FLAG_NONE,
                                                   &resource_desc, D3D12_RESOURCE_STATE_COMMON,
                                                   nullptr,
                                                   IID_PPV_ARGS(entry.resource.GetAddressOf())),
                  "CreateCommittedResource(texture)");

    if ((desc.usage & TextureUsage::kRenderTarget) != TextureUsage::kNone) {
        if (rtv_allocated_ >= kRtvHeapCapacity) {
            throw std::runtime_error("d3d12 backend: render target view heap exhausted");
        }
        D3D12_CPU_DESCRIPTOR_HANDLE rtv = rtv_heap_->GetCPUDescriptorHandleForHeapStart();
        rtv.ptr += static_cast<SIZE_T>(rtv_allocated_) * rtv_descriptor_size_;
        ++rtv_allocated_;
        device_->CreateRenderTargetView(entry.resource.Get(), nullptr, rtv);
        entry.rtv = rtv;
        entry.has_rtv = true;
    }

    const std::uint64_t handle_value = next_handle_++;
    textures_.emplace(handle_value, std::move(entry));
    return static_cast<TextureHandle>(handle_value);
}

void D3D12Device::DestroyTexture(TextureHandle handle) {
    WaitForGpuIdle();
    textures_.erase(static_cast<std::uint64_t>(handle));
}

PipelineHandle D3D12Device::CreatePipeline(const GraphicsPipelineDesc&) {
    return PipelineHandle::kInvalid;
}

void D3D12Device::DestroyPipeline(PipelineHandle) {}

ICommandList* D3D12Device::CreateCommandList() {
    auto* command_list = new D3D12CommandList(this);
    command_list->End();
    return command_list;
}

void D3D12Device::DestroyCommandList(ICommandList* command_list) {
    delete command_list;
}

ISwapchain* D3D12Device::CreateSwapchain(void*, std::uint32_t, std::uint32_t, Format) {
    return nullptr;
}

void D3D12Device::DestroySwapchain(ISwapchain*) {}

void D3D12Device::Submit(ICommandList& command_list) {
    auto& d3d12_list = static_cast<D3D12CommandList&>(command_list);
    ID3D12CommandList* lists[] = {d3d12_list.Native()};
    queue_->ExecuteCommandLists(static_cast<UINT>(std::size(lists)), lists);
    ++fence_value_;
    ThrowIfFailed(queue_->Signal(fence_.Get(), fence_value_), "QueueSignal");
}

void D3D12Device::WaitForGpuIdle() {
    ThrowIfFailed(fence_->SetEventOnCompletion(fence_value_, fence_event_), "SetEventOnCompletion");
    WaitForSingleObject(fence_event_, INFINITE);
    ThrowIfFailed(allocator_->Reset(), "AllocatorReset");
}

D3D12_CPU_DESCRIPTOR_HANDLE D3D12Device::RtvCpuHandle(TextureHandle handle) {
    const auto it = textures_.find(static_cast<std::uint64_t>(handle));
    if (it == textures_.end() || !it->second.has_rtv) {
        throw std::runtime_error("d3d12 backend: render target handle is not a valid target");
    }
    return it->second.rtv;
}

ID3D12Resource* D3D12Device::TextureResource(TextureHandle handle) {
    const auto it = textures_.find(static_cast<std::uint64_t>(handle));
    if (it == textures_.end()) {
        throw std::runtime_error("d3d12 backend: unknown texture handle");
    }
    return it->second.resource.Get();
}

ID3D12Resource* D3D12Device::EnsureReadBack(TextureHandle handle) {
    const std::uint64_t key = static_cast<std::uint64_t>(handle);
    const auto existing = read_backs_.find(key);
    if (existing != read_backs_.end()) {
        return existing->second.resource.Get();
    }

    const auto it = textures_.find(key);
    if (it == textures_.end()) {
        throw std::runtime_error("d3d12 backend: read back of an unknown texture");
    }
    const D3D12_RESOURCE_DESC source = it->second.desc;

    UINT64 total_bytes = 0;
    D3D12_PLACED_SUBRESOURCE_FOOTPRINT footprint{};
    device_->GetCopyableFootprints(&source, 0, 1, 0, &footprint, nullptr, nullptr, &total_bytes);

    D3D12_RESOURCE_DESC buffer_desc{};
    buffer_desc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    buffer_desc.Width = total_bytes;
    buffer_desc.Height = 1;
    buffer_desc.DepthOrArraySize = 1;
    buffer_desc.MipLevels = 1;
    buffer_desc.SampleDesc.Count = 1;
    buffer_desc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;

    D3D12_HEAP_PROPERTIES read_back_heap{};
    read_back_heap.Type = D3D12_HEAP_TYPE_READBACK;

    ComPtr<ID3D12Resource> resource;
    ThrowIfFailed(device_->CreateCommittedResource(&read_back_heap, D3D12_HEAP_FLAG_NONE,
                                                   &buffer_desc, D3D12_RESOURCE_STATE_COPY_DEST,
                                                   nullptr, IID_PPV_ARGS(resource.GetAddressOf())),
                  "CreateCommittedResource(read-back)");

    ReadBackEntry entry{};
    entry.row_pitch_bytes = footprint.Footprint.RowPitch;
    D3D12_RANGE mapped_range{0, 0};
    resource->Map(0, &mapped_range, reinterpret_cast<void**>(&entry.mapped));
    entry.resource = std::move(resource);

    auto [inserted, ok] = read_backs_.emplace(key, std::move(entry));
    if (!ok) {
        throw std::runtime_error("d3d12 backend: duplicate read-back registration");
    }
    return inserted->second.resource.Get();
}

MappedTexture D3D12Device::MapReadBack(TextureHandle handle) {
    const auto it = textures_.find(static_cast<std::uint64_t>(handle));
    if (it == textures_.end() || !it->second.has_rtv) {
        throw std::runtime_error("d3d12 backend: read back of an unknown texture");
    }
    ID3D12Resource* source_resource = it->second.resource.Get();
    ID3D12Resource* staging = EnsureReadBack(handle);

    ICommandList* copy_list = CreateCommandList();
    copy_list->Begin();
    static_cast<D3D12CommandList*>(copy_list)->CopyTextureToReadBack(source_resource, staging);
    copy_list->End();
    Submit(*copy_list);
    DestroyCommandList(copy_list);
    WaitForGpuIdle();

    const auto staged = read_backs_.find(static_cast<std::uint64_t>(handle));
    return MappedTexture{staged->second.mapped, staged->second.row_pitch_bytes};
}

D3D12CommandList::D3D12CommandList(D3D12Device* owner)
    : owner_(owner), allocator_(owner->Allocator()) {
    ThrowIfFailed(owner->Native()->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT,
                                                     allocator_.Get(), nullptr,
                                                     IID_PPV_ARGS(command_list_.GetAddressOf())),
                  "CreateCommandList");
}

void D3D12CommandList::Begin() {
    ThrowIfFailed(command_list_->Reset(allocator_.Get(), nullptr), "CommandListReset");
}

void D3D12CommandList::End() {
    ThrowIfFailed(command_list_->Close(), "CommandListClose");
}

void D3D12CommandList::BeginRendering(TextureHandle color_target, const ClearColor& clear_color) {
    D3D12_CPU_DESCRIPTOR_HANDLE rtv = owner_->RtvCpuHandle(color_target);

    D3D12_RESOURCE_BARRIER to_render_target{};
    to_render_target.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    to_render_target.Transition.pResource = owner_->TextureResource(color_target);
    to_render_target.Transition.StateBefore = D3D12_RESOURCE_STATE_COMMON;
    to_render_target.Transition.StateAfter = D3D12_RESOURCE_STATE_RENDER_TARGET;
    to_render_target.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    command_list_->ResourceBarrier(1, &to_render_target);

    command_list_->OMSetRenderTargets(1, &rtv, FALSE, nullptr);
    const float color[4] = {clear_color.r, clear_color.g, clear_color.b, clear_color.a};
    command_list_->ClearRenderTargetView(rtv, color, 0, nullptr);
}

void D3D12CommandList::EndRendering() {}

void D3D12CommandList::CopyTextureToReadBack(ID3D12Resource* source, ID3D12Resource* staging) {
    D3D12_RESOURCE_BARRIER to_copy_source{};
    to_copy_source.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    to_copy_source.Transition.pResource = source;
    to_copy_source.Transition.StateBefore = D3D12_RESOURCE_STATE_COMMON;
    to_copy_source.Transition.StateAfter = D3D12_RESOURCE_STATE_COPY_SOURCE;
    to_copy_source.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    command_list_->ResourceBarrier(1, &to_copy_source);

    D3D12_RESOURCE_DESC source_desc = source->GetDesc();
    D3D12_PLACED_SUBRESOURCE_FOOTPRINT footprint{};
    owner_->Native()->GetCopyableFootprints(&source_desc, 0, 1, 0, &footprint, nullptr, nullptr,
                                            nullptr);

    D3D12_TEXTURE_COPY_LOCATION source_location{};
    source_location.pResource = source;
    source_location.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
    D3D12_TEXTURE_COPY_LOCATION destination_location{};
    destination_location.pResource = staging;
    destination_location.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
    destination_location.PlacedFootprint = footprint;
    command_list_->CopyTextureRegion(&destination_location, 0, 0, 0, &source_location, nullptr);
}

void D3D12CommandList::SetPipeline(PipelineHandle) {
    throw std::runtime_error("d3d12 backend: SetPipeline is not implemented yet");
}

void D3D12CommandList::Draw(std::uint32_t, std::uint32_t) {
    throw std::runtime_error("d3d12 backend: Draw is not implemented yet");
}

void D3D12CommandList::CopyTexture(TextureHandle, TextureHandle) {
    throw std::runtime_error("d3d12 backend: CopyTexture is not implemented yet");
}

} // namespace jrpgmaker::rhi::d3d12

namespace jrpgmaker::rhi {

std::unique_ptr<IDevice> CreateDevice(Backend backend) {
    if (backend == Backend::kD3D12) {
        return d3d12::D3D12Device::Create();
    }
    throw std::runtime_error("no rhi backend available for the requested graphics api");
}

} // namespace jrpgmaker::rhi

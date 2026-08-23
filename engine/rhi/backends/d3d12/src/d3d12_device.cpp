#include "d3d12_device.h"

#include <dxgi1_4.h>

#include <format>
#include <memory>
#include <stdexcept>
#include <utility>

namespace jrpgmaker::rhi::d3d12 {
namespace {

using Microsoft::WRL::ComPtr;

void ThrowIfFailed(HRESULT hr, const char* context) {
    if (FAILED(hr)) {
        throw std::runtime_error(std::format("d3d12 backend failed in {}: hr=0x{:08X}", context,
                                             static_cast<unsigned int>(hr)));
    }
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

TextureHandle D3D12Device::CreateTexture(const TextureDesc&) {
    return TextureHandle::kInvalid;
}

void D3D12Device::DestroyTexture(TextureHandle) {}

PipelineHandle D3D12Device::CreatePipeline(const GraphicsPipelineDesc&) {
    return PipelineHandle::kInvalid;
}

void D3D12Device::DestroyPipeline(PipelineHandle) {}

ICommandList* D3D12Device::CreateCommandList() {
    auto* command_list = new D3D12CommandList(device_.Get(), allocator_.Get());
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

const std::byte* D3D12Device::MapReadBack(TextureHandle) {
    return nullptr;
}

D3D12CommandList::D3D12CommandList(ID3D12Device* device, ID3D12CommandAllocator* allocator)
    : allocator_(allocator) {
    ThrowIfFailed(device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, allocator, nullptr,
                                            IID_PPV_ARGS(command_list_.GetAddressOf())),
                  "CreateCommandList");
}

void D3D12CommandList::Begin() {
    ThrowIfFailed(command_list_->Reset(allocator_.Get(), nullptr), "CommandListReset");
}

void D3D12CommandList::End() {
    ThrowIfFailed(command_list_->Close(), "CommandListClose");
}

void D3D12CommandList::BeginRendering(TextureHandle, const ClearColor&) {
    throw std::runtime_error("d3d12 backend: BeginRendering is not implemented yet");
}

void D3D12CommandList::EndRendering() {
    throw std::runtime_error("d3d12 backend: EndRendering is not implemented yet");
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

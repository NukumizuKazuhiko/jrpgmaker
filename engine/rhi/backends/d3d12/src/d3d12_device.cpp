#include "d3d12_device.h"

#include <dxgi1_4.h>

#include <cstring>
#include <format>
#include <memory>
#include <stdexcept>
#include <utility>
#include <vector>

#include "d3d12_swapchain.h"

namespace jrpgmaker::rhi::d3d12 {
namespace {

using Microsoft::WRL::ComPtr;

constexpr UINT kRtvHeapCapacity = 64;
constexpr UINT kSrvHeapCapacity = 64;
constexpr UINT kSamplerHeapCapacity = 16;

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
    ThrowIfFailed(CreateDXGIFactory1(IID_PPV_ARGS(instance->factory_.GetAddressOf())),
                  "CreateDXGIFactory1");
    instance->device_ = CreateNativeDevice();
    instance->device_.As(&instance->info_queue_);

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

    D3D12_DESCRIPTOR_HEAP_DESC srv_heap_desc{};
    srv_heap_desc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
    srv_heap_desc.NumDescriptors = kSrvHeapCapacity;
    srv_heap_desc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
    ThrowIfFailed(instance->device_->CreateDescriptorHeap(
                      &srv_heap_desc, IID_PPV_ARGS(instance->srv_heap_.GetAddressOf())),
                  "CreateDescriptorHeap(CBV_SRV_UAV)");
    instance->srv_descriptor_size_ =
        instance->device_->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);

    D3D12_DESCRIPTOR_HEAP_DESC sampler_heap_desc{};
    sampler_heap_desc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER;
    sampler_heap_desc.NumDescriptors = kSamplerHeapCapacity;
    sampler_heap_desc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
    ThrowIfFailed(instance->device_->CreateDescriptorHeap(
                      &sampler_heap_desc, IID_PPV_ARGS(instance->sampler_heap_.GetAddressOf())),
                  "CreateDescriptorHeap(SAMPLER)");
    instance->sampler_descriptor_size_ =
        instance->device_->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER);

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

BufferHandle D3D12Device::CreateBuffer(const BufferDesc& desc) {
    if (desc.usage == BufferUsage::kNone) {
        throw std::runtime_error("d3d12 backend: buffer creation requires at least one usage bit");
    }
    if (desc.size_bytes == 0) {
        throw std::runtime_error("d3d12 backend: buffer size must be non-zero");
    }

    D3D12_RESOURCE_DESC resource_desc{};
    resource_desc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    resource_desc.Width = desc.size_bytes;
    resource_desc.Height = 1;
    resource_desc.DepthOrArraySize = 1;
    resource_desc.MipLevels = 1;
    resource_desc.SampleDesc.Count = 1;
    resource_desc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;

    // v0 buffers are host-visible (UPLOAD heap) so MapWrite needs no staging
    // copy; this matches the Vulkan backend's host-visible allocation.
    D3D12_HEAP_PROPERTIES upload_heap{};
    upload_heap.Type = D3D12_HEAP_TYPE_UPLOAD;

    BufferEntry entry{};
    entry.size_bytes = desc.size_bytes;
    ThrowIfFailed(device_->CreateCommittedResource(&upload_heap, D3D12_HEAP_FLAG_NONE,
                                                   &resource_desc,
                                                   D3D12_RESOURCE_STATE_GENERIC_READ, nullptr,
                                                   IID_PPV_ARGS(entry.resource.GetAddressOf())),
                  "CreateCommittedResource(buffer)");

    D3D12_RANGE read_range{0, 0};
    ThrowIfFailed(entry.resource->Map(0, &read_range, reinterpret_cast<void**>(&entry.mapped)),
                  "Map(buffer)");
    if (entry.mapped == nullptr) {
        throw std::runtime_error("d3d12 backend: failed to map buffer");
    }

    const std::uint64_t handle_value = next_handle_++;
    buffers_.emplace(handle_value, std::move(entry));
    return static_cast<BufferHandle>(handle_value);
}

void D3D12Device::DestroyBuffer(BufferHandle handle) {
    const auto it = buffers_.find(static_cast<std::uint64_t>(handle));
    if (it == buffers_.end()) {
        return;
    }
    WaitForGpuIdle();
    it->second.resource->Unmap(0, nullptr);
    buffers_.erase(it);
}

void D3D12Device::MapWrite(BufferHandle handle, const void* data, std::uint64_t size_bytes) {
    const BufferEntry& entry = BufferResource(handle);
    if (data == nullptr || size_bytes > entry.size_bytes) {
        throw std::runtime_error("d3d12 backend: MapWrite data exceeds buffer capacity");
    }
    std::memcpy(entry.mapped, data, static_cast<std::size_t>(size_bytes));
}

const D3D12Device::BufferEntry& D3D12Device::BufferResource(BufferHandle handle) {
    const auto it = buffers_.find(static_cast<std::uint64_t>(handle));
    if (it == buffers_.end()) {
        throw std::runtime_error("d3d12 backend: unknown buffer handle");
    }
    return it->second;
}

PipelineHandle D3D12Device::CreatePipeline(const GraphicsPipelineDesc& desc) {
    if (root_signature_ == nullptr) {
        // Root signature, shared by all pipelines:
        //  - parameter 0: 32-bit root constants (the v0 push-constant block,
        //    a single 64-byte view-proj matrix = 16 DWORDs, vertex shader).
        //  - parameter 1: descriptor table with one SRV (register t0, pixel
        //    shader) for the v0 sampled-texture slot.
        //  - parameter 2: descriptor table with one sampler (register s0,
        //    pixel shader) for the same slot.
        // Pipelines that never sample simply never bind parameters 1/2; the
        // shaders they use do not reference the resources, which D3D12 allows.
        D3D12_DESCRIPTOR_RANGE ranges[2]{};
        ranges[0].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
        ranges[0].NumDescriptors = 1;
        ranges[0].BaseShaderRegister = 0;
        ranges[1].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SAMPLER;
        ranges[1].NumDescriptors = 1;
        ranges[1].BaseShaderRegister = 0;

        D3D12_ROOT_PARAMETER root_parameters[3]{};
        root_parameters[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
        root_parameters[0].Constants.ShaderRegister = 0;
        root_parameters[0].Constants.RegisterSpace = 0;
        root_parameters[0].Constants.Num32BitValues = 16;
        root_parameters[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_VERTEX;

        root_parameters[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
        root_parameters[1].DescriptorTable.NumDescriptorRanges = 1;
        root_parameters[1].DescriptorTable.pDescriptorRanges = &ranges[0];
        root_parameters[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;

        root_parameters[2].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
        root_parameters[2].DescriptorTable.NumDescriptorRanges = 1;
        root_parameters[2].DescriptorTable.pDescriptorRanges = &ranges[1];
        root_parameters[2].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;

        D3D12_ROOT_SIGNATURE_DESC root_desc{};
        root_desc.NumParameters = static_cast<UINT>(std::size(root_parameters));
        root_desc.pParameters = root_parameters;
        root_desc.NumStaticSamplers = 0;
        // Without ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT the pipeline state cannot
        // bind vertex input via the input assembler (P2 vertex buffers); the P1
        // triangle generated geometry in-shader and never needed it.
        root_desc.Flags = D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;

        D3D12_VERSIONED_ROOT_SIGNATURE_DESC versioned_desc{};
        versioned_desc.Version = D3D_ROOT_SIGNATURE_VERSION_1_0;
        versioned_desc.Desc_1_0 = root_desc;
        Microsoft::WRL::ComPtr<ID3DBlob> signature_blob;
        Microsoft::WRL::ComPtr<ID3DBlob> signature_error;
        const HRESULT serialize_hr = D3D12SerializeVersionedRootSignature(
            &versioned_desc, &signature_blob, &signature_error);
        if (FAILED(serialize_hr)) {
            const char* message =
                signature_error ? static_cast<const char*>(signature_error->GetBufferPointer())
                                : "";
            throw std::runtime_error(
                std::format("d3d12 backend failed to serialize root signature: {} (hr=0x{:08X})",
                            message, static_cast<unsigned int>(serialize_hr)));
        }
        ThrowIfFailed(device_->CreateRootSignature(0, signature_blob->GetBufferPointer(),
                                                   signature_blob->GetBufferSize(),
                                                   IID_PPV_ARGS(root_signature_.GetAddressOf())),
                      "CreateRootSignature");
    }

    D3D12_GRAPHICS_PIPELINE_STATE_DESC pso_desc{};
    pso_desc.pRootSignature = root_signature_.Get();
    pso_desc.VS = D3D12_SHADER_BYTECODE{desc.vertex_shader.data, desc.vertex_shader.size};
    pso_desc.PS = D3D12_SHADER_BYTECODE{desc.pixel_shader.data, desc.pixel_shader.size};
    pso_desc.BlendState.RenderTarget[0].RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;
    pso_desc.SampleMask = D3D12_DEFAULT_SAMPLE_MASK;
    pso_desc.RasterizerState.FillMode = D3D12_FILL_MODE_SOLID;
    pso_desc.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;
    pso_desc.DepthStencilState.DepthEnable = FALSE;
    pso_desc.DepthStencilState.StencilEnable = FALSE;
    pso_desc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
    pso_desc.NumRenderTargets = 1;
    pso_desc.RTVFormats[0] = ToNativeFormat(desc.color_format);
    pso_desc.SampleDesc.Count = 1;
    pso_desc.SampleDesc.Quality = 0;

    // Vertex input layout (P2): maps the contract's VertexInputLayout to D3D12
    // input elements, all bound to input slot 0 (single interleaved buffer).
    std::vector<D3D12_INPUT_ELEMENT_DESC> input_elements;
    if (desc.vertex_input.attributes != nullptr) {
        if (desc.vertex_input.attribute_count == 0 || desc.vertex_input.stride_bytes == 0) {
            throw std::runtime_error(
                "d3d12 backend: vertex input layout requires attributes and a stride");
        }
        input_elements.reserve(desc.vertex_input.attribute_count);
        for (std::uint32_t i = 0; i < desc.vertex_input.attribute_count; ++i) {
            const VertexAttribute& attribute = desc.vertex_input.attributes[i];
            D3D12_INPUT_ELEMENT_DESC element{};
            element.SemanticName = attribute.semantic_name;
            element.SemanticIndex = 0;
            if (attribute.format == VertexAttributeFormat::kFloat3) {
                element.Format = DXGI_FORMAT_R32G32B32_FLOAT;
            } else if (attribute.format == VertexAttributeFormat::kFloat2) {
                element.Format = DXGI_FORMAT_R32G32_FLOAT;
            } else {
                throw std::runtime_error("d3d12 backend: unsupported vertex attribute format");
            }
            element.InputSlot = 0;
            element.AlignedByteOffset = attribute.offset_bytes;
            element.InputSlotClass = D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA;
            element.InstanceDataStepRate = 0;
            input_elements.push_back(element);
        }
        pso_desc.InputLayout.NumElements = static_cast<UINT>(input_elements.size());
        pso_desc.InputLayout.pInputElementDescs = input_elements.data();
    }

    PipelineEntry entry{};
    entry.sample_slot = desc.sample_slot;
    ThrowIfFailed(device_->CreateGraphicsPipelineState(&pso_desc,
                                                       IID_PPV_ARGS(entry.pipeline.GetAddressOf())),
                  "CreateGraphicsPipelineState");

    const std::uint64_t handle_value = next_handle_++;
    pipelines_.emplace(handle_value, std::move(entry));
    return static_cast<PipelineHandle>(handle_value);
}

void D3D12Device::DestroyPipeline(PipelineHandle handle) {
    pipelines_.erase(static_cast<std::uint64_t>(handle));
}

ID3D12PipelineState* D3D12Device::PipelineState(PipelineHandle handle) {
    const auto it = pipelines_.find(static_cast<std::uint64_t>(handle));
    if (it == pipelines_.end()) {
        throw std::runtime_error("d3d12 backend: unknown pipeline handle");
    }
    return it->second.pipeline.Get();
}

std::uint32_t D3D12Device::PipelineSampleSlot(PipelineHandle handle) {
    const auto it = pipelines_.find(static_cast<std::uint64_t>(handle));
    if (it == pipelines_.end()) {
        throw std::runtime_error("d3d12 backend: unknown pipeline handle");
    }
    return it->second.sample_slot;
}

TextureHandle D3D12Device::CreateTexture(const TextureDesc& desc) {
    const DXGI_FORMAT native_format = ToNativeFormat(desc.format);

    if (desc.usage == TextureUsage::kNone) {
        throw std::runtime_error("d3d12 backend: texture creation requires at least one usage bit");
    }

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

    D3D12_CLEAR_VALUE clear_value{};
    clear_value.Format = native_format;
    clear_value.Color[0] = 0.0f;
    clear_value.Color[1] = 0.0f;
    clear_value.Color[2] = 0.0f;
    clear_value.Color[3] = 1.0f;
    const D3D12_CLEAR_VALUE* clear_value_ptr = nullptr;
    if ((desc.usage & TextureUsage::kRenderTarget) != TextureUsage::kNone) {
        clear_value_ptr = &clear_value;
    }
    ThrowIfFailed(device_->CreateCommittedResource(&default_heap, D3D12_HEAP_FLAG_NONE,
                                                   &resource_desc, D3D12_RESOURCE_STATE_COMMON,
                                                   clear_value_ptr,
                                                   IID_PPV_ARGS(entry.resource.GetAddressOf())),
                  "CreateCommittedResource(texture)");

    if ((desc.usage & TextureUsage::kRenderTarget) != TextureUsage::kNone) {
        entry.rtv_slot = AllocateRtvSlot();
        D3D12_CPU_DESCRIPTOR_HANDLE rtv = rtv_heap_->GetCPUDescriptorHandleForHeapStart();
        rtv.ptr += static_cast<SIZE_T>(entry.rtv_slot) * rtv_descriptor_size_;
        device_->CreateRenderTargetView(entry.resource.Get(), nullptr, rtv);
        entry.rtv = rtv;
        entry.has_rtv = true;
    }

    if ((desc.usage & TextureUsage::kSampled) != TextureUsage::kNone) {
        entry.srv_slot = AllocateDescriptorSlot(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
        D3D12_CPU_DESCRIPTOR_HANDLE srv =
            CpuDescriptorHandle(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, entry.srv_slot);
        D3D12_SHADER_RESOURCE_VIEW_DESC srv_desc{};
        srv_desc.Format = native_format;
        srv_desc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
        srv_desc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
        srv_desc.Texture2D.MipLevels = 1;
        device_->CreateShaderResourceView(entry.resource.Get(), &srv_desc, srv);
        entry.srv = srv;
        entry.has_srv = true;
    }

    const std::uint64_t handle_value = next_handle_++;
    textures_.emplace(handle_value, std::move(entry));
    return static_cast<TextureHandle>(handle_value);
}

void D3D12Device::DestroyTexture(TextureHandle handle) {
    const std::uint64_t key = static_cast<std::uint64_t>(handle);
    const auto it = textures_.find(key);
    if (it != textures_.end() && it->second.is_swapchain) {
        throw std::runtime_error(
            "d3d12 backend: swapchain back buffers are owned by the swapchain");
    }
    WaitForGpuIdle();
    read_backs_.erase(key);
    if (it != textures_.end() && it->second.has_rtv) {
        ReleaseRtvSlot(it->second.rtv_slot);
    }
    if (it != textures_.end() && it->second.has_srv) {
        ReleaseDescriptorSlot(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, it->second.srv_slot);
    }
    textures_.erase(key);
}

SamplerHandle D3D12Device::CreateSampler(const SamplerDesc& desc) {
    const UINT slot = AllocateDescriptorSlot(D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER);

    D3D12_SAMPLER_DESC sampler_desc{};
    sampler_desc.Filter = (desc.filter == SamplerFilter::kLinear) ? D3D12_FILTER_MIN_MAG_MIP_LINEAR
                                                                  : D3D12_FILTER_MIN_MAG_MIP_POINT;
    sampler_desc.AddressU = (desc.address == SamplerAddress::kRepeat)
                                ? D3D12_TEXTURE_ADDRESS_MODE_WRAP
                                : D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
    sampler_desc.AddressV = sampler_desc.AddressU;
    sampler_desc.AddressW = sampler_desc.AddressU;
    sampler_desc.ComparisonFunc = D3D12_COMPARISON_FUNC_NEVER;
    sampler_desc.MinLOD = 0.0f;
    sampler_desc.MaxLOD = D3D12_FLOAT32_MAX;

    const D3D12_CPU_DESCRIPTOR_HANDLE descriptor =
        CpuDescriptorHandle(D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER, slot);
    device_->CreateSampler(&sampler_desc, descriptor);

    SamplerEntry entry{};
    entry.descriptor = descriptor;
    entry.slot = slot;
    const std::uint64_t handle_value = next_handle_++;
    samplers_.emplace(handle_value, std::move(entry));
    return static_cast<SamplerHandle>(handle_value);
}

void D3D12Device::DestroySampler(SamplerHandle handle) {
    const auto it = samplers_.find(static_cast<std::uint64_t>(handle));
    if (it == samplers_.end()) {
        return;
    }
    ReleaseDescriptorSlot(D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER, it->second.slot);
    samplers_.erase(it);
}

void D3D12Device::UploadTexture(TextureHandle handle, const void* data,
                                std::uint64_t row_pitch_bytes) {
    if (data == nullptr || row_pitch_bytes == 0) {
        throw std::runtime_error("d3d12 backend: invalid texture upload data");
    }
    const std::uint64_t key = static_cast<std::uint64_t>(handle);
    const auto it = textures_.find(key);
    if (it == textures_.end() || !it->second.has_srv) {
        throw std::runtime_error("d3d12 backend: upload requires a texture created with kSampled");
    }
    if (it->second.is_swapchain) {
        throw std::runtime_error("d3d12 backend: cannot upload into a swapchain texture");
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

    D3D12_HEAP_PROPERTIES upload_heap{};
    upload_heap.Type = D3D12_HEAP_TYPE_UPLOAD;

    Microsoft::WRL::ComPtr<ID3D12Resource> staging;
    ThrowIfFailed(device_->CreateCommittedResource(&upload_heap, D3D12_HEAP_FLAG_NONE, &buffer_desc,
                                                   D3D12_RESOURCE_STATE_GENERIC_READ, nullptr,
                                                   IID_PPV_ARGS(staging.GetAddressOf())),
                  "CreateCommittedResource(upload staging)");

    std::byte* mapped = nullptr;
    D3D12_RANGE write_range{0, 0};
    ThrowIfFailed(staging->Map(0, &write_range, reinterpret_cast<void**>(&mapped)),
                  "Map(upload staging)");
    if (mapped == nullptr) {
        throw std::runtime_error("d3d12 backend: failed to map upload staging buffer");
    }
    // The staging buffer uses the texture's footprint row pitch (aligned); the
    // caller's pitch is copied row-by-row so tight/legacy layouts both work.
    // Copy only the source's declared pitch per row - footprint RowPitch can be
    // larger (256-byte aligned) and the source buffer only guarantees
    // row_pitch_bytes, so copying the full footprint pitch would read OOB.
    const std::uint64_t pitch = footprint.Footprint.RowPitch;
    const std::uint64_t height = footprint.Footprint.Height;
    const std::uint64_t copy_bytes = std::min(pitch, row_pitch_bytes);
    const auto* src = static_cast<const std::byte*>(data);
    for (std::uint64_t row = 0; row < height; ++row) {
        std::memcpy(mapped + row * pitch, src + row * row_pitch_bytes,
                    static_cast<std::size_t>(copy_bytes));
    }
    staging->Unmap(0, nullptr);

    Microsoft::WRL::ComPtr<ID3D12CommandAllocator> copy_allocator;
    ThrowIfFailed(device_->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT,
                                                  IID_PPV_ARGS(copy_allocator.GetAddressOf())),
                  "CreateCommandAllocator(upload)");

    ICommandList* copy_list = CreateCommandList();
    static_cast<D3D12CommandList*>(copy_list)->SetAllocator(copy_allocator.Get());
    copy_list->Begin();
    static_cast<D3D12CommandList*>(copy_list)->CopyBufferToTexture(
        staging.Get(), it->second.resource.Get(), footprint);
    copy_list->End();
    Submit(*copy_list);
    DestroyCommandList(copy_list);
    WaitForGpuIdle();
}

ICommandList* D3D12Device::CreateCommandList() {
    auto* command_list = new D3D12CommandList(this);
    command_list->End();
    return command_list;
}

void D3D12Device::DestroyCommandList(ICommandList* command_list) {
    delete command_list;
}

ISwapchain* D3D12Device::CreateSwapchain(void* native_window_handle, std::uint32_t width,
                                         std::uint32_t height, Format format) {
    return new D3D12Swapchain(this, native_window_handle, width, height, format);
}

void D3D12Device::DestroySwapchain(ISwapchain* swapchain) {
    delete static_cast<D3D12Swapchain*>(swapchain);
}

TextureHandle D3D12Device::RegisterSwapchainBuffer(ID3D12Resource* resource, std::uint32_t width,
                                                   std::uint32_t height, Format format) {
    (void) width;
    (void) height;
    (void) format;
    TextureEntry entry{};
    entry.rtv_slot = AllocateRtvSlot();
    D3D12_CPU_DESCRIPTOR_HANDLE rtv = rtv_heap_->GetCPUDescriptorHandleForHeapStart();
    rtv.ptr += static_cast<SIZE_T>(entry.rtv_slot) * rtv_descriptor_size_;
    device_->CreateRenderTargetView(resource, nullptr, rtv);

    entry.resource = resource;
    entry.desc = resource->GetDesc();
    entry.rtv = rtv;
    entry.has_rtv = true;
    entry.is_swapchain = true;

    const std::uint64_t handle_value = next_handle_++;
    textures_.emplace(handle_value, std::move(entry));
    return static_cast<TextureHandle>(handle_value);
}

void D3D12Device::UnregisterSwapchainBuffer(TextureHandle handle) {
    const std::uint64_t key = static_cast<std::uint64_t>(handle);
    const auto it = textures_.find(key);
    if (it == textures_.end()) {
        return;
    }
    if (it->second.has_rtv) {
        ReleaseRtvSlot(it->second.rtv_slot);
    }
    textures_.erase(key);
}

UINT D3D12Device::AllocateRtvSlot() {
    if (!rtv_free_slots_.empty()) {
        const UINT slot = rtv_free_slots_.back();
        rtv_free_slots_.pop_back();
        return slot;
    }
    if (rtv_allocated_ >= kRtvHeapCapacity) {
        throw std::runtime_error("d3d12 backend: render target view heap exhausted");
    }
    return rtv_allocated_++;
}

void D3D12Device::ReleaseRtvSlot(UINT slot) {
    rtv_free_slots_.push_back(slot);
}

UINT D3D12Device::AllocateDescriptorSlot(D3D12_DESCRIPTOR_HEAP_TYPE heap_type) {
    if (heap_type == D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV) {
        if (!srv_free_slots_.empty()) {
            const UINT slot = srv_free_slots_.back();
            srv_free_slots_.pop_back();
            return slot;
        }
        if (srv_allocated_ >= kSrvHeapCapacity) {
            throw std::runtime_error("d3d12 backend: shader resource view heap exhausted");
        }
        return srv_allocated_++;
    }
    if (heap_type == D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER) {
        if (!sampler_free_slots_.empty()) {
            const UINT slot = sampler_free_slots_.back();
            sampler_free_slots_.pop_back();
            return slot;
        }
        if (sampler_allocated_ >= kSamplerHeapCapacity) {
            throw std::runtime_error("d3d12 backend: sampler heap exhausted");
        }
        return sampler_allocated_++;
    }
    throw std::runtime_error("d3d12 backend: unsupported descriptor heap type");
}

void D3D12Device::ReleaseDescriptorSlot(D3D12_DESCRIPTOR_HEAP_TYPE heap_type, UINT slot) {
    if (heap_type == D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV) {
        srv_free_slots_.push_back(slot);
    } else if (heap_type == D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER) {
        sampler_free_slots_.push_back(slot);
    }
}

D3D12_CPU_DESCRIPTOR_HANDLE D3D12Device::CpuDescriptorHandle(D3D12_DESCRIPTOR_HEAP_TYPE heap_type,
                                                             UINT slot) {
    Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> heap;
    UINT descriptor_size = 0;
    if (heap_type == D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV) {
        heap = srv_heap_;
        descriptor_size = srv_descriptor_size_;
    } else if (heap_type == D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER) {
        heap = sampler_heap_;
        descriptor_size = sampler_descriptor_size_;
    } else {
        throw std::runtime_error("d3d12 backend: unsupported descriptor heap type");
    }
    D3D12_CPU_DESCRIPTOR_HANDLE handle = heap->GetCPUDescriptorHandleForHeapStart();
    handle.ptr += static_cast<SIZE_T>(slot) * descriptor_size;
    return handle;
}

D3D12_GPU_DESCRIPTOR_HANDLE D3D12Device::GpuDescriptorHandle(D3D12_DESCRIPTOR_HEAP_TYPE heap_type,
                                                             UINT slot) {
    Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> heap;
    UINT descriptor_size = 0;
    if (heap_type == D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV) {
        heap = srv_heap_;
        descriptor_size = srv_descriptor_size_;
    } else if (heap_type == D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER) {
        heap = sampler_heap_;
        descriptor_size = sampler_descriptor_size_;
    } else {
        throw std::runtime_error("d3d12 backend: unsupported descriptor heap type");
    }
    D3D12_GPU_DESCRIPTOR_HANDLE handle = heap->GetGPUDescriptorHandleForHeapStart();
    handle.ptr += static_cast<UINT64>(slot) * descriptor_size;
    return handle;
}

D3D12_GPU_DESCRIPTOR_HANDLE D3D12Device::SrvGpuHandle(TextureHandle handle) {
    const auto it = textures_.find(static_cast<std::uint64_t>(handle));
    if (it == textures_.end() || !it->second.has_srv) {
        throw std::runtime_error("d3d12 backend: texture was not created with kSampled");
    }
    return GpuDescriptorHandle(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, it->second.srv_slot);
}

D3D12_GPU_DESCRIPTOR_HANDLE D3D12Device::SamplerGpuHandle(SamplerHandle handle) {
    const auto it = samplers_.find(static_cast<std::uint64_t>(handle));
    if (it == samplers_.end()) {
        throw std::runtime_error("d3d12 backend: unknown sampler handle");
    }
    return GpuDescriptorHandle(D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER, it->second.slot);
}

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
    CheckGpuErrors();
    const HRESULT removed = device_->GetDeviceRemovedReason();
    if (FAILED(removed)) {
        throw std::runtime_error(std::format("d3d12 backend device removed: hr=0x{:08X}",
                                             static_cast<unsigned>(removed)));
    }
}

void D3D12Device::CheckGpuErrors() {
    if (info_queue_ == nullptr) {
        return;
    }
    for (UINT64 i = 0; i < info_queue_->GetNumStoredMessages(); ++i) {
        D3D12_MESSAGE message{};
        SIZE_T message_size = sizeof(message);
        if (info_queue_->GetMessage(i, &message, &message_size) == S_OK) {
            if (message.Severity <= D3D12_MESSAGE_SEVERITY_ERROR) {
                info_queue_->ClearStoredMessages();
                const char* description = message.pDescription ? message.pDescription : "";
                throw std::runtime_error(std::format("d3d12 backend gpu error: {}", description));
            }
        }
    }
    info_queue_->ClearStoredMessages();
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
    if (entry.mapped == nullptr) {
        throw std::runtime_error("d3d12 backend: failed to map read-back buffer");
    }
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

    // The copy command list uses its own allocator so it can never collide with
    // the render command list that shares the device-level allocator.
    Microsoft::WRL::ComPtr<ID3D12CommandAllocator> copy_allocator;
    ThrowIfFailed(device_->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT,
                                                  IID_PPV_ARGS(copy_allocator.GetAddressOf())),
                  "CreateCommandAllocator(copy)");

    ICommandList* copy_list = CreateCommandList();
    static_cast<D3D12CommandList*>(copy_list)->SetAllocator(copy_allocator.Get());
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
    rendering_target_ = owner_->TextureResource(color_target);
    rendering_rtv_ = rtv;
    const D3D12_RESOURCE_DESC target_desc = rendering_target_->GetDesc();

    D3D12_VIEWPORT viewport{};
    viewport.Width = static_cast<float>(target_desc.Width);
    viewport.Height = static_cast<float>(target_desc.Height);
    viewport.MaxDepth = 1.0f;
    D3D12_RECT scissor{0, 0, static_cast<LONG>(target_desc.Width),
                       static_cast<LONG>(target_desc.Height)};
    command_list_->RSSetViewports(1, &viewport);
    command_list_->RSSetScissorRects(1, &scissor);

    D3D12_RESOURCE_BARRIER to_render_target{};
    to_render_target.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    to_render_target.Transition.pResource = rendering_target_;
    to_render_target.Transition.StateBefore = D3D12_RESOURCE_STATE_COMMON;
    to_render_target.Transition.StateAfter = D3D12_RESOURCE_STATE_RENDER_TARGET;
    to_render_target.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    command_list_->ResourceBarrier(1, &to_render_target);

    command_list_->OMSetRenderTargets(1, &rtv, FALSE, nullptr);
    const float color[4] = {clear_color.r, clear_color.g, clear_color.b, clear_color.a};
    command_list_->ClearRenderTargetView(rtv, color, 0, nullptr);
}

void D3D12CommandList::EndRendering() {
    if (rendering_target_ == nullptr) {
        return;
    }
    D3D12_RESOURCE_BARRIER to_common{};
    to_common.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    to_common.Transition.pResource = rendering_target_;
    to_common.Transition.StateBefore = D3D12_RESOURCE_STATE_RENDER_TARGET;
    to_common.Transition.StateAfter = D3D12_RESOURCE_STATE_COMMON;
    to_common.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    command_list_->ResourceBarrier(1, &to_common);
    rendering_target_ = nullptr;
}

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

void D3D12CommandList::CopyBufferToTexture(ID3D12Resource* staging, ID3D12Resource* destination,
                                           const D3D12_PLACED_SUBRESOURCE_FOOTPRINT& footprint) {
    D3D12_RESOURCE_BARRIER to_copy_dest{};
    to_copy_dest.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    to_copy_dest.Transition.pResource = destination;
    to_copy_dest.Transition.StateBefore = D3D12_RESOURCE_STATE_COMMON;
    to_copy_dest.Transition.StateAfter = D3D12_RESOURCE_STATE_COPY_DEST;
    to_copy_dest.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    command_list_->ResourceBarrier(1, &to_copy_dest);

    D3D12_TEXTURE_COPY_LOCATION source_location{};
    source_location.pResource = staging;
    source_location.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
    source_location.PlacedFootprint = footprint;
    D3D12_TEXTURE_COPY_LOCATION destination_location{};
    destination_location.pResource = destination;
    destination_location.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
    destination_location.SubresourceIndex = 0;
    command_list_->CopyTextureRegion(&destination_location, 0, 0, 0, &source_location, nullptr);

    D3D12_RESOURCE_BARRIER to_shader_read{};
    to_shader_read.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    to_shader_read.Transition.pResource = destination;
    to_shader_read.Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_DEST;
    to_shader_read.Transition.StateAfter = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
    to_shader_read.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    command_list_->ResourceBarrier(1, &to_shader_read);
}

void D3D12CommandList::SetPipeline(PipelineHandle handle) {
    command_list_->SetGraphicsRootSignature(owner_->RootSignature());
    command_list_->SetPipelineState(owner_->PipelineState(handle));
    bound_pipeline_ = handle;
}

void D3D12CommandList::Draw(std::uint32_t vertex_count, std::uint32_t instance_count) {
    command_list_->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    command_list_->DrawInstanced(vertex_count, instance_count, 0, 0);
    owner_->CheckGpuErrors();
}

void D3D12CommandList::SetVertexBuffer(BufferHandle handle, std::uint32_t stride_bytes) {
    const D3D12Device::BufferEntry& entry = owner_->BufferResource(handle);
    if (stride_bytes == 0 || entry.size_bytes < stride_bytes) {
        throw std::runtime_error("d3d12 backend: invalid vertex buffer stride");
    }
    D3D12_VERTEX_BUFFER_VIEW view{};
    view.BufferLocation = entry.resource->GetGPUVirtualAddress();
    view.SizeInBytes = static_cast<UINT>(entry.size_bytes);
    view.StrideInBytes = stride_bytes;
    command_list_->IASetVertexBuffers(0, 1, &view);
}

void D3D12CommandList::SetIndexBuffer(BufferHandle handle, bool indices_are_32_bit) {
    const D3D12Device::BufferEntry& entry = owner_->BufferResource(handle);
    D3D12_INDEX_BUFFER_VIEW view{};
    view.BufferLocation = entry.resource->GetGPUVirtualAddress();
    view.SizeInBytes = static_cast<UINT>(entry.size_bytes);
    view.Format = indices_are_32_bit ? DXGI_FORMAT_R32_UINT : DXGI_FORMAT_R16_UINT;
    command_list_->IASetIndexBuffer(&view);
}

void D3D12CommandList::DrawIndexed(std::uint32_t index_count, std::uint32_t instance_count) {
    command_list_->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    command_list_->DrawIndexedInstanced(index_count, instance_count, 0, 0, 0);
    owner_->CheckGpuErrors();
}

void D3D12CommandList::SetPushConstants(const void* data, std::uint32_t size_bytes) {
    if (data == nullptr || size_bytes == 0 || size_bytes > 64u) {
        throw std::runtime_error("d3d12 backend: invalid push constants (v0: 64 bytes max)");
    }
    if (size_bytes % 4u != 0) {
        throw std::runtime_error("d3d12 backend: push constants must be a multiple of 4 bytes");
    }
    command_list_->SetGraphicsRoot32BitConstants(0, size_bytes / 4u, data, 0);
}

void D3D12CommandList::SetSampledTexture(TextureHandle texture, SamplerHandle sampler) {
    if (bound_pipeline_ == PipelineHandle::kInvalid ||
        owner_->PipelineSampleSlot(bound_pipeline_) == 0) {
        throw std::runtime_error(
            "d3d12 backend: SetSampledTexture requires a pipeline with sample_slot > 0");
    }
    ID3D12DescriptorHeap* heaps[] = {owner_->srv_heap_.Get(), owner_->sampler_heap_.Get()};
    command_list_->SetDescriptorHeaps(static_cast<UINT>(std::size(heaps)), heaps);
    command_list_->SetGraphicsRootDescriptorTable(1, owner_->SrvGpuHandle(texture));
    command_list_->SetGraphicsRootDescriptorTable(2, owner_->SamplerGpuHandle(sampler));
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

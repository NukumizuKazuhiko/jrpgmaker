#include "d3d12_swapchain.h"

#include <format>
#include <stdexcept>

#include "d3d12_device.h"

namespace jrpgmaker::rhi::d3d12 {
namespace {

void ThrowIfFailed(HRESULT hr, const char* context) {
    if (FAILED(hr)) {
        throw std::runtime_error(std::format("d3d12 swapchain failed in {}: hr=0x{:08X}", context,
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
    throw std::runtime_error("d3d12 swapchain: unsupported swapchain format");
}

} // namespace

D3D12Swapchain::D3D12Swapchain(D3D12Device* owner, void* native_window_handle, std::uint32_t width,
                               std::uint32_t height, Format format)
    : owner_(owner) {
    const HWND hwnd = static_cast<HWND>(native_window_handle);

    DXGI_SWAP_CHAIN_DESC1 desc{};
    desc.Width = width;
    desc.Height = height;
    desc.Format = ToNativeFormat(format);
    desc.Stereo = FALSE;
    desc.SampleDesc.Count = 1;
    desc.SampleDesc.Quality = 0;
    desc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    desc.BufferCount = 2;
    desc.Scaling = DXGI_SCALING_STRETCH;
    desc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
    desc.AlphaMode = DXGI_ALPHA_MODE_UNSPECIFIED;

    Microsoft::WRL::ComPtr<IDXGISwapChain1> swapchain;
    ThrowIfFailed(owner_->factory_->CreateSwapChainForHwnd(owner_->queue_.Get(), hwnd, &desc,
                                                           nullptr, nullptr, &swapchain),
                  "CreateSwapChainForHwnd");
    ThrowIfFailed(swapchain.As(&swapchain_), "IDXGISwapChain1->IDXGISwapChain3");

    ThrowIfFailed(owner_->factory_->MakeWindowAssociation(hwnd, DXGI_MWA_NO_ALT_ENTER),
                  "MakeWindowAssociation");

    CreateSwapchainDXGI(width, height);
}

D3D12Swapchain::~D3D12Swapchain() {
    ReleaseBackBuffers();
}

void D3D12Swapchain::CreateSwapchainDXGI(std::uint32_t width, std::uint32_t height) {
    ReleaseBackBuffers();

    if (swapchain_ != nullptr) {
        ThrowIfFailed(swapchain_->ResizeBuffers(2, width, height, DXGI_FORMAT_UNKNOWN, 0),
                      "ResizeBuffers");
    }

    for (UINT i = 0; i < 2; ++i) {
        Microsoft::WRL::ComPtr<ID3D12Resource> buffer;
        ThrowIfFailed(swapchain_->GetBuffer(i, IID_PPV_ARGS(buffer.GetAddressOf())),
                      "IDXGISwapChain::GetBuffer");
        back_buffer_handles_.push_back(
            owner_->RegisterSwapchainBuffer(buffer.Get(), width, height, Format::kB8G8R8A8Unorm));
    }
}

void D3D12Swapchain::ReleaseBackBuffers() {
    for (TextureHandle handle : back_buffer_handles_) {
        owner_->UnregisterSwapchainBuffer(handle);
    }
    back_buffer_handles_.clear();
}

TextureHandle D3D12Swapchain::AcquireTexture() {
    current_buffer_ = swapchain_->GetCurrentBackBufferIndex();
    return back_buffer_handles_[current_buffer_];
}

void D3D12Swapchain::Present() {
    ThrowIfFailed(swapchain_->Present(1, 0), "IDXGISwapChain::Present");
}

void D3D12Swapchain::Resize(std::uint32_t width, std::uint32_t height) {
    owner_->WaitForGpuIdle();
    CreateSwapchainDXGI(width, height);
}

} // namespace jrpgmaker::rhi::d3d12
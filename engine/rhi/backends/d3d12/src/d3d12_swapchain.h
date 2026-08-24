#pragma once

#include <cstdint>
#include <vector>

#include <d3d12.h>
#include <dxgi1_4.h>
#include <wrl/client.h>

#include "jrpgmaker/rhi/common.hpp"
#include "jrpgmaker/rhi/swapchain.hpp"

namespace jrpgmaker::rhi::d3d12 {

class D3D12Device;

class D3D12Swapchain final : public ISwapchain {
public:
    D3D12Swapchain(D3D12Device* owner, void* native_window_handle, std::uint32_t width,
                   std::uint32_t height, Format format);
    ~D3D12Swapchain() override;

    TextureHandle AcquireTexture() override;
    void Present() override;
    void Resize(std::uint32_t width, std::uint32_t height) override;

private:
    void CreateSwapchainDXGI(std::uint32_t width, std::uint32_t height);
    void ReleaseBackBuffers();

    D3D12Device* owner_ = nullptr;
    Microsoft::WRL::ComPtr<IDXGISwapChain3> swapchain_;
    std::vector<TextureHandle> back_buffer_handles_;
    std::uint32_t current_buffer_ = 0;
};

} // namespace jrpgmaker::rhi::d3d12
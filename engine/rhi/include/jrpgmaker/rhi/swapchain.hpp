#pragma once

#include <cstdint>

#include "jrpgmaker/rhi/handles.hpp"

namespace jrpgmaker::rhi {

class ISwapchain {
public:
    virtual ~ISwapchain() = default;

    ISwapchain(const ISwapchain&) = delete;
    ISwapchain& operator=(const ISwapchain&) = delete;

    virtual TextureHandle AcquireTexture() = 0;
    virtual void Present() = 0;
    virtual void Resize(std::uint32_t width, std::uint32_t height) = 0;

protected:
    ISwapchain() = default;
};

} // namespace jrpgmaker::rhi

#pragma once

#include <cstdint>

#include "jrpgmaker/rhi/descriptors.hpp"
#include "jrpgmaker/rhi/handles.hpp"

namespace jrpgmaker::rhi {

class ICommandList {
public:
    virtual ~ICommandList() = default;

    ICommandList(const ICommandList&) = delete;
    ICommandList& operator=(const ICommandList&) = delete;

    virtual void Begin() = 0;
    virtual void End() = 0;

    virtual void BeginRendering(TextureHandle color_target, const ClearColor& clear_color) = 0;
    virtual void EndRendering() = 0;

    virtual void SetPipeline(PipelineHandle handle) = 0;
    virtual void Draw(std::uint32_t vertex_count, std::uint32_t instance_count) = 0;

    virtual void CopyTexture(TextureHandle source, TextureHandle destination) = 0;

protected:
    ICommandList() = default;
};

} // namespace jrpgmaker::rhi

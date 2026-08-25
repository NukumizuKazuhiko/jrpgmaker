#pragma once

#include <cstdint>

namespace jrpgmaker::rhi {

enum class TextureHandle : std::uint64_t { kInvalid = 0 };
enum class PipelineHandle : std::uint64_t { kInvalid = 0 };
enum class BufferHandle : std::uint64_t { kInvalid = 0 };
enum class SamplerHandle : std::uint64_t { kInvalid = 0 };

} // namespace jrpgmaker::rhi

#pragma once

#include <memory>

#include "jrpgmaker/rhi/common.hpp"
#include "jrpgmaker/rhi/device.hpp"

namespace jrpgmaker::rhi {

std::unique_ptr<IDevice> CreateDevice(Backend backend);

} // namespace jrpgmaker::rhi

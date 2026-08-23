#include <catch2/catch_test_macros.hpp>

#include <cstdint>
#include <memory>

#include "jrpgmaker/rhi/command_list.hpp"
#include "jrpgmaker/rhi/device.hpp"
#include "jrpgmaker/rhi/device_factory.hpp"
#include "jrpgmaker/rhi/handles.hpp"

namespace {

using namespace jrpgmaker::rhi;

#if defined(_WIN32)
constexpr Backend kBackend = Backend::kD3D12;
#else
constexpr Backend kBackend = Backend::kVulkan;
#endif

constexpr std::uint32_t kWidth = 8;
constexpr std::uint32_t kHeight = 8;
constexpr ClearColor kClearColor{1.0f, 0.5f, 0.25f, 1.0f};

std::uint8_t ToUnormByte(float channel) {
    const float clamped = channel < 0.0f ? 0.0f : (channel > 1.0f ? 1.0f : channel);
    return static_cast<std::uint8_t>(clamped * 255.0f + 0.5f);
}

} // namespace

TEST_CASE("clear baseline renders a known color with cross-backend tolerance",
          "[rhi][golden][clear]") {
    const std::unique_ptr<IDevice> device = CreateDevice(kBackend);
    REQUIRE(device != nullptr);

    const TextureHandle target = device->CreateTexture(
        TextureDesc{.width = kWidth,
                    .height = kHeight,
                    .format = Format::kR8G8B8A8Unorm,
                    .usage = TextureUsage::kRenderTarget | TextureUsage::kReadBack});
    REQUIRE(target != TextureHandle::kInvalid);

    ICommandList* command_list = device->CreateCommandList();
    REQUIRE(command_list != nullptr);
    command_list->Begin();
    command_list->BeginRendering(target, kClearColor);
    command_list->EndRendering();
    command_list->End();
    device->Submit(*command_list);
    device->WaitForGpuIdle();
    device->DestroyCommandList(command_list);

    const MappedTexture mapped = device->MapReadBack(target);
    REQUIRE(mapped.data != nullptr);

    const std::uint8_t expected_r = ToUnormByte(kClearColor.r);
    const std::uint8_t expected_g = ToUnormByte(kClearColor.g);
    const std::uint8_t expected_b = ToUnormByte(kClearColor.b);
    const std::uint8_t expected_a = ToUnormByte(kClearColor.a);

    const auto* bytes = reinterpret_cast<const std::uint8_t*>(mapped.data);
    for (std::uint32_t y = 0; y < kHeight; ++y) {
        const std::uint8_t* row = bytes + static_cast<std::uint64_t>(y) * mapped.row_pitch_bytes;
        for (std::uint32_t x = 0; x < kWidth; ++x) {
            const int r = row[x * 4 + 0];
            const int g = row[x * 4 + 1];
            const int b = row[x * 4 + 2];
            const int a = row[x * 4 + 3];
            INFO("pixel (" << x << ", " << y << "): rgba(" << r << ", " << g << ", " << b << ", "
                           << a << ")");
            CHECK((r > expected_r ? r - expected_r : expected_r - r) <= 1);
            CHECK((g > expected_g ? g - expected_g : expected_g - g) <= 1);
            CHECK((b > expected_b ? b - expected_b : expected_b - b) <= 1);
            CHECK((a > expected_a ? a - expected_a : expected_a - a) <= 1);
        }
    }

    device->WaitForGpuIdle();
    device->DestroyTexture(target);
}

#pragma once

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace jrpgmaker::golden {

// An RGB image (3 bytes per pixel, row-major) used as a golden reference.
struct Image {
    std::uint32_t width = 0;
    std::uint32_t height = 0;
    std::vector<std::uint8_t> rgb;
};

// Writes the image as a binary PPM (P6). Returns false and fills `error` on failure.
bool WritePpm(const std::filesystem::path& path, const Image& image, std::string& error);

// Reads a binary PPM (P6). Returns false and fills `error` on failure.
bool ReadPpm(const std::filesystem::path& path, Image& image, std::string& error);

// Result of a full-frame comparison. `passed` is true when every pixel of every
// channel is within `tolerance` (inclusive) of the reference.
struct CompareResult {
    std::uint64_t pixels_compared = 0;
    std::uint64_t pixels_differing = 0;
    int max_channel_delta = 0;
    bool passed = false;
};

// Compares an RGBA8 readback (4 bytes per pixel, row pitch in bytes) against an
// RGB reference. Only the R/G/B channels participate. `tolerance` is the maximum
// allowed per-channel absolute difference (inclusive).
CompareResult CompareRgba8(const std::uint8_t* data, std::uint64_t row_pitch_bytes,
                           const Image& reference, int tolerance);

} // namespace jrpgmaker::golden
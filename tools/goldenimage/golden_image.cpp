#include "golden_image.hpp"

#include <fstream>
#include <vector>

namespace jrpgmaker::golden {

bool WritePpm(const std::filesystem::path& path, const Image& image, std::string& error) {
    if (image.rgb.size() != static_cast<std::size_t>(image.width) * image.height * 3u) {
        error = "image buffer size does not match width*height*3";
        return false;
    }

    std::ofstream file(path, std::ios::binary | std::ios::trunc);
    if (!file) {
        error = "cannot open '" + path.string() + "' for writing";
        return false;
    }

    file << "P6\n" << image.width << ' ' << image.height << "\n255\n";
    file.write(reinterpret_cast<const char*>(image.rgb.data()),
               static_cast<std::streamsize>(image.rgb.size()));
    file.close();
    if (!file) {
        error = "failed to write PPM data to '" + path.string() + "'";
        return false;
    }
    return true;
}

bool ReadPpm(const std::filesystem::path& path, Image& image, std::string& error) {
    std::ifstream file(path, std::ios::binary);
    if (!file) {
        error = "cannot open '" + path.string() + "' for reading";
        return false;
    }

    char magic[2] = {};
    if (!file.read(magic, 2) || magic[0] != 'P' || magic[1] != '6') {
        error = "not a binary PPM (P6) file: '" + path.string() + "'";
        return false;
    }

    unsigned width = 0;
    unsigned height = 0;
    unsigned max_value = 0;
    if (!(file >> width >> height >> max_value)) {
        error = "malformed PPM header: '" + path.string() + "'";
        return false;
    }
    if (max_value != 255) {
        error = "unsupported PPM max value (only 255 supported): '" + path.string() + "'";
        return false;
    }

    // Skip a single whitespace character that terminates the header.
    char separator = '\0';
    if (!file.get(separator) || separator == '\0') {
        error = "malformed PPM header separator: '" + path.string() + "'";
        return false;
    }

    const std::size_t pixel_count = static_cast<std::size_t>(width) * height;
    if (pixel_count > image.rgb.max_size() / 3u) {
        error = "PPM dimensions overflow: '" + path.string() + "'";
        return false;
    }

    Image result;
    result.width = static_cast<std::uint32_t>(width);
    result.height = static_cast<std::uint32_t>(height);
    result.rgb.resize(pixel_count * 3u);
    if (!file.read(reinterpret_cast<char*>(result.rgb.data()),
                   static_cast<std::streamsize>(result.rgb.size()))) {
        error = "truncated PPM pixel data: '" + path.string() + "'";
        return false;
    }

    image = std::move(result);
    return true;
}

CompareResult CompareRgba8(const std::uint8_t* data, std::uint64_t row_pitch_bytes,
                           const Image& reference, int tolerance) {
    CompareResult result;
    result.pixels_compared = static_cast<std::uint64_t>(reference.width) * reference.height;

    for (std::uint32_t y = 0; y < reference.height; ++y) {
        const std::uint8_t* row =
            data + static_cast<std::uint64_t>(y) * row_pitch_bytes;
        const std::uint8_t* ref_row =
            reference.rgb.data() + static_cast<std::size_t>(y) * reference.width * 3u;
        for (std::uint32_t x = 0; x < reference.width; ++x) {
            const std::uint8_t* pixel = row + static_cast<std::size_t>(x) * 4u;
            const std::uint8_t* ref = ref_row + static_cast<std::size_t>(x) * 3u;
            for (int channel = 0; channel < 3; ++channel) {
                const int delta =
                    pixel[channel] > ref[channel] ? pixel[channel] - ref[channel]
                                                  : ref[channel] - pixel[channel];
                if (delta > result.max_channel_delta) {
                    result.max_channel_delta = delta;
                }
                if (delta > tolerance) {
                    ++result.pixels_differing;
                    break;
                }
            }
        }
    }

    result.passed = result.pixels_differing == 0;
    return result;
}

} // namespace jrpgmaker::golden
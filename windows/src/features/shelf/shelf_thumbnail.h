#pragma once
#include <algorithm>
#include <cstdint>
#include <vector>
namespace noitch {
struct ShelfThumbnail {
    uint32_t width = 0, height = 0;
    std::vector<uint32_t> pixels;
};
// CF_DIB has no file header. Only uncompressed 24/32-bit RGB is decoded here; other formats still
// round-trip through the clipboard, with their type shown on the card instead of a thumbnail.
inline ShelfThumbnail DecodeShelfThumbnail(const std::vector<uint8_t>& data) {
    if (data.size() < 40) return {};
    const auto word = [&](size_t at) { return uint32_t(data[at]) | uint32_t(data[at + 1]) << 8 | uint32_t(data[at + 2]) << 16 | uint32_t(data[at + 3]) << 24; };
    const uint32_t header = word(0);
    const int64_t width = static_cast<int32_t>(word(4));
    const int64_t signedHeight = static_cast<int32_t>(word(8));
    const int64_t height = signedHeight < 0 ? -signedHeight : signedHeight;
    const uint32_t bits = uint32_t(data[14]) | uint32_t(data[15]) << 8;
    if (header < 40 || header > data.size() || width <= 0 || height <= 0 || width > 20000 || height > 20000 || (bits != 24 && bits != 32) || word(16) != 0) return {};
    const uint64_t start = uint64_t(header) + uint64_t(word(32)) * 4;
    const uint64_t stride = (uint64_t(width) * bits + 31) / 32 * 4;
    if (start > data.size() || stride * height > data.size() - start) return {};
    const double scale = std::min(1.0, 128.0 / std::max(width, height));
    ShelfThumbnail result;
    result.width = std::max(1u, static_cast<uint32_t>(width * scale));
    result.height = std::max(1u, static_cast<uint32_t>(height * scale));
    result.pixels.resize(static_cast<size_t>(result.width) * result.height);
    for (uint32_t y = 0; y < result.height; ++y) for (uint32_t x = 0; x < result.width; ++x) {
        uint64_t sy = uint64_t(y) * height / result.height;
        if (signedHeight > 0) sy = height - sy - 1;
        const uint64_t at = start + sy * stride + (uint64_t(x) * width / result.width) * (bits / 8);
        result.pixels[size_t(y) * result.width + x] = 0xff000000u | uint32_t(data[at + 2]) << 16 | uint32_t(data[at + 1]) << 8 | data[at];
    }
    return result;
}
}

#pragma once

#include <expected>
#include <string_view>
#include <vector>

#include "engine/assets/AssetError.hpp"

namespace engine {

// Decoded image in 8-bit channels, row-major, bottom-up as stored by the file
// (no flipping applied; upload code decides the orientation).
struct ImageData {
    int width = 0;
    int height = 0;
    int channels = 0; // requested channel count actually stored per pixel
    std::vector<unsigned char> pixels; // width * height * channels
};

// Decodes an image file via stb_image. channels: 0 = as-is, 1 = grayscale,
// 3 = RGB, 4 = RGBA (force-expanding alpha as needed).
[[nodiscard]] std::expected<ImageData, AssetError> loadImage(std::string_view path, int channels = 0);

} // namespace engine

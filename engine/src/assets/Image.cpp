#define STB_IMAGE_IMPLEMENTATION
#define STBI_NO_PSD
#define STBI_NO_TGA
#define STBI_NO_HDR
#define STBI_NO_PIC
#define STBI_NO_PNM

#include "engine/assets/Image.hpp"

#include <filesystem>
#include <format>

#include <stb_image.h>

namespace engine {

std::expected<ImageData, AssetError> loadImage(std::string_view path, int channels)
{
    namespace fs = std::filesystem;
    if (!fs::exists(path)) {
        return std::unexpected(AssetError{
            AssetError::Kind::FileNotFound, std::format("image file not found: {}", path)});
    }

    ImageData image;
    image.pixels = {}; // silence maybe-uninitialized on some analyzers
    unsigned char* data = stbi_load(path.data(), &image.width, &image.height, &image.channels, channels);
    if (data == nullptr) {
        return std::unexpected(AssetError{
            AssetError::Kind::ParseFailed,
            std::format("stb_image failed to decode {}: {}", path,
                stbi_failure_reason() != nullptr ? stbi_failure_reason() : "unknown error")});
    }

    image.channels = (channels != 0) ? channels : image.channels;
    const size_t byteCount = static_cast<size_t>(image.width) * static_cast<size_t>(image.height)
        * static_cast<size_t>(image.channels);
    image.pixels.assign(data, data + byteCount);
    stbi_image_free(data);
    return image;
}

} // namespace engine

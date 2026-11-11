#pragma once

#include <expected>
#include <string_view>

#include "engine/assets/AssetError.hpp"

namespace engine {

// 2D texture uploaded from an image file (RGBA8 or SRGB8_ALPHA8, mipmapped,
// anisotropic filtering). DSA-based; sampling units are bound with
// glBindTextureUnit by the renderer.
class GlTexture final {
public:
    GlTexture() = default;
    ~GlTexture();

    GlTexture(const GlTexture&) = delete;
    GlTexture& operator=(const GlTexture&) = delete;
    GlTexture(GlTexture&& other) noexcept;
    GlTexture& operator=(GlTexture&& other) noexcept;

    [[nodiscard]] static std::expected<GlTexture, AssetError> createFromFile(
        std::string_view path, bool srgb = false);

    [[nodiscard]] unsigned int handle() const { return m_texture; }

private:
    unsigned int m_texture = 0;
};

} // namespace engine

#define GLEW_STATIC

#include "engine/renderers/gl/GlTexture.hpp"

#include <GL/glew.h>

#include "engine/assets/Image.hpp"
#include "engine/core/Log.hpp"

namespace engine {

GlTexture::~GlTexture()
{
    if (m_texture != 0) {
        glDeleteTextures(1, &m_texture);
    }
}

GlTexture::GlTexture(GlTexture&& other) noexcept
    : m_texture(std::exchange(other.m_texture, 0))
{
}

GlTexture& GlTexture::operator=(GlTexture&& other) noexcept
{
    if (this != &other) {
        if (m_texture != 0) {
            glDeleteTextures(1, &m_texture);
        }
        m_texture = std::exchange(other.m_texture, 0);
    }
    return *this;
}

std::expected<GlTexture, AssetError> GlTexture::createFromFile(std::string_view path, bool srgb)
{
    auto image = loadImage(path, 4); // force RGBA8
    if (!image) {
        return std::unexpected(std::move(image).error());
    }

    GlTexture texture;
    glCreateTextures(GL_TEXTURE_2D, 1, &texture.m_texture);
    glTextureStorage2D(texture.m_texture, 1 + static_cast<int>(std::log2(std::max(image->width, image->height))),
        srgb ? GL_SRGB8_ALPHA8 : GL_RGBA8, image->width, image->height);
    glTextureSubImage2D(texture.m_texture, 0, 0, 0, image->width, image->height, GL_RGBA,
        GL_UNSIGNED_BYTE, image->pixels.data());
    glGenerateTextureMipmap(texture.m_texture);

    glTextureParameteri(texture.m_texture, GL_TEXTURE_WRAP_S, GL_REPEAT);
    glTextureParameteri(texture.m_texture, GL_TEXTURE_WRAP_T, GL_REPEAT);
    glTextureParameteri(texture.m_texture, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
    glTextureParameteri(texture.m_texture, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

    GLfloat maxAnisotropy = 1.0F;
    glGetFloatv(GL_MAX_TEXTURE_MAX_ANISOTROPY_EXT, &maxAnisotropy);
    glTextureParameterf(texture.m_texture, GL_TEXTURE_MAX_ANISOTROPY_EXT, maxAnisotropy);

    Log::info("texture '{}' ({}x{}, {})", path, image->width, image->height, srgb ? "sRGB" : "linear");
    return texture;
}

} // namespace engine

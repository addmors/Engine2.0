#define GLEW_STATIC

#include <GL/glew.h>

#define STB_IMAGE_WRITE_IMPLEMENTATION
#include <stb_image_write.h>

#include <cstring>
#include <vector>

#include "engine/core/Log.hpp"

namespace engine {

// Reads back the default framebuffer and writes a PNG (selftest validation:
// lets automated runs verify what was actually rendered).
bool saveGlScreenshot(const std::string& path)
{
    GLint viewport[4] = {};
    glGetIntegerv(GL_VIEWPORT, viewport);
    const int width = viewport[2];
    const int height = viewport[3];

    std::vector<unsigned char> pixels(static_cast<size_t>(width) * static_cast<size_t>(height) * 4);
    glPixelStorei(GL_PACK_ALIGNMENT, 1);
    glReadPixels(0, 0, width, height, GL_RGBA, GL_UNSIGNED_BYTE, pixels.data());

    // Flip vertically (GL origin is bottom-left, PNG expects top-left).
    std::vector<unsigned char> flipped(pixels.size());
    for (int row = 0; row < height; ++row) {
        const size_t src = static_cast<size_t>(height - 1 - row) * static_cast<size_t>(width) * 4;
        const size_t dst = static_cast<size_t>(row) * static_cast<size_t>(width) * 4;
        std::memcpy(flipped.data() + dst, pixels.data() + src, static_cast<size_t>(width) * 4);
    }

    if (stbi_write_png(path.c_str(), width, height, 4, flipped.data(), width * 4) == 0) {
        Log::error("screenshot write failed: {}", path);
        return false;
    }
    Log::info("screenshot saved: {}", path);
    return true;
}

} // namespace engine

#pragma once

#include <cstdint>
#include <expected>
#include <span>
#include <string>
#include <vector>

#include <glm/glm.hpp>

#include "engine/assets/AssetError.hpp"
#include "engine/renderers/gl/GlMesh.hpp"
#include "engine/renderers/gl/GlShader.hpp"
#include "engine/renderers/gl/GlTexture.hpp"

namespace engine {

// The OpenGL 4.6 reference renderer (M2). Owns programs, UBOs, the shadow
// framebuffer and GPU meshes; the scene drives it through draw* calls.
class GlRenderer final {
public:
    struct Environment {
        glm::mat4 view{1.0F};
        glm::mat4 projection{1.0F};
        glm::vec3 cameraPosition{0.0F};
        glm::vec3 sunDirection{0.4F, -1.0F, 0.2F}; // pointing FROM the sun
        glm::vec3 sunColor{1.0F, 0.98F, 0.92F};
        glm::vec3 ambient{0.28F, 0.29F, 0.33F};
    };

    GlRenderer() = default;
    ~GlRenderer() = default;

    // Loads GLEW, enables debug output, creates shared UBOs + shadow FBO.
    [[nodiscard]] std::expected<void, AssetError> init();

    void resize(int width, int height);

    // Resource handles (0 = invalid).
    [[nodiscard]] uint32_t uploadMesh(const Mesh& mesh);
    [[nodiscard]] uint32_t createTextureFromFile(const std::string& path, bool srgb = false);

    void setBonePalette(std::span<const glm::mat4> palette);

    // Pass control ----------------------------------------------------------
    void beginShadowPass(const glm::mat4& lightViewProjection);
    void endShadowPass();
    void beginFrame(const Environment& environment);
    void finishFrame(); // just restores viewport; ImGui renders afterwards

    // Draws (automatically dispatched to the active pass) --------------------
    void drawMesh(uint32_t mesh, const glm::mat4& model); // untextured, lit
    void drawTerrain(uint32_t mesh, std::span<const uint32_t, 5> textures);
    void drawSkinned(uint32_t mesh, const glm::mat4& model, const uint32_t textures[3]);
    void drawInstanced(uint32_t mesh, std::span<const glm::mat4> instances, const glm::vec3& color);
    void drawSky(); // procedural, call after beginFrame clears

private:
    friend bool saveGlScreenshot(const std::string& path); // reads the framebuffer
    struct Programs {
        GlProgram shadow;
        GlProgram terrain;
        GlProgram skinned;
        GlProgram simple;
        GlProgram sky;
    };

    static constexpr int shadowMapSize = 2048;
    static constexpr int maxBones = 128;
    static constexpr int maxInstances = 256;

    [[nodiscard]] std::expected<void, AssetError> initFramebuffersAndPrograms();
    void uploadModelMatrix(const glm::mat4& model);
    void resetInstanceZero();

    void uploadEnvironment(const Environment& environment);
    void bindCommonState();

    Programs m_programs;
    std::vector<GlMesh> m_meshes;   // handles are index+1
    std::vector<GlTexture> m_textures; // handles are index+1

    unsigned int m_cameraUbo = 0; // binding 0 (view/proj/camPos)
    unsigned int m_lightUbo = 0;  // binding 3 (sun/ambient) — separate buffer:
                                  // glBindBufferRange offsets must respect
                                  // GL_UNIFORM_BUFFER_OFFSET_ALIGNMENT (256 on NV).
    unsigned int m_modelUbo = 0;  // binding 1
    unsigned int m_bonesUbo = 0;  // binding 2
    unsigned int m_instancesUbo = 0; // binding 4
    unsigned int m_shadowUbo = 0; // binding 5

    unsigned int m_shadowFbo = 0;
    unsigned int m_shadowDepthTexture = 0;
    bool m_inShadowPass = false;

    unsigned int m_emptyVao = 0; // for the fullscreen sky triangle

    int m_width = 0;
    int m_height = 0;

    glm::mat4 m_lastView{1.0F};
    glm::mat4 m_lastProjection{1.0F};
};

// Reads back the default framebuffer and writes a PNG (selftest validation).
bool saveGlScreenshot(const std::string& path);

} // namespace engine

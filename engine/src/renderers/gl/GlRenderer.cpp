#define GLEW_STATIC

#include "engine/renderers/gl/GlRenderer.hpp"

#include <array>
#include <format>
#include <utility>

#include <GL/glew.h>

#include "engine/core/Log.hpp"

#ifndef CANDY_SHADERS_DIR
#define CANDY_SHADERS_DIR "shaders/gl"
#endif

namespace engine {

namespace {

void GLAPIENTRY glDebugCallback(GLenum, GLenum, GLuint id, GLenum severity, GLsizei, const GLchar* message,
    const void*)
{
    if (id == 131185) {
        return; // buffer usage notifications
    }
    switch (severity) {
    case GL_DEBUG_SEVERITY_HIGH:
        Log::error("GL [{}] {}", id, message);
        break;
    case GL_DEBUG_SEVERITY_MEDIUM:
        Log::warn("GL [{}] {}", id, message);
        break;
    default:
        break;
    }
}

} // namespace

std::expected<void, AssetError> GlRenderer::init()
{
    glewExperimental = GL_TRUE;
    if (const GLenum err = glewInit(); err != GLEW_OK) {
        return std::unexpected(AssetError{AssetError::Kind::Unknown,
            std::format("glewInit failed: {}", reinterpret_cast<const char*>(glewGetErrorString(err)))});
    }
    glGetError(); // glewInit may leave a benign error flag

    glEnable(GL_DEBUG_OUTPUT_SYNCHRONOUS);
    glDebugMessageCallback(glDebugCallback, nullptr);

    // Shared UBOs ------------------------------------------------------------
    // CameraBlock (binding 0) and LightBlock (binding 3) are separate buffers:
    // a shared buffer would need the light range at offset 144, which breaks
    // GL_UNIFORM_BUFFER_OFFSET_ALIGNMENT (typically 256).
    glCreateBuffers(1, &m_cameraUbo);
    glNamedBufferData(m_cameraUbo, 144, nullptr, GL_DYNAMIC_DRAW);
    glBindBufferBase(GL_UNIFORM_BUFFER, 0, m_cameraUbo);

    glCreateBuffers(1, &m_lightUbo);
    glNamedBufferData(m_lightUbo, 48, nullptr, GL_DYNAMIC_DRAW);
    glBindBufferBase(GL_UNIFORM_BUFFER, 3, m_lightUbo);

    glCreateBuffers(1, &m_modelUbo);
    glNamedBufferData(m_modelUbo, 2 * sizeof(glm::mat4), nullptr, GL_DYNAMIC_DRAW);
    glBindBufferBase(GL_UNIFORM_BUFFER, 1, m_modelUbo);

    glCreateBuffers(1, &m_bonesUbo);
    glNamedBufferData(m_bonesUbo, maxBones * sizeof(glm::mat4), nullptr, GL_DYNAMIC_DRAW);
    glBindBufferBase(GL_UNIFORM_BUFFER, 2, m_bonesUbo);
    {
        std::vector<glm::mat4> identity(static_cast<size_t>(maxBones), glm::mat4{1.0F});
        glNamedBufferSubData(m_bonesUbo, 0, sizeof(glm::mat4) * maxBones, identity.data());
    }

    glCreateBuffers(1, &m_instancesUbo);
    glNamedBufferData(m_instancesUbo, maxInstances * sizeof(glm::mat4), nullptr, GL_DYNAMIC_DRAW);
    glBindBufferBase(GL_UNIFORM_BUFFER, 4, m_instancesUbo);
    {
        const glm::mat4 identity{1.0F};
        glNamedBufferSubData(m_instancesUbo, 0, sizeof(glm::mat4), &identity);
    }

    glCreateBuffers(1, &m_shadowUbo);
    glNamedBufferData(m_shadowUbo, sizeof(glm::mat4) + sizeof(glm::vec4), nullptr, GL_DYNAMIC_DRAW);
    glBindBufferBase(GL_UNIFORM_BUFFER, 5, m_shadowUbo);

    return initFramebuffersAndPrograms();
}

std::expected<void, AssetError> GlRenderer::initFramebuffersAndPrograms()
{
    // Shadow framebuffer -------------------------------------------------------
    glCreateFramebuffers(1, &m_shadowFbo);
    glCreateTextures(GL_TEXTURE_2D, 1, &m_shadowDepthTexture);
    glTextureStorage2D(m_shadowDepthTexture, 1, GL_DEPTH_COMPONENT32F, shadowMapSize, shadowMapSize);
    glTextureParameteri(m_shadowDepthTexture, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTextureParameteri(m_shadowDepthTexture, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTextureParameteri(m_shadowDepthTexture, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_BORDER);
    glTextureParameteri(m_shadowDepthTexture, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_BORDER);
    constexpr float border[4] = {1.0F, 1.0F, 1.0F, 1.0F};
    glTextureParameterfv(m_shadowDepthTexture, GL_TEXTURE_BORDER_COLOR, border);
    glNamedFramebufferTexture(m_shadowFbo, GL_DEPTH_ATTACHMENT, m_shadowDepthTexture, 0);
    glNamedFramebufferDrawBuffer(m_shadowFbo, GL_NONE);
    glNamedFramebufferReadBuffer(m_shadowFbo, GL_NONE);

    glCreateVertexArrays(1, &m_emptyVao); // for the fullscreen sky triangle

    // Programs ------------------------------------------------------------------
    auto shadow = GlProgram::createFromFile(CANDY_SHADERS_DIR "/shadow.vert", CANDY_SHADERS_DIR "/shadow.frag");
    if (!shadow) {
        return std::unexpected(std::move(shadow).error());
    }
    auto terrain = GlProgram::createFromFile(CANDY_SHADERS_DIR "/terrain.vert", CANDY_SHADERS_DIR "/terrain.frag");
    if (!terrain) {
        return std::unexpected(std::move(terrain).error());
    }
    auto skinned = GlProgram::createFromFile(CANDY_SHADERS_DIR "/skinned.vert", CANDY_SHADERS_DIR "/skinned.frag");
    if (!skinned) {
        return std::unexpected(std::move(skinned).error());
    }
    auto simple = GlProgram::createFromFile(CANDY_SHADERS_DIR "/skinned.vert", CANDY_SHADERS_DIR "/simple.frag");
    if (!simple) {
        return std::unexpected(std::move(simple).error());
    }
    auto sky = GlProgram::createFromFile(CANDY_SHADERS_DIR "/sky.vert", CANDY_SHADERS_DIR "/sky.frag");
    if (!sky) {
        return std::unexpected(std::move(sky).error());
    }
    m_programs.shadow = std::move(*shadow);
    m_programs.terrain = std::move(*terrain);
    m_programs.skinned = std::move(*skinned);
    m_programs.simple = std::move(*simple);
    m_programs.sky = std::move(*sky);

    glEnable(GL_DEPTH_TEST);
    glEnable(GL_CULL_FACE);
    glCullFace(GL_BACK);

    Log::info("OpenGL renderer initialized: {}", reinterpret_cast<const char*>(glGetString(GL_VERSION)));
    return {};
}

void GlRenderer::resize(int width, int height)
{
    m_width = width;
    m_height = height;
}

uint32_t GlRenderer::uploadMesh(const Mesh& mesh)
{
    m_meshes.emplace_back(GlMesh::create(mesh));
    return static_cast<uint32_t>(m_meshes.size());
}

uint32_t GlRenderer::createTextureFromFile(const std::string& path, bool srgb)
{
    auto texture = GlTexture::createFromFile(path, srgb);
    if (!texture) {
        Log::error("texture failed: {}", texture.error().message);
        return 0;
    }
    m_textures.emplace_back(std::move(*texture));
    return static_cast<uint32_t>(m_textures.size());
}

void GlRenderer::setBonePalette(std::span<const glm::mat4> palette)
{
    if (palette.empty() || palette.size() > static_cast<size_t>(maxBones)) {
        return;
    }
    glNamedBufferSubData(m_bonesUbo, 0, static_cast<GLsizeiptr>(palette.size() * sizeof(glm::mat4)),
        palette.data());
}

void GlRenderer::uploadEnvironment(const Environment& e)
{
    m_lastView = e.view;
    m_lastProjection = e.projection;

    const std::array<glm::mat4, 2> viewProjection = {e.view, e.projection};
    glNamedBufferSubData(m_cameraUbo, 0, 128, viewProjection.data());
    const glm::vec4 cameraPosition{e.cameraPosition, 1.0F};
    glNamedBufferSubData(m_cameraUbo, 128, 16, &cameraPosition);

    struct LightStd140 {
        glm::vec4 sunDirection;
        glm::vec4 sunColor;
        glm::vec4 ambient;
    };
    static_assert(sizeof(LightStd140) == 48);
    const LightStd140 light{glm::vec4{glm::normalize(e.sunDirection), 0.0F},
        glm::vec4{e.sunColor, 1.0F}, glm::vec4{e.ambient, 1.0F}};
    glNamedBufferSubData(m_lightUbo, 0, 48, &light);
}

void GlRenderer::uploadModelMatrix(const glm::mat4& model)
{
    const glm::mat4 normalMatrix = glm::transpose(glm::inverse(model));
    const std::array<glm::mat4, 2> both = {model, normalMatrix};
    glNamedBufferSubData(m_modelUbo, 0, sizeof(both), both.data());
}

void GlRenderer::resetInstanceZero()
{
    const glm::mat4 identity{1.0F};
    glNamedBufferSubData(m_instancesUbo, 0, sizeof(glm::mat4), &identity);
}

void GlRenderer::beginShadowPass(const glm::mat4& lightViewProjection)
{
    m_inShadowPass = true;
    resetInstanceZero();
    struct ShadowStd140 {
        glm::mat4 lightViewProjection;
        glm::vec4 params;
    };
    const ShadowStd140 shadow{lightViewProjection, glm::vec4{1.0F, 2.0F / static_cast<float>(shadowMapSize),
                                                             0.0F, 0.0F}};
    glNamedBufferSubData(m_shadowUbo, 0, sizeof(ShadowStd140), &shadow);

    m_programs.shadow.use();
    glBindFramebuffer(GL_FRAMEBUFFER, m_shadowFbo);
    glViewport(0, 0, shadowMapSize, shadowMapSize);
    glClear(GL_DEPTH_BUFFER_BIT);
    glCullFace(GL_FRONT); // reduce peter-panning on the sloped terrain
}

void GlRenderer::endShadowPass()
{
    m_inShadowPass = false;
    glCullFace(GL_BACK);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
}

void GlRenderer::beginFrame(const Environment& environment)
{
    uploadEnvironment(environment);
    glViewport(0, 0, m_width, m_height);
    glClearColor(0.18F, 0.30F, 0.48F, 1.0F);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    bindCommonState();
}

void GlRenderer::finishFrame() {}

void GlRenderer::bindCommonState()
{
    glEnable(GL_DEPTH_TEST);
    glEnable(GL_CULL_FACE);
    glCullFace(GL_BACK);
}

void GlRenderer::drawMesh(uint32_t mesh, const glm::mat4& model)
{
    if (mesh == 0) {
        return;
    }
    uploadModelMatrix(model);

    if (m_inShadowPass) {
        m_programs.shadow.use();
        resetInstanceZero();
    } else {
        m_programs.simple.use();
        m_programs.simple.setVec3("uColor", {0.45F, 0.47F, 0.52F});
        resetInstanceZero();
        glBindTextureUnit(8, 0);
    }
    m_meshes[mesh - 1].draw();
}

void GlRenderer::drawTerrain(uint32_t mesh, std::span<const uint32_t, 5> textures)
{
    if (mesh == 0) {
        return;
    }
    uploadModelMatrix(glm::mat4{1.0F});
    resetInstanceZero();

    if (m_inShadowPass) {
        m_programs.shadow.use();
    } else {
        m_programs.terrain.use();
        for (size_t i = 0; i < textures.size(); ++i) {
            if (textures[i] != 0) {
                glBindTextureUnit(static_cast<GLuint>(i), m_textures[textures[i] - 1].handle());
            }
        }
        glBindTextureUnit(8, m_shadowDepthTexture);
    }
    m_meshes[mesh - 1].draw();
}

void GlRenderer::drawSkinned(uint32_t mesh, const glm::mat4& model, const uint32_t textures[3])
{
    if (mesh == 0) {
        return;
    }
    uploadModelMatrix(model);
    resetInstanceZero(); // skinned.vert consumes instances[0]; non-instanced = identity

    if (m_inShadowPass) {
        m_programs.shadow.use();
    } else {
        m_programs.skinned.use();
        for (size_t i = 0; i < 3; ++i) {
            if (textures[i] != 0) {
                glBindTextureUnit(static_cast<GLuint>(i), m_textures[textures[i] - 1].handle());
            }
        }
        glBindTextureUnit(8, m_shadowDepthTexture);
    }
    m_meshes[mesh - 1].draw();
}

void GlRenderer::drawInstanced(uint32_t mesh, std::span<const glm::mat4> instances, const glm::vec3& color)
{
    if (mesh == 0 || instances.empty()) {
        return;
    }
    const size_t count = std::min(instances.size(), static_cast<size_t>(maxInstances));
    glNamedBufferSubData(m_instancesUbo, 0, static_cast<GLsizeiptr>(count * sizeof(glm::mat4)),
        instances.data());
    uploadModelMatrix(glm::mat4{1.0F});

    if (m_inShadowPass) {
        m_programs.shadow.use();
    } else {
        m_programs.simple.use();
        m_programs.simple.setVec3("uColor", color);
        glBindTextureUnit(8, m_shadowDepthTexture);
    }
    m_meshes[mesh - 1].drawInstanced(static_cast<unsigned int>(count));
}

void GlRenderer::drawSky()
{
    m_programs.sky.use();
    m_programs.sky.setMat4("uInverseViewProjection", glm::inverse(m_lastProjection * m_lastView));

    glDisable(GL_DEPTH_TEST);
    glDepthMask(GL_FALSE);
    glBindVertexArray(m_emptyVao);
    glDrawArrays(GL_TRIANGLES, 0, 3);
    glDepthMask(GL_TRUE);
    glEnable(GL_DEPTH_TEST);
}

} // namespace engine

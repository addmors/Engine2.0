#define GLEW_STATIC

#include "engine/renderers/gl/GlMesh.hpp"

#include <utility>

#include <GL/glew.h>

#include "engine/assets/Mesh.hpp"
#include "engine/core/Log.hpp"

namespace engine {

GlMesh::~GlMesh()
{
    if (m_vao != 0) {
        glDeleteVertexArrays(1, &m_vao);
        glDeleteBuffers(1, &m_vbo);
        glDeleteBuffers(1, &m_ebo);
    }
}

GlMesh::GlMesh(GlMesh&& other) noexcept
    : m_vao(std::exchange(other.m_vao, 0))
    , m_vbo(std::exchange(other.m_vbo, 0))
    , m_ebo(std::exchange(other.m_ebo, 0))
    , m_indexCount(std::exchange(other.m_indexCount, 0))
{
}

GlMesh& GlMesh::operator=(GlMesh&& other) noexcept
{
    if (this != &other) {
        if (m_vao != 0) {
            glDeleteVertexArrays(1, &m_vao);
            glDeleteBuffers(1, &m_vbo);
            glDeleteBuffers(1, &m_ebo);
        }
        m_vao = std::exchange(other.m_vao, 0);
        m_vbo = std::exchange(other.m_vbo, 0);
        m_ebo = std::exchange(other.m_ebo, 0);
        m_indexCount = std::exchange(other.m_indexCount, 0);
    }
    return *this;
}

GlMesh GlMesh::create(const Mesh& mesh)
{
    GlMesh out;
    out.m_indexCount = static_cast<unsigned int>(mesh.indices.size());

    glCreateVertexArrays(1, &out.m_vao);
    glCreateBuffers(1, &out.m_vbo);
    glCreateBuffers(1, &out.m_ebo);

    glNamedBufferData(out.m_vbo, static_cast<GLsizeiptr>(mesh.vertices.size() * sizeof(Vertex)),
        mesh.vertices.data(), GL_STATIC_DRAW);
    glNamedBufferData(out.m_ebo, static_cast<GLsizeiptr>(mesh.indices.size() * sizeof(unsigned)),
        mesh.indices.data(), GL_STATIC_DRAW);

    glVertexArrayVertexBuffer(out.m_vao, 0, out.m_vbo, 0, sizeof(Vertex));
    glVertexArrayElementBuffer(out.m_vao, out.m_ebo);

    // engine::Vertex layout: position, normal, uv, tangent, boneIds(ivec4), boneWeights.
    constexpr GLintptr offsets[] = {
        offsetof(Vertex, position), offsetof(Vertex, normal), offsetof(Vertex, uv),
        offsetof(Vertex, tangent), offsetof(Vertex, boneIds), offsetof(Vertex, boneWeights)};
    constexpr GLuint components[] = {4, 4, 2, 4, 4, 4};
    constexpr GLenum types[] = {GL_FLOAT, GL_FLOAT, GL_FLOAT, GL_FLOAT, GL_INT, GL_FLOAT};

    for (GLuint location = 0; location < 6; ++location) {
        glEnableVertexArrayAttrib(out.m_vao, location);
        if (location == 4) {
            glVertexArrayAttribIFormat(out.m_vao, location, components[location], types[location],
                static_cast<GLuint>(offsets[location]));
        } else {
            glVertexArrayAttribFormat(out.m_vao, location, components[location], types[location], GL_FALSE,
                offsets[location]);
        }
        glVertexArrayAttribBinding(out.m_vao, location, 0);
    }

    return out;
}

void GlMesh::draw() const
{
    glBindVertexArray(m_vao);
    glDrawElements(GL_TRIANGLES, static_cast<GLsizei>(m_indexCount), GL_UNSIGNED_INT, nullptr);
}

void GlMesh::drawInstanced(unsigned int instanceCount) const
{
    glBindVertexArray(m_vao);
    glDrawElementsInstanced(GL_TRIANGLES, static_cast<GLsizei>(m_indexCount), GL_UNSIGNED_INT, nullptr,
        static_cast<GLsizei>(instanceCount));
}

} // namespace engine

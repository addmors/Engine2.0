#pragma once

#include <glm/glm.hpp>

#include "engine/assets/Mesh.hpp"

namespace engine {

// GPU mesh buffer (DSA). Created from engine::Mesh; draw via draw()/drawInstanced().
class GlMesh final {
public:
    GlMesh() = default;
    ~GlMesh();

    GlMesh(const GlMesh&) = delete;
    GlMesh& operator=(const GlMesh&) = delete;
    GlMesh(GlMesh&& other) noexcept;
    GlMesh& operator=(GlMesh&& other) noexcept;

    [[nodiscard]] static GlMesh create(const Mesh& mesh);

    void draw() const;                                 // indexed triangles
    void drawInstanced(unsigned int instanceCount) const;

    [[nodiscard]] unsigned int indexCount() const { return m_indexCount; }
    [[nodiscard]] bool valid() const { return m_vao != 0; }

private:
    unsigned int m_vao = 0;
    unsigned int m_vbo = 0;
    unsigned int m_ebo = 0;
    unsigned int m_indexCount = 0;
};

} // namespace engine

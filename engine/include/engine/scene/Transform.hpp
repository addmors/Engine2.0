#pragma once

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>

namespace engine {

// Scene transform with dirty-flag matrix caching (clean port of the 2017 Transform).
class Transform {
public:
    // C++23 deducing this: one accessor serves const/non-const, lvalue/rvalue.
    template <typename Self>
    [[nodiscard]] auto&& position(this Self&& self)
    {
        return self.m_position;
    }

    template <typename Self>
    [[nodiscard]] auto&& eulerRotation(this Self&& self) // degrees
    {
        return self.m_eulerRotation;
    }

    template <typename Self>
    [[nodiscard]] auto&& scale(this Self&& self)
    {
        return self.m_scale;
    }

    void setPosition(glm::vec3 position)
    {
        m_position = position;
        flagDirty();
    }

    void setEulerRotation(glm::vec3 degrees)
    {
        m_eulerRotation = degrees;
        flagDirty();
    }

    void setScale(glm::vec3 scale)
    {
        m_scale = scale;
        flagDirty();
    }

    void computeModelMatrix()
    {
        m_modelMatrix = computeLocalMatrix();
        m_dirty = false;
    }

    void computeModelMatrix(const glm::mat4& parent)
    {
        m_modelMatrix = parent * computeLocalMatrix();
        m_dirty = false;
    }

    [[nodiscard]] const glm::mat4& modelMatrix() const { return m_modelMatrix; }
    [[nodiscard]] bool isDirty() const { return m_dirty; }
    [[nodiscard]] glm::vec3 globalPosition() const { return glm::vec3{m_modelMatrix[3]}; }
    [[nodiscard]] glm::vec3 right() const { return glm::vec3{m_modelMatrix[0]}; }
    [[nodiscard]] glm::vec3 up() const { return glm::vec3{m_modelMatrix[1]}; }
    [[nodiscard]] glm::vec3 forward() const { return -glm::vec3{m_modelMatrix[2]}; }

private:
    [[nodiscard]] glm::mat4 computeLocalMatrix() const
    {
        glm::mat4 m = glm::translate(glm::mat4{1.0F}, m_position);
        m *= glm::mat4_cast(glm::quat(glm::radians(m_eulerRotation)));
        return glm::scale(m, m_scale);
    }

    void flagDirty() { m_dirty = true; }

    glm::vec3 m_position{0.0F};
    glm::vec3 m_eulerRotation{0.0F}; // degrees
    glm::vec3 m_scale{1.0F};
    glm::mat4 m_modelMatrix{1.0F};
    bool m_dirty = true;
};

} // namespace engine

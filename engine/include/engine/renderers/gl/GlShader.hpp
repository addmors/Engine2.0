#pragma once

#include <expected>
#include <string>
#include <string_view>
#include <unordered_map>

#include <glm/glm.hpp>

#include "engine/assets/AssetError.hpp"

namespace engine {

// GLSL program with recursive #include resolution (the modern replacement for
// the 2017 FileLoader). Uniform locations are cached on first query.
class GlProgram final {
public:
    GlProgram() = default;
    ~GlProgram();

    GlProgram(const GlProgram&) = delete;
    GlProgram& operator=(const GlProgram&) = delete;
    GlProgram(GlProgram&& other) noexcept;
    GlProgram& operator=(GlProgram&& other) noexcept;

    [[nodiscard]] static std::expected<GlProgram, AssetError> createFromFile(
        std::string_view vertexPath, std::string_view fragmentPath);

    void use() const;

    [[nodiscard]] int uniformLocation(const std::string& name);
    void setFloat(const std::string& name, float value);
    void setInt(const std::string& name, int value);
    void setVec3(const std::string& name, const glm::vec3& value);
    void setMat4(const std::string& name, const glm::mat4& value);

    [[nodiscard]] unsigned int handle() const { return m_program; }

private:
    unsigned int m_program = 0;
    std::unordered_map<std::string, int> m_locations;
};

} // namespace engine

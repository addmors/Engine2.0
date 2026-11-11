#define GLEW_STATIC

#include "engine/renderers/gl/GlShader.hpp"

#include <filesystem>
#include <format>
#include <fstream>
#include <set>
#include <sstream>
#include <utility>

#include <GL/glew.h>

#include "engine/core/Log.hpp"

namespace engine {

namespace {

// Reads a shader file resolving `#include "..."` recursively (cycle-safe).
// Include paths are tried relative to the including file first, then relative
// to the top-level shader's directory (so inc/x.ubo can include inc/y.ubo or
// just y.ubo — both work). Replaces the 2017 FileLoader.
std::expected<std::string, AssetError> readShaderSource(
    const std::filesystem::path& path, const std::filesystem::path& rootDir,
    std::set<std::filesystem::path>& visited)
{
    std::error_code ec;
    const auto canonical = std::filesystem::weakly_canonical(path, ec);
    if (!visited.insert(ec ? path : canonical).second) {
        return std::unexpected(AssetError{
            AssetError::Kind::ParseFailed, std::format("circular #include at {}", path.generic_string())});
    }

    std::ifstream file{path};
    if (!file) {
        return std::unexpected(AssetError{
            AssetError::Kind::FileNotFound, std::format("shader file not found: {}", path.generic_string())});
    }

    std::stringstream out;
    std::string line;
    while (std::getline(file, line)) {
        if (line.rfind("#include", 0) == 0) {
            const size_t first = line.find('"');
            const size_t last = line.rfind('"');
            if (first == std::string::npos || last == first) {
                return std::unexpected(AssetError{AssetError::Kind::ParseFailed,
                    std::format("malformed #include in {}: {}", path.generic_string(), line)});
            }
            const std::string includeName = line.substr(first + 1, last - first - 1);

            std::filesystem::path includePath = path.parent_path() / includeName;
            if (!std::filesystem::exists(includePath)) {
                includePath = rootDir / includeName; // fallback: relative to the top-level shader dir
            }

            auto included = readShaderSource(includePath, rootDir, visited);
            if (!included) {
                return included; // propagate the error
            }
            out << *included << '\n';
        } else {
            out << line << '\n';
        }
    }
    return out.str();
}

std::expected<unsigned int, AssetError> compileStage(
    GLenum stage, const std::string& source, const std::string& debugName)
{
    const unsigned int shader = glCreateShader(stage);
    const char* sourcePtr = source.c_str();
    glShaderSource(shader, 1, &sourcePtr, nullptr);
    glCompileShader(shader);

    GLint status = 0;
    glGetShaderiv(shader, GL_COMPILE_STATUS, &status);
    if (status == GL_FALSE) {
        char log[2048];
        GLsizei length = 0;
        glGetShaderInfoLog(shader, sizeof(log), &length, log);
        glDeleteShader(shader);
        return std::unexpected(AssetError{AssetError::Kind::ParseFailed,
            std::format("failed to compile {}: {}", debugName,
                std::string_view{log, static_cast<size_t>(length)})});
    }
    return shader;
}

} // namespace

GlProgram::~GlProgram()
{
    if (m_program != 0) {
        glDeleteProgram(m_program);
    }
}

GlProgram::GlProgram(GlProgram&& other) noexcept
    : m_program(std::exchange(other.m_program, 0))
    , m_locations(std::move(other.m_locations))
{
}

GlProgram& GlProgram::operator=(GlProgram&& other) noexcept
{
    if (this != &other) {
        if (m_program != 0) {
            glDeleteProgram(m_program);
        }
        m_program = std::exchange(other.m_program, 0);
        m_locations = std::move(other.m_locations);
    }
    return *this;
}

std::expected<GlProgram, AssetError> GlProgram::createFromFile(
    std::string_view vertexPath, std::string_view fragmentPath)
{
    const std::filesystem::path vertexDir = std::filesystem::path{vertexPath}.parent_path();
    const std::filesystem::path fragmentDir = std::filesystem::path{fragmentPath}.parent_path();

    std::set<std::filesystem::path> visitedVertex;
    auto vertexSource = readShaderSource(vertexPath, vertexDir, visitedVertex);
    if (!vertexSource) {
        return std::unexpected(std::move(vertexSource).error());
    }

    std::set<std::filesystem::path> visitedFragment;
    auto fragmentSource = readShaderSource(fragmentPath, fragmentDir, visitedFragment);
    if (!fragmentSource) {
        return std::unexpected(std::move(fragmentSource).error());
    }

    auto vertexShader = compileStage(GL_VERTEX_SHADER, *vertexSource, std::string{vertexPath});
    if (!vertexShader) {
        return std::unexpected(std::move(vertexShader).error());
    }
    auto fragmentShader = compileStage(GL_FRAGMENT_SHADER, *fragmentSource, std::string{fragmentPath});
    if (!fragmentShader) {
        return std::unexpected(std::move(fragmentShader).error());
    }

    GlProgram program;
    program.m_program = glCreateProgram();
    glAttachShader(program.m_program, *vertexShader);
    glAttachShader(program.m_program, *fragmentShader);
    glLinkProgram(program.m_program);
    glDeleteShader(*vertexShader);
    glDeleteShader(*fragmentShader);

    GLint status = 0;
    glGetProgramiv(program.m_program, GL_LINK_STATUS, &status);
    if (status == GL_FALSE) {
        char log[2048];
        GLsizei length = 0;
        glGetProgramInfoLog(program.m_program, sizeof(log), &length, log);
        return std::unexpected(AssetError{AssetError::Kind::ParseFailed,
            std::format("failed to link {} + {}: {}", vertexPath, fragmentPath,
                std::string_view{log, static_cast<size_t>(length)})});
    }

    Log::info("program linked: {} + {}", vertexPath, fragmentPath);
    return program;
}

void GlProgram::use() const
{
    glUseProgram(m_program);
}

int GlProgram::uniformLocation(const std::string& name)
{
    const auto it = m_locations.find(name);
    if (it != m_locations.end()) {
        return it->second;
    }
    const int location = glGetUniformLocation(m_program, name.c_str());
    m_locations.emplace(name, location);
    return location;
}

void GlProgram::setFloat(const std::string& name, float value)
{
    glProgramUniform1f(m_program, uniformLocation(name), value);
}

void GlProgram::setInt(const std::string& name, int value)
{
    glProgramUniform1i(m_program, uniformLocation(name), value);
}

void GlProgram::setVec3(const std::string& name, const glm::vec3& value)
{
    glProgramUniform3fv(m_program, uniformLocation(name), 1, &value[0]);
}

void GlProgram::setMat4(const std::string& name, const glm::mat4& value)
{
    glProgramUniformMatrix4fv(m_program, uniformLocation(name), 1, GL_FALSE, &value[0][0]);
}

} // namespace engine

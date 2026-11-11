# Engine static library: core (log/time), platform (window/input), ui (ImGui overlay).
# RHI + OpenGL/Vulkan renderers arrive in M2/M3.

add_library(engine STATIC
    src/core/Log.cpp
    src/core/Input.cpp
    src/platform/Window.cpp
    src/ui/Ui.cpp
    src/assets/Image.cpp
    src/assets/Model.cpp
    src/assets/Animator.cpp
    src/scene/Terrain.cpp
    src/scene/TerrainMesh.cpp
    src/scene/Primitives.cpp
    src/physics/PhysicsWorld.cpp
    src/physics/PlayerController.cpp
    src/renderers/gl/GlShader.cpp
    src/renderers/gl/GlTexture.cpp
    src/renderers/gl/GlMesh.cpp
    src/renderers/gl/GlRenderer.cpp
    src/renderers/gl/GlFrameDump.cpp
)

target_include_directories(engine PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)

target_compile_definitions(engine PUBLIC
    GLFW_INCLUDE_NONE   # include glfw3.h without any GL headers
    NOMINMAX
    WIN32_LEAN_AND_MEAN
)

target_link_libraries(engine PUBLIC
    imgui
    glfw
    spdlog::spdlog
    glm::glm
    stb
)

# assimp headers are not exposed in engine's public interface -> PRIVATE.
target_link_libraries(engine PRIVATE assimp::assimp)

# bullet 3.25's old-style CMake doesn't export include dirs through its targets.
target_include_directories(engine PUBLIC ${bullet3_SOURCE_DIR}/src)
target_link_libraries(engine PUBLIC
    BulletDynamics
    BulletCollision
    LinearMath
)

# --- GLEW (temporary vendored static lib from Engine1.0) --------------------------
# TODO: replace with glad2 (FetchContent) once the GitHub credential manager
# stops popping auth dialogs on this machine.
add_library(glew INTERFACE)
target_include_directories(glew INTERFACE ${CMAKE_SOURCE_DIR}/third_party/glew/include)
target_link_libraries(glew INTERFACE
    ${CMAKE_SOURCE_DIR}/third_party/glew/lib/glew32s.lib
    opengl32
)
target_compile_definitions(glew INTERFACE GLEW_STATIC)

# GL usage stays inside engine .cpp files -> PRIVATE.
target_link_libraries(engine PRIVATE glew)

# Baked-in path so the renderer can find its shader sources.
target_compile_definitions(engine PRIVATE
    CANDY_SHADERS_DIR="${CMAKE_SOURCE_DIR}/shaders/gl"
)

target_link_libraries(engine PUBLIC candy_warnings)

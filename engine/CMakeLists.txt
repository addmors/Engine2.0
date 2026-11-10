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
    src/physics/PhysicsWorld.cpp
    src/physics/PlayerController.cpp
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

target_link_libraries(engine PUBLIC candy_warnings)

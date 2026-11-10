# Engine static library: core (log/time), platform (window/input), ui (ImGui overlay).
# RHI + OpenGL/Vulkan renderers arrive in M2/M3.

add_library(engine STATIC
    src/core/Log.cpp
    src/platform/Window.cpp
    src/ui/Ui.cpp
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
)

target_link_libraries(engine PUBLIC candy_warnings)

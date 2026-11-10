cmake_minimum_required(VERSION 3.28)

project(Candy
    VERSION 0.1.0
    DESCRIPTION "Engine 2.0 'Candy': OpenGL 4.6 reference renderer + Vulkan 1.4, C++23, Dear ImGui"
    LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")
include(CompilerWarnings)
include(Dependencies)

add_subdirectory(engine)
add_subdirectory(app)

# candy as the startup project when opening the generated solution in Visual Studio.
set_property(DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR} PROPERTY VS_STARTUP_PROJECT candy)

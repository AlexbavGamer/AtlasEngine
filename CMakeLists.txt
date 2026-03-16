cmake_minimum_required(VERSION 3.16)

project(VulkanEngineV2 VERSION 1.0 LANGUAGES C CXX)

# =========================
# Configuração geral
# =========================
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if(NOT CMAKE_CONFIGURATION_TYPES AND NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Debug CACHE STRING "Build type" FORCE)
endif()

include(FetchContent)
find_package(Vulkan REQUIRED)

# =========================
# Pastas persistentes de dependências
# =========================
set(DEPS_DIR        "${CMAKE_SOURCE_DIR}/deps")
set(DEPS_SRC_DIR    "${DEPS_DIR}/src")
set(DEPS_BUILD_DIR  "${DEPS_DIR}/build")

file(MAKE_DIRECTORY "${DEPS_SRC_DIR}")
file(MAKE_DIRECTORY "${DEPS_BUILD_DIR}")

# Evita tentar atualizar repositórios sempre
set(FETCHCONTENT_UPDATES_DISCONNECTED ON)

# =========================
# GLFW
# =========================
set(GLFW_BUILD_DOCS OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(GLFW_INSTALL OFF CACHE BOOL "" FORCE)

FetchContent_Declare(
    glfw
    GIT_REPOSITORY https://github.com/glfw/glfw.git
    GIT_TAG 3.4
    SOURCE_DIR "${DEPS_SRC_DIR}/glfw"
    BINARY_DIR "${DEPS_BUILD_DIR}/glfw"
)
FetchContent_MakeAvailable(glfw)

# =========================
# GLM
# =========================
set(GLM_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(GLM_BUILD_INSTALL OFF CACHE BOOL "" FORCE)

FetchContent_Declare(
    glm
    GIT_REPOSITORY https://github.com/g-truc/glm.git
    GIT_TAG 1.0.1
    SOURCE_DIR "${DEPS_SRC_DIR}/glm"
    BINARY_DIR "${DEPS_BUILD_DIR}/glm"
)
FetchContent_MakeAvailable(glm)

# =========================
# STB
# =========================
FetchContent_Declare(
    stb
    GIT_REPOSITORY https://github.com/nothings/stb.git
    GIT_TAG master
    SOURCE_DIR "${DEPS_SRC_DIR}/stb"
    BINARY_DIR "${DEPS_BUILD_DIR}/stb"
)
FetchContent_MakeAvailable(stb)

# =========================
# ImGui
# =========================
FetchContent_Declare(
    imgui
    GIT_REPOSITORY https://github.com/ocornut/imgui.git
    GIT_TAG docking
    SOURCE_DIR "${DEPS_SRC_DIR}/imgui"
    BINARY_DIR "${DEPS_BUILD_DIR}/imgui"
)
FetchContent_MakeAvailable(imgui)

# =========================
# ImGui como target próprio
# =========================
add_library(imgui_lib STATIC
    ${imgui_SOURCE_DIR}/imgui.cpp
    ${imgui_SOURCE_DIR}/imgui_draw.cpp
    ${imgui_SOURCE_DIR}/imgui_tables.cpp
    ${imgui_SOURCE_DIR}/imgui_widgets.cpp
    ${imgui_SOURCE_DIR}/imgui_demo.cpp
    ${imgui_SOURCE_DIR}/backends/imgui_impl_glfw.cpp
    ${imgui_SOURCE_DIR}/backends/imgui_impl_vulkan.cpp
)

target_include_directories(imgui_lib PUBLIC
    ${imgui_SOURCE_DIR}
    ${imgui_SOURCE_DIR}/backends
)

target_link_libraries(imgui_lib PUBLIC glfw Vulkan::Vulkan)

# =========================
# Executável principal
# =========================
set(SOURCES
    src/main.cpp
    src/vulkan/instance.cpp
    src/vulkan/device.cpp
    src/vulkan/swapchain.cpp
    src/vulkan/render_pass.cpp
    src/vulkan/framebuffer.cpp
    src/vulkan/command_buffers.cpp
    src/imgui/imgui_manager.cpp
    src/scene/scene.cpp
    src/ui/ui_manager.cpp
    src/ecs/systems.cpp
)

add_executable(${PROJECT_NAME} ${SOURCES})

target_include_directories(${PROJECT_NAME} PRIVATE
    ${glm_SOURCE_DIR}
    ${stb_SOURCE_DIR}
)

target_link_libraries(${PROJECT_NAME} PRIVATE
    Vulkan::Vulkan
    glfw
    imgui_lib
)

# =========================
# GLSLC / shaders
# =========================
if(TARGET Vulkan::glslc)
    set(GLSLC_EXECUTABLE Vulkan::glslc)
elseif(DEFINED Vulkan_GLSLC_EXECUTABLE)
    set(GLSLC_EXECUTABLE "${Vulkan_GLSLC_EXECUTABLE}")
elseif(EXISTS "$ENV{VULKAN_SDK}/Bin/glslc.exe")
    set(GLSLC_EXECUTABLE "$ENV{VULKAN_SDK}/Bin/glslc.exe")
elseif(EXISTS "$ENV{VULKAN_SDK}/bin/glslc")
    set(GLSLC_EXECUTABLE "$ENV{VULKAN_SDK}/bin/glslc")
else()
    set(GLSLC_EXECUTABLE "")
endif()

set(SHADER_SOURCE_DIR "${CMAKE_SOURCE_DIR}/shaders")
set(SHADER_OUTPUT_DIR "${CMAKE_BINARY_DIR}/shaders")
file(MAKE_DIRECTORY "${SHADER_OUTPUT_DIR}")

if(GLSLC_EXECUTABLE)
    add_custom_command(
        OUTPUT "${SHADER_OUTPUT_DIR}/vert.spv"
        COMMAND ${GLSLC_EXECUTABLE} -fshader-stage=vert
                "${SHADER_SOURCE_DIR}/vert.glsl"
                -o "${SHADER_OUTPUT_DIR}/vert.spv"
        DEPENDS "${SHADER_SOURCE_DIR}/vert.glsl"
        COMMENT "Compiling vertex shader"
        VERBATIM
    )

    add_custom_command(
        OUTPUT "${SHADER_OUTPUT_DIR}/frag.spv"
        COMMAND ${GLSLC_EXECUTABLE} -fshader-stage=frag
                "${SHADER_SOURCE_DIR}/frag.glsl"
                -o "${SHADER_OUTPUT_DIR}/frag.spv"
        DEPENDS "${SHADER_SOURCE_DIR}/frag.glsl"
        COMMENT "Compiling fragment shader"
        VERBATIM
    )

    add_custom_target(shaders ALL
        DEPENDS
            "${SHADER_OUTPUT_DIR}/vert.spv"
            "${SHADER_OUTPUT_DIR}/frag.spv"
    )

    add_dependencies(${PROJECT_NAME} shaders)
else()
    message(WARNING "glslc not found. Shaders will not be compiled automatically.")
endif()

# Fallback: copia shaders fonte
add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
            "${SHADER_SOURCE_DIR}"
            "${CMAKE_BINARY_DIR}/shaders_src"
)
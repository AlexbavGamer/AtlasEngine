cmake_minimum_required(VERSION 3.16)

project(AtlasEngine VERSION 1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

option(ENABLE_TRACY "Enable Tracy profiler" ON)

find_package(Threads REQUIRED)

if(NOT CMAKE_CONFIGURATION_TYPES AND NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Debug CACHE STRING "Build type" FORCE)
endif()

include(FetchContent)
find_package(Vulkan REQUIRED)

# Dependencies directories
set(DEPS_DIR "${CMAKE_SOURCE_DIR}/deps")
set(DEPS_SRC_DIR "${DEPS_DIR}/src")
set(DEPS_BUILD_DIR "${DEPS_DIR}/build")

file(MAKE_DIRECTORY "${DEPS_SRC_DIR}")
file(MAKE_DIRECTORY "${DEPS_BUILD_DIR}")

set(FETCHCONTENT_UPDATES_DISCONNECTED TRUE)

# Helper macro for FetchContent
macro(atlas_fetch dep_name git_repo git_tag)
    FetchContent_Declare(
        ${dep_name}
        GIT_REPOSITORY ${git_repo}
        GIT_TAG ${git_tag}
        SOURCE_DIR "${DEPS_SRC_DIR}/${dep_name}"
        BINARY_DIR "${DEPS_BUILD_DIR}/${dep_name}"
    )
    FetchContent_MakeAvailable(${dep_name})
endmacro()

# GLFW
set(GLFW_BUILD_DOCS OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(GLFW_INSTALL OFF CACHE BOOL "" FORCE)
atlas_fetch(glfw https://github.com/glfw/glfw.git 3.4)

# GLM
set(GLM_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(GLM_BUILD_INSTALL OFF CACHE BOOL "" FORCE)
atlas_fetch(glm https://github.com/g-truc/glm.git 1.0.1)

# STB
atlas_fetch(stb https://github.com/nothings/stb.git master)

# ImGui
atlas_fetch(imgui https://github.com/ocornut/imgui.git docking)

# ImGuiFileDialog - just declare, don't build
FetchContent_Declare(
    imguifiledialog
    GIT_REPOSITORY https://github.com/aiekick/ImGuiFileDialog.git
    GIT_TAG v0.6.8
    SOURCE_DIR "${DEPS_SRC_DIR}/imguifiledialog"
)
set(FETCHCONTENT_QUIET OFF)
FetchContent_GetProperties(imguifiledialog)
if(NOT imguifiledialog_POPULATED)
    FetchContent_Populate(imguifiledialog)
    add_subdirectory(${imguifiledialog_SOURCE_DIR} ${imguifiledialog_BINARY_DIR} EXCLUDE_FROM_ALL)
endif()
target_include_directories(ImGuiFileDialog PRIVATE ${imgui_SOURCE_DIR})

# ImGuizmo - header-only library for transform gizmos
FetchContent_Declare(
    imguizmo
    GIT_REPOSITORY https://github.com/CedricGuillemet/ImGuizmo.git
    GIT_TAG master
    SOURCE_DIR "${DEPS_SRC_DIR}/imguizmo"
)
FetchContent_MakeAvailable(imguizmo)

# EnTT
atlas_fetch(entt https://github.com/skypjack/entt.git v3.12.1)

# VMA
set(VMA_BUILD_EXAMPLE OFF CACHE BOOL "" FORCE)
atlas_fetch(vma https://github.com/GPUOpen-LibrariesAndSDKs/VulkanMemoryAllocator.git v3.3.0)

# Tracy (profiler) - optional
if(ENABLE_TRACY)
    # Disable frame image compression thread for improved stability in this integration.
    set(TRACY_NO_FRAME_IMAGE ON CACHE BOOL "Disable the frame image support and its thread" FORCE)
    atlas_fetch(tracy https://github.com/wolfpld/tracy.git v0.13.1)
endif()

# Assimp - Only FBX, OBJ, DAE
set(ASSIMP_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(ASSIMP_BUILD_SAMPLES OFF CACHE BOOL "" FORCE)
set(ASSIMP_BUILD_ASSIMP_TOOLS OFF CACHE BOOL "" FORCE)
set(ASSIMP_BUILD_UNIT_TESTS OFF CACHE BOOL "" FORCE)
set(ASSIMP_ENABLE_BOOST_WORKAROUND OFF CACHE BOOL "" FORCE)
set(ASSIMP_WARNINGS_AS_ERRORS OFF CACHE BOOL "" FORCE)
set(ASSIMP_INSTALL OFF CACHE BOOL "" FORCE)

# Disable unused file formats to reduce binary size
set(ASSIMP_BUILD_3DS_EXPORTER OFF CACHE BOOL "" FORCE)
set(ASSIMP_BUILD_3DS_IMPORTER OFF CACHE BOOL "" FORCE)
set(ASSIMP_BUILD_AC_EXPORTER OFF CACHE BOOL "" FORCE)
set(ASSIMP_BUILD_AC_IMPORTER OFF CACHE BOOL "" FORCE)
set(ASSIMP_BUILD_AMF_EXPORTER OFF CACHE BOOL "" FORCE)
set(ASSIMP_BUILD_AMF_IMPORTER OFF CACHE BOOL "" FORCE)
set(ASSIMP_BUILD_ASE_EXPORTER OFF CACHE BOOL "" FORCE)
set(ASSIMP_BUILD_ASE_IMPORTER OFF CACHE BOOL "" FORCE)
set(ASSIMP_BUILD_ASSBIN_EXPORTER OFF CACHE BOOL "" FORCE)
set(ASSIMP_BUILD_ASSBIN_IMPORTER OFF CACHE BOOL "" FORCE)
set(ASSIMP_BUILD_ASSXML_EXPORTER OFF CACHE BOOL "" FORCE)
set(ASSIMP_BUILD_B3D_EXPORTER OFF CACHE BOOL "" FORCE)
set(ASSIMP_BUILD_B3D_IMPORTER OFF CACHE BOOL "" FORCE)
set(ASSIMP_BUILD_BLEND_EXPORTER OFF CACHE BOOL "" FORCE)
set(ASSIMP_BUILD_BLEND_IMPORTER OFF CACHE BOOL "" FORCE)
set(ASSIMP_BUILD_BVH_EXPORTER OFF CACHE BOOL "" FORCE)
set(ASSIMP_BUILD_BVH_IMPORTER OFF CACHE BOOL "" FORCE)
set(ASSIMP_BUILD_COB_EXPORTER OFF CACHE BOOL "" FORCE)
set(ASSIMP_BUILD_COB_IMPORTER OFF CACHE BOOL "" FORCE)
set(ASSIMP_BUILD_CSM_EXPORTER OFF CACHE BOOL "" FORCE)
set(ASSIMP_BUILD_CSM_IMPORTER OFF CACHE BOOL "" FORCE)
set(ASSIMP_BUILD_DXF_EXPORTER OFF CACHE BOOL "" FORCE)
set(ASSIMP_BUILD_DXF_IMPORTER OFF CACHE BOOL "" FORCE)
set(ASSIMP_BUILD_FBX_EXPORTER ON CACHE BOOL "" FORCE)
set(ASSIMP_BUILD_FBX_IMPORTER ON CACHE BOOL "" FORCE)
set(ASSIMP_BUILD_GLTF_EXPORTER OFF CACHE BOOL "" FORCE)
set(ASSIMP_BUILD_GLTF_IMPORTER ON CACHE BOOL "" FORCE)
set(ASSIMP_BUILD_HLM_EXPORTER OFF CACHE BOOL "" FORCE)
set(ASSIMP_BUILD_HLM_IMPORTER OFF CACHE BOOL "" FORCE)
set(ASSIMP_BUILD_IFC_EXPORTER OFF CACHE BOOL "" FORCE)
set(ASSIMP_BUILD_IFC_IMPORTER OFF CACHE BOOL "" FORCE)
set(ASSIMP_BUILD_IQM_EXPORTER OFF CACHE BOOL "" FORCE)
set(ASSIMP_BUILD_IQM_IMPORTER OFF CACHE BOOL "" FORCE)
set(ASSIMP_BUILD_IRR_EXPORTER OFF CACHE BOOL "" FORCE)
set(ASSIMP_BUILD_IRR_IMPORTER OFF CACHE BOOL "" FORCE)
set(ASSIMP_BUILD_LWO_EXPORTER OFF CACHE BOOL "" FORCE)
set(ASSIMP_BUILD_LWO_IMPORTER OFF CACHE BOOL "" FORCE)
set(ASSIMP_BUILD_LWS_EXPORTER OFF CACHE BOOL "" FORCE)
set(ASSIMP_BUILD_LWS_IMPORTER OFF CACHE BOOL "" FORCE)
set(ASSIMP_BUILD_M3_EXPORTER OFF CACHE BOOL "" FORCE)
set(ASSIMP_BUILD_M3_IMPORTER OFF CACHE BOOL "" FORCE)
set(ASSIMP_BUILD_MD2_EXPORTER OFF CACHE BOOL "" FORCE)
set(ASSIMP_BUILD_MD2_IMPORTER OFF CACHE BOOL "" FORCE)
set(ASSIMP_BUILD_MD3_EXPORTER OFF CACHE BOOL "" FORCE)
set(ASSIMP_BUILD_MD3_IMPORTER OFF CACHE BOOL "" FORCE)
set(ASSIMP_BUILD_MD5_EXPORTER OFF CACHE BOOL "" FORCE)
set(ASSIMP_BUILD_MD5_IMPORTER OFF CACHE BOOL "" FORCE)
set(ASSIMP_BUILD_MDC_EXPORTER OFF CACHE BOOL "" FORCE)
set(ASSIMP_BUILD_MDC_IMPORTER OFF CACHE BOOL "" FORCE)
set(ASSIMP_BUILD_MDL_EXPORTER OFF CACHE BOOL "" FORCE)
set(ASSIMP_BUILD_MDL_IMPORTER OFF CACHE BOOL "" FORCE)
set(ASSIMP_BUILD_MMD_EXPORTER OFF CACHE BOOL "" FORCE)
set(ASSIMP_BUILD_MMD_IMPORTER OFF CACHE BOOL "" FORCE)
set(ASSIMP_BUILD_MS3D_EXPORTER OFF CACHE BOOL "" FORCE)
set(ASSIMP_BUILD_MS3D_IMPORTER OFF CACHE BOOL "" FORCE)
set(ASSIMP_BUILD_NDO_EXPORTER OFF CACHE BOOL "" FORCE)
set(ASSIMP_BUILD_NDO_IMPORTER OFF CACHE BOOL "" FORCE)
set(ASSIMP_BUILD_NFF_EXPORTER OFF CACHE BOOL "" FORCE)
set(ASSIMP_BUILD_NFF_IMPORTER OFF CACHE BOOL "" FORCE)
set(ASSIMP_BUILD_OBJ_EXPORTER ON CACHE BOOL "" FORCE)
set(ASSIMP_BUILD_OBJ_IMPORTER ON CACHE BOOL "" FORCE)
set(ASSIMP_BUILD_OGRE_EXPORTER OFF CACHE BOOL "" FORCE)
set(ASSIMP_BUILD_OGRE_IMPORTER OFF CACHE BOOL "" FORCE)
set(ASSIMP_BUILD_OPENGEX_EXPORTER OFF CACHE BOOL "" FORCE)
set(ASSIMP_BUILD_OPENGEX_IMPORTER OFF CACHE BOOL "" FORCE)
set(ASSIMP_BUILD_PBRT_EXPORTER OFF CACHE BOOL "" FORCE)
set(ASSIMP_BUILD_PLY_EXPORTER OFF CACHE BOOL "" FORCE)
set(ASSIMP_BUILD_PLY_IMPORTER OFF CACHE BOOL "" FORCE)
set(ASSIMP_BUILD_Q3BSP_EXPORTER OFF CACHE BOOL "" FORCE)
set(ASSIMP_BUILD_Q3BSP_IMPORTER OFF CACHE BOOL "" FORCE)
set(ASSIMP_BUILD_Q3D_EXPORTER OFF CACHE BOOL "" FORCE)
set(ASSIMP_BUILD_Q3D_IMPORTER OFF CACHE BOOL "" FORCE)
set(ASSIMP_BUILD_RAW_EXPORTER OFF CACHE BOOL "" FORCE)
set(ASSIMP_BUILD_RAW_IMPORTER OFF CACHE BOOL "" FORCE)
set(ASSIMP_BUILD_SIB_EXPORTER OFF CACHE BOOL "" FORCE)
set(ASSIMP_BUILD_SIB_IMPORTER OFF CACHE BOOL "" FORCE)
set(ASSIMP_BUILD_SMD_EXPORTER OFF CACHE BOOL "" FORCE)
set(ASSIMP_BUILD_SMD_IMPORTER OFF CACHE BOOL "" FORCE)
set(ASSIMP_BUILD_STEP_EXPORTER OFF CACHE BOOL "" FORCE)
set(ASSIMP_BUILD_STEP_IMPORTER OFF CACHE BOOL "" FORCE)
set(ASSIMP_BUILD_STL_EXPORTER OFF CACHE BOOL "" FORCE)
set(ASSIMP_BUILD_STL_IMPORTER OFF CACHE BOOL "" FORCE)
set(ASSIMP_BUILD_TERRAGEN_EXPORTER OFF CACHE BOOL "" FORCE)
set(ASSIMP_BUILD_TERRAGEN_IMPORTER OFF CACHE BOOL "" FORCE)
set(ASSIMP_BUILD_X3D_EXPORTER OFF CACHE BOOL "" FORCE)
set(ASSIMP_BUILD_X3D_IMPORTER OFF CACHE BOOL "" FORCE)
set(ASSIMP_BUILD_X_EXPORTER OFF CACHE BOOL "" FORCE)
set(ASSIMP_BUILD_X_IMPORTER OFF CACHE BOOL "" FORCE)
set(ASSIMP_BUILD_DAE_EXPORTER ON CACHE BOOL "" FORCE)
set(ASSIMP_BUILD_DAE_IMPORTER ON CACHE BOOL "" FORCE)
set(ASSIMP_BUILD_HMP_EXPORTER OFF CACHE BOOL "" FORCE)
set(ASSIMP_BUILD_HMP_IMPORTER OFF CACHE BOOL "" FORCE)

atlas_fetch(assimp https://github.com/assimp/assimp.git v6.0.4)

# Common include directories
set(ATLAS_INCLUDE_DIRS
    ${imgui_SOURCE_DIR}
    ${glm_SOURCE_DIR}
    ${stb_SOURCE_DIR}
    ${entt_SOURCE_DIR}/src
    ${vma_SOURCE_DIR}/include
    ${assimp_SOURCE_DIR}/include
    ${imguizmo_SOURCE_DIR}
)

if(ENABLE_TRACY)
    list(APPEND ATLAS_INCLUDE_DIRS ${tracy_SOURCE_DIR}/public)
endif()

# ============================================================================
# Library: imgui_lib
# ============================================================================
add_library(imgui_lib STATIC
    ${imgui_SOURCE_DIR}/imgui.cpp
    ${imgui_SOURCE_DIR}/imgui_draw.cpp
    ${imgui_SOURCE_DIR}/imgui_tables.cpp
    ${imgui_SOURCE_DIR}/imgui_widgets.cpp
    ${imgui_SOURCE_DIR}/imgui_demo.cpp
    ${imgui_SOURCE_DIR}/backends/imgui_impl_glfw.cpp
    ${imgui_SOURCE_DIR}/backends/imgui_impl_vulkan.cpp
    ${imguifiledialog_SOURCE_DIR}/ImGuiFileDialog.cpp
)
target_compile_definitions(imgui_lib PRIVATE IMGUI_DEFINE_MATH_OPERATORS)

add_library(imguizmo_lib STATIC ${imguizmo_SOURCE_DIR}/ImGuizmo.cpp)
target_include_directories(imguizmo_lib PUBLIC ${imguizmo_SOURCE_DIR} ${imgui_SOURCE_DIR})
target_compile_definitions(imguizmo_lib PRIVATE IMGUI_DEFINE_MATH_OPERATORS)

target_include_directories(imgui_lib PUBLIC
    ${imgui_SOURCE_DIR}
    ${imgui_SOURCE_DIR}/backends
    ${imguifiledialog_SOURCE_DIR}
    ${imguizmo_SOURCE_DIR}
)

add_library(imguifiledialog INTERFACE)
target_include_directories(imguifiledialog INTERFACE
    ${imgui_SOURCE_DIR}
    ${imgui_SOURCE_DIR}/backends
    ${imguifiledialog_SOURCE_DIR}
)

# Tracy client
if(ENABLE_TRACY)
    add_library(tracy_lib INTERFACE)
    target_include_directories(tracy_lib INTERFACE ${tracy_SOURCE_DIR}/public)
    target_link_libraries(tracy_lib INTERFACE TracyClient)
endif()

target_link_libraries(imgui_lib PUBLIC glfw Vulkan::Vulkan imguifiledialog)

# ============================================================================
# Main executable: AtlasEngine
# ============================================================================
file(GLOB_RECURSE ATLAS_SOURCES
    "${CMAKE_SOURCE_DIR}/src/*.cpp"
)

add_executable(${PROJECT_NAME} ${ATLAS_SOURCES})

target_include_directories(${PROJECT_NAME} PRIVATE
    ${ATLAS_INCLUDE_DIRS}
)

target_link_libraries(${PROJECT_NAME} PRIVATE
    Vulkan::Vulkan
    glfw
    imgui_lib
    imguizmo_lib
    assimp
)

if(ENABLE_TRACY)
    target_link_libraries(${PROJECT_NAME} PRIVATE TracyClient tracy_lib)
    target_compile_definitions(${PROJECT_NAME} PRIVATE TRACY_ENABLE TRACY_NO_FRAME_IMAGE)
endif()

target_compile_definitions(${PROJECT_NAME} PRIVATE VMA_STATIC)
target_link_libraries(${PROJECT_NAME} PRIVATE Threads::Threads)

# ============================================================================
# Shader compilation
# ============================================================================
find_program(GLSLC_EXECUTABLE NAMES glslc PATHS
    "$ENV{VULKAN_SDK}/Bin"
    "$ENV{VULKAN_SDK}/bin"
    "/usr/bin"
    "/usr/local/bin"
    NO_DEFAULT_PATH
)

set(SHADER_SOURCE_DIR "${CMAKE_SOURCE_DIR}/shaders")
set(SHADER_OUTPUT_DIR "${CMAKE_BINARY_DIR}/shaders")
file(MAKE_DIRECTORY "${SHADER_OUTPUT_DIR}")

if(GLSLC_EXECUTABLE)
    message(STATUS "Found glslc: ${GLSLC_EXECUTABLE}")

    set(ATLAS_SHADER_SOURCES
        pbr_vert.glsl
        pbr_frag.glsl
    )
    set(ATLAS_SHADER_OUTPUTS)

    foreach(shader_file IN LISTS ATLAS_SHADER_SOURCES)
        get_filename_component(shader_name "${shader_file}" NAME_WE)
        get_filename_component(shader_ext "${shader_file}" LAST_EXT)
        string(REPLACE "." "" shader_stage "${shader_ext}")
        set(shader_output "${SHADER_OUTPUT_DIR}/${shader_name}.spv")
        list(APPEND ATLAS_SHADER_OUTPUTS "${shader_output}")

        add_custom_command(
            OUTPUT "${shader_output}"
            COMMAND ${GLSLC_EXECUTABLE} -fshader-stage=${shader_stage}
                    "${SHADER_SOURCE_DIR}/${shader_file}"
                    -o "${shader_output}"
            DEPENDS "${SHADER_SOURCE_DIR}/${shader_file}"
            COMMENT "Compiling ${shader_file}"
            VERBATIM
        )
    endforeach()

    add_custom_target(shaders ALL DEPENDS ${ATLAS_SHADER_OUTPUTS})
    add_dependencies(${PROJECT_NAME} shaders)
else()
    message(WARNING "glslc not found. Shaders will not be compiled automatically.")
    message(STATUS "Install Vulkan SDK or add glslc to PATH to enable shader compilation")
endif()

# Copy compiled shaders next to the executable runtime directory
add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
            "${SHADER_OUTPUT_DIR}"
            "$<TARGET_FILE_DIR:${PROJECT_NAME}>/shaders"
)

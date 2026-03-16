# VulkanEngineV2 Build System

## CMake Configuration
- Minimum CMake version: 3.16
- Project name: VulkanEngineV2
- Version: 1.0
- Languages: C, C++
- C++ Standard: C++17 (required)
- Build type: Debug (default if not specified)

## Dependencies (managed via FetchContent)
- GLFW 3.4: Windowing and input handling
  - Build options: DOCS, TESTS, EXAMPLES, INSTALL all disabled
- GLM 1.0.1: Mathematics for graphics
  - Build options: TESTS, INSTALL disabled
- stb: Single-header libraries (stb_image for texture loading)
  - Using master branch
- imgui (docking branch): Immediate mode GUI
  - Built as static library (imgui_lib)
  - Includes backends for GLFW and Vulkan

## Build Targets
1. **imgui_lib**: Static library containing ImGui and backends
   - Sources: imgui.cpp, imgui_draw.cpp, imgui_tables.cpp, imgui_widgets.cpp, imgui_demo.cpp
   - Backends: imgui_impl_glfw.cpp, imgui_impl_vulkan.cpp
   - Links against: glfw, Vulkan

2. **${PROJECT_NAME} (VulkanEngineV2)**: Main executable
   - Sources: 
     - src/main.cpp
     - src/vulkan/*.cpp (instance, device, swapchain, render_pass, framebuffer, command_buffers)
     - src/imgui/imgui_manager.cpp
     - src/scene/scene.cpp
     - src/ui/ui_manager.cpp
     - src/ecs/systems.cpp
   - Includes: glm_SOURCE_DIR, stb_SOURCE_DIR
   - Links against: Vulkan, glfw, imgui_lib

## Shader Compilation
- Uses glslc (GLSL compiler from Vulkan SDK) to compile GLSL to SPIR-V
- Vertex shader: shaders/vert.glsl → shaders/vert.spv
- Fragment shader: shaders/frag.glsl → shaders/frag.spv
- Custom commands generate these during build
- Main executable depends on shaders target
- Fallback: Copies shader source to build directory if glslc not found

## Directory Structure
- Source: src/
- Dependencies: deps/
  - Source: deps/src/
  - Build: deps/build/
- Shaders: shaders/ (GLSL source)
- Compiled shaders: ${CMAKE_BINARY_DIR}/shaders/ (SPIR-V)
- Shader sources copied to: ${CMAKE_BINARY_DIR}/shaders_src/

## Key Features
- FETCHCONTENT_UPDATES_DISCONNECTED: Prevents updating repositories on every build
- CMAKE_EXPORT_COMPILE_COMMANDS: Enables IDE integration (compile_commands.json)
- Proper error handling for missing glslc compiler
- POST_BUILD command to copy shader sources for debugging/editing
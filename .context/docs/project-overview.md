# Vulkan Engine V2 - Project Overview

## Project Overview
VulkanEngineV2 is a modern graphics rendering engine built with Vulkan API, featuring an Entity Component System (ECS) architecture for flexible game object management. The engine provides a foundation for real-time 3D rendering with support for ImGui-based user interfaces and cross-platform window management via GLFW.

## Codebase Reference
> **Detailed Analysis**: For complete symbol counts, architecture layers, and dependency graphs, see [`codebase-map.json`](./codebase-map.json).

## Quick Facts
- Root: `F:\VulkanEngineV2`
- Languages: C++ (primary), C (dependencies)
- Entry: `src/main.cpp`
- Full analysis: [`codebase-map.json`](./codebase-map.json)

## Entry Points
- Main application: [`src/main.cpp`](./src/main.cpp) - Contains the `VulkanEngine` class and `main()` function

## Key Exports
- VulkanEngine class: Main engine class handling initialization, rendering loop, and cleanup
- ECS World: Entity-component-system for game object management
- Scene Manager: Handles rendering of both legacy and ECS-based objects
- UI Manager: ImGui-based interface for engine interaction

## File Structure & Code Organization
- `src/` - Core engine source code
  - `main.cpp` - Engine entry point and main loop
  - `ecs/` - Entity Component System implementation
  - `scene/` - Scene management and rendering
  - `vulkan/` - Vulkan resource wrappers and utilities
  - `imgui/` - ImGui integration
  - `ui/` - User interface components
- `shaders/` - GLSL shaders compiled to SPIR-V
- `deps/` - Third-party dependencies (GLFW, GLM, stb_image, etc.)
- `.context/` - AI-generated documentation and agent playbooks

## Technology Stack Summary
- **Graphics API**: Vulkan 1.0 for high-performance graphics rendering
- **Windowing & Input**: GLFW for cross-platform window and input handling
- **Immediate Mode GUI**: ImGui for debug overlays and development tools
- **Mathematics**: GLM for vector and matrix operations
- **Build System**: CMake for cross-platform compilation
- **Dependencies**: stb_image for texture loading (via deps)

## Core Framework Stack
- **Rendering**: Custom Vulkan renderer with swapchain, render passes, and graphics pipelines
- **Entity Management**: ECS architecture for decoupled game logic and data
- **Scene Organization**: Hybrid approach supporting both legacy renderables and ECS entities
- **User Interface**: ImGui integration with Vulkan rendering backend

## UI & Interaction Libraries
- **ImGui**: Immediate mode GUI for development tools and debug interfaces
- **Custom UI Manager**: Wrapper around ImGui for engine-specific interface elements

## Development Tools Overview
- **Compiler**: MSVC/GCC/Clang compatible (via CMake)
- **Debugger**: Visual Studio debugger or GDB/LLDB
- **Graphics Debugger**: RenderDoc or Nsight Graphics for Vulkan debugging
- **Validation Layers**: Vulkan validation layers enabled in debug builds

## Getting Started Checklist
1. **Prerequisites**: Install Vulkan SDK, CMake, and a C++ compiler
2. **Dependencies**: The project includes GLFW, GLM, and stb_image in the `deps/` directory
3. **Build**: Run CMake to generate build files, then compile with your preferred IDE or command line
4. **Run**: Execute the compiled binary to see the engine in action
5. **Explore**: Review the codebase starting with `src/main.cpp` to understand the engine flow

## Next Steps
The engine currently implements a basic rendering loop with ECS initialization. Future development should focus on:
- Completing the ECS-based rendering system (moving from Scene::render to RenderSystem::update)
- Implementing resource management for meshes, textures, and materials
- Adding advanced rendering features (lighting, shadows, post-processing)
- Optimizing performance and memory usage
- Expanding the ECS with additional components and systems
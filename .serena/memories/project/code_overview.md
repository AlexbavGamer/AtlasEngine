# VulkanEngineV2 Code Overview

## Project Structure
- `src/main.cpp` - Entry point, initializes Vulkan, GLFW, ImGui, ECS, and runs main loop
- `src/ecs/` - Entity Component System implementation
  - `ecs.h` - Core ECS classes (Entity, Component, System, World)
  - `components.h` - Component definitions (Transform, Mesh, RenderableComponent)
  - `systems.h/cpp` - System implementations (rendering, physics, etc.)
- `src/scene/` - Scene management
  - `scene.h/cpp` - Handles both legacy renderables and ECS entities
- `src/vulkan/` - Vulkan abstraction layer
  - `instance.h/cpp` - Vulkan instance creation
  - `device.h/cpp` - Logical device and queue setup
  - `swapchain.h/cpp` - Swapchain management
  - `render_pass.h/cpp` - Render pass and framebuffer creation
  - `command_buffers.h/cpp` - Command buffer allocation and recording
  - `framebuffer.h/cpp` - Framebuffer management
  - `vulkan_structs.h` - Helper structs (QueueFamilyIndices, SwapChainSupportDetails)
- `src/imgui/` - ImGui integration
  - `imgui_manager.h/cpp` - ImGui initialization and rendering
- `src/ui/` - UI management
  - `ui_manager.h/cpp` - UI element handling
- `assets/` - Static assets (models, textures, etc.)
- `deps/` - Third-party dependencies (GLFW, GLM, etc.)

## Key Technologies
- Language: C++
- Graphics: Vulkan API
- Windowing: GLFW
- Immediate Mode GUI: ImGui
- Mathematics: GLM
- Build System: CMake

## Current Implementation Notes
- Engine initializes Vulkan context with validation layers
- Creates swapchain, render pass, framebuffers, and command buffers
- ImGui is integrated for UI/debug overlay
- ECS is initialized but current rendering still uses legacy approach (direct rendering in main loop)
- Scene management bridges legacy renderables and ECS entities
- No asset loading system implemented yet (assets/ directory present but unused)
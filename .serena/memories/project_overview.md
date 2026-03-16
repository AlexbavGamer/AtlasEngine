# VulkanEngineV2 Project Overview

## Project Structure
- **Main Engine**: `src/main.cpp` - Implements VulkanEngine class handling window creation, Vulkan setup, rendering loop, and ECS integration
- **ECS System**: `src/ecs/` - Entity Component System architecture
  - `ecs.h`: Core ECS implementation
  - `components.h`: Component definitions (Transform, Mesh, RenderableComponent)
  - `systems.h/cpp`: System implementations (RenderSystem)
- **Scene Management**: `src/scene/` - Handles both legacy renderables and ECS entities
  - `scene.h/cpp`: Scene class managing renderables and ECS world
- **Vulkan Wrappers**: `src/vulkan/` - Vulkan resource management
  - `vulkan_structs.h`: Helper structs (QueueFamilyIndices, SwapChainSupportDetails)
  - Instance, device, swapchain, render pass, pipeline, command buffers, framebuffers
- **UI Systems**: 
  - `src/imgui/`: ImGui integration
  - `src/ui/`: UI manager

## Key Technologies
- **Graphics API**: Vulkan 1.0
- **Windowing**: GLFW
- **UI**: ImGui
- **Math**: GLM
- **Architecture**: Entity Component System (ECS)

## Current Implementation Status
1. **Rendering Pipeline**: 
   - Vulkan instance, device, swapchain setup complete
   - Render pass, graphics pipeline, framebuffers implemented
   - Command buffers and synchronization in place
   - Offscreen rendering to framebuffer then present to swapchain

2. **ECS System**:
   - Core ECS implemented with entity/component/system architecture
   - Sample entities created with Transform and RenderableComponent
   - RenderSystem created but rendering still handled in Scene::render (legacy approach)

3. **UI Integration**:
   - ImGui initialized and integrated
   - UI manager for entity inspection/modification

4. **Resource Management**:
   - Basic texture sampling setup for offscreen rendering
   - Shader loading from SPIR-V files

## Areas for Improvement
1. **Complete ECS-based rendering**: Move rendering logic from Scene::render to RenderSystem::update
2. **Resource Management System**: Implement proper mesh/texture loading and management
3. **Rendering Features**: Add lighting, materials, and more advanced rendering techniques
4. **Performance Optimization**: Review memory usage, synchronization, and potential bottlenecks
5. **Error Handling**: Enhance validation and error reporting

## Dependencies
- Vulkan SDK
- GLFW
- GLM
- stb_image (via deps)
- ImGui
# VulkanEngineV2 Glossary

## Glossary & Domain Concepts
This glossary defines key terms, acronyms, and concepts used throughout the VulkanEngineV2 codebase and documentation.

## Type Definitions
- **VulkanEngine** (`src/main.cpp`) - Main engine class handling initialization, rendering loop, and cleanup
- **World** (`src/ecs/ecs.h`) - ECS container managing entities and systems
- **Entity** (`src/ecs/ecs.h`) - Unique identifier in ECS that holds components
- **Component** (`src/ecs/components.h`) - Data structure attached to entities (Transform, Mesh, RenderableComponent)
- **System** (`src/ecs/ecs.h`) - Logic processor that operates on entities with specific component combinations
- **RenderSystem** (`src/ecs/systems.h`) - System responsible for rendering entities with Renderable and Transform components
- **Scene** (`src/scene/scene.h`) - Manager for both legacy renderables and ECS-based rendering
- **ImGuiManager** (`src/imgui/imgui_manager.h`) - Wrapper for ImGui initialization and rendering with Vulkan
- **UIManager** (`src/ui/ui_manager.h`) - Engine-specific interface for entity inspection and manipulation

## Enumerations
- **VkResult** - Vulkan return codes indicating success or specific error conditions
- **VkFormat** - Pixel formats for images and attachments (e.g., VK_FORMAT_B8G8R8A8_SRGB)
- **VkPresentModeKHR** - Swapchain presentation modes (e.g., VK_PRESENT_MODE_MAILBOX_KHR, VK_PRESENT_MODE_FIFO_KHR)
- **VkPolygonMode** - Rasterization polygon modes (e.g., VK_POLYGON_MODE_FILL, VK_POLYGON_MODE_LINE)
- **VkCullModeFlagBits** - Face culling options (e.g., VK_CULL_MODE_BACK_BIT, VK_CULL_MODE_FRONT_BIT)

## Core Terms
- **Swapchain** - Series of Vulkan images used for presenting rendered frames to the window surface
- **Render Pass** - Defines the attachments (color, depth, stencil) used in a rendering operation and their load/store operations
- **Framebuffer** - Collection of image attachments that correspond to a render pass
- **Command Buffer** - Records GPU commands for execution (drawing, state changes, memory operations)
- **Pipeline** - Fixed-function and programmable graphics state (shaders, vertex input, blending, etc.)
- **Descriptor Set** - Shader resource bindings (uniforms, textures, samplers)
- **Semaphore** - Synchronization primitive for GPU operations (signal/wait between queue submissions)
- **Fence** - Synchronization primitive for CPU-GPU coordination (CPU waits for GPU to signal)
- **Queue Family** - Group of Vulkan queues with similar properties (graphics, present, compute, transfer)
- **Layer** - Vulkan validation layers that intercept API calls for debugging and validation
- **Extension** - Additional Vulkan functionality beyond core specification (e.g., VK_KHR_SWAPCHAIN_EXTENSION_NAME)
- **ECS (Entity Component System)** - Architectural pattern separating data (components) from behavior (systems)
- **Entity** - Unique ID representing a game object that can have components attached
- **Component** - Data-only struct defining an aspect of an entity (position, color, etc.)
- **System** - Logic that processes entities with specific component combinations
- **World** - Container holding all entities, components, and systems in an ECS
- **Renderable** - Object that can be drawn to screen (legacy approach)
- **RenderableComponent** - ECS component marking an entity as renderable
- **Transform** - ECS component defining position, rotation, and scale of an entity
- **Mesh** - Geometric data (vertices, indices) for rendering (placeholder in current implementation)
- **ImGui** - Immediate mode GUI library for creating debug interfaces and tools
- **GLFW** - Multi-platform library for OpenGL/Vulkan window and input handling
- **GLM** - OpenGL Mathematics library for vector and matrix operations
- **SPIR-V** - Intermediate language for Vulkan shaders (compiled from GLSL)

## Acronyms & Abbreviations
- **VK** - Vulkan (prefix for Vulkan types and functions)
- **GLSL** - OpenGL Shading Language
- **HLSL** - High-Level Shading Language (DirectX)
- **SLANG** - Shading language developed by NVIDIA
- **DX** - DirectX
- **GPU** - Graphics Processing Unit
- **CPU** - Central Processing Unit
- **RAM** - Random Access Memory
- **VRAM** - Video Random Access Memory (GPU memory)
- **FPS** - Frames Per Second
- **VSYNC** - Vertical Synchronization
- **LOD** - Level of Detail
- **FRUSTUM** - Camera viewing volume for culling
- **AABB** - Axis-Aligned Bounding Box
- **OBB** - Oriented Bounding Box
- **BVH** - Bounding Volume Hierarchy
- **PBR** - Physically Based Rendering
- **IBL** - Image-Based Lighting
- **SSR** - Screen Space Reflections
- **SSAO** - Screen Space Ambient Occlusion
- **HDR** - High Dynamic Range
- **SDR** - Standard Dynamic Range
- **UI** - User Interface
- **UX** - User Experience
- **API** - Application Programming Interface
- **SDK** - Software Development Kit
- **IDE** - Integrated Development Environment
- **CI/CD** - Continuous Integration/Continuous Deployment
- **PR** - Pull Request
- **WIP** - Work In Progress
- **TODO** - Task to be done
- **FIXME** - Known issue needing attention
- **HACK** - Temporary or suboptimal solution

## Personas / Actors
- **Graphics Engineer**: Focuses on rendering performance, visual quality, and Vulkan API usage
- **Game Developer**: Uses the engine to create game logic, entities, and gameplay systems
- **Tools Developer**: Creates editor extensions, debug visualizers, and artist-facing tools
- **Platform Engineer**: Ensures engine works across different hardware and operating systems
- **Performance Analyst**: Profiles and optimizes CPU/GPU usage, memory allocation, and frame times
- **Quality Assurance**: Tests engine functionality, reports bugs, and verifies fixes
- **Technical Artist**: Bridges art and implementation, creates shaders, materials, and visual effects
- **DevOps Engineer**: Manages build systems, dependencies, and deployment pipelines

## Domain Rules & Invariants
1. **Vulkan Object Lifetime**: All Vulkan objects must be properly destroyed before the Vulkan instance is terminated
2. **Thread Safety**: Vulkan commands should only be recorded on the main thread unless explicit external synchronization is used
3. **Resource Management**: Every vkCreate* call must have a corresponding vkDestroy* call
4. **Image Layout Transitions**: Images must be in the correct layout for their intended use (e.g., COLOR_ATTACHMENT_OPTIMAL for rendering)
5. **ECS Entity Validity**: Entities retrieved from the World must be valid (not destroyed) before accessing components
6. **Component Uniqueness**: An entity can have at most one instance of each component type
7. **System Execution Order**: Systems should be designed to work correctly regardless of update order, or explicit ordering should be documented
8. **Render Pass Compatibility**: Framebuffer attachments must match the render pass specification
9. **Shader Interface Matching**: Push constants, descriptor sets, and vertex inputs must match between shaders and pipeline layout
10. **Swapchain Recreation**: Swapchain must be recreated when window is resized or becomes invalid (VK_ERROR_OUT_OF_DATE_KHR)
11. **ImGui Context**: ImGui::NewFrame() must be called before any ImGui widgets, and ImGui::Render() before drawing
12. **GLFW Context**: GLFW window must be current on the thread calling Vulkan/GL functions
13. **Memory Alignment**: Vulkan memory allocations must respect required alignments for buffer and image usage
14. **Queue Submission**: Command buffers must be reset before re-recording, and fences must be waited on before reuse
15. **Validation Layers**: In debug builds, validation layers should report no errors for correct Vulkan usage
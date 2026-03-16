# VulkanEngineV2 Architecture

## Architecture Notes
VulkanEngineV2 follows a modular architecture centered around a Vulkan rendering engine with Entity Component System (ECS) for game object management. The design separates concerns between rendering infrastructure, scene management, entity systems, and user interface layers. The engine uses a hybrid approach where legacy rendering systems coexist with ECS-based rendering, allowing for gradual migration.

## System Architecture Overview
The engine follows a layered monolithic architecture where all components run in a single process. Control flows from the main engine loop through ECS systems to rendering subsystems, with the UI layer operating on a separate render pass. Requests (in this case, frame rendering) traverse: Input → ECS System Updates → Scene Rendering → Offscreen Rendering → UI Rendering → Presentation.

## Architectural Layers
- **Core Engine**: `src/main.cpp` - Main loop, Vulkan initialization, and system coordination
- **ECS Layer**: `src/ecs/` - Entity-component-system for game logic and data management
- **Scene Management**: `src/scene/` - Hybrid renderer handling both legacy objects and ECS entities
- **Vulkan Abstraction**: `src/vulkan/` - Low-level Vulkan resource wrappers (swapchain, pipelines, etc.)
- **User Interface**: `src/imgui/` and `src/ui/` - ImGui integration and UI management
- **Rendering Shaders**: `shaders/` - GLSL source and compiled SPIR-V shaders

> See [`codebase-map.json`](./codebase-map.json) for complete symbol counts and dependency graphs.

## Detected Design Patterns
| Pattern | Confidence | Locations | Description |
|---------|------------|-----------|-------------|
| Entity Component System | 95% | `src/ecs/ecs.h`, `src/ecs/components.h`, `src/ecs/systems.h` | Decouples game objects into entities, components, and systems |
| Singleton | 70% | `VulkanEngine` class in `src/main.cpp` | Single engine instance managing all systems |
| Facade | 80% | `VulkanEngine` class | Provides simplified interface to complex Vulkan subsystems |
| Observer | 60% | ECS system updates | Systems react to entity/component changes |
| Resource Acquisition Is Initialization (RAII) | 85% | Throughout Vulkan wrappers | Automatic resource cleanup via destructors |

## Entry Points
- Main application: [`src/main.cpp`](./src/main.cpp) - Contains the `VulkanEngine` class and `main()` function

## Public API
| Symbol | Type | Location |
|--------|------|----------|
| VulkanEngine | Class | `src/main.cpp` |
| World | Class | `src/ecs/ecs.h` |
| Entity | Class | `src/ecs/ecs.h` |
| System | Class | `src/ecs/ecs.h` |
| RenderSystem | Class | `src/ecs/systems.h` |
| Scene | Class | `src/scene/scene.h` |
| ImGuiManager | Class | `src/imgui/imgui_manager.h` |
| UIManager | Class | `src/ui/ui_manager.h` |

## Internal System Boundaries
The engine maintains clear boundaries between:
- **Rendering Infrastructure** (Vulkan layer) and **Game Logic** (ECS layer) - Communication happens through component data
- **Scene Management** and **Rendering Systems** - Scene aggregates renderables from both legacy and ECS sources
- **User Interface** and **Engine Core** - UI operates on a separate render pass and communicates via shared ECS world

## External Service Dependencies
- **Vulkan SDK**: Graphics API interface - No authentication required, platform-dependent installation
- **GLFW**: Windowing and input - Open-source, no external services
- **GLM**: Mathematics - Header-only library, no external dependencies
- **stb_image**: Texture loading - Single-header library, no external services
- **ImGui**: Immediate mode GUI - No external services required

## Key Decisions & Trade-offs
1. **Hybrid Rendering Approach**: Chose to maintain legacy renderables while implementing ECS to allow gradual migration rather than risky complete rewrite
2. **Offscreen Rendering**: Render to texture then present to swapchain enables UI overlay without complex Vulkan state management
3. **ECS Design**: Used type_index-based component storage for simplicity over more complex bitset approaches
4. **Validation Layers**: Kept validation layers enabled in debug builds for Vulkan error catching despite performance cost

## Risks & Constraints
- **Vulkan Verbosity**: Significant boilerplate code required for basic operations
- **Memory Management**: Manual Vulkan memory management introduces risk of leaks if not carefully handled
- **Platform Dependency**: While Vulkan is cross-platform, GLFW windowing must be tested on target platforms
- **Performance**: Current implementation renders entire scene each frame without frustum culling or LOD

## Top Directories Snapshot
- `src/` - ~20 files - Core engine source
- `deps/` - ~150 files - Third-party dependencies (GLFW, GLM, stb_image, etc.)
- `shaders/` - 2 files - Vertex and fragment shaders
- `.context/` - Generated documentation and agent playbooks

## Related Resources
- [Project Overview](./project-overview.md) - High-level project description and technology stack
- [Data Flow](./data-flow.md) - Information flow through engine systems
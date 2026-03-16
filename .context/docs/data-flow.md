# VulkanEngineV2 Data Flow

## Data Flow & Integrations
Data in VulkanEngineV2 flows primarily through the rendering pipeline and ECS system. The main data flow begins with user input and system updates, proceeds through ECS entity processing, moves to rendering commands, and ends with frame presentation. External integrations are limited to windowing (GLFW), graphics API (Vulkan), and UI (ImGui), all of which operate through well-defined interfaces without complex data exchange protocols.

## Module Dependencies
- **src/main.cpp** → Depends on: 
  - `src/vulkan/*` (Vulkan initialization and rendering)
  - `src/ecs/*` (ECS world and systems)
  - `src/scene/*` (Scene management)
  - `src/imgui/*` and `src/ui/*` (User interface)
  - `shaders/*` (GLSL shaders)
  
- **src/ecs/ecs.h** → Depends on:
  - `src/ecs/components.h` (Component definitions)
  - Standard library (memory, vector, unordered_map, etc.)
  
- **src/ecs/systems.h/cpp** → Depends on:
  - `src/ecs/ecs.h` (ECS world)
  - `src/ecs/components.h` (Component access)
  - `src/vulkan/vulkan_structs.h` (Vulkan engine reference)
  
- **src/scene/scene.h/cpp** → Depends on:
  - `src/ecs/ecs.h` (ECS world access)
  - `src/ecs/components.h` (RenderableComponent)
  
- **src/vulkan/** → Internal dependencies between Vulkan wrapper files
  - Instance → Device → Swapchain → RenderPass → Pipeline → Framebuffers → CommandBuffers
  
- **src/imgui/imgui_manager.h/cpp** → Depends on:
  - Vulkan device and render pass
  - GLFW window
  
- **src/ui/ui_manager.h/cpp** → Depends on:
  - `src/ecs/ecs.h` (ECS world for entity inspection)

## Service Layer
VulkanEngineV2 doesn't have a traditional service layer but rather has manager classes that provide specific functionalities:
- **VulkanEngine** (`src/main.cpp`) - Main engine coordinator
- **World** (`src/ecs/ecs.h`) - ECS entity and system management
- **RenderSystem** (`src/ecs/systems.h`) - ECS-based rendering (currently placeholder)
- **Scene** (`src/scene/scene.h`) - Hybrid rendering manager
- **ImGuiManager** (`src/imgui/imgui_manager.h`) - ImGui integration
- **UIManager** (`src/ui/ui_manager.h`) - Engine-specific UI

## High-level Flow
The primary pipeline from input to output follows this sequence:

```mermaid
graph TD
    A[Input Processing] --> B[ECS System Updates]
    B --> C[Scene Preparation]
    C --> D[Offscreen Rendering]
    D --> E[UI Rendering]
    E --> F[Frame Presentation]
    
    subgraph Input Processing
        A1[GLFW Event Polling] --> A2[Input Handling]
    end
    
    subgraph ECS System Updates
        B1[ECS World Update] --> B2[System::update() calls]
        B2 --> B3[Transform/System Logic]
    end
    
    subgraph Scene Preparation
        C1[Scene::render() call] --> C2[Legacy Renderable Processing]
        C2 --> C3[ECS Entity Processing]
        C3 --> C4[RenderableComponent Check]
    end
    
    subgraph Offscreen Rendering
        D1[Command Buffer Recording] --> D2[Offscreen Framebuffer Bind]
        D2 --> D3[Render Pass Begin]
        D3 --> D4[Scene Render Commands]
        D4 --> D5[Render Pass End]
    end
    
    subgraph UI Rendering
        E1[ImGui New Frame] --> E2[UI Logic/Rendering]
        E2 --> E3[ImGui Render Draw Data]
        E3 --> E4[ImGui Vulkan Render Commands]
    end
    
    subgraph Frame Presentation
        F1[Swapchain Framebuffer Bind] --> F2[Render Pass Begin]
        F2 --> F3[Offscreen Texture Sample]
        F3 --> F4[UI Overlay Commands]
        F4 --> F5[Render Pass End]
        F5 --> F6[Queue Submit]
        F6 --> F7[Present to Screen]
    end
```

### Detailed Flow Description:
1. **Input Processing**: GLFW polls events and forwards them to the engine
2. **ECS System Updates**: ECS world updates all registered systems (currently RenderSystem is a placeholder)
3. **Scene Preparation**: Scene manager prepares both legacy renderables and ECS entities for rendering
4. **Offscreen Rendering**: 
   - Command buffer begins recording
   - Offscreen framebuffer is bound
   - Render pass begins with clear color
   - Scene renders to offscreen framebuffer (both legacy and ECS entities)
   - Render pass ends
5. **UI Rendering**:
   - ImGui begins new frame
   - UI logic runs and generates draw data
   - ImGui renders to command buffer using offscreen texture as input
6. **Frame Presentation**:
   - Command buffer continues recording
   - Swapchain framebuffer is bound
   - Render pass begins
   - Offscreen texture is sampled (as full-screen quad)
   - UI overlay is rendered
   - Render pass ends
   - Command buffer submitted to graphics queue
   - Present queue displays the final image

## Internal Movement
- **ECS Communication**: Entities communicate through component data systems; systems process entities with specific component combinations
- **Rendering Communication**: Scene aggregates renderables from both legacy vectors and ECS queries
- **UI Communication**: UI manager reads from ECS world to display entity information and accepts user commands to modify entities
- **Vulkan Communication**: Command buffers record rendering commands that execute asynchronously on the GPU

## External Integrations
- **GLFW**: 
  - Purpose: Window creation, input handling, OpenGL/Vulkan context creation
  - Payload: Window events (keyboard, mouse, resize, close)
  - Authentication: None (local library)
  - Retry Strategy: N/A (immediate failure handling)
  
- **Vulkan**:
  - Purpose: Graphics rendering and compute operations
  - Payload: Command buffers, shaders, textures, buffers
  - Authentication: None (local API)
  - Retry Strategy: Error codes returned; application handles recreation (swapchain) or aborts (critical errors)
  
- **ImGui**:
  - Purpose: Immediate mode GUI for debugging and configuration
  - Payload: Draw commands, text input, widget states
  - Authentication: None (local library)
  - Retry Strategy: N/A (immediate failure handling)

## Observability & Failure Modes
- **Validation Layers**: Vulkan validation layers enabled in debug builds catch API misuse and provide detailed error messages
- **Error Checking**: Vulkan function calls return VkResult codes that are checked and throw exceptions on failure
- **Device Lost Handling**: Not fully implemented; would require swapchain recreation and resource reinitialization
- **Out of Memory**: Vulkan returns VK_ERROR_OUT_OF_DEVICE_MEMORY; application would need to free resources or abort
- **Logging**: Standard error output via std::cerr for validation messages and exceptions
- **Metrics**: No built-in performance metrics; would require timer queries or external profiling tools
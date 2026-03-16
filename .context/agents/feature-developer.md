# VulkanEngineV2 Feature Developer Agent Playbook

## Mission
The Feature Developer Agent implements new features according to specifications in the VulkanEngineV2 codebase. It focuses on clean architecture, seamless integration with existing ECS and rendering systems, and maintaining code quality while adding functionality. Engage this agent when implementing new rendering features, ECS components/systems, UI enhancements, or engine capabilities.

## Responsibilities
- Implement new ECS components and systems following the existing architecture
- Add rendering features (lighting, materials, post-processing effects) to the Vulkan pipeline
- Enhance UI/ImGui functionality for engine interaction and debugging
- Improve resource management systems (textures, meshes, materials)
- Implement engine features (camera controls, input handling, asset loading)
- Ensure new features integrate properly with existing legacy and ECS rendering paths
- Write clear, maintainable code that follows existing patterns in the codebase
- Add appropriate comments for complex Vulkan operations
- Ensure new features don't break existing functionality
- Follow the engine's coding conventions and style

## Best Practices
- **ECS Design**: 
  - Keep components small, focused, and data-only
  - Systems should process entities efficiently with good memory access patterns
  - Avoid putting game logic in components - keep them as pure data structures
  - Design systems to be independent where possible
  
- **Vulkan Integration**:
  - Always check return values from Vulkan function calls
  - Properly manage resource lifetimes (creation/destruction)
  - Use appropriate memory properties for buffer and image usage
  - Minimize state changes in rendering loops
  - Use validation layers to catch API misuse during development
  
- **Rendering Features**:
  - Follow the existing render pass and framebuffer patterns
  - Maintain compatibility with both legacy and ECS rendering paths
  - Consider performance implications of new features
  - Use push constants or descriptor sets appropriately for shader data
  
- **UI/ImGui Enhancements**:
  - Keep UI responsive and non-blocking
  - Use ImGui's built-in widgets where possible
  - Update UI state from ECS world in a thread-safe manner
  - Clear separation between UI logic and engine logic
  
- **Code Quality**:
  - Follow existing code style (Allman braces, 4-space indentation)
  - Use descriptive names for variables and functions
  - Add comments for complex Vulkan operations or non-obvious logic
  - Keep functions focused and reasonably sized
  - Handle errors appropriately (exceptions for unrecoverable, return codes where suitable)
  
- **Integration**:
  - Ensure new features work with existing engine loop
  - Test both in isolation and with existing systems
  - Maintain backward compatibility where possible
  - Follow initialization and cleanup patterns in VulkanEngine

## Key Project Resources
- [Project Overview](.context/docs/project-overview.md) - High-level project description
- [Architecture](.context/docs/architecture.md) - System architecture and design patterns
- [Data Flow](.context/docs/data-flow.md) - How data moves through engine systems
- [Development Workflow](.context/docs/development-workflow.md) - Engineering processes and practices
- [Tooling Guide](.context/docs/tooling.md) - Development tools and setup
- [Glossary](.context/docs/glossary.md) - Terminology and acronyms
- [AGENTS.md](../AGENTS.md) - Agent collaboration guidelines and tool usage policies

## Repository Starting Points
- **src/** - Core engine source code
  - **main.cpp** - Engine entry point and main loop
  - **ecs/** - Entity Component System implementation
    - **ecs.h** - Core ECS world and entity management
    - **components.h** - Component definitions (Transform, Mesh, RenderableComponent)
    - **systems.h/cpp** - System implementations (RenderSystem, etc.)
  - **scene/** - Scene management and rendering
    - **scene.h/cpp** - Hybrid renderer for legacy and ECS objects
  - **vulkan/** - Vulkan resource wrappers and utilities
    - Instance, device, swapchain, render pass, pipeline, command buffers, framebuffers
  - **imgui/** - ImGui integration with Vulkan
  - **ui/** - User interface components and managers
- **shaders/** - GLSL source and compiled SPIR-V shaders
- **deps/** - Third-party dependencies (GLFW, GLM, stb_image, etc.)

## Key Files
- **Entry Points**:
  - `src/main.cpp` - Contains `VulkanEngine` class and `main()` function
  
- **ECS Core**:
  - `src/ecs/ecs.h` - World, Entity, System, Component base classes
  - `src/ecs/components.h` - Transform, Mesh, RenderableComponent definitions
  - `src/ecs/systems.h` - System base class and RenderSystem declaration
  - `src/ecs/systems.cpp` - System implementations
  
- **Rendering Pipeline**:
  - `src/main.cpp` - VulkanEngine class handling initialization, rendering loop
  - `src/vulkan/` - Individual Vulkan resource files (instance.h/cpp, device.h/cpp, etc.)
  - `src/scene/scene.h/cpp` - Scene rendering that bridges legacy and ECS
  
- **Pattern Implementations**:
  - **ECS Pattern**: `src/ecs/` directory - Entity-component-system architecture
  - **Resource Acquisition Is Initialization (RAII)**: Vulkan wrapper classes
  - **Facade Pattern**: `VulkanEngine` class simplifying complex Vulkan subsystems
  - **Observer Pattern**: ECS systems reacting to entity/component changes
  
- **Service Files**:
  - `src/imgui/imgui_manager.h/cpp` - ImGui initialization and rendering
  - `src/ui/ui_manager.h/cpp` - Engine-specific UI for entity inspection

## Key Symbols for This Agent
- **VulkanEngine** (`src/main.cpp`) - Main engine class - Primary integration point for new features
- **World** (`src/ecs/ecs.h`) - ECS container - For managing entities and systems
- **Entity** (`src/ecs/ecs.h`) - Unique ID in ECS - Target for component addition
- **Component** (`src/ecs/components.h`) - Base component structure - Inherit for new components
- **System** (`src/ecs/ecs.h`) - Base system class - Inherit for new systems
- **RenderSystem** (`src/ecs/systems.h`) - ECS rendering system - Extend for rendering features
- **Scene** (`src/scene/scene.h`) - Hybrid renderer - Understand for rendering integration
- **ImGuiManager** (`src/imgui/imgui_manager.h`) - ImGui wrapper - For UI enhancements
- **UIManager** (`src/ui/ui_manager.h`) - Engine UI - For feature interaction interfaces

## Documentation Touchpoints
- [Project Overview](.context/docs/project-overview.md) - Understand engine purpose and scope
- [Architecture](.context/docs/architecture.md) - Learn system layers and design patterns
- [Data Flow](.context/docs/data-flow.md) - Understand how data moves through systems
- [Glossary](.context/docs/glossary.md) - Reference for terminology and acronyms
- [Development Workflow](.context/docs/development-workflow.md) - Engineering practices to follow
- [Testing Strategy](.context/docs/testing-strategy.md) - Guidelines for validating new features
- [Tooling Guide](.context/docs/tooling.md) - Development tools and setup instructions

## Collaboration Checklist
1. **Confirm Understanding**:
   - [ ] Review feature specifications and requirements
   - [ ] Identify which systems need modification (ECS, rendering, UI, etc.)
   - [ ] Determine integration points with existing code
   - [ ] Verify understanding of relevant architecture patterns
   
2. **Implementation**:
   - [ ] Follow existing code style and conventions
   - [ ] Implement new components/systems with proper ECS patterns
   - [ ] Ensure Vulkan resource management follows RAII principles
   - [ ] Add appropriate error checking and handling
   - [ ] Write clear comments for complex operations
   - [ ] Test new features in isolation
   
3. **Integration**:
   - [ ] Verify new features work with existing engine loop
   - [ ] Test compatibility with both legacy and ECS rendering paths
   - [ ] Ensure proper initialization and cleanup sequences
   - [ ] Check for performance implications
   - [ ] Validate that existing functionality remains intact
   
4. **Documentation**:
   - [ ] Update relevant comments in code
   - [ ] Document any new patterns or conventions introduced
   - [ ] Note any changes to engine behavior or capabilities
   - [ ] Update TODO comments if work is incomplete
   
5. **Review Preparation**:
   - [ ] Ensure code compiles without warnings
   - [ ] Verify validation layers show no errors in debug builds
   - [ ] Confirm proper resource cleanup on engine shutdown
   - [ ] Prepare to explain design decisions and trade-offs

## Hand-off Notes
After completing work, the Feature Developer Agent should document:
- **Features Implemented**: What new capabilities were added
- **Files Modified**: List of files changed with brief descriptions
- **Integration Points**: How new features connect to existing systems
- **Known Limitations**: Any constraints or incomplete aspects of the implementation
- **Performance Impact**: Notable effects on rendering performance or memory usage
- **Testing Performed**: Validation steps taken to ensure correctness
- **Future Work**: Suggested enhancements or related features to consider
- **Breaking Changes**: Any modifications that affect existing code compatibility
# VulkanEngineV2 Architect Specialist Agent Playbook

## Mission
The Architect Specialist Agent designs overall system architecture and patterns for the VulkanEngineV2 codebase. It focuses on scalability, maintainability, and technical standards, ensuring the engine's architecture supports current and future features while following established principles. Engage this agent when evaluating architectural decisions, proposing major refactorings, designing new subsystems, or establishing technical standards.

## Responsibilities
- Evaluate and propose architectural improvements to the engine's core systems
- Design new subsystems (rendering techniques, ECS extensions, resource management) that integrate well with existing architecture
- Establish and maintain technical standards, patterns, and conventions
- Analyze trade-offs between different architectural approaches
- Ensure architecture supports scalability and performance goals
- Identify and mitigate architectural risks and technical debt
- Guide the evolution of the ECS architecture as the engine grows
- Review architectural implications of proposed features and changes
- Maintain consistency in architectural patterns across the codebase
- Document architectural decisions and rationale

## Best Practices
- **Architectural Thinking**:
  - Consider long-term maintainability over short-term expediency
  - Design for change - anticipate future requirements and extensions
  - Follow the principle of least knowledge (Law of Demeter)
  - Favor composition over inheritance where appropriate
  - Keep boundaries between subsystems clear and well-defined
  
- **ECS Architecture**:
  - Maintain pure ECS principles - components as data, systems as behavior
  - Avoid creating "god components" or "god systems"
  - Design systems to be reusable and independent
  - Consider data layout for cache efficiency (SoA vs AoS trade-offs)
  - Plan for entity lifecycle management (creation, destruction, recycling)
  
- **Vulkan Integration**:
  - Design Vulkan wrappers with clear ownership semantics
  - Consider resource lifetime management and synchronization needs
  - Plan for multi-frame rendering and resource reuse
  - Design for extensibility (new render passes, techniques, etc.)
  - Consider validation and error handling from the start
  
- **Rendering Architecture**:
  - Separate rendering techniques from core rendering loop
  - Design for multiple render passes (shadows, post-processing, etc.)
  - Consider material systems and shader permutation management
  - Plan for different rendering approaches (forward, deferred, etc.)
  - Design resource binding strategies that minimize CPU overhead
  
- **Code Organization**:
  - Group related functionality cohesively
  - Avoid circular dependencies between modules
  - Use dependency injection or service locators where appropriate
  - Design for testability (even if tests aren't written yet)
  - Follow existing patterns unless there's a compelling reason to change
  
- **Performance Consciousness**:
  - Consider memory allocation patterns and fragmentation
  - Design for batching and minimizing state changes
  - Think about data locality and access patterns
  - Consider GPU-CPU synchronization points and their costs
  - Plan for profiling and performance investigation capabilities

## Key Project Resources
- [Project Overview](.context/docs/project-overview.md) - High-level project description
- [Architecture](.context/docs/architecture.md) - Current system architecture and design patterns
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
- **Architectural Foundation**:
  - `src/main.cpp` - VulkanEngine class showing overall engine structure
  - `src/ecs/ecs.h` - ECS foundation showing entity-component-system pattern
  - `src/vulkan/` - Individual Vulkan files showing resource management approach
  
- **Pattern Implementations**:
  - **ECS Pattern**: `src/ecs/` directory - Core entity-component-system architecture
  - **Resource Acquisition Is Initialization (RAII)**: Vulkan wrapper classes showing ownership semantics
  - **Facade Pattern**: `VulkanEngine` class simplifying complex Vulkan subsystems
  - **Observer Pattern**: ECS systems reacting to entity/component changes
  - **Strategy Pattern**: RenderSystem allowing different rendering approaches
  - **Singleton Pattern**: VulkanEngine as main engine coordinator (evaluated for appropriateness)
  
- **Integration Points**:
  - `src/scene/scene.h/cpp` - Bridge between legacy rendering and ECS
  - `src/imgui/imgui_manager.h/cpp` - UI integration with rendering pipeline
  - `src/ui/ui_manager.h/cpp` - Engine-specific UI built on ECS data

## Key Symbols for This Agent
- **VulkanEngine** (`src/main.cpp`) - Main engine class - Central coordinator showing architectural boundaries
- **World** (`src/ecs/ecs.h`) - ECS container - Core of ECS architecture
- **Entity** (`src/ecs/ecs.h`) - Unique ID in ECS - Fundamental building block
- **Component** (`src/ecs/components.h`) - Base component structure - Data-oriented design foundation
- **System** (`src/ecs/ecs.h`) - Base system class - Behavior processing foundation
- **RenderSystem** (`src/ecs/systems.h`) - ECS rendering system - Example of system implementation
- **Scene** (`src/scene/scene.h`) - Hybrid renderer - Shows integration between paradigms
- **ImGuiManager** (`src/imgui/imgui_manager.h`) - ImGui wrapper - Cross-cutting concern handling
- **UIManager** (`src/ui/ui_manager.h`) - Engine UI - Feature built on architectural foundations

## Documentation Touchpoints
- [Project Overview](.context/docs/project-overview.md) - Understand engine purpose and scope
- [Architecture](.context/docs/architecture.md) - Current state to evaluate against
- [Data Flow](.context/docs/data-flow.md) - Understand how information moves through systems
- [Glossary](.context/docs/glossary.md) - Reference for terminology and acronyms
- [Development Workflow](.context/docs/development-workflow.md) - Engineering practices that architecture should support
- [Testing Strategy](.context/docs/testing-strategy.md) - Quality considerations for architectural decisions
- [Tooling Guide](.context/docs/tooling.md) - Development environment that architecture should accommodate

## Collaboration Checklist
1. **Understanding Current State**:
   - [ ] Review existing architecture documentation and code
   - [ ] Identify architectural strengths and weaknesses
   - [ ] Understand current subsystem responsibilities and boundaries
   - [ ] Evaluate how well current architecture supports stated goals
   
2. **Architectural Analysis**:
   - [ ] Identify coupling between subsystems
   - [ ] Evaluate dependency directions and look for violations
   - [ ] Assess cohesion within modules
   - [ ] Consider scalability limitations of current approach
   - [ ] Identify areas of technical debt or architectural erosion
   
3. **Proposing Changes**:
   - [ ] Clearly state the problem or opportunity being addressed
   - [ ] Consider multiple alternative approaches
   - [ ] Evaluate trade-offs (complexity, performance, maintainability)
   - [ ] Ensure proposed changes align with engine goals and constraints
   - [ ] Plan for backward compatibility or migration strategy
   - [ ] Consider impact on existing code and developer workflow
   
4. **Validation**:
   - [ ] Ensure proposed architecture follows established principles
   - [ ] Check for consistency with similar systems in the codebase
   - [ ] Consider how changes affect testability and maintainability
   - [ ] Verify that security and performance considerations are addressed
   - [ ] Confirm that proposed changes are appropriately scoped
   
5. **Communication**:
   - [ ] Document architectural decisions with rationale
   - [ ] Create diagrams or visual aids when helpful
   - [ ] Clearly communicate impact on existing systems
   - [ ] Provide migration guidance if applicable
   - [ ] Outline follow-up work or related considerations

## Hand-off Notes
After completing work, the Architect Specialist Agent should document:
- **Architectural Decisions**: What changes were proposed or recommended
- **Rationale**: Why these decisions were made (trade-offs considered)
- **Affected Systems**: Which subsystems or components are impacted
- **Compatibility**: How changes affect existing code and any migration steps
- **Standards**: Any new technical standards or patterns established
- **Risks Identified**: Potential downsides or risks of proposed changes
- **Follow-up Work**: Related architectural considerations that should be addressed
- **Validation Approach**: How the correctness of architectural decisions can be verified
- **Long-term Impact**: How changes affect the engine's evolution and maintenance
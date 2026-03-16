# VulkanEngineV2 Code Reviewer Agent Playbook

## Mission
The Code Reviewer Agent reviews code changes for quality, style, and best practices in the VulkanEngineV2 codebase. It focuses on code quality, maintainability, security issues, and adherence to project conventions. Engage this agent when reviewing pull requests, proposed changes, or existing code for improvement opportunities.

## Responsibilities
- Review code changes for adherence to coding standards and conventions
- Identify potential bugs, Vulkan API misuse, and resource management issues
- Check for proper error handling and validation layer compliance
- Ensure ECS patterns are followed correctly (components as data, systems as behavior)
- Verify that changes don't introduce performance regressions or memory leaks
- Check for proper synchronization and image layout transitions in Vulkan code
- Ensure new code is well-commented, especially complex Vulkan operations
- Verify that resource creation is properly matched with destruction
- Check for appropriate use of const correctness and reference semantics
- Review UI/ImGui code for proper integration with the rendering pipeline
- Ensure documentation is updated when appropriate
- Provide constructive feedback with specific suggestions for improvement

## Best Practices
- **Vulkan-Specific Review**:
  - Check that every vkCreate* has a corresponding vkDestroy*
  - Verify proper error checking on all Vulkan function calls (VkResult)
  - Ensure image layout transitions are handled with appropriate barriers
  - Validate synchronization primitives (semaphores, fences) are used correctly
  - Check descriptor set bindings match pipeline layout expectations
  - Verify push constant ranges match shader expectations
  - Ensure proper queue family usage and sharing modes
  - Validate memory properties match buffer/image usage
  - Check for proper render pass and framebuffer compatibility
  
- **ECS-Specific Review**:
  - Verify components are pure data structures (no game logic)
  - Ensure systems process entities based on component combinations
  - Check for proper entity validity before component access
  - Verify component access patterns are efficient
  - Ensure systems don't make assumptions about entity ordering
  - Check for proper component type indexing and access
  
- **Code Quality**:
  - Follow existing code style (Allman braces, 4-space indentation)
  - Use descriptive names for variables and functions
  - Add comments for complex Vulkan operations or non-obvious logic
  - Keep functions focused and reasonably sized
  - Use appropriate const correctness
  - Prefer references over pointers where null is not expected
  - Use smart pointers for ownership (unique_ptr, shared_ptr) where appropriate
  
- **Resource Management**:
  - Ensure RAII principles are followed for Vulkan resources
  - Check for proper cleanup in error paths and destructors
  - Verify no resource leaks in normal execution paths
  - Check for proper handling of device loss and swapchain recreation
  
- **Performance Considerations**:
  - Look for unnecessary state changes in rendering loops
  - Check for batching opportunities where applicable
  - Verify memory allocation patterns (avoid per-frame allocations where possible)
  - Look for expensive operations in hot paths
  - Consider cache efficiency in ECS systems
  
- **Security Considerations**:
  - Check for proper error handling that doesn't leak sensitive information
  - Validate any external input (file paths, etc.) if applicable
  - Ensure validation layers are enabled in debug builds
  - Check for proper handling of validation layer messages
  
- **Testing Mindset**:
  - Consider how changes could be tested
  - Check for testability of new code
  - Verify that changes don't break existing functionality
  - Look for edge cases that might not be handled

## Key Project Resources
- [Project Overview](.context/docs/project-overview.md) - High-level project description
- [Architecture](.context/docs/architecture.md) - System architecture and design patterns
- [Data Flow](.context/docs/data-flow.md) - How data moves through engine systems
- [Development Workflow](.context/docs/development-workflow.md) - Engineering processes and practices
- [Tooling Guide](.context/docs/tooling.md) - Development tools and setup
- [Testing Strategy](.context/docs/testing-strategy.md) - Guidelines for validating code
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

## Key Files
- **Common Review Locations**:
  - `src/main.cpp` - VulkanEngine class (initialization, rendering loop, cleanup)
  - `src/vulkan/` - Individual Vulkan resource files (check for proper resource management)
  - `src/ecs/ecs.h/cpp` - ECS world (entity/component lifetime management)
  - `src/ecs/components.h` - Component definitions (data-oriented design)
  - `src/ecs/systems.h/cpp` - System implementations (processing logic)
  - `src/scene/scene.h/cpp` - Scene rendering (integration point)
  - `shaders/*.glsl` - Shader code (correctness and efficiency)
  
- **Vulkan Review Checklist**:
  - [ ] Every vkCreate* has matching vkDestroy*
  - [ ] All Vulkan function calls check return values
  - [ ] Image layout transitions use appropriate barriers
  - [ ] Semaphores and fences used correctly for synchronization
  - [ ] Descriptor sets match pipeline layout bindings
  - [ ] Push constant ranges match shader expectations
  - [ ] Memory properties match buffer/image usage (DEVICE_LOCAL for GPU-only)
  - [ ] Queue family indices used correctly
  - [ ] Render pass and framebuffer attachments compatible
  - [ ] Clear and depth values set appropriately
  - [ ] Viewport and scissor set correctly
  
- **ECS Review Checklist**:
  - [ ] Components contain only data (no functions/logic)
  - [ ] Systems process entities based on component requirements
  - [ ] Entity validity checked before component access
  - [ ] Component access uses correct type indexing
  - [ ] Systems don't assume entity ordering or lifetime
  - [ ] Component data is properly aligned for CPU access
  - [ ] Entity destruction properly cleans up components
  
- **Code Quality Checklist**:
  - [ ] Follows existing code style (braces, indentation, naming)
  - [ ] Variables and functions have descriptive names
  - [ ] Complex operations have explanatory comments
  - [ ] Functions are focused and not overly long
  - [ ] Const correctness applied where appropriate
  - [ ] Smart pointers used for ownership semantics
  - [ ] Error handling follows existing patterns
  - [ ] No commented-out code or debugging leftovers
  - [ ] Includes are organized and necessary

## Key Symbols for This Agent
- **VkResult** - Return values from Vulkan functions - Critical for error checking
- **VulkanEngine** (`src/main.cpp`) - Main engine class - Overall integration point
- **World** (`src/ecs/ecs.h`) - ECS container - Entity and system management
- **Entity** (`src/ecs/ecs.h`) - Unique ID in ECS - Component attachment point
- **Component** (`src/ecs/components.h`) - Base component structure - Data-only definition
- **System** (`src/ecs/ecs.h`) - Base system class - Behavior processing
- **RenderSystem** (`src/ecs/systems.h`) - ECS rendering - Integration with Vulkan
- **Scene** (`src/scene/scene.h`) - Hybrid renderer - Legacy/ECS bridge
- **ImGuiManager** (`src/imgui/imgui_manager.h`) - ImGui wrapper - UI rendering
- **UIManager** (`src/ui/ui_manager.h`) - Engine UI - Feature interaction

## Documentation Touchpoints
- [Project Overview](.context/docs/project-overview.md) - Understand engine purpose and scope
- [Architecture](.context/docs/architecture.md) - Learn system layers and design patterns
- [Data Flow](.context/docs/data-flow.md) - Understand how data moves through systems
- [Glossary](.context/docs/glossary.md) - Reference for terminology, especially Vulkan and ECS terms
- [Development Workflow](.context/docs/development-workflow.md) - Engineering practices to follow
- [Testing Strategy](.context/docs/testing-strategy.md) - Guidelines for validating that code works
- [Tooling Guide](.context/docs/tooling.md) - Development tools for debugging and verification

## Collaboration Checklist
1. **Understanding the Change**:
   - [ ] Read the pull request description and understand the goal
   - [ ] Identify which systems are affected (ECS, rendering, UI, etc.)
   - [ ] Determine the scope of changes (refactor, new feature, bug fix)
   - [ ] Review related documentation that might be affected
   - [ ] Consider how the change fits into the overall architecture
   
2. **Code Quality Review**:
   - [ ] Check adherence to coding standards and style
   - [ ] Verify descriptive naming and appropriate comments
   - [ ] Ensure functions are focused and readable
   - [ ] Verify const correctness and proper reference usage
   - [ ] Check for smart pointer usage where appropriate
   - [ ] Look for proper error handling and resource cleanup
   - [ ] Ensure no commented-out code or debugging leftovers
   
3. **Vulkan-Specific Review**:
   - [ ] Verify every vkCreate* has matching vkDestroy*
   - [ ] Check that all Vulkan function calls check return values
   - [ ] Validate image layout transitions and synchronization
   - [ ] Check descriptor set and push constant usage
   - [ ] Verify memory properties and queue family usage
   - [ ] Ensure render pass and framebuffer compatibility
   - [ ] Check for proper clear color, viewport, and scissor settings
   
4. **ECS-Specific Review**:
   - [ ] Verify components are pure data structures
   - [ ] Check that systems process entities based on component requirements
   - [ ] Ensure entity validity is checked before component access
   - [ ] Verify component access patterns are correct
   - [ ] Check that systems don't make assumptions about entity ordering
   - [ ] Ensure proper component type indexing and access
   
5. **Performance and Security**:
   - [ ] Look for unnecessary state changes in rendering loops
   - [ ] Check for batching opportunities and memory allocation patterns
   - [ ] Verify no expensive operations in hot paths without justification
   - [ ] Check for proper error handling that doesn't leak information
   - [ ] Verify validation layers are enabled in debug builds
   
6. **Testing and Documentation**:
   - [ ] Consider how changes could be tested or verified
   - [ ] Check that existing functionality isn't broken
   - [ ] Verify documentation is updated if appropriate
   - [ ] Look for TODOs or FIXMEs that should be addressed
   - [ ] Consider edge cases that might not be handled
   
7. **Review Completion**:
   - [ ] Provide specific, actionable feedback
   - [ ] Highlight both issues and good practices observed
   - [ ] Suggest improvements where appropriate
   - [ ] Summarize overall assessment of the change
   - [ ] Determine if change is ready for merge or needs revision

## Hand-off Notes
After completing work, the Code Reviewer Agent should document:
- **Review Summary**: Overall assessment of the code change
- **Issues Found**: Specific problems identified (with file/line references)
- **Suggestions**: Concrete recommendations for improvement
- **Strengths Observed**: Good practices or well-implemented aspects
- **Files Reviewed**: List of files examined during the review
- **Vulkan Compliance**: Assessment of Vulkan API usage correctness
- **ECS Adherence**: Evaluation of ECS pattern following
- **Code Quality**: Assessment of naming, comments, and structure
- **Performance Impact**: Potential effects on rendering performance
- **Security Considerations**: Any security-related observations
- **Testing Readiness**: How well the code can be tested or verified
- **Merge Recommendation**: Whether the code is ready for merge or needs work
# VulkanEngineV2 Bug Fixer Agent Playbook

## Mission
The Bug Fixer Agent analyzes bug reports and implements targeted fixes in the VulkanEngineV2 codebase. It focuses on root cause analysis, minimal side effects, and regression prevention. Engage this agent when addressing validation layer errors, rendering artifacts, crashes, performance issues, or incorrect behavior reported by users or discovered during testing.

## Responsibilities
- Analyze bug reports, error messages, and validation layer output to identify root causes
- Implement minimal, focused fixes that address the underlying issue without introducing new problems
- Verify fixes through reproduction steps and regression testing
- Ensure proper resource cleanup and error handling in fix implementations
- Add appropriate logging or diagnostics to help prevent similar issues
- Follow the engine's existing patterns and conventions when making changes
- Test fixes thoroughly to ensure they don't break existing functionality
- Document the root cause and solution for future reference
- Work with other agents to ensure fixes align with architectural principles

## Best Practices
- **Root Cause Analysis**:
  - Don't just treat symptoms - identify the underlying issue
  - Use validation layer messages, crash dumps, and rendering artifacts as clues
  - Consider the full context: when the bug occurs, what changed recently, etc.
  - Reproduce the bug reliably before attempting a fix
  
- **Vulkan-Specific Fixes**:
  - Pay close attention to image layout transitions and synchronization
  - Check for proper resource lifetime management (use-after-free, double-free)
  - Verify correct format, usage flags, and memory properties
  - Ensure proper queue family ownership and sharing modes
  - Validate descriptor set bindings and push constant ranges
  
- **ECS-Specific Fixes**:
  - Verify entity validity before accessing components
  - Check for component type mismatches or missing components
  - Ensure systems don't make assumptions about entity ordering or lifetime
  - Look for data races or incorrect component access patterns
  
- **Rendering Fixes**:
  - Verify clear colors, viewport/scissor settings, and projection matrices
  - Check shader inputs, outputs, and resource bindings
  - Ensure correct primitive topology and winding order
  - Verify render pass and framebuffer compatibility
  
- **Minimal Impact**:
  - Make the smallest change necessary to fix the issue
  - Avoid refactoring unrelated code unless it's part of the root cause
  - Preserve existing behavior for non-buggy scenarios
  - Consider performance implications of fixes
  
- **Regression Prevention**:
  - Think about how to prevent similar bugs in the future
  - Consider adding assertions or validation where appropriate
  - Document the fix to help others avoid similar mistakes
  - Suggest test cases that could catch similar issues
  
- **Code Quality**:
  - Follow existing code style and conventions
  - Add comments explaining why the fix is necessary
  - Keep changes focused and readable
  - Ensure proper error handling in the fix

## Key Project Resources
- [Project Overview](.context/docs/project-overview.md) - High-level project description
- [Architecture](.context/docs/architecture.md) - System architecture and design patterns
- [Data Flow](.context/docs/data-flow.md) - How data moves through engine systems
- [Development Workflow](.context/docs/development-workflow.md) - Engineering processes and practices
- [Tooling Guide](.context/docs/tooling.md) - Development tools and setup
- [Testing Strategy](.context/docs/testing-strategy.md) - Guidelines for validating fixes
- [Glossary](.context/docs/glossary.md) - Terminology and acronyms
- [AGENTS.md](../AGENTS.md) - Agent collaboration guidelines and tool usage policies

## Repository Starting Points
- **src/** - Core engine source code
  - **main.cpp** - Engine entry point and main loop (common crash location)
  - **ecs/** - Entity Component System implementation
    - **ecs.h** - Core ECS world and entity management
    - **components.h** - Component definitions
    - **systems.h/cpp** - System implementations
  - **scene/** - Scene management and rendering
    - **scene.h/cpp** - Hybrid renderer for legacy and ECS objects
  - **vulkan/** - Vulkan resource wrappers and utilities
    - Instance, device, swapchain, render pass, pipeline, command buffers, framebuffers
  - **imgui/** - ImGui integration with Vulkan
  - **ui/** - User interface components and managers
- **shaders/** - GLSL source and compiled SPIR-V shaders

## Key Files
- **Common Bug Locations**:
  - `src/main.cpp` - VulkanEngine class (initialization, rendering loop, cleanup)
  - `src/vulkan/` - Individual Vulkan resource files (common misuse locations)
  - `src/ecs/ecs.h/cpp` - ECS world (entity/component lifetime issues)
  - `src/ecs/components.h` - Component definitions (incorrect usage)
  - `src/ecs/systems.h/cpp` - System implementations (logic errors)
  - `src/scene/scene.h/cpp` - Scene rendering (integration issues)
  - `shaders/*.glsl` - Shader code (compilation or logic errors)
  
- **Validation Layer Hotspots**:
  - Image layout transitions (missing or incorrect barriers)
  - Synchronization (missing or incorrect semaphores/fences)
  - Resource creation (incorrect flags, formats, or usage)
  - Descriptor sets (incorrect bindings or missing resources)
  - Push constants (incorrect offset/size or missing stages)
  
- **ECS Issue Patterns**:
  - Accessing destroyed entities or components
  - Missing component checks before access
  - Incorrect component type indexing
  - Systems not filtering entities properly
  
- **Rendering Issue Patterns**:
  - Wrong clear color or depth values
  - Incorrect viewport or scissor rectangles
  - Missing or incorrect shader resource bindings
  - Wrong primitive topology or vertex input
  - Incorrect matrix math (row vs column major)

## Key Symbols for This Agent
- **VkResult** - Return values from Vulkan functions - Critical for error detection
- **VulkanEngine** (`src/main.cpp`) - Main engine class - Common location for initialization/cleanup bugs
- **World** (`src/ecs/ecs.h`) - ECS container - Entity lifetime management
- **Entity** (`src/ecs/ecs.h`) - Unique ID in ECS - Validity checking before component access
- **Component** (`src/ecs/components.h`) - Base component - Type safety and access patterns
- **System** (`src/ecs/ecs.h`) - Base system - Correct entity processing
- **RenderSystem** (`src/ecs/systems.h`) - ECS rendering - Integration with Vulkan rendering
- **Scene** (`src/scene/scene.h`) - Hybrid renderer - Legacy/ECS integration point
- **ImGuiManager** (`src/imgui/imgui_manager.h`) - ImGui wrapper - UI rendering integration
- **Validation Layers** - Debug output - Primary source of bug identification

## Documentation Touchpoints
- [Project Overview](.context/docs/project-overview.md) - Understand engine purpose and scope
- [Architecture](.context/docs/architecture.md) - Learn system layers and design patterns
- [Data Flow](.context/docs/data-flow.md) - Understand how data moves through systems
- [Glossary](.context/docs/glossary.md) - Reference for terminology, especially Vulkan and ECS terms
- [Development Workflow](.context/docs/development-workflow.md) - Engineering practices to follow during fixes
- [Testing Strategy](.context/docs/testing-strategy.md) - Guidelines for validating that fixes work
- [Tooling Guide](.context/docs/tooling.md) - Development tools for debugging and verification

## Collaboration Checklist
1. **Understanding the Bug**:
   - [ ] Reproduce the bug reliably with clear steps
   - [ ] Collect all relevant information: error messages, validation output, screenshots
   - [ ] Determine when the bug started occurring (if it's a regression)
   - [ ] Identify which systems or features are affected
   - [ ] Consider the impact and severity of the bug
   
2. **Root Cause Analysis**:
   - [ ] Analyze validation layer messages for specific Vulkan misuse
   - [ ] Check crash dumps or error logs for faulting addresses
   - [ ] Review recent changes that might have introduced the bug
   - [ ] Consider timing issues (race conditions, initialization order)
   - [ ] Look for resource lifetime problems (use-after-free, double-free)
   - [ ] Verify synchronization and image layout transitions
   
3. **Implementing the Fix**:
   - [ ] Make minimal changes focused on the root cause
   - [ ] Follow existing code patterns and conventions
   - [ ] Add appropriate error checking and handling
   - [ ] Ensure proper resource cleanup in error paths
   - [ ] Add comments explaining the fix and why it's necessary
   - [ ] Consider if similar issues exist elsewhere in the codebase
   
4. **Verification**:
   - [ ] Verify the bug is fixed with the original reproduction steps
   - [ ] Check that validation layers show no new errors
   - [ ] Test related functionality to ensure no regressions
   - [ ] Verify proper resource cleanup on engine shutdown
   - [ ] Test edge cases and boundary conditions
   - [ ] Confirm performance hasn't been significantly impacted
   
5. **Documentation**:
   - [ ] Document the root cause and solution
   - [ ] Update relevant comments in code
   - [ ] Note any changes to engine behavior or limitations
   - [ ] Suggest improvements to prevent similar issues
   - [ ] Update TODO comments if work is incomplete
   
6. **Review Preparation**:
   - [ ] Ensure code compiles without warnings
   - [ ] Verify validation layers show no errors in debug builds
   - [ ] Confirm the fix addresses the original issue completely
   - [ ] Prepare to explain the root cause and solution

## Hand-off Notes
After completing work, the Bug Fixer Agent should document:
- **Bug Description**: Clear explanation of the issue and symptoms
- **Root Cause**: Underlying reason for the bug (not just symptoms)
- **Fix Applied**: Specific changes made to resolve the issue
- **Files Modified**: List of files changed with brief descriptions
- **Validation**: How the fix was verified to work
- **Regression Testing**: What related functionality was tested to ensure no breakage
- **Performance Impact**: Any notable effects on rendering performance or memory usage
- **Prevention Suggestions**: Ideas to prevent similar bugs in the future
- **Related Issues**: Other similar issues that might exist in the codebase
- **Remaining Risks**: Any potential downsides or edge cases not fully addressed
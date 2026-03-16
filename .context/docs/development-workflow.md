# VulkanEngineV2 Development Workflow

## Development Workflow
The development workflow for VulkanEngineV2 follows a standard iterative process for graphics engine development. Developers typically work on features or bug fixes in isolation, test locally, and submit changes for review. The workflow emphasizes careful consideration of Vulkan API usage, memory management, and rendering correctness.

## Branching & Releases
- **Branching Model**: Trunk-based development with short-lived feature branches
  - Main branch (`main` or `master`) contains stable, releasable code
  - Feature branches created from main for new features or bug fixes
  - Branches named descriptively (e.g., `feature/ecs-rendering`, `fix/swapchain-recreation`)
  - Pull requests used for code review before merging to main
- **Release Cadence**: No fixed schedule; releases tagged as milestones are reached
  - Tags follow semantic versioning (v1.0.0, v1.1.0, etc.)
  - Release notes document new features, breaking changes, and bug fixes
  - Pre-release versions available for testing (v1.2.0-alpha.1, etc.)

## Local Development
```bash
# Install dependencies (assuming Vulkan SDK is already installed)
# Dependencies are included in the deps/ directory

# Generate build files
cmake -S . -B build

# Build the project
cmake --build build --config Release  # For Windows/MSVC
# or
cmake --build build  # For Linux/Mac with Makefiles

# Run the engine
./build/VulkanEngine  # Executable name may vary by platform

# For debugging
cmake --build build --config Debug
./build/VulkanEngine-d  # Debug executable
```

### Development Tips:
1. **Validation Layers**: Keep enabled in debug builds to catch Vulkan API errors
2. **Shader Changes**: Modify GLSL files in `shaders/` directory, then recompile to SPIR-V using `glslc`
3. **Resource Management**: Watch for memory leaks in Vulkan resources (images, buffers, memory)
4. **Testing**: Test on multiple GPU vendors (NVIDIA, AMD, Intel) if possible due to Vulkan implementation differences

## Code Review Expectations
When submitting changes for review, ensure:
1. **Vulkan Correctness**: 
   - Proper error checking on all Vulkan function calls
   - Correct resource lifecycle management (creation/destruction)
   - Appropriate use of synchronization primitives (semaphores, fences)
   - Valid image layout transitions and access flags
2. **ECS Consistency**:
   - Component access follows ECS patterns
   - Systems don't make assumptions about entity lifetime
   - Data-oriented design principles followed
3. **Code Quality**:
   - Clear, descriptive variable and function names
   - Appropriate comments for complex Vulkan operations
   - Consistent formatting (follow existing style in file)
   - No commented-out code or debugging leftovers
4. **Performance Considerations**:
   - Minimize state changes in rendering loops
   - Batch similar operations where possible
   - Consider memory allocation patterns (prefer pooling for frequent allocations)
5. **Documentation**:
   - Update relevant documentation for API changes
   - Add comments to complex sections
   - Ensure README and other docs reflect current state

Refer to `AGENTS.md` for specific agent collaboration guidelines and tool usage policies.

## Onboarding Tasks
New contributors should:
1. **Explore the Codebase**: Start with `src/main.cpp` to understand the engine flow
2. **Run the Engine**: Build and run to see the current state
3. **Make a Small Change**: Try modifying the clear color or adding a simple ImGui widget
4. **Review Existing Issues**: Look for "good first issue" labeled tasks
5. **Ask Questions**: Don't hesitate to ask for clarification on Vulkan or ECS patterns

### First Issues to Consider:
- Improve error handling in Vulkan initialization
- Add FPS counter to ImGui overlay
- Implement basic camera controls
- Add support for windowed mode (currently starts maximized)
- Create a simple triangle rendering example in ECS
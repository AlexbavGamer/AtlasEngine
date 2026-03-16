# VulkanEngineV2 Tooling & Productivity Guide

## Tooling & Productivity Guide
This document outlines the tools, configurations, and productivity practices used in the VulkanEngineV2 project to maintain code quality and developer efficiency.

## Required Tooling
- **C++ Compiler**: 
  - Windows: MSVC 2019 or later (via Visual Studio Build Tools)
  - Linux: GCC 9+ or Clang 10+
  - macOS: Clang 12+ (via Xcode Command Line Tools)
  - Minimum C++17 support required
  
- **CMake**: Version 3.16 or higher for project configuration
  - Used to generate build files for the target platform
  - Download from https://cmake.org/download/
  
- **Vulkan SDK**: 
  - Required for Vulkan headers, libraries, and validation layers
  - Version 1.2.170 or newer recommended
  - Download from https://vulkan.lunarg.com/
  
- **GLSL Compiler**: 
  - `glslc` (from Vulkan SDK) for compiling GLSL to SPIR-V
  - Used implicitly through CMake custom commands
  
- **Git**: Version control system
  - Used for source code management
  - LFS not currently required as no large binary assets in repo

## Recommended Automation
- **Build Automation**:
  - CMake handles dependency tracking and rebuilds
  - Incremental builds supported via standard make/ninja/jom
  
- **Shader Compilation**:
  - Custom CMake commands automatically compile GLSL to SPIR-V during build
  - Shaders in `shaders/` directory (.vert, .frag) compiled to (.vert.spv, .frag.spv)
  
- **Validation Layers**:
  - Automatically enabled in debug builds via CMake configuration
  - Provides real-time Vulkan API validation
  
- **Code Formatting**:
  - No automated formatter currently configured
  - Follow existing style in each file (generally: Allman braces, 4-space indentation)
  
- **Static Analysis**:
  - Can be integrated via clang-tidy or cppcheck (manual setup required)
  - Recommended for catching potential issues early

## IDE / Editor Setup
### Visual Studio (Windows)
- **Workload**: Desktop development with C++
- **Components**: MSVC v142, Windows 10 SDK, C++ CMake tools
- **Extensions**:
  - Visual Assist (optional, for enhanced productivity)
  - RenderDoc integration (for graphics debugging)
  
### Visual Studio Code (Cross-platform)
- **Extensions**:
  - C/C++ by Microsoft (IntelliSense, debugging)
  - CMake Tools (for CMake integration)
  - Shader languages support (for GLSL syntax highlighting)
  - GitLens (enhanced Git capabilities)
  
### CLion (Cross-platform)
- **Built-in**: Excellent CMake support
- **Plugins**: 
  - Shader support
  - Debugger for Vulkan applications

### General Editor Settings
- **Indentation**: 4 spaces (not tabs)
- **Line Endings**: LF (Unix-style) preferred, but CRLF (Windows) accepted
- **Character Encoding**: UTF-8 without BOM
- **Max Line Length**: 120 characters (flexible for complex expressions)

## Productivity Tips
### Vulkan Development
1. **Validation Layers**: Always keep them enabled during development - they catch 90% of common Vulkan mistakes
2. **RenderDoc**: Use for frame capture and analysis - invaluable for debugging rendering issues
3. **Nsight Graphics**: NVIDIA-specific tool for detailed GPU profiling and debugging
4. **AMD Radeon GPU Profiler**: For AMD GPU analysis
5. **Intel Graphics Performance Analyzers**: For Intel GPU analysis

### ECS Development
1. **Data-Oriented Design**: Think in terms of data arrays and processing loops, not individual objects
2. **Component Design**: Keep components small and focused on a single aspect of data
3. **System Design**: Systems should process entities efficiently with good memory access patterns
4. **Avoid Game Object Anti-Patterns**: Don't try to recreate traditional OOP game objects with ECS

### Debugging Techniques
1. **Start Simple**: Begin with a clear color render, then add geometry, then textures, etc.
2. **Use ImGui**: Add debug controls to tweak parameters in real-time
3. **Check Return Values**: Every Vulkan call should be checked - most issues are caught this way
4. **Frame Capture**: Use RenderDoc to examine exactly what commands are being sent to the GPU
5. **Pipeline Validation**: Use `vkValidateProperties` or similar tools to check pipeline compatibility

### Performance Optimization
1. **Minimize State Changes**: Group similar rendering operations together
2. **Batch Draw Calls**: Use instancing or indirect rendering where possible
3. **Resource Pooling**: Reuse buffers and textures rather than constantly allocating/freeing
4. **Descriptor Set Layouts**: Design them to minimize changes between draw calls
5. **Memory Management**: Use appropriate memory properties (DEVICE_LOCAL for GPU-only data)

## Useful Scripts and Commands
### Shader Compilation (Manual)
```bash
# Compile vertex shader
glslc shaders/vert.glsl -o shaders/vert.spv

# Compile fragment shader
glslc shaders/frag.glsl -o shaders/frag.spv
```

### Validation Layer Info
```bash
# List available validation layers
vulkaninfo | findstr "VK_LAYER_KHRONOS_validation"

# Check Vulkan version
vulkaninfo | findstr "Vulkan Instance Version"
```

### Project Clean
```bash
# Remove build directory to start fresh
rm -rf build
# or on Windows
rmdir /s /q build
```

### Quick Rebuild
```bash
# Regenerate build files and compile
cmake -S . -B build
cmake --build build
```

## Documentation Generation
The project uses AI-generated documentation in the `.context/` directory:
- **Documentation**: `.context/docs/` - Markdown guides and overviews
- **Agent Playbooks**: `.context/agents/` - Specialized agent instructions
- **Semantic Context**: Generated from codebase analysis

To regenerate documentation:
1. Ensure context tools are available
2. Run documentation generation scripts (if/when implemented)
3. Review and commit changes as needed

## Continuous Integration (Future)
When CI is implemented, consider:
1. **Build Matrix**: Test on multiple compilers (MSVC, GCC, Clang)
2. **Validation Layer Checks**: Ensure no validation errors in debug builds
3. **Basic Rendering Test**: Verify engine starts, renders a frame, and exits cleanly
4. **Shader Compilation**: Verify all shaders compile to valid SPIR-V
5. **Code Analysis**: Run static analysis tools on pull requests

## Community and Learning Resources
- **Vulkan Tutorial**: https://vulkan-tutorial.com/ (excellent beginner to intermediate guide)
- **Vulkan Documentation**: https://www.khronos.org/registry/vulkan/specs/1.2-extensions/html/vkspec.html
- **GPU Gems**: https://developer.nvidia.com/gpugems/GPUGems/gpugems_pref01.html (classic rendering techniques)
- **Real-Time Rendering**: https://www.realtimerendering.com/ (blog and book series)
- **ECS Resources**: 
  - "Entity Component Systems" by Adam Martin
  - "Game Programming Patterns" by Robert Nystrom (chapter on ECS)
  - EnTT library documentation (https://github.com/skypjack/entt) for ECS patterns
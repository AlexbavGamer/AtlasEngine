# Shader Information

## Shader Files Location
- Compiled SPIR-V shaders: `./build/shaders/`
  - `vert.spv` - Vertex shader
  - `frag.spv` - Fragment shader
- Source GLSL shaders: `./build/shaders_src/`
  - `vert.glsl` - Vertex shader source
  - `frag.glsl` - Fragment shader source

## Shader Usage in Code
In `src/main.cpp`, the `createGraphicsPipeline()` method:
1. Reads compiled SPIR-V shaders from `shaders/vert.spv` and `shaders/frag.spv`
2. Creates shader modules from the SPIR-V code
3. Sets up pipeline shader stages with entry point "main"
4. Destroys shader modules after pipeline creation

## Shader Content (from source files)
Based on the directory structure, the GLSL source files are located in `./build/shaders_src/`:
- `vert.glsl` - Vertex shader source
- `frag.glsl` - Fragment shader source

These are compiled to SPIR-V and placed in `./build/shaders/` during the build process.

## Note on Shader Development
The engine currently uses basic pass-through shaders that likely:
- Vertex shader: Passes vertex positions through unchanged
- Fragment shader: Outputs a solid color

For enhanced rendering, these shaders would need to be updated to include:
- Model/View/Projection matrix transformations
- Lighting calculations
- Texture sampling
- Material properties
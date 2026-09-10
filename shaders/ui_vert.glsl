#version 450

// Custom UI vertex shader. The UI is rendered in screen space with the
// top-left origin, converted to NDC here (Vulkan has a flipped Y).
layout(push_constant) uniform PC {
    vec2 viewportSize;
} pc;

layout(location = 0) in vec2 inPos;
layout(location = 1) in vec2 inUV;
layout(location = 2) in vec4 inColor;
layout(location = 3) in uint inTexture;

layout(location = 0) out vec2 outUV;
layout(location = 1) out vec4 outColor;
layout(location = 2) out flat uint outTexture;

void main() {
    // Vulkan NDC has Y pointing down (y=-1 is the top of the framebuffer,
    // y=+1 is the bottom). Vertex positions are already in screen space with
    // a top-left origin (y grows downward), so map y directly without negation.
    vec2 ndc = vec2(
        (inPos.x / pc.viewportSize.x) * 2.0 - 1.0,
        (inPos.y / pc.viewportSize.y) * 2.0 - 1.0
    );
    gl_Position = vec4(ndc, 0.0, 1.0);
    outUV = inUV;
    outColor = inColor;
    outTexture = inTexture;
}
#version 450

// Depth-only vertex shader for the directional shadow pass.
// Push layout is model + viewProj (128 bytes, like the picking convention).
// NOTE: skinning is intentionally skipped here — skinned meshes cast shadows
// from their bind pose (v1 limitation, documented in ARCHITECTURE.md).
layout(push_constant) uniform PushConstants {
    mat4 model;
    mat4 viewProj;
} pc;

layout(location = 0) in vec3 inPosition;

void main() {
    gl_Position = pc.viewProj * pc.model * vec4(inPosition, 1.0);
}

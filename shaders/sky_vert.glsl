#version 450

// Fullscreen-triangle sky vertex shader (no vertex buffers: positions come
// from gl_VertexIndex). Push block MUST match Atlas::SkyPushConstants in
// src/renderer/renderer.h exactly (same order, same types, std430).
layout(push_constant) uniform SkyPC {
    mat4 invViewProj;
    vec4 sunDir;
    vec4 horizonColor;
    vec4 zenithColor;
    vec4 groundColor;
    vec4 sunColorSize;
    vec4 params;
} pc;

layout(location = 0) out vec2 uv;

void main() {
    vec2 p = vec2(float((gl_VertexIndex << 1) & 2), float(gl_VertexIndex & 2));
    uv = p;
    // z = 1 (far plane): passes LEQUAL against the cleared depth buffer,
    // depth writes stay off so opaques overdraw normally.
    gl_Position = vec4(p * 2.0 - 1.0, 1.0, 1.0);
}

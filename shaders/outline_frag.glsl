#version 450

layout(push_constant) uniform OutlinePC {
    mat4 model;
    mat4 view;
    mat4 proj;
    vec4 color;
    float width;
    // NOTE: three scalars (not vec3) so std430 layout matches the C++
    // OutlinePushConstants struct exactly (224 bytes, no hidden padding).
    float _pad0;
    float _pad1;
    float _pad2;
} pc;

layout(location = 0) out vec4 outColor;

void main() {
    outColor = pc.color;
}

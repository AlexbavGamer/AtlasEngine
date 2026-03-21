#version 450

layout(push_constant) uniform OutlinePC {
    mat4 model;
    mat4 view;
    mat4 proj;
    vec4 color;
    float width;
    vec3 _pad;
} pc;

layout(location = 0) out vec4 outColor;

void main() {
    outColor = pc.color;
}

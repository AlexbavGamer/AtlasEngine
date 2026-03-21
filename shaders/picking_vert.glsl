#version 450

layout(push_constant) uniform PickingPC {
    mat4 model;
    mat4 view;
    mat4 proj;
    uint entityIdPlusOne;
} pc;

layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec3 inColor;
layout(location = 2) in vec2 inTexCoord;
layout(location = 3) in vec3 inNormal;

void main() {
    gl_Position = pc.proj * pc.view * pc.model * vec4(inPosition, 1.0);
}

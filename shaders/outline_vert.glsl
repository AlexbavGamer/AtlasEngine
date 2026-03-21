#version 450

layout(push_constant) uniform OutlinePC {
    mat4 model;
    mat4 view;
    mat4 proj;
    vec4 color;
    float width;
    vec3 _pad;
} pc;

layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec3 inColor;
layout(location = 2) in vec2 inTexCoord;
layout(location = 3) in vec3 inNormal;

void main() {
    vec4 worldPos = pc.model * vec4(inPosition, 1.0);
    vec3 worldNormal = normalize(mat3(pc.model) * inNormal);
    worldPos.xyz += worldNormal * pc.width;

    gl_Position = pc.proj * pc.view * worldPos;
}

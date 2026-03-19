#version 450

layout(push_constant) uniform PushConstants {
    mat4 model;
    mat4 view;
    mat4 proj;
    vec4 baseColor;
    float metallic;
    float roughness;
    vec2 padding;
} pc;

layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec3 inColor;
layout(location = 2) in vec2 inTexCoord;
layout(location = 3) in vec3 inNormal;

layout(location = 0) out vec3 fragColor;
layout(location = 1) out vec2 fragTexCoord;
layout(location = 2) out vec3 fragNormal;
layout(location = 3) out vec3 fragWorldPos;

void main() {
    gl_Position = pc.proj * pc.view * pc.model * vec4(inPosition, 1.0);
    fragColor = pc.baseColor.rgb * inColor;
    fragTexCoord = inTexCoord;
    fragNormal = mat3(transpose(inverse(pc.model))) * inNormal;
    fragWorldPos = vec3(pc.model * vec4(inPosition, 1.0));
}

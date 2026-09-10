#version 450

// Push block MUST match Atlas::PushConstants in src/renderer/renderer.h
// exactly (same order, same types, std430 layout). The C++ side pins this
// with offsetof static_asserts — update both sides together.
layout(push_constant) uniform PushConstants {
    mat4 model;
    mat4 viewProj;
    vec4 baseColor;
    vec4 emissiveFactor;
    float metallic;
    float roughness;
    float alphaCutoff;
    int albedoTexIndex;
    int normalTexIndex;
    int metallicRoughnessTexIndex;
    int aoTexIndex;
    int emissiveTexIndex;
    int flags;
} pc;

// Bones palette (mat4 per bone). Bound as a dynamic SSBO (set=1).
layout(set = 1, binding = 0) readonly buffer BonesBuffer {
    mat4 bones[];
} gBones;

layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec3 inColor;
layout(location = 2) in vec2 inTexCoord;
layout(location = 3) in vec3 inNormal;
layout(location = 4) in uvec4 inJoints;
layout(location = 5) in vec4 inWeights;

layout(location = 0) out vec3 fragColor;
layout(location = 1) out vec2 fragTexCoord;
layout(location = 2) out vec3 fragNormal;
layout(location = 3) out vec3 fragWorldPos;

void main() {
    mat4 skin =
        inWeights.x * gBones.bones[inJoints.x] +
        inWeights.y * gBones.bones[inJoints.y] +
        inWeights.z * gBones.bones[inJoints.z] +
        inWeights.w * gBones.bones[inJoints.w];

    vec4 localPos = skin * vec4(inPosition, 1.0);
    vec4 worldPos = pc.model * localPos;

    gl_Position = pc.viewProj * worldPos;
    fragColor = pc.baseColor.rgb * inColor;
    fragTexCoord = inTexCoord;
    fragNormal = mat3(transpose(inverse(pc.model))) * (mat3(skin) * inNormal);
    fragWorldPos = vec3(worldPos);
}

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

layout(set = 1, binding = 0) readonly buffer BonesBuffer {
    mat4 bones[];
} gBones;

layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec3 inColor;
layout(location = 2) in vec2 inTexCoord;
layout(location = 3) in vec3 inNormal;
layout(location = 4) in uvec4 inJoints;
layout(location = 5) in vec4 inWeights;

void main() {
    mat4 skin =
        inWeights.x * gBones.bones[inJoints.x] +
        inWeights.y * gBones.bones[inJoints.y] +
        inWeights.z * gBones.bones[inJoints.z] +
        inWeights.w * gBones.bones[inJoints.w];

    vec4 worldPos = pc.model * (skin * vec4(inPosition, 1.0));
    vec3 worldNormal = normalize(mat3(pc.model) * (mat3(skin) * inNormal));
    worldPos.xyz += worldNormal * pc.width;

    gl_Position = pc.proj * pc.view * worldPos;
}

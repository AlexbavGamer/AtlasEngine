#version 450

layout(push_constant) uniform PickingPC {
    mat4 model;
    mat4 view;
    mat4 proj;
    uint entityIdPlusOne;
} pc;

layout(location = 0) out uint outId;

void main() {
    outId = pc.entityIdPlusOne;
}

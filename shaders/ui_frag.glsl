#version 450

// Custom UI fragment shader. Samples from a small texture array using the
// PER-VERTEX texture index (bindless), so each quad/mesh picks its own texture
// with no shared/global batch state to leak:
//   slot 0 = font atlas (white RGB, alpha mask)   -> outColor = inColor * tex.a
//   slot N = external image (viewport, thumbnails) -> outColor = inColor * tex
// Colors are premultiplied-alpha; blending is ONE / ONE_MINUS_SRC_ALPHA.
//
// Clipping (scissors) is done by the hardware via vkCmdSetScissor, NOT by
// discard in the shader — so a widget's clip rect can never leak into the next
// batch.
layout(binding = 0) uniform sampler2D uTextures[8];

layout(location = 0) in vec2 inUV;
layout(location = 1) in vec4 inColor;
layout(location = 2) in flat uint inTexture;

layout(location = 0) out vec4 outColor;

void main() {
    int idx = int(clamp(inTexture, 0u, 7u));
    vec4 tex = texture(uTextures[idx], inUV);
    if (idx == 0) {
        // Font atlas: white RGB, alpha is the coverage mask.
        outColor = inColor * tex.a;
    } else {
        // External image: tint premultiplied by the image color.
        outColor = inColor * tex;
    }
}
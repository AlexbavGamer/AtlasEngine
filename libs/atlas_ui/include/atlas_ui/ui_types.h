#pragma once

#include <array>
#include <cstdint>
#include <string>

#include <glm/glm.hpp>

namespace Atlas::UI {

// UV of the reserved opaque-white texel at the bottom-right corner of the font
// atlas. Solid-colour quads (backgrounds, fills, borders) sample this so the
// fragment shader sees alpha == 1 and outputs the premultiplied vertex colour
// unchanged. Mirrored in UIFont::load which writes the texel block.
constexpr float kSolidU = 1020.5f / 1024.0f;
constexpr float kSolidV = 1020.5f / 1024.0f;

// A single quad corner in the batched UI vertex buffer.
struct UIVertex {
    glm::vec2 pos;    // screen space (pixels, top-left origin)
    glm::vec2 uv;     // atlas uv
    glm::vec4 color;  // premultiplied-alpha tint
    uint32_t texture; // texture array index (0 = font atlas, 1+ = external)
};

// Metrics for a single glyph in the font atlas.
struct UIFontGlyph {
    glm::vec2 uv0;
    glm::vec2 uv1;
    glm::vec2 size;    // glyph pixel size
    glm::vec2 bearing; // offset from pen position
    float advance = 0.0f;
};

// Shared theming. Set in the constructor so aggregate member initializers don't
// trip static-analysis rules.
struct UIStyle {
    UIStyle()
        : background(0.10f, 0.10f, 0.12f, 0.92f)
        , panel(0.15f, 0.15f, 0.18f, 0.95f)
        , text(0.92f, 0.92f, 0.95f, 1.00f)
        , accent(0.25f, 0.55f, 0.95f, 1.00f)
        , accentHover(0.35f, 0.62f, 0.98f, 1.00f)
        , accentActive(0.18f, 0.42f, 0.80f, 1.00f) {}

    glm::vec4 background;
    glm::vec4 panel;
    glm::vec4 text;
    glm::vec4 accent;
    glm::vec4 accentHover;
    glm::vec4 accentActive;
    float rounding = 4.0f;
    float padding = 8.0f;
};

// Input state sampled each frame by the UI.
struct UIInput {
    bool leftDown = false;
    bool leftPressed = false;  // rising edge
    bool leftReleased = false; // falling edge
    bool rightDown = false;
    bool rightPressed = false;
    bool rightReleased = false;
    bool middleDown = false;
    bool middlePressed = false;
    float mouseX = 0.0f;
    float mouseY = 0.0f;
    float scrollY = 0.0f;

    // Keyboard, indexed by GLFW key code. keyDown = held; keyPressed = rising edge.
    std::array<bool, 512> keyDown{};
    std::array<bool, 512> keyPressed{};
    // Text entered this frame (UTF-8 codepoints).
    std::string textInput;
};

} // namespace Atlas::UI

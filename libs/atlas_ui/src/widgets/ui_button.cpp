#include "atlas_ui/ui_button.h"

#include "atlas_ui/ui_font.h"
#include "atlas_ui/ui_draw_list.h"
#include "atlas_ui/ui_types.h"

namespace Atlas::UI {

UIButton::UIButton(const std::string& label, Callback onClick)
    : m_Label(label), m_Callback(std::move(onClick)) {}

glm::vec2 UIButton::measure(const UIFont& font, const UIStyle& style) const {
    float textW = font.textWidth(m_Label.c_str());
    float h = font.lineHeight();
    return glm::vec2(textW + style.padding * 2.0f, h + style.padding);
}

void UIButton::draw(UIDrawList& drawList, const UIFont& font, const UIStyle& style) {
    // Determine colors based on state
    glm::vec4 bgColor = m_Pressed ? style.accentActive
                        : m_Hovered ? style.accentHover
                        : style.accent;

    // Draw rounded background
    drawList.addRoundedRect(min(), max(), style.rounding, bgColor);

    // Draw label centered
    float textW = font.textWidth(m_Label.c_str());
    float textH = font.lineHeight();
    glm::vec2 pos = min() + (size() - glm::vec2(textW, textH)) * 0.5f;
    pos.y += font.ascentPx(); // Align baseline
    font.drawString(drawList, m_Label, pos, 1.0f, style.text);
}

bool UIButton::handle(const UIInput& input) {
    bool wasHovered = m_Hovered;
    m_Hovered = hovered(input);

    if (m_Pressed) {
        if (input.leftReleased) {
            m_Pressed = false;
            if (m_Hovered && m_Callback) {
                m_Callback();
            }
            return true; // Consumed click
        }
        return true; // Consume while pressed
    }

    if (m_Hovered && input.leftPressed) {
        m_Pressed = true;
        return true; // Consumed press
    }

    return false;
}

} // namespace Atlas::UI
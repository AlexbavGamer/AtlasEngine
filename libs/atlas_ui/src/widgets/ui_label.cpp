#include "atlas_ui/ui_label.h"

#include "atlas_ui/ui_font.h"
#include "atlas_ui/ui_draw_list.h"
#include "atlas_ui/ui_types.h"

namespace Atlas::UI {

UILabel::UILabel(const std::string& text, const glm::vec4& color)
    : m_Text(text), m_Color(color) {}

glm::vec2 UILabel::measure(const UIFont& font, const UIStyle& style) const {
    float textW = font.textWidth(m_Text.c_str());
    float textH = font.lineHeight();
    return glm::vec2(textW, textH);
}

void UILabel::draw(UIDrawList& drawList, const UIFont& font, const UIStyle& style) {
    float textH = font.lineHeight();
    glm::vec2 pos = min();
    pos.y += font.ascentPx();
    font.drawString(drawList, m_Text, pos, 1.0f, m_Color);
}

} // namespace Atlas::UI
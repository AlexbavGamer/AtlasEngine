#include "atlas_ui/ui_separator.h"

#include "atlas_ui/ui_font.h"
#include "atlas_ui/ui_draw_list.h"
#include "atlas_ui/ui_types.h"

namespace Atlas::UI {

UISeparator::UISeparator(float thickness, const glm::vec4& color)
    : m_Thickness(thickness), m_Color(color) {}

glm::vec2 UISeparator::measure(const UIFont& font, const UIStyle& style) const {
    return glm::vec2(0.0f, m_Thickness + style.padding * 0.5f);
}

void UISeparator::draw(UIDrawList& drawList, const UIFont& font, const UIStyle& style) {
    float y = min().y + style.padding * 0.25f;
    drawList.addQuad(glm::vec2(min().x, y),
                     glm::vec2(max().x, y + m_Thickness),
                     m_Color);
}

} // namespace Atlas::UI
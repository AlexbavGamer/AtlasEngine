#pragma once

#include <glm/glm.hpp>

#include "atlas_ui/ui_widget.h"

namespace Atlas::UI {

class UIFont;
class UIDrawList;
struct UIStyle;
struct UIInput;

// Horizontal separator line.
class UISeparator : public UIWidget {
public:
    UISeparator(float thickness = 1.0f, const glm::vec4& color = glm::vec4(0.3f, 0.3f, 0.35f, 1.0f));
    ~UISeparator() override = default;

    glm::vec2 measure(const UIFont& font, const UIStyle& style) const override;
    void draw(UIDrawList& drawList, const UIFont& font, const UIStyle& style) override;

private:
    float m_Thickness;
    glm::vec4 m_Color;
};

} // namespace Atlas::UI
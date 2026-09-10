#pragma once

#include <string>

#include <glm/glm.hpp>

#include "atlas_ui/ui_widget.h"

namespace Atlas::UI {

class UIFont;
class UIDrawList;
struct UIStyle;
struct UIInput;

// Static text label.
class UILabel : public UIWidget {
public:
    UILabel(const std::string& text, const glm::vec4& color = glm::vec4(1.0f));
    ~UILabel() override = default;

    glm::vec2 measure(const UIFont& font, const UIStyle& style) const override;
    void draw(UIDrawList& drawList, const UIFont& font, const UIStyle& style) override;

    const std::string& text() const { return m_Text; }
    void setText(const std::string& text) { m_Text = text; }
    const glm::vec4& color() const { return m_Color; }
    void setColor(const glm::vec4& color) { m_Color = color; }

private:
    std::string m_Text;
    glm::vec4 m_Color;
};

} // namespace Atlas::UI
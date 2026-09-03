#pragma once

#include <glm/glm.hpp>

namespace Atlas::UI {

class UIFont;
class UIDrawList;
struct UIStyle;
struct UIInput;

// Base class for all custom UI widgets. Widgets are retained-mode objects: they
// hold their own state (position, size, value, pressed/hovered) and are drawn
// and hit-tested every frame by the UIContext. Every component (button, slider,
// text input, panel, ...) derives from this.
class UIWidget {
public:
    UIWidget() = default;
    virtual ~UIWidget() = default;

    UIWidget(const UIWidget&) = delete;
    UIWidget& operator=(const UIWidget&) = delete;

    // --- Layout -------------------------------------------------------------
    // Measure the widget's intrinsic size (content-dependent). Used by
    // containers to lay children out before calling setBounds.
    virtual glm::vec2 measure(const UIFont& font, const UIStyle& style) const = 0;

    // Position the widget in screen space.
    void setBounds(const glm::vec2& min, const glm::vec2& max) {
        m_Min = min;
        m_Max = max;
    }
    glm::vec2 min() const { return m_Min; }
    glm::vec2 max() const { return m_Max; }
    glm::vec2 size() const { return m_Max - m_Min; }

    // True when a point is inside the widget bounds.
    bool contains(const glm::vec2& p) const {
        return p.x >= m_Min.x && p.x <= m_Max.x && p.y >= m_Min.y && p.y <= m_Max.y;
    }

    // --- Visibility / margin ------------------------------------------------
    bool visible() const { return m_Visible; }
    void setVisible(bool v) { m_Visible = v; }
    void setMargin(const glm::vec2& margin) { m_Margin = margin; }
    const glm::vec2& margin() const { return m_Margin; }

    // --- Draw / input -------------------------------------------------------
    // Draw into the draw list (called only when visible).
    virtual void draw(UIDrawList& drawList, const UIFont& font, const UIStyle& style) = 0;

    // Handle input. Return true when the widget consumed the event (so it is
    // not processed by widgets drawn beneath it). Implementations use the
    // pointer-capture rules in UIContext (press inside -> capture until release).
    virtual bool handle(const UIInput& input) { return false; }

    // True while the pointer is hovering this widget (drag-relevant). Provided
    // for containers/windows that need to know their hover state.
    bool hovered(const UIInput& input) const {
        return contains(glm::vec2(input.mouseX, input.mouseY));
    }

protected:
    glm::vec2 m_Min{0.0f};
    glm::vec2 m_Max{0.0f};
    glm::vec2 m_Margin{0.0f};
    bool m_Visible = true;
};

} // namespace Atlas::UI

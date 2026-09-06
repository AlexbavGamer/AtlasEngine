#pragma once

#include <string>
#include <functional>

#include <glm/glm.hpp>

#include "atlas_ui/ui_widget.h"

namespace Atlas::UI {

class UIFont;
class UIDrawList;
struct UIStyle;
struct UIInput;

// Simple push button with label and callback.
class UIButton : public UIWidget {
public:
    using Callback = std::function<void()>;

    UIButton(const std::string& label, Callback onClick = nullptr);
    ~UIButton() override = default;

    // Measure intrinsic size (label + padding).
    glm::vec2 measure(const UIFont& font, const UIStyle& style) const override;

    // Draw the button (background, border, label).
    void draw(UIDrawList& drawList, const UIFont& font, const UIStyle& style) override;

    // Handle mouse input (press, release, hover).
    bool handle(const UIInput& input) override;

    // Accessors.
    const std::string& label() const { return m_Label; }
    void setLabel(const std::string& label) { m_Label = label; }
    void setCallback(Callback cb) { m_Callback = std::move(cb); }

private:
    std::string m_Label;
    Callback m_Callback;
    bool m_Hovered = false;
    bool m_Pressed = false;
};

} // namespace Atlas::UI
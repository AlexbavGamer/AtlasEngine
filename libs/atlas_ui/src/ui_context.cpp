#include "atlas_ui/ui_context.h"

#include <algorithm>
#include <cmath>

namespace Atlas::UI {

UIContext::UIContext(const UIFont& font, const UIStyle& style)
    : m_Font(font), m_Style(style) {}

void UIContext::beginFrame(const UIInput& input, const glm::vec2& screenSize) {
    m_Input = input;
    m_ScreenSize = screenSize;

    // Clear frame draw list
    m_DrawList.clear();
    m_DrawList.setTexture(0); // Font atlas is slot 0
    m_DrawList.clearScissor();

    // Clear clip stack
    m_ClipStack.clear();

    // Determine mouse capture state
    bool mouseDown = input.leftDown;
    if (mouseDown && !m_MouseWasDown) {
        // Mouse just pressed - will be handled in processInput
    } else if (!mouseDown && m_MouseWasDown) {
        // Mouse released - release capture
        m_CapturedWidget = nullptr;
    }
    m_MouseWasDown = mouseDown;

    // Process input (hit-test + handle)
    processInput();

    // Layout pass
    layoutWidgets();

    // Draw pass
    drawWidgets();
}

void UIContext::endFrame() {
    // Frame complete - draw list is ready for renderer
}

bool UIContext::removeWidget(UIWidget* widget) {
    auto it = std::find_if(m_Widgets.begin(), m_Widgets.end(),
                           [widget](const std::unique_ptr<UIWidget>& w) {
                               return w.get() == widget;
                           });
    if (it != m_Widgets.end()) {
        // If this widget had capture, release it
        if (m_CapturedWidget == widget) {
            m_CapturedWidget = nullptr;
        }
        m_Widgets.erase(it);
        return true;
    }
    return false;
}

void UIContext::clearWidgets() {
    m_Widgets.clear();
    m_CapturedWidget = nullptr;
}

void UIContext::pushClip(const glm::vec2& min, const glm::vec2& max) {
    glm::vec4 clip(min.x, min.y, max.x - min.x, max.y - min.y);

    // Intersect with parent clip if exists
    if (!m_ClipStack.empty()) {
        const glm::vec4& parent = m_ClipStack.back();
        clip.x = std::max(clip.x, parent.x);
        clip.y = std::max(clip.y, parent.y);
        float right = std::min(clip.x + clip.z, parent.x + parent.z);
        float bottom = std::min(clip.y + clip.w, parent.y + parent.w);
        clip.z = std::max(0.0f, right - clip.x);
        clip.w = std::max(0.0f, bottom - clip.y);
    }

    m_ClipStack.push_back(clip);
    m_DrawList.setScissor(clip.x, clip.y, clip.z, clip.w);
}

void UIContext::popClip() {
    if (!m_ClipStack.empty()) {
        m_ClipStack.pop_back();
    }
    if (!m_ClipStack.empty()) {
        const glm::vec4& clip = m_ClipStack.back();
        m_DrawList.setScissor(clip.x, clip.y, clip.z, clip.w);
    } else {
        m_DrawList.clearScissor();
    }
}

glm::vec4 UIContext::currentClip() const {
    if (!m_ClipStack.empty()) {
        return m_ClipStack.back();
    }
    return glm::vec4(0.0f);
}

bool UIContext::beginScrollArea(const std::string& id,
                                const glm::vec2& outerMin,
                                const glm::vec2& outerMax,
                                bool followTail) {
    // Draw scroll area background
    m_DrawList.addRoundedRect(outerMin, outerMax, m_Style.rounding, m_Style.panel);

    // Get or create scroll state
    auto& state = m_ScrollStates[id];

    // Handle scroll input
    if (UIWidget::contains(m_Input, outerMin, outerMax)) {
        state.scrollY -= m_Input.scrollY * 20.0f; // 20px per scroll notch
    }

    // Clamp scroll
    // We'll clamp after we know content height in endScrollArea

    // Push clip for content area (reserve space for scrollbar if needed)
    const float scrollbarWidth = 12.0f;
    glm::vec2 contentMin = outerMin;
    glm::vec2 contentMax = outerMax - glm::vec2(scrollbarWidth, 0.0f);

    pushClip(contentMin, contentMax);

    // Apply scroll offset to draw list (translate all subsequent drawing)
    // We do this by adjusting the clip rect's effective origin
    // For simplicity, we'll track the scroll offset and apply it in draw calls
    // Actually, let's use a simpler approach: offset the draw positions
    m_DrawList.setScissor(contentMin.x, contentMin.y - state.scrollY,
                          contentMax.x - contentMin.x, contentMax.y - contentMin.y);

    return true;
}

void UIContext::endScrollArea(float contentHeight) {
    // Get the current scroll area ID (last one pushed)
    // For simplicity, we assume single nested scroll area or use a stack
    // This is a simplified implementation

    // Pop clip
    popClip();

    // Draw scrollbar if needed
    // (Implementation would go here - omitted for brevity)
}

bool UIContext::beginWindow(const std::string& title,
                            glm::vec2& rect,
                            float titleBarHeight) {
    // Get or create window state
    auto& state = m_WindowStates[title];

    // Title bar rect
    glm::vec2 titleMin = rect;
    glm::vec2 titleMax = rect + glm::vec2(0.0f, titleBarHeight);

    // Content rect
    glm::vec2 contentMin = rect;
    glm::vec2 contentMax = rect + glm::vec2(0.0f, 0.0f); // Will be set by content

    // Draw window background
    m_DrawList.addRoundedRect(rect, rect + glm::vec2(300.0f, 200.0f), // placeholder size
                              m_Style.rounding, m_Style.panel);

    // Draw title bar
    m_DrawList.addRoundedRect(titleMin, titleMax, m_Style.rounding, m_Style.accent);

    // Draw title text
    drawText(m_DrawList, title, titleMin + glm::vec2(m_Style.padding, m_Style.padding * 0.5f),
             m_Style.text);

    // Handle title bar drag
    if (m_Input.leftPressed && UIWidget::contains(m_Input, titleMin, titleMax)) {
        state.dragging = true;
        state.dragStart = glm::vec2(m_Input.mouseX, m_Input.mouseY);
        state.rectStart = rect;
    }

    if (state.dragging) {
        if (m_Input.leftDown) {
            rect = state.rectStart + glm::vec2(m_Input.mouseX, m_Input.mouseY) - state.dragStart;
        } else {
            state.dragging = false;
        }
    }

    // Handle close button (top-right)
    glm::vec2 closeBtnMin = titleMax - glm::vec2(titleBarHeight, titleBarHeight);
    glm::vec2 closeBtnMax = titleMax;
    if (m_Input.leftPressed && UIWidget::contains(m_Input, closeBtnMin, closeBtnMax)) {
        return false; // Window closed
    }

    // Draw close button
    m_DrawList.addQuad(closeBtnMin + glm::vec2(4.0f), closeBtnMax - glm::vec2(4.0f),
                       UIWidget::contains(m_Input, closeBtnMin, closeBtnMax) ? m_Style.accentHover : m_Style.accent);

    // Push clip for content (below title bar)
    pushClip(contentMin + glm::vec2(0.0f, titleBarHeight),
             rect + glm::vec2(300.0f, 200.0f)); // placeholder

    return true;
}

void UIContext::endWindow() {
    popClip();
}

float UIContext::textWidth(const char* text) const {
    return m_Font.textWidth(text);
}

void UIContext::drawText(UIDrawList& dl,
                         const std::string& text,
                         const glm::vec2& pos,
                         const glm::vec4& color,
                         float scale) {
    m_Font.drawString(dl, text, pos, scale, color);
}

uint64_t UIContext::nextId() {
    return m_NextId++;
}

void UIContext::processInput() {
    // Hit-test widgets in reverse draw order (top-most first)
    for (auto it = m_WidgetDrawOrder.rbegin(); it != m_WidgetDrawOrder.rend(); ++it) {
        UIWidget* widget = *it;
        if (!widget->visible()) continue;

        if (m_CapturedWidget) {
            // If we have a captured widget, only it gets input
            if (widget == m_CapturedWidget) {
                if (widget->handle(m_Input)) {
                    break; // Consumed
                }
            }
        } else {
            // No capture - check hover and handle
            if (widget->hovered(m_Input)) {
                if (m_Input.leftPressed) {
                    m_CapturedWidget = widget;
                }
                if (widget->handle(m_Input)) {
                    break; // Consumed
                }
            }
        }
    }
}

void UIContext::layoutWidgets() {
    // Build draw order (simple: order of addition)
    m_WidgetDrawOrder.clear();
    m_WidgetDrawOrder.reserve(m_Widgets.size());
    for (auto& w : m_Widgets) {
        if (w->visible()) {
            m_WidgetDrawOrder.push_back(w.get());
        }
    }

    // Measure all widgets (for layout-aware containers)
    for (auto* w : m_WidgetDrawOrder) {
        // Widgets that are containers should measure their children here
        // For now, leaf widgets just need their bounds set by containers
        // Base implementation does nothing
    }
}

void UIContext::drawWidgets() {
    for (auto* w : m_WidgetDrawOrder) {
        w->draw(m_DrawList, m_Font, m_Style);
    }
}

} // namespace Atlas::UI
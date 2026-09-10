#pragma once

#include <vector>
#include <memory>
#include <string>
#include <unordered_map>
#include <functional>

#include <glm/glm.hpp>

#include "atlas_ui/ui_widget.h"
#include "atlas_ui/ui_types.h"
#include "atlas_ui/ui_draw_list.h"
#include "atlas_ui/ui_font.h"

namespace Atlas::UI {

class UIContext {
public:
    using WidgetFactory = std::function<std::unique_ptr<UIWidget>()>;

    UIContext(const UIFont& font, const UIStyle& style);
    ~UIContext() = default;

    UIContext(const UIContext&) = delete;
    UIContext& operator=(const UIContext&) = delete;

    // --- Frame lifecycle ----------------------------------------------------
    // Call once per frame with current input + screen size.
    void beginFrame(const UIInput& input, const glm::vec2& screenSize);
    // Call after all widget layout/draw calls for this frame.
    void endFrame();

    // Get the draw list produced by this frame (valid after endFrame).
    const UIDrawList& drawList() const { return m_DrawList; }
    UIDrawList& drawList() { return m_DrawList; }

    // --- Widget management (retained-mode) ----------------------------------
    // Add a widget to the context. The context owns it and will call
    // measure/draw/handle on it every frame. Returns a non-owning pointer.
    template <typename T, typename... Args>
    T* addWidget(Args&&... args);

    // Remove a widget previously added. Returns true if found and removed.
    bool removeWidget(UIWidget* widget);

    // Clear all widgets.
    void clearWidgets();

    // --- Layout helpers (used by containers/windows) ------------------------
    // Push a clip rect onto the stack. All subsequent primitives are clipped.
    void pushClip(const glm::vec2& min, const glm::vec2& max);
    // Pop the current clip rect.
    void popClip();
    // Current clip rect (top of stack), or {0,0,0,0} if none.
    glm::vec4 currentClip() const;

    // Scroll area: begins a clipped region with vertical scrollbar.
    // Returns the content rect (inner area excluding scrollbar).
    // 'id' must be unique per scroll area per frame.
    // 'followTail' = if content was scrolled to bottom last frame, keep it there.
    bool beginScrollArea(const std::string& id,
                         const glm::vec2& outerMin,
                         const glm::vec2& outerMax,
                         bool followTail = false);
    // End the scroll area. 'contentHeight' is the total height of all children.
    void endScrollArea(float contentHeight);

    // Window: begins a floating window with title bar, drag, resize, close.
    // 'rect' is in/out: on first call, use initial position/size; updated each frame.
    // Returns false if window is closed (user clicked close button).
    bool beginWindow(const std::string& title,
                     glm::vec2& rect,
                     float titleBarHeight = 24.0f);
    void endWindow();

    // --- Layout utilities ---------------------------------------------------
    // Measure a UTF-8 string using the context's font.
    float textWidth(const char* text) const;
    // Draw text directly to the draw list (no widget needed).
    void drawText(UIDrawList& dl,
                  const std::string& text,
                  const glm::vec2& pos,
                  const glm::vec4& color = glm::vec4(1.0f),
                  float scale = 1.0f);

    // --- Accessors ----------------------------------------------------------
    const UIFont& font() const { return m_Font; }
    const UIStyle& style() const { return m_Style; }
    const UIInput& input() const { return m_Input; }
    const glm::vec2& screenSize() const { return m_ScreenSize; }

    // Unique ID generator for widgets that need stable IDs across frames.
    uint64_t nextId();

    // Get a widget by type name (for panels that need to find specific widgets).
    template <typename T>
    T* findWidget(const std::string& name);

private:
    // Internal: process input for all widgets (hit-test + handle).
    void processInput();

    // Internal: measure all widgets and run layout passes.
    void layoutWidgets();

    // Internal: draw all visible widgets.
    void drawWidgets();

    // Widget storage (owned).
    std::vector<std::unique_ptr<UIWidget>> m_Widgets;

    // Widgets currently being processed (for z-order).
    std::vector<UIWidget*> m_WidgetDrawOrder;

    // Input state for current frame.
    UIInput m_Input;
    glm::vec2 m_ScreenSize{0.0f};

    // Font and style (referenced, not owned).
    const UIFont& m_Font;
    const UIStyle& m_Style;

    // Draw list for this frame.
    UIDrawList m_DrawList;

    // Clip rect stack.
    std::vector<glm::vec4> m_ClipStack;

    // Scroll area state (persists across frames).
    struct ScrollState {
        float scrollY = 0.0f;
        bool wasAtBottom = false;
    };
    std::unordered_map<std::string, ScrollState> m_ScrollStates;

    // Window state (persists across frames).
    struct WindowState {
        bool dragging = false;
        bool resizing = false;
        glm::vec2 dragStart{0.0f};
        glm::vec2 rectStart{0.0f};
    };
    std::unordered_map<std::string, WindowState> m_WindowStates;

    // Widget capture state (which widget has mouse capture).
    UIWidget* m_CapturedWidget = nullptr;
    bool m_MouseWasDown = false;

    // Unique ID counter.
    uint64_t m_NextId = 1;
};

// --- Template implementations ------------------------------------------------

template <typename T, typename... Args>
T* UIContext::addWidget(Args&&... args) {
    auto widget = std::make_unique<T>(std::forward<Args>(args)...);
    T* ptr = widget.get();
    m_Widgets.push_back(std::move(widget));
    return ptr;
}

template <typename T>
T* UIContext::findWidget(const std::string& name) {
    for (auto& w : m_Widgets) {
        if (auto* t = dynamic_cast<T*>(w.get())) {
            // Note: requires widgets to have a name() method or similar.
            // For now, return first of type T. Can be extended.
            return t;
        }
    }
    return nullptr;
}

} // namespace Atlas::UI
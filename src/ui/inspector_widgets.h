// inspector_widgets.h
// Unity-style inspector primitives for the AtlasEngine Properties panel.
//
// Provides:
//   - Component cards: bordered auto-resize frame + fold header + gear menu
//     ("..." button with "Remove Component").
//   - Property rows: left label column (fixed, clamped width) + control that
//     fills the remainder, so every control in a card starts at the same X.
//   - Vec3 XYZ rows with colored axis badges (Unity Transform look).
//   - Case-insensitive section filter helper.
//
// Header-only, depends only on <imgui.h>. Safe to include from reflection.h
// and inspector_ui.h (no Atlas includes, no statics with side effects).

#pragma once

#include <imgui.h>

#include <cctype>
#include <cfloat>
#include <cstring>

namespace Atlas::InspectorUI {

// Width of the label column, derived from available width and clamped so
// narrow windows don't collapse the controls and wide windows don't spread
// labels too far from their controls.
inline float FieldLabelWidth() {
    const float avail = ImGui::GetContentRegionAvail().x;
    const float w = avail * 0.38f;
    if (w < 100.0f) return 100.0f;
    if (w > 200.0f) return 200.0f;
    return w;
}

// Draws the label half of a property row and sets up the next item to fill
// the remainder of the line. The caller must draw exactly one widget right
// after this call using an id-only label ("##slug") so no second label is
// drawn. Optional tooltip shows when hovering the label.
inline void FieldRow(const char* label, const char* tooltip = nullptr) {
    ImGui::AlignTextToFramePadding();
    const float rowX = ImGui::GetCursorPosX();
    ImGui::TextUnformatted((label && label[0]) ? label : "Value");
    if (tooltip && tooltip[0] && ImGui::IsItemHovered()) {
        ImGui::SetTooltip("%s", tooltip);
    }
    ImGui::SameLine();
    ImGui::SetCursorPosX(rowX + FieldLabelWidth());
    ImGui::SetNextItemWidth(-FLT_MIN);
}

// Small dimmed help line, Unity-style ("Learn more" hint look).
inline void HelpText(const char* text) {
    ImGui::TextDisabled("%s", text);
}

// Case-insensitive substring match for the section filter box.
// Empty filter matches everything.
inline bool PassesFilter(const char* filter, const char* name) {
    if (!filter || !filter[0]) return true;
    if (!name || !name[0]) return true;
    const size_t fn = std::strlen(filter);
    const size_t nn = std::strlen(name);
    if (fn > nn) return false;
    for (size_t i = 0; i + fn <= nn; ++i) {
        size_t j = 0;
        for (; j < fn; ++j) {
            const char a = static_cast<char>(std::tolower(static_cast<unsigned char>(filter[j])));
            const char b = static_cast<char>(std::tolower(static_cast<unsigned char>(name[i + j])));
            if (a != b) break;
        }
        if (j == fn) return true;
    }
    return false;
}

// Unity-like vec3 row: label column + X/Y/Z sub-fields with colored axis
// badges sharing the control width. Reports per-frame activation so callers
// can keep undo tracking (same pattern as raw DragFloat3 usage).
inline void Vec3Row(const char* label, float* v, float speed, const char* id,
                    bool* outActivated = nullptr, bool* outDeactivated = nullptr,
                    const char* fmt = "%.3f") {
    FieldRow(label);
    const float spacing = ImGui::GetStyle().ItemSpacing.x;
    const float avail = ImGui::GetContentRegionAvail().x;
    const float colW = (avail - spacing * 2.0f) / 3.0f;
    static const char axes[3] = {'X', 'Y', 'Z'};
    static const ImVec4 axisColors[3] = {
        ImVec4(0.95f, 0.30f, 0.22f, 1.0f),
        ImVec4(0.45f, 0.80f, 0.25f, 1.0f),
        ImVec4(0.30f, 0.55f, 0.95f, 1.0f),
    };
    bool activated = false;
    bool deactivated = false;
    ImGui::PushID(id);
    for (int i = 0; i < 3; ++i) {
        if (i > 0) ImGui::SameLine(0.0f, spacing);
        ImGui::PushID(i);
        ImGui::TextColored(axisColors[i], "%c", axes[i]);
        ImGui::SameLine(0.0f, 2.0f);
        const float badgeW = ImGui::GetItemRectSize().x + 2.0f;
        if (i < 2) {
            ImGui::SetNextItemWidth(colW - badgeW);
        } else {
            ImGui::SetNextItemWidth(-FLT_MIN);
        }
        ImGui::DragFloat("##v", &v[i], speed, 0.0f, 0.0f, fmt);
        if (ImGui::IsItemActivated()) activated = true;
        if (ImGui::IsItemDeactivatedAfterEdit()) deactivated = true;
        ImGui::PopID();
    }
    ImGui::PopID();
    if (outActivated) *outActivated = activated;
    if (outDeactivated) *outDeactivated = deactivated;
}

// Begins a component card: bordered auto-resize frame containing a fold
// header plus an optional "..." gear button with a "Remove Component" item.
// Always pair with EndComponentCard(), even when the returned open state is
// false. When canRemove is true, *outRemoveClicked is set when the user
// picks the menu item (the caller performs the actual removal after End).
inline bool BeginComponentCard(const char* id, const char* title, bool defaultOpen,
                               bool canRemove, bool* outRemoveClicked) {
    bool removeClicked = false;
    ImGui::PushID(id);
    ImGui::BeginChild("##card", ImVec2(0.0f, 0.0f),
                      ImGuiChildFlags_Borders | ImGuiChildFlags_AutoResizeY);
    const bool open = ImGui::CollapsingHeader(
        title, defaultOpen ? ImGuiTreeNodeFlags_DefaultOpen : ImGuiTreeNodeFlags_None);
    // Right-click on the header opens the context menu (Unity-style quick remove).
    // NOTE: a single BeginPopup below serves both triggers. Using
    // BeginPopupContextItem + BeginPopup with the same id would begin the same
    // popup twice in one frame and draw "Remove Component" with a duplicate ID.
    if (canRemove && ImGui::IsItemHovered() && ImGui::IsMouseReleased(ImGuiMouseButton_Right)) {
        ImGui::OpenPopup("##compmenu");
    }
    if (canRemove) {
        ImGui::SameLine(ImGui::GetWindowContentRegionMax().x - 26.0f);
        if (ImGui::SmallButton("...")) {
            ImGui::OpenPopup("##compmenu");
        }
        if (ImGui::BeginPopup("##compmenu")) {
            if (ImGui::MenuItem("Remove Component")) removeClicked = true;
            ImGui::EndPopup();
        }
    }
    if (outRemoveClicked) *outRemoveClicked = removeClicked;
    if (open) {
        ImGui::Spacing();
        ImGui::Indent(4.0f);
    }
    return open;
}

inline void EndComponentCard(bool wasOpen) {
    if (wasOpen) {
        ImGui::Unindent(4.0f);
    }
    ImGui::EndChild();
    ImGui::PopID();
    ImGui::Spacing();
}

} // namespace Atlas::InspectorUI

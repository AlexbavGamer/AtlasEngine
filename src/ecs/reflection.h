// reflection.h
// Modern, zero-overhead reflection system for ImGui component inspectors.
// C++17 compatible, zero runtime overhead, fully extensible.

#pragma once

#include <imgui.h>
#include "../ui/inspector_widgets.h"
#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>
#include <string>
#include <string_view>
#include <tuple>
#include <type_traits>
#include <utility>
#include <cstdint>
#include <cstddef>
#include <cstring>
#include <memory>
#include <vector>

namespace ecs { namespace refl {

// ============================================================================
// Field metadata
// ============================================================================

struct FieldMeta {
    const char* name = nullptr;
    const char* label = nullptr;
    const char* tooltip = nullptr;
    bool read_only = false;
    float min = 0.0f;
    float max = 0.0f;
    float step = 0.0f;
    const char* format = nullptr;
    bool color_picker = false;
    bool drag = true;

    constexpr FieldMeta() noexcept
        : name(nullptr), label(nullptr), tooltip(nullptr), read_only(false),
          min(0.0f), max(0.0f), step(0.0f), format(nullptr),
          color_picker(false), drag(true) {}

    constexpr FieldMeta(const char* l) noexcept
        : name(l), label(l) {}

    constexpr FieldMeta(const char* n, const char* l) noexcept
        : name(n), label(l) {}

    constexpr FieldMeta(const char* n, const char* l, const char* t) noexcept
        : name(n), label(l), tooltip(t) {}

    // Fluent setters for chaining in macros (operate on temporaries).
    constexpr FieldMeta& set_label(const char* l) noexcept { label = l; return *this; }
    constexpr FieldMeta& set_tooltip(const char* t) noexcept { tooltip = t; return *this; }
    constexpr FieldMeta& set_read_only(bool v = true) noexcept { read_only = v; return *this; }
    constexpr FieldMeta& set_range(float mn, float mx) noexcept { min = mn; max = mx; return *this; }
    constexpr FieldMeta& set_step(float s) noexcept { step = s; return *this; }
    constexpr FieldMeta& set_format(const char* f) noexcept { format = f; return *this; }
    constexpr FieldMeta& set_color_picker(bool v = true) noexcept { color_picker = v; return *this; }
    constexpr FieldMeta& set_slider(bool v = true) noexcept { drag = !v; return *this; }
};

// ============================================================================
// Field descriptor: member pointer + metadata.
//
// The fluent methods below return a modified *copy* so call sites can write:
//   COMPONENT_FIELD(T, member, "Label").range(0, 1).step(0.01f)
// inside COMPONENT_FIELDS(...).
// ============================================================================

template <typename T, typename Member>
struct Field {
    Member T::* ptr;
    FieldMeta meta;

    constexpr Field(Member T::* p, FieldMeta m) noexcept : ptr(p), meta(m) {}

    constexpr Field with_meta(FieldMeta m) const noexcept {
        Field f = *this;
        f.meta = m;
        return f;
    }

    constexpr Field label(const char* l) const noexcept {
        Field f = *this; f.meta.label = l; return f;
    }
    constexpr Field tooltip(const char* t) const noexcept {
        Field f = *this; f.meta.tooltip = t; return f;
    }
    constexpr Field read_only(bool v = true) const noexcept {
        Field f = *this; f.meta.read_only = v; return f;
    }
    constexpr Field range(float mn, float mx) const noexcept {
        Field f = *this; f.meta.min = mn; f.meta.max = mx; return f;
    }
    constexpr Field step(float s) const noexcept {
        Field f = *this; f.meta.step = s; return f;
    }
    constexpr Field format(const char* fmt) const noexcept {
        Field f = *this; f.meta.format = fmt; return f;
    }
    constexpr Field color_picker(bool v = true) const noexcept {
        Field f = *this; f.meta.color_picker = v; return f;
    }
    constexpr Field slider(bool v = true) const noexcept {
        Field f = *this; f.meta.drag = !v; return f;
    }
};

// ============================================================================
// ComponentFields trait - specialize per component type via COMPONENT_FIELDS.
// Primary has no `fields` member; use has_reflection_v<T> to detect.
// ============================================================================

template <typename T>
struct ComponentFields {
    static constexpr size_t count = 0;
};

template <typename T, typename = void>
struct has_reflection : std::false_type {};

template <typename T>
struct has_reflection<T, std::void_t<decltype(ComponentFields<std::decay_t<T>>::fields)>> : std::true_type {};

template <typename T>
inline constexpr bool has_reflection_v = has_reflection<T>::value;

// ============================================================================
// Field renderer: dispatch based on member type.
//
// Specialize ecs::refl::detail::FieldRenderer<MyType> for custom types.
// The primary template handles enums (as ints), pointers (read-only hex)
// and everything else (read-only placeholder) so unregistered types never
// break the build.
// ============================================================================

namespace detail {

template <typename Member>
struct FieldRenderer {
    static void renderFieldImpl(Member& v, const FieldMeta& m) {
        const char* label = (m.label && m.label[0]) ? m.label : "value";
        ::Atlas::InspectorUI::FieldRow(label, m.tooltip);
        if constexpr (std::is_enum_v<Member>) {
            using Underlying = std::underlying_type_t<Member>;
            Underlying tmp = static_cast<Underlying>(v);
            // NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast)
            int tmpInt = static_cast<int>(tmp);
            int lo = static_cast<int>(m.min);
            int hi = static_cast<int>(m.max);
            if (m.min != m.max) {
                if (ImGui::DragInt("##value", &tmpInt, m.step ? m.step : 1.0f, lo, hi)) {
                    v = static_cast<Member>(static_cast<Underlying>(tmpInt));
                }
            } else {
                if (ImGui::DragInt("##value", &tmpInt, m.step ? m.step : 1.0f)) {
                    v = static_cast<Member>(static_cast<Underlying>(tmpInt));
                }
            }
        } else if constexpr (std::is_pointer_v<Member>) {
            ImGui::Text("%p", static_cast<const void*>(v));
        } else {
            ImGui::TextDisabled("(not editable)");
        }
        (void)v;
    }
};

template <typename T>
struct FieldRenderer<std::vector<T>> {
    static void renderFieldImpl(std::vector<T>& v, const FieldMeta& m) {
        const char* label = (m.label && m.label[0]) ? m.label : "items";
        ::Atlas::InspectorUI::FieldRow(label, m.tooltip);
        ImGui::TextDisabled("[%zu item(s)]", v.size());
    }
};

template <typename T>
struct FieldRenderer<std::shared_ptr<T>> {
    static void renderFieldImpl(std::shared_ptr<T>& v, const FieldMeta& m) {
        const char* label = (m.label && m.label[0]) ? m.label : "ptr";
        ::Atlas::InspectorUI::FieldRow(label, m.tooltip);
        ImGui::TextDisabled("%s", v ? "(set)" : "(null)");
    }
};

template <>
struct FieldRenderer<bool> {
    static void renderFieldImpl(bool& v, const FieldMeta& m) {
        const char* label = (m.label && m.label[0]) ? m.label : "Enabled";
        ::Atlas::InspectorUI::FieldRow(label, m.tooltip);
        ImGui::Checkbox("##value", &v);
    }
};

template <>
struct FieldRenderer<float> {
    static void renderFieldImpl(float& v, const FieldMeta& m) {
        const char* label = (m.label && m.label[0]) ? m.label : "##value";
        const char* fmt = m.format ? m.format : "%.3f";
        const float speed = m.step ? m.step : 0.01f;
        ::Atlas::InspectorUI::FieldRow(label, m.tooltip);
        if (m.min != m.max && !m.drag) {
            ImGui::SliderFloat("##value", &v, m.min, m.max, fmt);
        } else if (m.min != m.max) {
            ImGui::DragFloat("##value", &v, speed, m.min, m.max, fmt);
        } else {
            ImGui::DragFloat("##value", &v, speed, 0.0f, 0.0f, fmt);
        }
    }
};

template <>
struct FieldRenderer<double> {
    static void renderFieldImpl(double& v, const FieldMeta& m) {
        const char* label = (m.label && m.label[0]) ? m.label : "##value";
        const char* fmt = m.format ? m.format : "%.3f";
        const float speed = m.step ? m.step : 0.01f;
        ::Atlas::InspectorUI::FieldRow(label, m.tooltip);
        if (m.min != m.max) {
            double lo = static_cast<double>(m.min);
            double hi = static_cast<double>(m.max);
            if (!m.drag) {
                ImGui::SliderScalar("##value", ImGuiDataType_Double, &v, &lo, &hi, fmt);
            } else {
                ImGui::DragScalar("##value", ImGuiDataType_Double, &v, speed, &lo, &hi, fmt);
            }
        } else {
            ImGui::DragScalar("##value", ImGuiDataType_Double, &v, speed, nullptr, nullptr, fmt);
        }
    }
};

template <>
struct FieldRenderer<int32_t> {
    static void renderFieldImpl(int32_t& v, const FieldMeta& m) {
        const char* label = (m.label && m.label[0]) ? m.label : "##value";
        ::Atlas::InspectorUI::FieldRow(label, m.tooltip);
        if (m.min != m.max && !m.drag) {
            ImGui::SliderInt("##value", &v, static_cast<int>(m.min), static_cast<int>(m.max));
        } else if (m.min != m.max) {
            ImGui::DragInt("##value", &v, m.step ? m.step : 1.0f,
                            static_cast<int>(m.min), static_cast<int>(m.max));
        } else {
            ImGui::DragInt("##value", &v, m.step ? m.step : 1.0f);
        }
    }
};

template <>
struct FieldRenderer<uint32_t> {
    static void renderFieldImpl(uint32_t& v, const FieldMeta& m) {
        const char* label = (m.label && m.label[0]) ? m.label : "##value";
        const float speed = m.step ? m.step : 1.0f;
        ::Atlas::InspectorUI::FieldRow(label, m.tooltip);
        if (m.min != m.max) {
            uint32_t lo = static_cast<uint32_t>(m.min);
            uint32_t hi = static_cast<uint32_t>(m.max);
            if (!m.drag) {
                ImGui::SliderScalar("##value", ImGuiDataType_U32, &v, &lo, &hi);
            } else {
                ImGui::DragScalar("##value", ImGuiDataType_U32, &v, speed, &lo, &hi);
            }
        } else {
            ImGui::DragScalar("##value", ImGuiDataType_U32, &v, speed);
        }
    }
};

template <>
struct FieldRenderer<int64_t> {
    static void renderFieldImpl(int64_t& v, const FieldMeta& m) {
        const char* label = (m.label && m.label[0]) ? m.label : "##value";
        ::Atlas::InspectorUI::FieldRow(label, m.tooltip);
        ImGui::DragScalar("##value", ImGuiDataType_S64, &v, m.step ? m.step : 1.0f);
    }
};

template <>
struct FieldRenderer<uint64_t> {
    static void renderFieldImpl(uint64_t& v, const FieldMeta& m) {
        const char* label = (m.label && m.label[0]) ? m.label : "##value";
        ::Atlas::InspectorUI::FieldRow(label, m.tooltip);
        ImGui::DragScalar("##value", ImGuiDataType_U64, &v, m.step ? m.step : 1.0f);
    }
};

template <>
struct FieldRenderer<int8_t> {
    static void renderFieldImpl(int8_t& v, const FieldMeta& m) {
        const char* label = (m.label && m.label[0]) ? m.label : "##value";
        ::Atlas::InspectorUI::FieldRow(label, m.tooltip);
        ImGui::DragScalar("##value", ImGuiDataType_S8, &v, m.step ? m.step : 1.0f);
    }
};

template <>
struct FieldRenderer<uint8_t> {
    static void renderFieldImpl(uint8_t& v, const FieldMeta& m) {
        const char* label = (m.label && m.label[0]) ? m.label : "##value";
        ::Atlas::InspectorUI::FieldRow(label, m.tooltip);
        ImGui::DragScalar("##value", ImGuiDataType_U8, &v, m.step ? m.step : 1.0f);
    }
};

template <>
struct FieldRenderer<int16_t> {
    static void renderFieldImpl(int16_t& v, const FieldMeta& m) {
        const char* label = (m.label && m.label[0]) ? m.label : "##value";
        ::Atlas::InspectorUI::FieldRow(label, m.tooltip);
        ImGui::DragScalar("##value", ImGuiDataType_S16, &v, m.step ? m.step : 1.0f);
    }
};

template <>
struct FieldRenderer<uint16_t> {
    static void renderFieldImpl(uint16_t& v, const FieldMeta& m) {
        const char* label = (m.label && m.label[0]) ? m.label : "##value";
        ::Atlas::InspectorUI::FieldRow(label, m.tooltip);
        ImGui::DragScalar("##value", ImGuiDataType_U16, &v, m.step ? m.step : 1.0f);
    }
};

template <>
struct FieldRenderer<std::string> {
    static void renderFieldImpl(std::string& v, const FieldMeta& m) {
        const char* label = (m.label && m.label[0]) ? m.label : "Value";
        ::Atlas::InspectorUI::FieldRow(label, m.tooltip);
        if (m.read_only) {
            ImGui::TextUnformatted(v.c_str());
            return;
        }
        char buf[256];
        std::strncpy(buf, v.c_str(), sizeof(buf) - 1);
        buf[sizeof(buf) - 1] = '\0';
        if (ImGui::InputText("##value", buf, sizeof(buf))) {
            v = buf;
        }
    }
};

template <>
struct FieldRenderer<glm::vec2> {
    static void renderFieldImpl(glm::vec2& v, const FieldMeta& m) {
        const char* label = (m.label && m.label[0]) ? m.label : "Value";
        ::Atlas::InspectorUI::FieldRow(label, m.tooltip);
        if (m.read_only) {
            ImGui::Text("%.2f, %.2f", v.x, v.y);
            return;
        }
        ImGui::DragFloat2("##value", &v.x, m.step ? m.step : 0.05f);
    }
};

template <>
struct FieldRenderer<glm::vec3> {
    static void renderFieldImpl(glm::vec3& v, const FieldMeta& m) {
        const char* label = (m.label && m.label[0]) ? m.label : "Value";
        ::Atlas::InspectorUI::FieldRow(label, m.tooltip);
        if (m.read_only && !m.color_picker) {
            ImGui::Text("%.2f, %.2f, %.2f", v.x, v.y, v.z);
            return;
        }
        if (m.color_picker) {
            ImGui::ColorEdit3("##value", &v.x,
                ImGuiColorEditFlags_Float | ImGuiColorEditFlags_PickerHueWheel);
        } else {
            ImGui::DragFloat3("##value", &v.x, m.step ? m.step : 0.05f);
        }
    }
};

template <>
struct FieldRenderer<glm::vec4> {
    static void renderFieldImpl(glm::vec4& v, const FieldMeta& m) {
        const char* label = (m.label && m.label[0]) ? m.label : "Value";
        ::Atlas::InspectorUI::FieldRow(label, m.tooltip);
        if (m.read_only && !m.color_picker) {
            ImGui::Text("%.2f, %.2f, %.2f, %.2f", v.x, v.y, v.z, v.w);
            return;
        }
        if (m.color_picker) {
            ImGui::ColorEdit4("##value", &v.x,
                ImGuiColorEditFlags_Float | ImGuiColorEditFlags_PickerHueWheel);
        } else {
            ImGui::DragFloat4("##value", &v.x, m.step ? m.step : 0.05f);
        }
    }
};

template <>
struct FieldRenderer<glm::quat> {
    static void renderFieldImpl(glm::quat& v, const FieldMeta& m) {
        const char* label = (m.label && m.label[0]) ? m.label : "Value";
        ::Atlas::InspectorUI::FieldRow(label, m.tooltip);
        if (m.read_only) {
            ImGui::Text("(%.2f, %.2f, %.2f, %.2f)", v.x, v.y, v.z, v.w);
            return;
        }
        ImGui::DragFloat4("##value", &v.x, m.step ? m.step : 0.01f);
    }
};

} // namespace detail

// ============================================================================
// Single-field dispatch: read-only wrapper + tooltip in one place.
// ============================================================================

template <typename Member>
void renderFieldValue(Member& value, const FieldMeta& meta) {
    if (meta.read_only) {
        ImGui::BeginDisabled();
    }
    detail::FieldRenderer<std::decay_t<Member>>::renderFieldImpl(value, meta);
    if (meta.tooltip && ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) {
        ImGui::SetTooltip("%s", meta.tooltip);
    }
    if (meta.read_only) {
        ImGui::EndDisabled();
    }
}

// ============================================================================
// Auto reflection renderer: renders every registered field of a component.
// ============================================================================

namespace detail {

template <typename T, typename Member>
void renderSingleField(T& component, const Field<T, Member>& field, int index) {
    ImGui::PushID(index);
    ::ecs::refl::renderFieldValue(component.*(field.ptr), field.meta);
    ImGui::PopID();
}

} // namespace detail

template <typename T>
void renderAutoComponentProperties(T& component) {
    using Decayed = std::decay_t<T>;
    if constexpr (has_reflection_v<Decayed>) {
        constexpr auto& fields = ComponentFields<Decayed>::fields;
        // Stable ID scope per component type + per object instance.
        ImGui::PushID(static_cast<const void*>(&component));
        int index = 0;
        std::apply([&](const auto&... field) {
            ((detail::renderSingleField(component, field, index++)), ...);
        }, fields);
        ImGui::PopID();
    }
}

} } // namespace ecs::refl

// ============================================================================
// Macros for component field registration.
//
//   COMPONENT_FIELD(Type, member, "Display Label").range(0, 1).step(0.01f)
//   COMPONENT_FIELDS(Type,
//       COMPONENT_FIELD(Type, position, "Position").step(0.1f),
//       COMPONENT_FIELD(Type, visible, "Visible"))
// ============================================================================

#define COMPONENT_FIELD(T, member, label) \
    ::ecs::refl::Field<T, decltype(T::member)>(&T::member, ::ecs::refl::FieldMeta{label})

// NOTE: must be used at global (or ecs::refl-enclosing) scope, NOT nested
// inside another namespace, so the explicit specialization lands in
// ecs::refl. All component registrations live at file scope for this reason.
// Classic (non-nested) namespace form + make_tuple keep this usable even
// from pre-C++17 translation units / bare language servers.
#define COMPONENT_FIELDS(T, ...) \
    namespace ecs { namespace refl { \
    template <> struct ComponentFields<T> { \
        static constexpr auto fields = std::make_tuple(__VA_ARGS__); \
        static constexpr size_t count = std::tuple_size<decltype(fields)>::value; \
    }; \
    } } // namespace ecs::refl

// ============================================================================
// Example usage
// ============================================================================

/*
// In your component header:
struct Transform {
    glm::vec3 position = {0,0,0};
    glm::vec3 rotation = {0,0,0};
    glm::vec3 scale = {1,1,1};
};

COMPONENT_FIELDS(Transform,
    COMPONENT_FIELD(Transform, position, "Position")
        .tooltip("World position")
        .range(-1000, 1000)
        .step(0.1f),
    COMPONENT_FIELD(Transform, rotation, "Rotation")
        .tooltip("Euler angles in degrees")
        .step(1.0f),
    COMPONENT_FIELD(Transform, scale, "Scale")
        .step(0.05f));

// In your inspector code:
void renderTransform(Transform& t) {
    ecs::refl::renderAutoComponentProperties(t);
}

// To add a custom field renderer for your own type:
namespace ecs::refl::detail {
template <>
struct FieldRenderer<MyType> {
    static void renderFieldImpl(MyType& v, const FieldMeta& m) {
        // your custom ImGui code (label in m.label, no need to handle
        // read_only/tooltip - the renderFieldValue wrapper does it)
    }
};
}
*/

#pragma once

// Inspector UI for ECS components (ImGui-based property editors).
// Split out of ecs.h (Fatia 1 of the ECS<->Vulkan decoupling): ecs.h is now
// ImGui-free, so anything including it (renderer, physics, scripting...)
// no longer pays for the UI dependency. Only editor UI code includes this.
#include "ecs.h"
#include "components/components.h"

#include <imgui.h>

#include <cctype>
#include <cstdint>
#include <string>
#include <vector>

namespace ecs {

template <typename T, typename = void>
struct is_auto_reflected : std::false_type {};
template <typename T>
struct is_auto_reflected<T, std::void_t<decltype(T::getFieldPointers())>> : std::true_type {};
template <typename T>
inline constexpr bool is_auto_reflected_v = is_auto_reflected<T>::value;

template <typename F>
struct auto_field_unsupported : std::false_type {};

inline std::string autoPrettyName(const std::string& field) {
    std::string out;
    out.reserve(field.size() + 4);
    for (size_t i = 0; i < field.size(); ++i) {
        const char c = field[i];
        if (i > 0 && std::isupper(static_cast<unsigned char>(c)) &&
            std::islower(static_cast<unsigned char>(field[i - 1]))) {
            out += ' ';
        }
        out += c;
    }
    if (!out.empty()) {
        out[0] = static_cast<char>(std::toupper(static_cast<unsigned char>(out[0])));
    }
    return out;
}

template <typename T>
const std::vector<std::string>& autoFieldNames() {
    static const std::vector<std::string> names = []() {
        std::vector<std::string> out;
        const std::string raw = T::getFieldNamesRaw();
        size_t start = 0;
        for (size_t i = 0; i <= raw.size(); ++i) {
            if (i == raw.size() || raw[i] == ',') {
                std::string tok = raw.substr(start, i - start);
                const size_t a = tok.find_first_not_of(" \t");
                const size_t b = tok.find_last_not_of(" \t");
                tok = (a == std::string::npos) ? "" : tok.substr(a, b - a + 1);
                if (!tok.empty() && tok[0] == '&') tok = tok.substr(1);
                const size_t sep = tok.rfind("::");
                if (sep != std::string::npos) tok = tok.substr(sep + 2);
                if (!tok.empty()) out.push_back(autoPrettyName(tok));
                start = i + 1;
            }
        }
        return out;
    }();
    return names;
}

template <typename F>
void renderAutoField(F& field, const char* label) {
    if constexpr (std::is_same_v<F, bool>) {
        ImGui::Checkbox(label, &field);
    } else if constexpr (std::is_same_v<F, float>) {
        ImGui::DragFloat(label, &field, 0.05f);
    } else if constexpr (std::is_same_v<F, int32_t>) {
        ImGui::DragInt(label, &field, 1.0f);
    } else if constexpr (std::is_same_v<F, uint32_t>) {
        ImGui::DragScalar(label, ImGuiDataType_U32, &field, 1.0f);
    } else if constexpr (std::is_same_v<F, std::string>) {
        char buf[256] = {};
        std::strncpy(buf, field.c_str(), sizeof(buf) - 1);
        if (ImGui::InputText(label, buf, sizeof(buf))) {
            field = std::string(buf);
        }
    } else if constexpr (std::is_same_v<F, glm::vec2>) {
        ImGui::DragFloat2(label, &field.x, 0.05f);
    } else if constexpr (std::is_same_v<F, glm::vec3>) {
        ImGui::DragFloat3(label, &field.x, 0.05f);
    } else if constexpr (std::is_same_v<F, glm::vec4>) {
        ImGui::DragFloat4(label, &field.x, 0.05f);
    } else {
        static_assert(auto_field_unsupported<F>::value,
            "renderAutoField: field type not supported — use bool/float/int32_t/uint32_t/std::string/glm::vec2-4");
    }
}

// Renders every field listed in COMPONENT_FIELDS with a unique ID scope.
// Components with custom needs (undo, clamps, read-only state) keep a manual
// branch in renderComponentProperties instead (Transform, Mesh, Camera, LOD).
template <typename T>
void renderAutoComponentProperties(T& component) {
    const auto& names = autoFieldNames<T>();
    ImGui::PushID(static_cast<const void*>(&component));
    size_t index = 0;
    std::apply([&](auto... ptrs) {
        ((renderAutoField(component.*ptrs,
            (index < names.size() ? names[index].c_str() : "field")),
          ++index), ...);
    }, T::getFieldPointers());
    ImGui::PopID();
}

template<typename T>
void renderComponentProperties(T& component, uint32_t entityId) {
    if constexpr (std::is_same_v<T, Transform>) {
        ImGui::DragFloat3("Position##T", &component.position.x, 0.1f);
        ImGui::DragFloat3("Rotation##T", &component.rotation.x, 1.0f);
        ImGui::DragFloat3("Scale##T", &component.scale.x, 0.1f);
    } else if constexpr (std::is_same_v<T, Mesh>) {
        ImGui::Text("Mesh Path: %s", component.meshPath.c_str());
        ImGui::Text("Vertices: %u", component.vertexCount);
        ImGui::Text("Indices: %u", component.indexCount);
        if (component.hasBounds) {
            ImGui::Text("Bounds Min: %.2f %.2f %.2f", component.boundsMin.x, component.boundsMin.y, component.boundsMin.z);
            ImGui::Text("Bounds Max: %.2f %.2f %.2f", component.boundsMax.x, component.boundsMax.y, component.boundsMax.z);
        } else {
            ImGui::Text("Bounds: (none)");
        }
    } else if constexpr (std::is_same_v<T, Camera> || std::is_same_v<T, EditorCamera>) {
        ImGui::DragFloat3("Position##C", &component.position.x, 0.1f);
        ImGui::DragFloat3("Target##C", &component.target.x, 0.1f);
        ImGui::DragFloat("FOV##C", &component.fov, 1.0f, 1.0f, 180.0f);
        ImGui::DragFloat("Near##C", &component.nearPlane, 0.1f);
        ImGui::DragFloat("Far##C", &component.farPlane, 1.0f);

        if (component.nearPlane < 0.001f) component.nearPlane = 0.001f;
        if (component.farPlane < component.nearPlane + 0.001f) component.farPlane = component.nearPlane + 0.001f;
    } else if constexpr (std::is_same_v<T, Atlas::LODComponent>) {
        // TDD §5: read-only live state + editable importance bias.
        const char* levelNames[] = {"LOD0", "LOD1", "LOD2", "Impostor", "Culled"};
        const uint8_t idx = (component.current <= 4) ? component.current : 0;
        ImGui::Text("Level: %s (dist %.1fm, screen %.3f)", levelNames[idx], component.distance, component.screenSize);
        ImGui::DragFloat("Importance bias##LOD", &component.screenSizeBias, 0.05f, 0.1f, 8.0f, "%.2f");
        if (component.screenSizeBias < 0.1f) component.screenSizeBias = 0.1f;
    } else if constexpr (std::is_same_v<T, Atlas::ECS::LightComponent>) {
        using LightT = Atlas::ECS::LightComponent;
        const char* typeNames[] = {"Point", "Directional", "Spot"};
        int typeIdx = static_cast<int>(component.type);
        if (ImGui::Combo("Type##L", &typeIdx, typeNames, 3)) {
            component.type = static_cast<LightT::Type>(typeIdx);
        }
        ImGui::ColorEdit3("Color##L", &component.color.x);
        ImGui::DragFloat("Intensity##L", &component.intensity, 0.5f, 0.0f, 10000.0f);
        ImGui::Checkbox("Cast shadows##L", &component.castShadows);
        if (component.type == LightT::Type::Directional) {
            ImGui::TextDisabled("Direction comes from the entity Transform rotation (-Z).");
        } else {
            ImGui::DragFloat("Constant##L", &component.constant, 0.01f, 0.0f, 2.0f);
            ImGui::DragFloat("Linear##L", &component.linear, 0.001f, 0.0f, 1.0f);
            ImGui::DragFloat("Quadratic##L", &component.quadratic, 0.001f, 0.0f, 1.0f);
        }
    } else if constexpr (is_auto_reflected_v<T>) {
        // Automatic UI from COMPONENT_FIELDS (e.g. Renderable).
        renderAutoComponentProperties(component);
    }
    (void)entityId;

} // renderComponentProperties

} // namespace ecs

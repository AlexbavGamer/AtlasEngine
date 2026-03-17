#pragma once

#include <entt/entt.hpp>
#include <vulkan/vulkan_core.h>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <string>
#include <memory>
#include <cstdint>
#include <imgui.h>
#include <tuple>
#include <type_traits>

using Registry = entt::registry;
using Entity = entt::entity;

struct Transform {
    glm::vec3 position{0.0f, 0.0f, 0.0f};
    glm::vec3 rotation{0.0f, 0.0f, 0.0f};
    glm::vec3 scale{1.0f, 1.0f, 1.0f};

    Transform() = default;
    Transform(const glm::vec3& pos, const glm::vec3& rot, const glm::vec3& sc)
        : position(pos), rotation(rot), scale(sc) {}

    glm::mat4 getModelMatrix() const {
        glm::mat4 model = glm::mat4(1.0f);
        model = glm::translate(model, position);
        model = glm::rotate(model, glm::radians(rotation.x), glm::vec3(1.0f, 0.0f, 0.0f));
        model = glm::rotate(model, glm::radians(rotation.y), glm::vec3(0.0f, 1.0f, 0.0f));
        model = glm::rotate(model, glm::radians(rotation.z), glm::vec3(0.0f, 0.0f, 1.0f));
        model = glm::scale(model, scale);
        return model;
    }
};

struct Mesh {
    std::string meshPath;
    uint32_t vertexCount = 0;
    uint32_t indexCount = 0;
    VkBuffer vertexBuffer = VK_NULL_HANDLE;
    VkBuffer indexBuffer = VK_NULL_HANDLE;
    VkDeviceMemory vertexMemory = VK_NULL_HANDLE;
    VkDeviceMemory indexMemory = VK_NULL_HANDLE;
};

struct Renderable {
    bool visible = true;
    uint32_t materialID = 0;
};

struct Camera {
    glm::vec3 position{0.0f, 0.0f, 5.0f};
    glm::vec3 target{0.0f, 0.0f, 0.0f};
    glm::vec3 up{0.0f, 1.0f, 0.0f};
    float fov = 45.0f;
    float aspectRatio = 16.0f / 9.0f;
    float nearPlane = 0.1f;
    float farPlane = 100.0f;

    glm::mat4 getViewMatrix() const {
        return glm::lookAt(position, target, up);
    }

    glm::mat4 getProjectionMatrix() const {
        return glm::perspective(glm::radians(fov), aspectRatio, nearPlane, farPlane);
    }
};

using World = entt::registry;

#define COMPONENT_FIELDS(TYPE, ...) \
    static constexpr auto getFields() { \
        return std::make_tuple(__VA_ARGS__); \
    } \
    static constexpr const char* getName() { return #TYPE; }

namespace ecs {

template<typename T>
void renderComponentProperties(T& component) {
    if constexpr (std::is_same_v<T, Transform>) {
        ImGui::DragFloat3("Position", &component.position.x, 0.1f);
        ImGui::DragFloat3("Rotation", &component.rotation.x, 1.0f);
        ImGui::DragFloat3("Scale", &component.scale.x, 0.1f);
    } else if constexpr (std::is_same_v<T, Renderable>) {
        ImGui::Checkbox("Visible", &component.visible);
        ImGui::DragScalar("Material ID", ImGuiDataType_U32, &component.materialID);
    } else if constexpr (std::is_same_v<T, Mesh>) {
        ImGui::Text("Mesh Path: %s", component.meshPath.c_str());
        ImGui::Text("Vertices: %u", component.vertexCount);
        ImGui::Text("Indices: %u", component.indexCount);
    } else if constexpr (std::is_same_v<T, Camera>) {
        ImGui::DragFloat3("Position", &component.position.x, 0.1f);
        ImGui::DragFloat3("Target", &component.target.x, 0.1f);
        ImGui::DragFloat("FOV", &component.fov, 1.0f, 1.0f, 180.0f);
        ImGui::DragFloat("Near", &component.nearPlane, 0.1f);
        ImGui::DragFloat("Far", &component.farPlane, 1.0f);
    }
}

}

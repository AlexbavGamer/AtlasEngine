#include "ui_manager.h"
#include <imgui.h>
#include <imgui_impl_vulkan.h>
#include "../ecs/components.h"
#include <glm/glm.hpp>

UIManager::UIManager(std::shared_ptr<World> world) : ecsWorld(world) {}

void UIManager::render(ImTextureID viewportTexture) {
    // Unity-style dock layout
    // Main viewport area
    ImGui::Begin("Viewport", &ImGui::GetContentRegionAvail());
    renderViewport(viewportTexture);
    ImGui::End();

    // Dock layout panels
    ImGui::DockNode("Viewport", 1);  // Top panel - Viewport
    ImGui::DockNode("Hierarchy", 1, ImGuiDockNodeFlags::AutoResize);  // Right panel - Hierarchy
    ImGui::DockNode("Properties", 1, ImGuiDockNodeFlags::AutoResize);  // Left panel - Properties
    ImGui::DockNode("Content Explorer", 1, ImGuiDockNodeFlags::AutoResize);  // Bottom panel - Content Explorer
    ImGui::DockNode("Transform", 1, ImGuiDockNodeFlags::AutoResize);  // Top-left panel - Transform

    // Render panels
    renderTransformPanel();
    renderHierarchy();
    renderProperties();
    renderContentExplorer();
}

void UIManager::setSelectedEntity(EntityID entity) {
    selectedEntity = entity;
}

EntityID UIManager::getSelectedEntity() const {
    return selectedEntity;
}

void UIManager::renderViewport(ImTextureID viewportTexture) {
    ImGui::Begin("Viewport");
    ImVec2 size = ImGui::GetContentRegionAvail();
    ImGui::Image(viewportTexture, size);
    ImGui::End();
}

void UIManager::renderTransformPanel() {
    // Transform panel at viewport top
    ImGui::Begin("Transform Options");

    if (ImGui::CollapsingHeader("Transform Properties", ImGuiTreeNodeFlags_DefaultOpen)) {
        if (selectedEntity != 0 && ecsWorld) {
            auto entity = ecsWorld->getEntity(selectedEntity);
            if (entity) {
                auto transform = entity->getComponent<Transform>();
                if (transform) {
                    ImGui::Text("Position:");
                    ImGui::DragFloat3("X", &transform->position.x, 0.1f);
                    ImGui::DragFloat3("Y", &transform->position.y, 0.1f);
                    ImGui::DragFloat3("Z", &transform->position.z, 0.1f);

                    ImGui::Text("Rotation:");
                    ImGui::DragFloat3("X", &transform->rotation.x, 1.0f);
                    ImGui::DragFloat3("Y", &transform->rotation.y, 1.0f);
                    ImGui::DragFloat3("Z", &transform->rotation.z, 1.0f);

                    ImGui::Text("Scale:");
                    ImGui::DragFloat3("X", &transform->scale.x, 0.1f);
                    ImGui::DragFloat3("Y", &transform->scale.y, 0.1f);
                    ImGui::DragFloat3("Z", &transform->scale.z, 0.1f);
                }
            }
        }

        ImGui::End();
    }
}

void UIManager::renderHierarchy() {
    ImGui::Begin("Hierarchy");

    if (ecsWorld) {
        for (const auto& pair : ecsWorld->getEntities()) {
            EntityID entityId = pair.first;
            auto entity = pair.second;

            // Simple entity name - in a real engine you'd have a Name component
            std::string entityName = "Entity " + std::to_string(entityId);

            bool isSelected = (selectedEntity == entityId);
            if (ImGui::Selectable(entityName.c_str(), isSelected)) {
                setSelectedEntity(entityId);
            }
        }
    }

    ImGui::End();
}

void UIManager::renderProperties() {
    ImGui::Begin("Properties");

    if (selectedEntity != 0 && ecsWorld) {
        auto entity = ecsWorld->getEntity(selectedEntity);
        if (entity) {
            ImGui::Text("Entity ID: %u", selectedEntity);

            // Transform component
            auto transform = entity->getComponent<Transform>();
            if (transform) {
                if (ImGui::CollapsingHeader("Transform", ImGuiTreeNodeFlags_DefaultOpen)) {
                    ImGui::DragFloat3("Position", &transform->position.x, 0.1f);
                    ImGui::DragFloat3("Rotation", &transform->rotation.x, 1.0f);
                    ImGui::DragFloat3("Scale", &transform->scale.x, 0.1f);
                }
            }

            // Renderable component
            auto renderable = entity->getComponent<RenderableComponent>();
            if (renderable) {
                if (ImGui::CollapsingHeader("Renderable")) {
                    ImGui::Checkbox("Visible", &renderable->visible);
                }
            }

            // Mesh component
            auto mesh = entity->getComponent<Mesh>();
            if (mesh) {
                if (ImGui::CollapsingHeader("Mesh")) {
                    ImGui::Text("Path: %s", mesh->meshPath.c_str());
                }
            }
        }
    } else {
        ImGui::Text("No entity selected");
    }

    ImGui::End();
}

void UIManager::renderContentExplorer() {
    ImGui::Begin("Content Explorer");
    ImGui::BeginChild("EntityList", ImVec2(0, 0), true);
    for (const auto& pair : ecsWorld->getEntities()) {
        EntityID entityId = pair.first;
        auto entity = pair.second;

        std::string entityName = "Entity " + std::to_string(entityId);
        ImGui::Selectable(entityName.c_str());
    }
    ImGui::EndChild();
    ImGui::BeginChild("EntityDetails", ImVec2(0, 0), true);
    if (selectedEntity != 0 && ecsWorld) {
        auto entity = ecsWorld->getEntity(selectedEntity);
        if (entity) {
            auto transform = entity->getComponent<Transform>();
            if (transform) {
                ImGui::Text("Position: (%f, %f, %f)", transform->position.x,
                           transform->position.y, transform->position.z);
                ImGui::Text("Rotation: (%f, %f, %f)", transform->rotation.x,
                           transform->rotation.y, transform->rotation.z);
                ImGui::Text("Scale: (%f, %f, %f)", transform->scale.x,
                           transform->scale.y, transform->scale.z);
            }
        }
    }
    ImGui::End();
}

#include "ui_manager.h"
#include <imgui.h>
#include <imgui_internal.h>
#include <imgui_impl_vulkan.h>
#include "../ecs/ecs.h"
#include "../ecs/components.h"
#include <glm/glm.hpp>
#include <filesystem>

UIManager::UIManager(Scene* scene) : m_Scene(scene) {}

void UIManager::render(ImTextureID viewportTexture) {
    renderNewProjectDialog();
    renderOpenProjectDialog();
    renderMenuBar();
    
    static bool dockspaceInitialized = false;
    static ImGuiID dockspaceID = 0;

    if (!dockspaceInitialized) {
        
        
        dockspaceID = ImGui::GetID("MyDockspace");
        ImGuiViewport* viewport = ImGui::GetMainViewport();
        
        if (ImGui::DockBuilderGetNode(dockspaceID) == nullptr) {
            ImGui::DockBuilderAddNode(dockspaceID, ImGuiDockNodeFlags_DockSpace);
            ImGui::DockBuilderSetNodeSize(dockspaceID, viewport->Size);
            
            ImGuiID dockMain = dockspaceID;
            ImGuiID dockLeft = 0;
            ImGui::DockBuilderSplitNode(dockMain, ImGuiDir_Left, 0.2f, &dockLeft, &dockMain);
            
            ImGuiID dockLeftTop = 0;
            ImGuiID dockLeftBottom = 0;
            ImGui::DockBuilderSplitNode(dockLeft, ImGuiDir_Up, 0.5f, &dockLeftTop, &dockLeftBottom);
            
            ImGui::DockBuilderDockWindow("Viewport", dockMain);
            ImGui::DockBuilderDockWindow("Hierarchy", dockLeftBottom);
            ImGui::DockBuilderDockWindow("Properties", dockLeftTop);
            ImGui::DockBuilderDockWindow("Transform", dockLeftTop);
            ImGui::DockBuilderDockWindow("Content Explorer", dockLeftBottom);
            ImGui::DockBuilderFinish(dockspaceID);
        }
        
        dockspaceInitialized = true;
    }

    ImGui::DockSpaceOverViewport(dockspaceID, ImGui::GetMainViewport(), ImGuiDockNodeFlags_PassthruCentralNode);

    renderViewport(viewportTexture);
    renderTransformPanel();
    renderHierarchy();
    renderProperties();
    renderContentExplorer();
}

void UIManager::setSelectedEntity(Entity entity) {
    selectedEntity = entity;
}

Entity UIManager::getSelectedEntity() const {
    return selectedEntity;
}

void UIManager::setOnAssetDropped(std::function<void(const std::string&)> callback) {
    onAssetDropped = callback;
}

void UIManager::setProjectManager(ProjectManager* projManager) {
    projectManager = projManager;
}

void UIManager::openProject(const std::string& path) {
    if (projectManager) {
        projectManager->openProject(path);
    }
}

void UIManager::setWindow(GLFWwindow* win) {
    window = win;
}

void UIManager::renderViewport(ImTextureID viewportTexture) {
    ImGui::Begin("Viewport", nullptr, ImGuiWindowFlags_NoTitleBar);
    ImVec2 size = ImGui::GetContentRegionAvail();
    ImGui::Image(viewportTexture, size);
    
    if (ImGui::BeginDragDropTarget()) {
        const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("ASSET_DROP");
        if (payload != nullptr && onAssetDropped) {
            const char* assetPath = static_cast<const char*>(payload->Data);
            onAssetDropped(std::string(assetPath));
        }
        ImGui::EndDragDropTarget();
    }
    
    ImGui::End();
}

void UIManager::renderTransformPanel() {
    ImGui::Begin("Transform", nullptr, ImGuiWindowFlags_AlwaysAutoResize);
    
    if (selectedEntity != entt::null && m_Scene && m_Scene->getRegistry().valid(selectedEntity)) {
        if (m_Scene->getRegistry().all_of<Transform>(selectedEntity)) {
            auto& transform = m_Scene->getRegistry().get<Transform>(selectedEntity);
            
            ImGui::Text("Position:");
            ImGui::DragFloat3("##pos", &transform.position.x, 0.1f);

            ImGui::Text("Rotation:");
            ImGui::DragFloat3("##rot", &transform.rotation.x, 1.0f);

            ImGui::Text("Scale:");
            ImGui::DragFloat3("##scale", &transform.scale.x, 0.1f);
        }
    }
    
    ImGui::End();
}

void UIManager::renderHierarchy() {
    ImGui::Begin("Hierarchy", nullptr, ImGuiWindowFlags_AlwaysAutoResize);

    if (m_Scene) {
        for (auto entity : m_Scene->getAllEntities()) {
            std::string entityName = "Entity " + std::to_string(static_cast<uint32_t>(entity));

            bool isSelected = (selectedEntity == entity);
            if (ImGui::Selectable(entityName.c_str(), isSelected)) {
                setSelectedEntity(entity);
            }
        }
    }

    ImGui::End();
}

void UIManager::renderProperties() {
    ImGui::Begin("Properties", nullptr, ImGuiWindowFlags_AlwaysAutoResize);

    if (selectedEntity != entt::null && m_Scene && m_Scene->getRegistry().valid(selectedEntity)) {
        ImGui::Text("Entity ID: %u", static_cast<uint32_t>(selectedEntity));

        auto renderComponent = [this](auto&& component, const char* name) {
            if (ImGui::CollapsingHeader(name, ImGuiTreeNodeFlags_DefaultOpen)) {
                ecs::renderComponentProperties(component);
            }
        };

        if (m_Scene->getRegistry().all_of<Transform>(selectedEntity)) {
            renderComponent(m_Scene->getRegistry().get<Transform>(selectedEntity), "Transform");
        }

        if (m_Scene->getRegistry().all_of<Renderable>(selectedEntity)) {
            renderComponent(m_Scene->getRegistry().get<Renderable>(selectedEntity), "Renderable");
        }

        if (m_Scene->getRegistry().all_of<Mesh>(selectedEntity)) {
            renderComponent(m_Scene->getRegistry().get<Mesh>(selectedEntity), "Mesh");
        }

        if (m_Scene->getRegistry().all_of<Camera>(selectedEntity)) {
            renderComponent(m_Scene->getRegistry().get<Camera>(selectedEntity), "Camera");
        }
    } else {
        ImGui::Text("No entity selected");
    }

    ImGui::End();
}

void UIManager::renderContentExplorer() {
    ImGui::Begin("Content Explorer", nullptr, ImGuiWindowFlags_AlwaysAutoResize);

    if (projectManager && projectManager->hasProject()) {
        std::vector<std::string> models = projectManager->getModelFiles();
        
        ImGui::Text("Models:");
        for (const auto& modelPath : models) {
            std::string filename = std::filesystem::path(modelPath).filename().string();
            
            ImGui::Selectable(("📦 " + filename).c_str(), false, ImGuiSelectableFlags_AllowOverlap);
            
            if (ImGui::BeginDragDropSource(ImGuiDragDropFlags_None)) {
                ImGui::SetDragDropPayload("ASSET_DROP", modelPath.c_str(), modelPath.length() + 1);
                ImGui::Text("Drop: %s", filename.c_str());
                ImGui::EndDragDropSource();
            }
        }

        std::vector<std::string> textures = projectManager->getTextureFiles();
        
        ImGui::Separator();
        ImGui::Text("Textures:");
        for (const auto& texPath : textures) {
            std::string filename = std::filesystem::path(texPath).filename().string();
            
            ImGui::Selectable(("🖼️ " + filename).c_str(), false, ImGuiSelectableFlags_AllowOverlap);
            
            if (ImGui::BeginDragDropSource(ImGuiDragDropFlags_None)) {
                ImGui::SetDragDropPayload("ASSET_DROP", texPath.c_str(), texPath.length() + 1);
                ImGui::Text("Drop: %s", filename.c_str());
                ImGui::EndDragDropSource();
            }
        }
    } else {
        ImGui::Text("No project open.\nOpen a project to see assets.");
    }

    ImGui::End();
}

void UIManager::renderMenuBar() {
    if (ImGui::BeginMainMenuBar()) {
        if (ImGui::BeginMenu("Project")) {
            if (ImGui::MenuItem("New Project", "Ctrl+N")) {
                showNewProjectDialog = true;
            }
            if (ImGui::MenuItem("Open Project", "Ctrl+O")) {
                showOpenProjectDialog = true;
            }
            ImGui::Separator();
            if (ImGui::MenuItem("Save Project", "Ctrl+S")) {
                if (onSaveProject) onSaveProject();
            }
            ImGui::Separator();
            if (ImGui::MenuItem("Exit", "Alt+F4")) {
                if (onExit) onExit();
            }
            ImGui::EndMenu();
        }
        if (ImGui::BeginMenu("File")) {
            if (ImGui::MenuItem("Import Model...", "Ctrl+I")) {
            }
            if (ImGui::MenuItem("Import Texture...", "Ctrl+T")) {
            }
            ImGui::EndMenu();
        }
        if (ImGui::BeginMenu("View")) {
            ImGui::MenuItem("Viewport", NULL, true);
            ImGui::MenuItem("Hierarchy", NULL, true);
            ImGui::MenuItem("Properties", NULL, true);
            ImGui::MenuItem("Content Explorer", NULL, true);
            ImGui::EndMenu();
        }
        if (ImGui::BeginMenu("Help")) {
            if (ImGui::MenuItem("About")) {
            }
            ImGui::EndMenu();
        }
        ImGui::EndMainMenuBar();
    }
}

void UIManager::renderNewProjectDialog() {
    if (!showNewProjectDialog) return;
    
    ImGui::OpenPopup("New Project");
    if (ImGui::BeginPopupModal("New Project", &showNewProjectDialog, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::Text("Project Name:");
        ImGui::InputText("##name", newProjectName, IM_ARRAYSIZE(newProjectName));
        
        ImGui::Text("Location:");
        ImGui::InputText("##path", newProjectPath, IM_ARRAYSIZE(newProjectPath));
        
        ImGui::Separator();
        
        if (ImGui::Button("Create")) {
            if (projectManager && strlen(newProjectName) > 0 && strlen(newProjectPath) > 0) {
                std::string fullPath = std::string(newProjectPath) + "/" + newProjectName;
                projectManager->createNewProject(newProjectName, fullPath);
                if (onNewProject) onNewProject();
            }
            showNewProjectDialog = false;
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel")) {
            showNewProjectDialog = false;
        }
        
        ImGui::EndPopup();
    }
}

void UIManager::renderOpenProjectDialog() {
    if (!showOpenProjectDialog) return;
    
    ImGui::OpenPopup("Open Project");
    if (ImGui::BeginPopupModal("Open Project", &showOpenProjectDialog, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::Text("Select Project Folder:");
        ImGui::InputText("##openpath", projectPathBuffer, IM_ARRAYSIZE(projectPathBuffer));
        
        ImGui::Separator();
        
        if (ImGui::Button("Open")) {
            if (projectManager && strlen(projectPathBuffer) > 0) {
                projectManager->openProject(projectPathBuffer);
                if (onOpenProject) onOpenProject();
            }
            showOpenProjectDialog = false;
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel")) {
            showOpenProjectDialog = false;
        }
        
        ImGui::EndPopup();
    }
}

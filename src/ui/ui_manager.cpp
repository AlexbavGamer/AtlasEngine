#define IMGUI_DEFINE_MATH_OPERATORS
#include "ui_manager.h"
#include <imgui.h>
#include <imgui_internal.h>
#include <imgui_impl_vulkan.h>
#include <ImGuiFileDialog.h>
#include <ImGuizmo.h>
#include "../ecs/ecs.h"
#include "../ecs/components.h"
#include <glm/glm.hpp>
#include <glm/gtc/type_ptr.hpp>
#define GLM_ENABLE_EXPERIMENTAL
#include <glm/gtx/matrix_decompose.hpp>
#include <glm/gtx/transform.hpp>
#include <filesystem>
#include <iostream>

UIManager::UIManager(Scene* scene) : m_Scene(scene) {}

void UIManager::renderToolbar() {
    ImGui::Begin("Toolbar", nullptr, ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoScrollbar);
    
    ImGui::Text("Transform: ");
    ImGui::SameLine();
    
    bool isTranslate = (m_TransformMode == TransformMode::Translate);
    bool isRotate = (m_TransformMode == TransformMode::Rotate);
    bool isScale = (m_TransformMode == TransformMode::Scale);
    
    ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 0);
    if (ImGui::Button(isTranslate ? "T [Active]" : "T", ImVec2(40, 25))) {
        m_TransformMode = TransformMode::Translate;
    }
    ImGui::SameLine();
    if (ImGui::Button(isRotate ? "R [Active]" : "R", ImVec2(40, 25))) {
        m_TransformMode = TransformMode::Rotate;
    }
    ImGui::SameLine();
    if (ImGui::Button(isScale ? "S [Active]" : "S", ImVec2(40, 25))) {
        m_TransformMode = TransformMode::Scale;
    }
    ImGui::PopStyleVar();
    
    ImGui::End();
}

void UIManager::render(ImTextureID viewportTexture) {
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
            
            ImGuiID dockTop = 0;
            ImGuiID dockCenter = 0;
            ImGui::DockBuilderSplitNode(dockMain, ImGuiDir_Up, 0.05f, &dockTop, &dockCenter);
            
            ImGui::DockBuilderDockWindow("Toolbar", dockTop);
            ImGui::DockBuilderDockWindow("Viewport", dockCenter);
            ImGui::DockBuilderDockWindow("Hierarchy", dockLeftBottom);
            ImGui::DockBuilderDockWindow("Properties", dockLeftTop);
            ImGui::DockBuilderDockWindow("Content Explorer", dockLeftBottom);
            ImGui::DockBuilderFinish(dockspaceID);
        }
        
        dockspaceInitialized = true;
    }

    ImGui::DockSpaceOverViewport(dockspaceID, ImGui::GetMainViewport(), ImGuiDockNodeFlags_PassthruCentralNode);

    renderToolbar();
    renderViewport(viewportTexture);
    renderHierarchy();
    renderProperties();
    renderContentExplorer();
    
    renderNewProjectDialog();
    renderOpenProjectDialog();
    renderMenuBar();
    
    if (ImGuiFileDialog::Instance()->Display("OpenProject")) {
        if (ImGuiFileDialog::Instance()->IsOk()) {
            std::string folderPath = ImGuiFileDialog::Instance()->GetCurrentPath();
            ImGuiFileDialog::Instance()->Close();
            if (projectManager && !folderPath.empty()) {
                projectManager->openProject(folderPath);
            }
        }
        ImGuiFileDialog::Instance()->Close();
    }
    
    if (ImGuiFileDialog::Instance()->Display("SaveProject")) {
        if (ImGuiFileDialog::Instance()->IsOk()) {
            std::string filePath = ImGuiFileDialog::Instance()->GetFilePathName();
            ImGuiFileDialog::Instance()->Close();
            if (onSaveProject) onSaveProject();
        }
        ImGuiFileDialog::Instance()->Close();
    }
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

void UIManager::setCameraMatrices(glm::mat4 view, glm::mat4 proj) {
    m_ViewMatrix = view;
    m_ProjMatrix = proj;
}

void UIManager::setCameraController(void* controller) {
    m_CameraController = controller;
}

void UIManager::setRenderer(Renderer* r) {
    renderer = r;
}

void UIManager::renderViewport(ImTextureID viewportTexture) {
    ImGui::Begin("Viewport", nullptr, ImGuiWindowFlags_NoTitleBar);
    
    ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 10);
    ImVec2 imagePos = ImGui::GetCursorPos();
    ImVec2 viewportSize = ImGui::GetContentRegionAvail();
    ImGui::Image(viewportTexture, viewportSize);
    
    if (selectedEntity != entt::null && m_Scene && m_Scene->getRegistry().all_of<Transform>(selectedEntity)) {
        auto& transform = m_Scene->getRegistry().get<Transform>(selectedEntity);
        glm::mat4 modelMatrix = transform.getModelMatrix();
        
        ImGuizmo::SetOrthographic(false);
        ImGuizmo::SetDrawlist(ImGui::GetWindowDrawList());
        
        ImVec2 windowPos = ImGui::GetWindowPos();
        float x = windowPos.x + imagePos.x;
        float y = windowPos.y + imagePos.y;
        
        ImGuizmo::SetRect(x, y, viewportSize.x, viewportSize.y);
        
        ImGuizmo::OPERATION operation = ImGuizmo::TRANSLATE;
        if (m_TransformMode == TransformMode::Rotate) operation = ImGuizmo::ROTATE;
        if (m_TransformMode == TransformMode::Scale) operation = ImGuizmo::SCALE;
        
        glm::mat4 deltaMatrix = glm::mat4(1.0f);
        bool used = ImGuizmo::Manipulate(
            glm::value_ptr(m_ViewMatrix),
            glm::value_ptr(m_ProjMatrix),
            operation,
            ImGuizmo::LOCAL,
            glm::value_ptr(modelMatrix),
            glm::value_ptr(deltaMatrix),
            nullptr
        );
        
        if (used) {
            glm::vec3 translation, scale;
            glm::vec3 rotation;
            glm::quat quat;
            
            glm::vec3 skew;
            glm::vec4 perspective;
            glm::decompose(modelMatrix, scale, quat, translation, skew, perspective);
            rotation = glm::eulerAngles(quat);
            
            transform.position = translation;
            transform.rotation = glm::degrees(rotation);
            transform.scale = scale;
        }
    }
    
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
                ecs::renderComponentProperties(component, static_cast<uint32_t>(selectedEntity));
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

static std::vector<std::string> folderStack;

std::string getFileIcon(const std::string& filename, bool isFolder) {
    if (isFolder) return "[D]";
    std::string ext = fs::path(filename).extension().string();
    if (ext == ".fbx" || ext == ".gltf" || ext == ".glb" || ext == ".obj" || ext == ".dae") return "[M]";
    if (ext == ".png" || ext == ".jpg" || ext == ".jpeg" || ext == ".bmp" || ext == ".tga" || ext == ".hdr") return "[T]";
    if (ext == ".mat" || ext == ".material") return "[*]";
    if (ext == ".scene" || ext == ".json") return "[S]";
    return "[F]";
}

ProjectManager::FileEntry getFolderAtPath(ProjectManager* pm, const std::vector<std::string>& path) {
    auto tree = pm->getAssetTree();
    
    if (path.empty()) return tree;
    
    ProjectManager::FileEntry* current = &tree;
    
    for (const auto& folderName : path) {
        bool found = false;
        for (auto& child : current->children) {
            if (child.name == folderName && child.isFolder) {
                current = &child;
                found = true;
                break;
            }
        }
        if (!found) return tree;
    }
    return *current;
}

void UIManager::renderContentExplorer() {
    ImGui::Begin("Content Explorer", nullptr, ImGuiWindowFlags_AlwaysAutoResize);

    if (projectManager && projectManager->hasProject()) {
        if (ImGui::Button("Home")) {
            folderStack.clear();
        }
        
        if (!folderStack.empty()) {
            ImGui::SameLine();
            if (ImGui::Button("..")) {
                folderStack.pop_back();
            }
        }
        
        std::string pathDisplay = "assets/";
        for (const auto& f : folderStack) {
            pathDisplay += f + "/";
        }
        ImGui::Text("%s", pathDisplay.c_str());
        ImGui::Separator();
        
        auto currentFolder = getFolderAtPath(projectManager, folderStack);
        
        // std::cout << "[UI] Current folder children: " << currentFolder.children.size() << std::endl;
        
        bool hasItems = false;
        for (const auto& child : currentFolder.children) {
            hasItems = true;
            // std::cout << "  Rendering: " << child.name << " (isFolder=" << child.isFolder << ")" << std::endl;
            
            if (child.isFolder) {
                std::string icon = getFileIcon(child.name, true);
                std::string label = icon + " " + child.name;
                
                ImGui::Selectable(label.c_str(), false, ImGuiSelectableFlags_SpanAllColumns);
                
                if (ImGui::IsItemClicked()) {
                    folderStack.push_back(child.name);
                }
            } else {
                std::string icon = getFileIcon(child.name, false);
                std::string label = icon + " " + child.name;
                
                if (ImGui::Selectable(label.c_str(), false, ImGuiSelectableFlags_SpanAllColumns | ImGuiSelectableFlags_AllowOverlap)) {
                }
                
                if (ImGui::BeginDragDropSource(ImGuiDragDropFlags_None)) {
                    ImGui::SetDragDropPayload("ASSET_DROP", child.relativePath.c_str(), child.relativePath.length() + 1);
                    ImGui::Text("%s", label.c_str());
                    ImGui::EndDragDropSource();
                }
            }
        }
        
        if (!hasItems) {
            ImGui::Text("Empty folder");
        }
        
        if (ImGui::BeginDragDropTarget()) {
            if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("FILE_DROP")) {
                if (payload->DataSize > 0 && onAssetDropped) {
                    const char* droppedPath = static_cast<const char*>(payload->Data);
                    onAssetDropped(droppedPath);
                }
            }
            ImGui::EndDragDropTarget();
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
                IGFD::FileDialogConfig config;
                config.path = ".";
                ImGuiFileDialog::Instance()->OpenDialog("OpenProject", "Open Project Folder", nullptr, config);
            }
            ImGui::Separator();
            if (ImGui::MenuItem("Save Project", "Ctrl+S")) {
                IGFD::FileDialogConfig saveConfig;
                saveConfig.path = ".";
                ImGuiFileDialog::Instance()->OpenDialog("SaveProject", "Save Project", nullptr, saveConfig);
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
            ImGui::Separator();
            if (renderer) {
                glm::vec4 color = renderer->getClearColor();
                if (ImGui::ColorEdit3("Background", &color.x, ImGuiColorEditFlags_Float)) {
                    renderer->setClearColor(color);
                }
            }
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
    if (ImGuiFileDialog::Instance()->Display("SelectNewProjectFolder")) {
        if (ImGuiFileDialog::Instance()->IsOk()) {
            std::string folderPath = ImGuiFileDialog::Instance()->GetFilePathName();
            ImGuiFileDialog::Instance()->Close();
            strncpy(newProjectPath, folderPath.c_str(), sizeof(newProjectPath) - 1);
            showNewProjectDialog = true;
        }
        ImGuiFileDialog::Instance()->Close();
    }
    
    if (!showNewProjectDialog) return;
    
    ImGui::SetNextWindowSize(ImVec2(400, 200), ImGuiCond_Always);
    ImGui::OpenPopup("New Project");
    if (ImGui::BeginPopupModal("New Project", &showNewProjectDialog)) {
        ImGui::Text("Project Name:");
        ImGui::InputText("##name", newProjectName, IM_ARRAYSIZE(newProjectName));
        
        ImGui::Text("Location:");
        ImGui::InputText("##path", newProjectPath, IM_ARRAYSIZE(newProjectPath));
        ImGui::SameLine();
        if (ImGui::Button("Browse...")) {
            showNewProjectDialog = false;
            IGFD::FileDialogConfig config;
            config.path = ".";
            ImGuiFileDialog::Instance()->OpenDialog("SelectNewProjectFolder", "Select Project Folder", nullptr, config);
        }
        
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

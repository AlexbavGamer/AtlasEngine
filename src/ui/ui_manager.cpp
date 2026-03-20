#define IMGUI_DEFINE_MATH_OPERATORS
#include "ui_manager.h"
#include <imgui.h>
#include <imgui_internal.h>
#include <imgui_impl_vulkan.h>
#include <ImGuiFileDialog.h>
#include <ImGuizmo.h>
#include "../ecs/ecs.h"
#include "../ecs/components.h"
#include "../assets/asset_manager.h"
#include "../ecs/components/components.h"
#include <glm/glm.hpp>
#include <cstring>
#include <glm/gtc/type_ptr.hpp>
#include <cfloat>
#define GLM_ENABLE_EXPERIMENTAL
#include <glm/gtx/matrix_decompose.hpp>
#include <glm/gtx/transform.hpp>
#include <filesystem>
#include <iostream>

UIManager::UIManager(Atlas::Scene* scene) : m_Scene(scene) {}

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

    renderProfilerWindow();
    
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

void UIManager::setProjectManager(::ProjectManager* projManager) {
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

void UIManager::setRenderer(Atlas::Renderer* r) {
    renderer = r;
    if (renderer) {
        m_VSyncEnabled = renderer->isVSyncEnabled();
    }
}

void UIManager::renderViewport(ImTextureID viewportTexture) {
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
    ImGui::Begin("Viewport", nullptr, ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoScrollbar);
    
    ImVec2 contentSize = ImGui::GetContentRegionAvail();
    ImVec2 windowPos = ImGui::GetWindowPos();
    
    ImGui::Image(viewportTexture, contentSize);
    
    ImVec2 imageEndPos = ImGui::GetCursorPos();
    
    if (selectedEntity != entt::null && m_Scene && m_Scene->getRegistry().all_of<Transform>(selectedEntity)) {
        auto& transform = m_Scene->getRegistry().get<Transform>(selectedEntity);
        glm::mat4 localModel = transform.getModelMatrix();
        glm::mat4 worldModel = m_Scene->getWorldTransform(selectedEntity);

        // If entity has parent, keep parent matrix to compute local transform after manipulate.
        glm::mat4 parentWorld = glm::mat4(1.0f);
        if (m_Scene->getRegistry().all_of<Atlas::ECS::ParentComponent>(selectedEntity)) {
            entt::entity parent = m_Scene->getRegistry().get<Atlas::ECS::ParentComponent>(selectedEntity).parent;
            if (parent != entt::null) {
                parentWorld = m_Scene->getWorldTransform(parent);
            }
        }

        ImGuizmo::SetOrthographic(false);
        ImGuizmo::SetDrawlist();
        ImGuizmo::SetRect(windowPos.x, windowPos.y, contentSize.x, contentSize.y);
        
        ImGuizmo::OPERATION operation = ImGuizmo::TRANSLATE;
        if (m_TransformMode == TransformMode::Rotate) operation = ImGuizmo::ROTATE;
        if (m_TransformMode == TransformMode::Scale) operation = ImGuizmo::SCALE;

        glm::mat4 deltaMatrix = glm::mat4(1.0f);
        ImGuizmo::Manipulate(
            glm::value_ptr(m_ViewMatrix),
            glm::value_ptr(m_ProjMatrix),
            operation,
            ImGuizmo::WORLD,
            glm::value_ptr(worldModel),
            glm::value_ptr(deltaMatrix)
        );

        m_GizmoUsing = ImGuizmo::IsUsing();

        if (m_GizmoUsing) {
            glm::vec3 translation;
            glm::vec3 skew;
            glm::vec4 perspective;
            glm::quat orientation;
            glm::vec3 newScale;
            
            if (glm::decompose(worldModel, newScale, orientation, translation, skew, perspective)) {
                glm::mat4 editedWorld = glm::translate(glm::mat4(1.0f), translation);
                editedWorld *= glm::mat4_cast(orientation);
                editedWorld = glm::scale(editedWorld, newScale);

                glm::mat4 editedLocal = parentWorld == glm::mat4(1.0f) ? editedWorld : glm::inverse(parentWorld) * editedWorld;

                glm::vec3 localPos;
                glm::quat localOrient;
                glm::vec3 localScale;
                if (glm::decompose(editedLocal, localScale, localOrient, localPos, skew, perspective)) {
                    transform.position = localPos;
                    transform.rotation = glm::eulerAngles(localOrient);
                    transform.scale = localScale;
                }
            }
        }
    } else {
        m_GizmoUsing = false;
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
    ImGui::PopStyleVar();
}

void UIManager::renderHierarchy() {
    ImGui::Begin("Hierarchy", nullptr, ImGuiWindowFlags_AlwaysAutoResize);

    if (m_Scene) {
        auto& registry = m_Scene->getRegistry();

        const auto renderEntityRecursively = [&](auto&& self, entt::entity entity, std::vector<Entity>& pendingDelete) -> void {
            std::string entityName;
            if (registry.all_of<Atlas::ECS::TagComponent>(entity)) {
                entityName = registry.get<Atlas::ECS::TagComponent>(entity).name;
            } else {
                entityName = "Entity " + std::to_string(static_cast<uint32_t>(entity));
            }

            const auto children = m_Scene->getChildren(entity);
            ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_SpanAvailWidth;
            if (entity == selectedEntity) flags |= ImGuiTreeNodeFlags_Selected;
            if (children.empty()) flags |= ImGuiTreeNodeFlags_Leaf | ImGuiTreeNodeFlags_NoTreePushOnOpen;

            ImGui::PushID(static_cast<int>(static_cast<uint32_t>(entity)));
            bool nodeOpen = ImGui::TreeNodeEx(entityName.c_str(), flags);

            if (ImGui::IsItemClicked()) {
                setSelectedEntity(entity);
            }
            if (ImGui::IsItemClicked(ImGuiMouseButton_Right)) {
                bool isCameraEntity = m_Scene->getRegistry().all_of<Camera>(entity);
                if (!isCameraEntity) {
                    pendingDelete.push_back(entity);
                }
            }

            if (nodeOpen && !children.empty()) {
                for (auto child : children) {
                    self(self, child, pendingDelete);
                }
                ImGui::TreePop();
            }
            ImGui::PopID();
        };

        std::vector<Entity> pendingDelete;

        auto roots = m_Scene->getRootEntities();
        if (roots.empty()) {
            for (auto entity : m_Scene->getAllEntities()) {
                renderEntityRecursively(renderEntityRecursively, entity, pendingDelete);
            }
        } else {
            for (auto root : roots) {
                renderEntityRecursively(renderEntityRecursively, root, pendingDelete);
            }
        }

        for (auto entity : pendingDelete) {
            if (m_Scene->getRegistry().valid(entity)) {
                if (selectedEntity == entity) {
                    selectedEntity = entt::null;
                }
                m_Scene->destroyEntity(entity);
            }
        }
    }

    ImGui::End();
}

void UIManager::renderProperties() {
    ImGui::Begin("Properties", nullptr, ImGuiWindowFlags_AlwaysAutoResize);

    if (selectedEntity != entt::null && m_Scene && m_Scene->getRegistry().valid(selectedEntity)) {
        ImGui::Text("Entity ID: %u", static_cast<uint32_t>(selectedEntity));

        bool isCameraEntity = m_Scene->getRegistry().all_of<Camera>(selectedEntity);
        if (isCameraEntity) {
            ImGui::TextColored(ImVec4(1.0f, 0.5f, 0.0f, 1.0f), "Camera entity cannot be deleted");
        }

        ImGui::BeginDisabled(isCameraEntity);
        if (ImGui::Button("Delete Entity")) {
            m_Scene->destroyEntity(selectedEntity);
            selectedEntity = entt::null;
        }
        ImGui::EndDisabled();

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

        if (m_Scene->getRegistry().all_of<::Mesh>(selectedEntity)) {
            if (ImGui::CollapsingHeader("Mesh", ImGuiTreeNodeFlags_DefaultOpen)) {
                ecs::renderComponentProperties(m_Scene->getRegistry().get<::Mesh>(selectedEntity), static_cast<uint32_t>(selectedEntity));

                // Material/texture controls (kept under Mesh for convenience)
                if (m_Scene->getRegistry().all_of<Atlas::ECS::MaterialComponent>(selectedEntity)) {
                    auto& mat = m_Scene->getRegistry().get<Atlas::ECS::MaterialComponent>(selectedEntity);

                    ImGui::Separator();
                    ImGui::Text("Material");

                    ImGui::ColorEdit4("Base Color##Mat", &mat.baseColor.x, ImGuiColorEditFlags_Float);
                    ImGui::DragFloat("Metallic##Mat", &mat.metallic, 0.01f, 0.0f, 1.0f);
                    ImGui::DragFloat("Roughness##Mat", &mat.roughness, 0.01f, 0.0f, 1.0f);

                    ImGui::Checkbox("Use Albedo Texture##Mat", &mat.useAlbedoTexture);

                    uint32_t entId = static_cast<uint32_t>(selectedEntity);
                    if (m_MaterialEditEntityId != entId) {
                        m_MaterialEditEntityId = entId;
                        std::memset(m_AlbedoTexturePathBuf, 0, sizeof(m_AlbedoTexturePathBuf));
                        if (!mat.albedoTexturePath.empty()) {
                            std::strncpy(m_AlbedoTexturePathBuf, mat.albedoTexturePath.c_str(), sizeof(m_AlbedoTexturePathBuf) - 1);
                        }
                    }

                    ImGui::SetNextItemWidth(320.0f);
                    ImGui::InputText("Albedo Texture##Mat", m_AlbedoTexturePathBuf, sizeof(m_AlbedoTexturePathBuf));
                    ImGui::SameLine();
                    if (ImGui::Button("Browse##Mat")) {
                        IGFD::FileDialogConfig cfg;
                        cfg.path = ".";
                        ImGuiFileDialog::Instance()->OpenDialog("SelectAlbedoTex", "Select Albedo Texture", ".png,.jpg,.jpeg,.tga,.bmp", cfg);
                    }

                    bool applyTex = false;
                    if (ImGuiFileDialog::Instance()->Display("SelectAlbedoTex")) {
                        if (ImGuiFileDialog::Instance()->IsOk()) {
                            std::string filePath = ImGuiFileDialog::Instance()->GetFilePathName();
                            std::memset(m_AlbedoTexturePathBuf, 0, sizeof(m_AlbedoTexturePathBuf));
                            std::strncpy(m_AlbedoTexturePathBuf, filePath.c_str(), sizeof(m_AlbedoTexturePathBuf) - 1);
                            applyTex = true;
                        }
                        ImGuiFileDialog::Instance()->Close();
                    }

                    if (ImGui::Button("Apply##Mat") || applyTex) {
                        std::string newPath = std::string(m_AlbedoTexturePathBuf);
                        if (mat.useAlbedoTexture && !newPath.empty() && assetManager && renderer) {
                            auto tex = assetManager->loadTexture(Atlas::StringID(newPath + "#srgb"), newPath, Atlas::AssetManager::TextureColorSpace::SRGB);
                            if (tex && tex->isValid()) {
                                uint32_t slot = 0;
                                if (mat.albedoTextureIndex > 0) {
                                    renderer->updateTexture(static_cast<uint32_t>(mat.albedoTextureIndex), tex->getImageView(), tex->getSampler());
                                    slot = static_cast<uint32_t>(mat.albedoTextureIndex);
                                } else {
                                    slot = renderer->bindTexture(tex->getImageView(), tex->getSampler());
                                }

                                if (slot != 0) {
                                    mat.albedoTextureIndex = static_cast<int32_t>(slot);
                                    mat.albedoTextureId = Atlas::StringID(newPath);
                                    mat.albedoTexturePath = newPath;
                                }
                            }
                        }

                        if (!mat.useAlbedoTexture) {
                            mat.albedoTextureIndex = -1;
                            mat.albedoTextureId = Atlas::StringID::null();
                            mat.albedoTexturePath.clear();
                            std::memset(m_AlbedoTexturePathBuf, 0, sizeof(m_AlbedoTexturePathBuf));
                        }
                    }

                    ImGui::SameLine();
                    if (ImGui::Button("Clear##Mat")) {
                        mat.useAlbedoTexture = false;
                        mat.albedoTextureIndex = -1;
                        mat.albedoTextureId = Atlas::StringID::null();
                        mat.albedoTexturePath.clear();
                        std::memset(m_AlbedoTexturePathBuf, 0, sizeof(m_AlbedoTexturePathBuf));
                    }

                    if (!assetManager || !renderer) {
                        ImGui::TextColored(ImVec4(1, 0.6f, 0.2f, 1), "Texture swapping requires AssetManager+Renderer");
                    }
                }
            }
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
    if (ext == ".fbx" || ext == ".gltf" || ext == ".glb" || ext == ".obj" || ext == ".dae" || ext == ".blend") return "[M]";
    if (ext == ".png" || ext == ".jpg" || ext == ".jpeg" || ext == ".bmp" || ext == ".tga" || ext == ".hdr") return "[T]";
    if (ext == ".mat" || ext == ".material") return "[*]";
    if (ext == ".scene" || ext == ".json") return "[S]";
    return "[F]";
}

::ProjectManager::FileEntry getFolderAtPath(::ProjectManager* pm, const std::vector<std::string>& path) {
    auto tree = pm->getAssetTree();
    
    if (path.empty()) return tree;
    
    ::ProjectManager::FileEntry* current = &tree;
    
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

void UIManager::updateProfiler(float deltaTime)
{
    m_FrameTimeMs = deltaTime * 1000.0f;
    m_Fps = deltaTime > 0.0f ? 1.0f / deltaTime : 0.0f;

    m_FrameTimeHistory[m_FrameTimeIndex] = m_FrameTimeMs;
    m_FrameTimeIndex = (m_FrameTimeIndex + 1) % PROFILER_HISTORY;
}

void UIManager::renderProfilerWindow()
{
    if (!m_ShowProfilerWindow) return;

    ImGui::Begin("Profiler", &m_ShowProfilerWindow, ImGuiWindowFlags_AlwaysAutoResize);

    ImGui::Text("FPS: %.1f", m_Fps);
    ImGui::Text("Frame: %.3f ms", m_FrameTimeMs);

    float minTime = FLT_MAX;
    float maxTime = 0.0f;
    for (int i = 0; i < PROFILER_HISTORY; ++i) {
        float v = m_FrameTimeHistory[i];
        if (v > 0.0f) {
            minTime = ImMin(minTime, v);
            maxTime = ImMax(maxTime, v);
        }
    }

    if (minTime == FLT_MAX) minTime = 0.0f;

    ImGui::Text("Frame history (last %d frames)", PROFILER_HISTORY);
    ImGui::PlotLines("##FrameTimes", m_FrameTimeHistory, PROFILER_HISTORY, m_FrameTimeIndex, nullptr, minTime, ImMax(maxTime, 1.0f), ImVec2(320, 80));

    ImGui::SeparatorText("Frame limiting");

    bool prevVsync = m_VSyncEnabled;
    ImGui::Checkbox("VSync", &m_VSyncEnabled);
    if (prevVsync != m_VSyncEnabled && renderer) {
        renderer->setVSyncEnabled(m_VSyncEnabled);
    }

    ImGui::SliderInt("Max FPS (0=Unlimited)", &m_MaxFps, 0, 240);

#ifdef TRACY_ENABLE
    bool connected = tracy::GetProfiler().IsConnected();
    ImGui::Text("Tracy status: %s", connected ? "Connected" : "Disconnected");
#else
    ImGui::Text("Tracy status: disabled (TRACY_ENABLE not set)");
#endif

    ImGui::Checkbox("Show Tracy Connection", &m_ShowTracyConnection);
    ImGui::Checkbox("Show profiler window", &m_ShowProfilerWindow);

    ImGui::End();
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

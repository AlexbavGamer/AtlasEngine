#pragma once

#include <functional>
#include <imgui.h>
#include <imgui_impl_vulkan.h>
#include <GLFW/glfw3.h>
#include <memory>
#include "../ecs/ecs.h"
#include "../scene/scene.h"
#include "../project/project_manager.h"
#include "../renderer/renderer.h"
#include <ImGuiFileDialog.h>
#include <ImGuizmo.h>

using namespace Atlas;

enum class TransformMode { None, Translate, Rotate, Scale };

class UIManager {
public:
    UIManager(Atlas::Scene* scene);

    void render(ImTextureID viewportTexture);
    void setSelectedEntity(Entity entity);
    Entity getSelectedEntity() const;
    
    void setProjectManager(ProjectManager* projManager);
    void setOnAssetDropped(std::function<void(const std::string&)> callback);
    void setRenderer(Renderer* renderer);
    void openProject(const std::string& path);
    void setWindow(GLFWwindow* win);
    void setCameraMatrices(glm::mat4 view, glm::mat4 proj);
    void setCameraController(void* controller);

    TransformMode getTransformMode() const { return m_TransformMode; }
    bool isGizmoUsing() const { return m_GizmoUsing; }

private:
    Atlas::Scene* m_Scene = nullptr;
    Entity selectedEntity = entt::null;
    ProjectManager* projectManager = nullptr;
    Renderer* renderer = nullptr;
    std::function<void(const std::string&)> onAssetDropped;

    void renderToolbar();
    void renderViewport(ImTextureID viewportTexture);
    void renderHierarchy();
    void renderProperties();
    void renderContentExplorer();
    void renderMenuBar();

    void renderNewProjectDialog();
    void renderOpenProjectDialog();

    std::function<void()> onNewProject;
    std::function<void()> onOpenProject;
    std::function<void()> onSaveProject;
    std::function<void()> onExit;

    GLFWwindow* window = nullptr;

    TransformMode m_TransformMode = TransformMode::Translate;
    glm::mat4 m_ViewMatrix = glm::mat4(1.0f);
    glm::mat4 m_ProjMatrix = glm::mat4(1.0f);
    void* m_CameraController = nullptr;
    bool m_GizmoUsing = false;

    bool showNewProjectDialog = false;
    bool showOpenProjectDialog = false;
    char newProjectName[256] = "MyProject";
    char newProjectPath[512] = ".";
    char projectPathBuffer[512] = ".";

    bool showOpenProjectFileDialog = false;
    bool showSaveProjectFileDialog = false;
};

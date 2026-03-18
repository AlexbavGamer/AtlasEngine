#pragma once

#include <functional>
#include <imgui.h>
#include <imgui_impl_vulkan.h>
#include <GLFW/glfw3.h>
#include <memory>
#include "../ecs/ecs.h"
#include "../scene/scene.h"
#include "../project/project_manager.h"

using namespace Atlas;

class UIManager {
public:
    UIManager(Atlas::Scene* scene);

    void render(ImTextureID viewportTexture);
    void setSelectedEntity(Entity entity);
    Entity getSelectedEntity() const;
    
    void setProjectManager(ProjectManager* projManager);
    void setOnAssetDropped(std::function<void(const std::string&)> callback);
    void openProject(const std::string& path);
    void setWindow(GLFWwindow* win);

private:
    Atlas::Scene* m_Scene = nullptr;
    Entity selectedEntity = entt::null;
    ProjectManager* projectManager = nullptr;
    std::function<void(const std::string&)> onAssetDropped;

    void renderViewport(ImTextureID viewportTexture);
    void renderHierarchy();
    void renderProperties();
    void renderContentExplorer();
    void renderTransformPanel();
    void renderMenuBar();

    void renderNewProjectDialog();
    void renderOpenProjectDialog();

    std::function<void()> onNewProject;
    std::function<void()> onOpenProject;
    std::function<void()> onSaveProject;
    std::function<void()> onExit;

    GLFWwindow* window = nullptr;

    bool showNewProjectDialog = false;
    bool showOpenProjectDialog = false;
    char newProjectName[256] = "MyProject";
    char newProjectPath[512] = ".";
    char projectPathBuffer[512] = ".";
};

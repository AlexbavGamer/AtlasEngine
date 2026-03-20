#pragma once

#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

#include <entt/entt.hpp>

#include "editor_viewport.h"
#include "../imgui/imgui_manager.h"
#include "../project/project_manager.h"
#include "../ui/ui_manager.h"
#include "../utils/camera_controller.h"
#include "../utils/model_loader.h"

namespace Atlas {
class Window;
class Renderer;
class AssetManager;
class Scene;

class EditorApp {
public:
    EditorApp();
    ~EditorApp();

    void run();

private:
    struct PendingModel {
        std::shared_ptr<::ModelData> modelData;
        std::string modelName;
        std::string basePath;
        entt::entity placeholderEntity = entt::null;
    };

    void processPendingModels();
    void queueModelImport(const std::string& assetPath);

    std::unique_ptr<Window> m_Window;
    std::unique_ptr<Renderer> m_Renderer;
    std::unique_ptr<AssetManager> m_AssetManager;
    std::unique_ptr<Scene> m_Scene;
    std::unique_ptr<::UIManager> m_UIManager;
    std::unique_ptr<::ProjectManager> m_ProjectManager;
    std::unique_ptr<::CameraController> m_CameraController;
    std::unique_ptr<::ImGuiManager> m_ImGuiManager;
    EditorViewport m_Viewport;

    std::unordered_map<std::string, uint32_t> m_TextureSlots;
    std::vector<PendingModel> m_PendingModels;
    std::mutex m_PendingModelsMutex;
};

}

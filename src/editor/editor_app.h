#pragma once

#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>
#include <filesystem>
#include <deque>

#include <entt/entt.hpp>
#include <glm/glm.hpp>

#include "editor_viewport.h"
#include "../imgui/imgui_manager.h"
#include "../project/project_manager.h"
#include "../ui/ui_manager.h"
#include "../utils/camera_controller.h"
#include "../utils/model_loader.h"

namespace Atlas { class WorldPartition; }

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
    struct ImportOptions {
        float uniformScale = 1.0f;
        bool importAnimations = true;
        bool startPlaying = true;
    };

    struct PendingModel {
        std::shared_ptr<::ModelData> modelData;
        std::string modelName;
        std::string basePath;
        entt::entity placeholderEntity = entt::null;

        ImportOptions importOptions;

        bool loadFailed = false;
        std::string error;

        bool hasRootPosition = false;
        glm::vec3 rootPosition{0.0f};

        bool isWorldChunk = false;
        uint64_t worldCellKey = 0;
    };

    void processPendingModels();

    struct ImportRequest {
        std::string assetPath;
        glm::vec3 rootPosition{0.0f};
        bool isWorldChunk = false;
        uint64_t cellKey = 0;
    };

    void queueModelImport(const std::string& assetPath);
    void queueModelImportAt(const std::string& assetPath, const glm::vec3& rootPosition, bool isWorldChunk, uint64_t cellKey);
    void startModelImportAt(const std::string& assetPath, const glm::vec3& rootPosition, bool isWorldChunk, uint64_t cellKey, const ImportOptions& options);
    void enqueueImportRequest(const ImportRequest& req);
    void renderImportOptionsPopup();

    void updateWorldStreaming();
    void onMeshDestroyed(entt::registry& registry, entt::entity entity);

    void onExternalFileDrop(const std::vector<std::string>& paths);
    glm::vec3 getDefaultSpawnPosition() const;
    std::string importExternalModelToProjectAssets(const std::string& srcPathStr);


    static uint64_t makeCellKey(int x, int z);
    static void decodeCellKey(uint64_t key, int& outX, int& outZ);

    std::unique_ptr<Window> m_Window;
    std::unique_ptr<Renderer> m_Renderer;
    std::unique_ptr<AssetManager> m_AssetManager;
    std::unique_ptr<Scene> m_Scene;
    std::unique_ptr<::UIManager> m_UIManager;
    std::unique_ptr<::ProjectManager> m_ProjectManager;
    std::unique_ptr<::CameraController> m_CameraController;
    std::unique_ptr<::ImGuiManager> m_ImGuiManager;
    EditorViewport m_Viewport;

    std::unique_ptr<WorldPartition> m_WorldPartition;

    std::unordered_map<std::string, uint32_t> m_TextureSlots;
    std::vector<PendingModel> m_PendingModels;
    std::mutex m_PendingModelsMutex;

    // World streaming (cell-based)
    bool m_WorldStreamingEnabled = true;
    float m_WorldCellSize = 256.0f;
    int m_WorldLoadRadius = 3;
    std::string m_WorldChunksSubdir = "world/chunks";
    std::vector<std::string> m_WorldChunkExtensions = {".gltf", ".glb", ".fbx", ".obj", ".dae"};

    std::unordered_set<uint64_t> m_WorldActiveCells;
    std::unordered_set<uint64_t> m_WorldLoadingCells;
    std::unordered_map<uint64_t, Entity> m_WorldCellRoots;
    std::unordered_map<uint64_t, double> m_WorldFailedCells;
    float m_WorldFailRetrySeconds = 2.0f;

    // Import options popup (models).
    std::deque<ImportRequest> m_ImportQueue;
    bool m_ShowImportOptionsPopup = false;
    ImportRequest m_ActiveImport;
    std::string m_ActiveImportFullPath;
    std::string m_ActiveImportModelName;
    ImportOptions m_ActiveImportOptions;
    ImportOptions m_LastImportOptions;
};

}

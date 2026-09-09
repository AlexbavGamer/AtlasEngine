#pragma once

#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>
#include <deque>

#include <entt/entt.hpp>
#include <glm/glm.hpp>

#include "editor_viewport.h"
#include "editor.h"
#include "layer_stack.h"
#include "../imgui/imgui_manager.h"
#include "../project/project_manager.h"
#include "../ui/ui_manager.h"
#include "../utils/camera_controller.h"
#include "../utils/mesh_data.h"
#include "../utils/pbr_texture_sets.h"

namespace Atlas {
class WorldPartition;
namespace Physics { class PhysicsSystem; }
namespace Scripting { class ScriptEngine; }
} // namespace Atlas

#include "../world/culling.h"
#include "../world/lod.h"
#include "../world/hlod.h"
#include "../world/occlusion.h"
#include "../world/city_generator.h"

namespace Atlas {
class Window;
class Renderer;
class AssetManager;
class Scene;

class EditorApp {
public:
    friend class WorldStreamingLayer;
    friend class HLODViewerLayer;

    EditorApp();
    ~EditorApp();

    void run();

private:
    struct ImportOptions {
        float uniformScale = 1.0f;
        glm::vec3 rotationEulerDeg{0.0f};
        bool importAnimations = true;
        bool startPlaying = true;
        bool loadTextures = true;
        // PBR texture-set override (name from discoverPbrTextureSets, empty = none).
        std::string textureSet;
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
    void startPlayMode();
    void togglePausePlayMode();
    void stopPlayMode();
    void rebuildRuntimePhysics();
    void updateRuntimePhysics(Scene* scene, float deltaTime);
    void updateAnimationRuntime(Scene* scene, float deltaTime);
    void updateFollowCameras(Scene* scene, float deltaTime);
    void updateGameCameras(Scene* scene);
    void cloneSceneToRuntime();
    Entity createPrimitiveEntity(const std::string& primitiveType, Entity parent = entt::null);
    Entity createGameCameraEntity(Entity parent = entt::null);
    Entity createLightEntity(ECS::LightComponent::Type type = ECS::LightComponent::Type::Directional, Entity parent = entt::null);
    void resetEditorScene(bool createEditorCamera = true);
    void ensureEditorCamera();
    void rebindEditorCameraController();
    bool saveProjectScene();
    bool loadProjectScene();
    bool exportGamePackage();
    bool exportGamePackageLinux();

    void newScene();
    bool loadSceneFromAssetPath(const std::string& assetRelativePath);

    std::string m_CurrentSceneAssetPath = "scenes/main.scene";
    // Current startup phase; reported in fatal-error context on failure.
    std::string m_InitStep = "begin";

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
    // TDD §10 runtime flow: partition -> culling -> LOD -> HLOD on the active scene.
    void updateCityRendering(Scene* scene, const glm::vec3& camPos, const glm::mat4& view,
                             const glm::mat4& viewProj, const glm::mat4& proj, float viewportHeight, float deltaTime);
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
    std::unique_ptr<Scene> m_RuntimeScene;
    std::unique_ptr<Atlas::Physics::PhysicsSystem> m_PhysicsSystem;
    std::unique_ptr<Atlas::Scripting::ScriptEngine> m_ScriptEngine;
    std::unique_ptr<::UIManager> m_UIManager;
    std::unique_ptr<::ProjectManager> m_ProjectManager;
    std::unique_ptr<::CameraController> m_CameraController;
    std::unique_ptr<::ImGuiManager> m_ImGuiManager;
    EditorViewport m_Viewport;

    std::unique_ptr<WorldPartition> m_WorldPartition;

    // TDD large-city pipeline systems (retargeted to the active scene).
    std::unique_ptr<Atlas::CullingPipeline> m_CullingPipeline;
    std::unique_ptr<Atlas::HLODSystem> m_HLODSystem;
    Atlas::CullingConfig m_CullingConfig;
    Atlas::LODConfig m_LODConfig;
    Atlas::HLODConfig m_HLODConfig;
    Atlas::CullingStats m_LastCullingStats;
    Atlas::LODStats m_LastLODStats;
    Scene* m_CityScene = nullptr;
    // TDD §7 software occlusion (occluders from previous frame's LOD data).
    Atlas::OcclusionCuller m_OcclusionCuller;
    Atlas::OcclusionConfig m_OcclusionConfig;

    // TDD §12 procedural test city (shared mesh/materials, instanceable).
    Atlas::CityGenResult m_TestCity;
    int m_TestCityBlocks = 8;

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
    bool m_GameModePlaying = false;
    bool m_GameModePaused = false;
    ImportRequest m_ActiveImport;
    std::string m_ActiveImportFullPath;
    std::string m_ActiveImportModelName;
    ImportOptions m_ActiveImportOptions;
    ImportOptions m_LastImportOptions;
    // PBR sets discovered next to the active import (popup chooser).
    std::vector<Atlas::PbrTextureSet> m_ActiveImportTextureSets;

    // Walnut-style layer stack (tool/debug layers render on top of the editor UI).
    // Declared last so layers detach before engine systems are torn down.
    LayerStack<EditorLayer> m_LayerStack;
};

}

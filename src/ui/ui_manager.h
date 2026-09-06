#pragma once

#include <functional>
#include <imgui.h>
#include <imgui_impl_vulkan.h>
#include <GLFW/glfw3.h>
#include <memory>
#include <vector>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <cstdint>
#include <filesystem>
#include "../ecs/ecs.h"
#include "../scene/scene.h"
#include "../project/project_manager.h"
#include "../renderer/renderer.h"

namespace Atlas { class AssetManager; }
class CameraController;
#include <ImGuizmo.h>

enum class TransformMode { None, Translate, Rotate, Scale };

class UIManager {
public:
    UIManager(Atlas::Scene* scene);
    ~UIManager();

    void render(ImTextureID viewportTexture, ImTextureID gameViewportTexture, bool gameModeActive, bool gameModePaused);
    void setSelectedEntity(Entity entity);
    void toggleSelectedEntity(Entity entity);
    void clearSelection();
    bool isSelected(Entity entity) const;

    Entity getSelectedEntity() const;
    const std::vector<Entity>& getSelectedEntities() const { return m_SelectedEntities; }
    
    void setProjectManager(::ProjectManager* projManager);
    void setScene(Atlas::Scene* scene) { m_Scene = scene; }
    void setOnAssetDropped(std::function<void(const std::string&)> callback);
    void setRenderer(Atlas::Renderer* renderer);
    void setAssetManager(Atlas::AssetManager* am) { assetManager = am; }
    void openProject(const std::string& path);
    void setWindow(GLFWwindow* win);
    void setOnNewProject(std::function<void()> callback) { onNewProject = std::move(callback); }
    void setOnOpenProject(std::function<void()> callback) { onOpenProject = std::move(callback); }
    void setOnSaveProject(std::function<void()> callback) { onSaveProject = std::move(callback); }
    void setOnExportGame(std::function<void()> callback) { onExportGame = std::move(callback); }
    void setOnExportGameLinux(std::function<void()> callback) { onExportGameLinux = std::move(callback); }
    void setOnNewScene(std::function<void()> callback) { onNewScene = std::move(callback); }
    void setOnOpenSceneAsset(std::function<void(const std::string&)> callback) { onOpenSceneAsset = std::move(callback); }
    void setOnExit(std::function<void()> callback) { onExit = std::move(callback); }
    void setCameraMatrices(glm::mat4 view, glm::mat4 proj);
    void setCameraController(CameraController* controller);
    void setOnPlay(std::function<void()> callback) { onPlay = std::move(callback); }
    void setOnPause(std::function<void()> callback) { onPause = std::move(callback); }
    void setOnStop(std::function<void()> callback) { onStop = std::move(callback); }
    void setOnReleaseGameFocus(std::function<void()> callback) { onReleaseGameFocus = std::move(callback); }
    void requestFocusGameViewport() { m_RequestFocusGameViewport = true; }
    bool wasViewportFocused() const { return m_ViewportFocusedPrevFrame; }
    bool wasGameViewportFocused() const { return m_GameViewportFocusedPrevFrame; }
    void setOnCreatePrimitive(std::function<void(const std::string&, Entity)> callback) { onCreatePrimitive = std::move(callback); }
    void setOnCreateGameCamera(std::function<void(Entity)> callback) { onCreateGameCamera = std::move(callback); }

    TransformMode getTransformMode() const { return m_TransformMode; }
    bool isGizmoUsing() const { return m_GizmoUsing; }
    bool allowViewportCameraInput() const { return m_ViewportAllowCameraInput; }

    // Debug panels (owned/controlled by EditorApp)
    bool m_ShowWorldStreamingWindow = false;

    void updateProfiler(float deltaTime);
    void renderProfilerWindow();
    void renderCameraWindow();
    void renderConsoleWindow();

    int getMaxFps() const { return m_MaxFps; }

    bool popViewportPickRequest(uint32_t& outX, uint32_t& outY, bool& outAdditive, bool& outDeselectOnMiss);

    void undo();
    void redo();

    void copySelectedEntitiesToClipboard();
    void pasteEntitiesFromClipboard();

private:
    struct ContentTextureThumb {
        VkDescriptorSet descriptorSet = VK_NULL_HANDLE;
        std::string assetIdStr;
        uint32_t width = 0;
        uint32_t height = 0;
        uint64_t lastUsedFrame = 0;
    };

    ImTextureID getOrCreateContentTextureThumb(const std::string& fullPath);
    void pruneContentTextureThumbs();
    void clearContentTextureThumbs();

    std::unordered_map<std::string, ContentTextureThumb> m_ContentTextureThumbs;
    uint64_t m_ContentTextureThumbFrame = 0;
    static constexpr size_t MAX_CONTENT_TEXTURE_THUMBS = 128;
    static constexpr uint64_t CONTENT_TEXTURE_THUMB_TTL_FRAMES = 600; // ~10s @ 60fps

    struct ContentContextTarget {
        std::string name;
        std::string fullPath;
        std::string relativePath;
        bool isFolder = false;
    };

    ContentContextTarget m_ContentCtxTarget;
    bool m_ContentCtxHasTarget = false;

    struct ContentClipboard {
        std::string fullPath;
        bool cut = false;
    };

    ContentClipboard m_ContentClipboard;

    bool m_ShowRenameAssetPopup = false;
    char m_RenameAssetBuf[256] = {};

    bool m_ShowDeleteAssetPopup = false;

    struct EntityClipboardItem {
        Entity source = entt::null;
        Entity parent = entt::null;

        std::string name;

        bool hasTransform = false;
        Transform transform;

        bool hasRenderable = false;
        Renderable renderable;

        bool hasCamera = false;
        Camera camera;

        bool hasMesh = false;
        ::Mesh mesh;

        bool hasMaterial = false;
        Atlas::ECS::MaterialComponent material;

        bool hasLOD = false;
        Atlas::LODComponent lod;

        bool hasRigidBody = false;
        Atlas::ECS::RigidBodyComponent rigidBody;

        bool hasBoxCollider = false;
        Atlas::ECS::BoxColliderComponent boxCollider;

        bool hasSphereCollider = false;
        Atlas::ECS::SphereColliderComponent sphereCollider;

        bool hasCapsuleCollider = false;
        Atlas::ECS::CapsuleColliderComponent capsuleCollider;

        bool hasLight = false;
        Atlas::ECS::LightComponent light;

        bool hasScript = false;
        Atlas::ECS::ScriptComponent script;

        bool hasFollowCamera = false;
        Atlas::ECS::FollowCameraComponent followCamera;

        bool hasGameCamera = false;
        Atlas::ECS::GameCameraComponent gameCamera;

        bool hasWorldChunk = false;
        ::WorldChunk worldChunk;

        bool hasWorldTransform = false;
        ::WorldTransform worldTransform;

        bool hasSkinnedMesh = false;
        Atlas::ECS::SkinnedMeshComponent skinnedMesh;

        bool hasSkeleton = false;
        Atlas::ECS::SkeletonComponent skeleton;

        bool hasAnimationPlayer = false;
        Atlas::ECS::AnimationPlayerComponent animPlayer;

        bool hasBonePoseOverride = false;
        Atlas::ECS::BonePoseOverrideComponent bonePoseOverride;
    };

    struct EntityClipboard {
        bool hasData = false;
        std::vector<EntityClipboardItem> items;
    };

    EntityClipboard m_EntityClipboard;
    uint32_t m_EntityPasteSerial = 0;

    // Hierarchy context actions
    bool m_ShowHierarchyRenamePopup = false;
    uint32_t m_HierarchyRenameEntityId = 0;
    char m_HierarchyRenameBuf[256] = {};

    // Rig editing (V1): select a skeleton entity + a bone index.
    Entity m_RigEditEntity = entt::null;
    int32_t m_RigSelectedBone = -1;
    bool m_RigShowSkeleton = true;
    Atlas::Scene* m_Scene = nullptr;
    std::vector<Entity> m_SelectedEntities;
    Entity m_PrimarySelected = entt::null;
    ::ProjectManager* projectManager = nullptr;
    Atlas::Renderer* renderer = nullptr;
    Atlas::AssetManager* assetManager = nullptr;
    std::function<void(const std::string&)> onAssetDropped;

    void renderToolbar(bool gameModeActive, bool gameModePaused);
    void renderViewport(ImTextureID viewportTexture);
    void renderGameViewport(ImTextureID viewportTexture);
    void renderHierarchy();
    void renderProperties();
    void renderContentExplorer();
    void renderMenuBar();

    void renderNewProjectDialog();
    void renderOpenProjectDialog();

    std::function<void()> onNewProject;
    std::function<void()> onOpenProject;
    std::function<void()> onSaveProject;
    std::function<void()> onExportGame;
    std::function<void()> onExportGameLinux;
    std::function<void()> onNewScene;
    std::function<void(const std::string&)> onOpenSceneAsset;
    std::function<void()> onExit;
    std::function<void()> onPlay;
    std::function<void()> onPause;
    std::function<void()> onStop;
    std::function<void()> onReleaseGameFocus;
    std::function<void(const std::string&, Entity)> onCreatePrimitive;
    std::function<void(Entity)> onCreateGameCamera;

    GLFWwindow* window = nullptr;

    TransformMode m_TransformMode = TransformMode::Translate;
    glm::mat4 m_ViewMatrix = glm::mat4(1.0f);
    glm::mat4 m_ProjMatrix = glm::mat4(1.0f);
    CameraController* m_CameraController = nullptr;
    bool m_GizmoUsing = false;

    bool m_GizmoLocal = false;
    bool m_GizmoSnap = false;
    float m_GizmoSnapTranslate = 0.5f;
    float m_GizmoSnapRotate = 15.0f;
    float m_GizmoSnapScale = 0.1f;

    bool m_HasViewportPickRequest = false;
    uint32_t m_ViewportPickX = 0;
    uint32_t m_ViewportPickY = 0;
    bool m_ViewportPickAdditive = false;
    bool m_ViewportPickDeselectOnMiss = false;

    // Cached from recent Viewport/Game window focus state.
    bool m_ViewportAllowCameraInput = false;
    bool m_ViewportFocusedPrevFrame = false;
    bool m_ViewportFocusedLastFrame = false;
    bool m_GameViewportFocusedPrevFrame = false;
    bool m_GameViewportFocusedLastFrame = false;
    bool m_RequestFocusGameViewport = false;
    bool showNewProjectDialog = false;
    bool showOpenProjectDialog = false;
    char newProjectName[256] = "MyProject";
    char newProjectPath[512] = ".";
    char projectPathBuffer[512] = ".";

    bool showOpenProjectFileDialog = false;
    bool showSaveProjectFileDialog = false;

    // Frame limiting
    bool m_VSyncEnabled = true;
    int m_MaxFps = 0;

    // Panel visibility
    bool m_ShowViewportWindow = true;
    bool m_ShowGameViewportWindow = true;
    bool m_ShowHierarchyWindow = true;
    bool m_ShowPropertiesWindow = true;
    bool m_ShowContentExplorerWindow = true;

    // Simple ImGui profiler panel data
    bool m_ShowProfilerWindow = false;
    bool m_ShowCameraWindow = false;
    bool m_ShowConsoleWindow = true;

    float m_FrameTimeMs = 0.0f;
    float m_Fps = 0.0f;
    static constexpr int PROFILER_HISTORY = 120;
    float m_FrameTimeHistory[PROFILER_HISTORY] = {};
    int m_FrameTimeIndex = 0;
    bool m_ShowTracyConnection = true;

    // Material editor state
    // Tracks which material entity is currently bound to the path buffers below.
    uint32_t m_MaterialEditEntityId = 0;
    // Tracks which material entity is selected in the materials list.
    uint32_t m_MaterialInspectEntityId = 0;

    char m_AlbedoTexturePathBuf[512] = {};
    char m_NormalTexturePathBuf[512] = {};
    char m_MetallicRoughnessTexturePathBuf[512] = {};
    char m_AOTexturePathBuf[512] = {};
    char m_EmissiveTexturePathBuf[512] = {};

    uint32_t m_ScriptEditEntityId = 0;
    char m_ScriptPathBuf[512] = {};
    std::string m_ScriptInspectError;

    struct TransformState {
        glm::vec3 position{0.0f};
        glm::vec3 rotation{0.0f};
        glm::vec3 scale{1.0f};
    };

    struct UndoCommand {
        virtual ~UndoCommand() = default;
        virtual void undo(Atlas::Scene* scene) = 0;
        virtual void redo(Atlas::Scene* scene) = 0;
    };

    struct TransformCommand final : UndoCommand {
        Entity entity = entt::null;
        TransformState before;
        TransformState after;

        void apply(Atlas::Scene* scene, const TransformState& s);
        void undo(Atlas::Scene* scene) override { apply(scene, before); }
        void redo(Atlas::Scene* scene) override { apply(scene, after); }
    };

    struct RenameCommand final : UndoCommand {
        Entity entity = entt::null;
        std::string before;
        std::string after;

        void apply(Atlas::Scene* scene, const std::string& name) {
            if (!scene) return;
            auto& registry = scene->getRegistry();
            if (entity == entt::null || !registry.valid(entity)) return;

            if (!registry.all_of<Atlas::ECS::TagComponent>(entity)) {
                registry.emplace<Atlas::ECS::TagComponent>(entity, name);
            } else {
                registry.get<Atlas::ECS::TagComponent>(entity).name = name;
            }
        }

        void undo(Atlas::Scene* scene) override { apply(scene, before); }
        void redo(Atlas::Scene* scene) override { apply(scene, after); }
    };

    struct ReparentCommand final : UndoCommand {
        Entity child = entt::null;
        Entity beforeParent = entt::null;
        Entity afterParent = entt::null;

        void undo(Atlas::Scene* scene) override {
            if (!scene) return;
            scene->setParent(child, beforeParent);
        }

        void redo(Atlas::Scene* scene) override {
            if (!scene) return;
            scene->setParent(child, afterParent);
        }
    };

    struct SoftDeleteCommand final : UndoCommand {
        std::vector<Entity> entities;

        void setHidden(Atlas::Scene* scene, bool hidden) {
            if (!scene) return;
            auto& registry = scene->getRegistry();
            for (Entity e : entities) {
                if (e == entt::null || !registry.valid(e)) continue;
                if (hidden) {
                    registry.emplace_or_replace<Atlas::ECS::EditorHiddenComponent>(e, Atlas::ECS::EditorHiddenComponent{});
                } else {
                    if (registry.all_of<Atlas::ECS::EditorHiddenComponent>(e)) {
                        registry.remove<Atlas::ECS::EditorHiddenComponent>(e);
                    }
                }
            }
        }

        void undo(Atlas::Scene* scene) override { setHidden(scene, false); }
        void redo(Atlas::Scene* scene) override { setHidden(scene, true); }
    };

    struct MaterialScalarState {
        glm::vec4 baseColor{1.0f};
        float metallic = 0.0f;
        float roughness = 0.5f;
        float ambientOcclusion = 1.0f;
        glm::vec3 emissiveFactor{0.0f};
        Atlas::ECS::MaterialComponent::AlphaMode alphaMode = Atlas::ECS::MaterialComponent::AlphaMode::Opaque;
        float alphaCutoff = 0.5f;
        bool doubleSided = false;
        bool invertCulling = false;
    };

    struct MaterialScalarCommand final : UndoCommand {
        Entity entity = entt::null;
        MaterialScalarState before;
        MaterialScalarState after;

        static void apply(Atlas::Scene* scene, Entity e, const MaterialScalarState& s) {
            if (!scene) return;
            auto& registry = scene->getRegistry();
            if (e == entt::null || !registry.valid(e)) return;
            if (!registry.all_of<Atlas::ECS::MaterialComponent>(e)) return;

            auto& mat = registry.get<Atlas::ECS::MaterialComponent>(e);
            mat.baseColor = s.baseColor;
            mat.metallic = s.metallic;
            mat.roughness = s.roughness;
            mat.ambientOcclusion = s.ambientOcclusion;
            mat.emissiveFactor = s.emissiveFactor;
            mat.alphaMode = s.alphaMode;
            mat.alphaCutoff = s.alphaCutoff;
            mat.doubleSided = s.doubleSided;
            mat.invertCulling = s.invertCulling;
        }

        void undo(Atlas::Scene* scene) override { apply(scene, entity, before); }
        void redo(Atlas::Scene* scene) override { apply(scene, entity, after); }
    };

    void pushCommand(std::unique_ptr<UndoCommand> cmd);

    std::vector<std::unique_ptr<UndoCommand>> m_UndoStack;
    std::vector<std::unique_ptr<UndoCommand>> m_RedoStack;
    static constexpr size_t MAX_UNDO = 256;

    struct MultiTransformCommand final : UndoCommand {
        std::vector<Entity> entities;
        std::vector<TransformState> before;
        std::vector<TransformState> after;

        void apply(Atlas::Scene* scene, const std::vector<TransformState>& states);
        void undo(Atlas::Scene* scene) override { apply(scene, before); }
        void redo(Atlas::Scene* scene) override { apply(scene, after); }
    };

    // Gizmo edit tracking
    bool m_GizmoWasUsing = false;
    std::vector<Entity> m_GizmoEditEntities;
    std::vector<TransformState> m_GizmoBeforeMulti;

    // Properties edit tracking
    bool m_PropTransformEditing = false;
    Entity m_PropTransformEntity = entt::null;
    TransformState m_PropTransformBefore;
    // Bulk (multi-selection) transform editing via Properties
    bool m_PropTransformMultiEditing = false;
    std::vector<Entity> m_PropTransformMultiEntities;
    std::vector<TransformState> m_PropTransformBeforeMulti;

    uint32_t m_PropNameEditEntityId = 0;
    bool m_PropNameEditing = false;
    std::string m_PropNameBefore;
    char m_PropNameBuf[256] = {};

    bool m_MatScalarEditing = false;
    Entity m_MatScalarEntity = entt::null;
    MaterialScalarState m_MatScalarBefore;


    // Selection helpers
    void setSelectionSingle(Entity entity);
};

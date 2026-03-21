#pragma once

#include <functional>
#include <imgui.h>
#include <imgui_impl_vulkan.h>
#include <GLFW/glfw3.h>
#include <memory>
#include <vector>
#include <string>
#include <unordered_map>
#include <cstdint>
#include <filesystem>
#include "../ecs/ecs.h"
#include "../scene/scene.h"
#include "../project/project_manager.h"
#include "../renderer/renderer.h"
#include <ImGuiFileDialog.h>

namespace Atlas { class AssetManager; }
class CameraController;
#include <ImGuizmo.h>

enum class TransformMode { None, Translate, Rotate, Scale };

class UIManager {
public:
    UIManager(Atlas::Scene* scene);
    ~UIManager();

    void render(ImTextureID viewportTexture);
    void setSelectedEntity(Entity entity);
    void toggleSelectedEntity(Entity entity);
    void clearSelection();
    bool isSelected(Entity entity) const;

    Entity getSelectedEntity() const;
    const std::vector<Entity>& getSelectedEntities() const { return m_SelectedEntities; }
    
    void setProjectManager(::ProjectManager* projManager);
    void setOnAssetDropped(std::function<void(const std::string&)> callback);
    void setRenderer(Atlas::Renderer* renderer);
    void setAssetManager(Atlas::AssetManager* am) { assetManager = am; }
    void openProject(const std::string& path);
    void setWindow(GLFWwindow* win);
    void setCameraMatrices(glm::mat4 view, glm::mat4 proj);
    void setCameraController(CameraController* controller);

    TransformMode getTransformMode() const { return m_TransformMode; }
    bool isGizmoUsing() const { return m_GizmoUsing; }
    bool allowViewportCameraInput() const { return m_ViewportAllowCameraInput; }

void updateProfiler(float deltaTime);
    void renderProfilerWindow();
    void renderCameraWindow();

    int getMaxFps() const { return m_MaxFps; }

    bool popViewportPickRequest(uint32_t& outX, uint32_t& outY, bool& outAdditive);

    void undo();
    void redo();

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
    Atlas::Scene* m_Scene = nullptr;
    std::vector<Entity> m_SelectedEntities;
    Entity m_PrimarySelected = entt::null;
    ::ProjectManager* projectManager = nullptr;
    Atlas::Renderer* renderer = nullptr;
    Atlas::AssetManager* assetManager = nullptr;
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

    // Cached from last frame's Viewport window.
    bool m_ViewportAllowCameraInput = false;
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

    // Simple ImGui profiler panel data
    bool m_ShowProfilerWindow = true;
    bool m_ShowCameraWindow = true;
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

    // Selection helpers
    void setSelectionSingle(Entity entity);
};

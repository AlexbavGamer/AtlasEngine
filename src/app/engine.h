#pragma once

#include <memory>
#include <string>
#include <vector>
#include <atomic>

#include "../platform/window.h"
#include "../scene/scene.h"
#include "../project/project_manager.h"

namespace Atlas {

class Renderer;
class AssetManager;
class EditorLayer;

class Engine {
public:
    Engine();
    ~Engine();

    void init();
    void run();
    void shutdown();

    Window& getWindow() { return *m_Window; }
    Renderer& getRenderer();
    AssetManager& getAssetManager();
    ProjectManager& getProjectManager();
    Scene& getScene();

    bool isRunning() const { return m_Running; }
    bool isMinimized() const;

    void setEditorLayer(std::unique_ptr<EditorLayer> layer);

private:
    void mainLoop();
    void update(float deltaTime);
    void render();

    float calculateDeltaTime();
    void calculateFrameTime();

    // Core
    std::unique_ptr<Window> m_Window;
    std::unique_ptr<Renderer> m_Renderer;
    std::unique_ptr<AssetManager> m_AssetManager;
    std::unique_ptr<ProjectManager> m_ProjectManager;
    std::unique_ptr<Scene> m_Scene;

    // Editor
    std::unique_ptr<EditorLayer> m_EditorLayer;

    // Loop control
    bool m_Running = false;
    bool m_Minimized = false;

    // Timing
    double m_LastTime = 0.0;
    double m_CurrentTime = 0.0;
    float m_DeltaTime = 0.0f;
    float m_FrameTime = 0.0f;
    uint32_t m_FrameCount = 0;
    float m_FPSUpdateTimer = 0.0f;
    float m_FPS = 0.0f;
};

}

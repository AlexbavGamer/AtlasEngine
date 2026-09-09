#pragma once

#include <memory>
#include <string>
#include <vector>

#include <imgui.h>

namespace Atlas {

class Engine; // Legacy forward decl (Editor::m_Engine); the live path uses EditorApp.

// Base interface for Walnut-style editor layers pumped by LayerStack
// (see layer_stack.h). Layers receive what they need via constructor
// injection (Hazel/Cherno pattern); onAttach takes no engine pointer.
class EditorLayer {
public:
    EditorLayer() = default;
    virtual ~EditorLayer() = default;

    virtual void onAttach() {}
    virtual void onDetach() {}

    virtual void update(float deltaTime) {}
    virtual void render() {}
    virtual void renderUI() {}

    virtual void onResize(int width, int height) {}
};

class Editor {
public:
    Editor();
    ~Editor();

    void init();
    void shutdown();

    void update(float deltaTime);
    void render();
    void renderUI();

    void onResize(int width, int height);

    void addLayer(std::unique_ptr<EditorLayer> layer);
    void removeLayer(const std::string& name);

    EditorLayer* getLayer(const std::string& name);

    Engine* getEngine() const { return m_Engine.get(); }

private:
    std::unique_ptr<Engine> m_Engine;
    std::vector<std::unique_ptr<EditorLayer>> m_Layers;
};

}

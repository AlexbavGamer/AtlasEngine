#pragma once

#include <memory>
#include <string>
#include <vector>

#include <imgui.h>

namespace Atlas {

class Engine;

class EditorLayer {
public:
    EditorLayer() = default;
    virtual ~EditorLayer() = default;

    virtual void onAttach(Engine* engine) { m_Engine = engine; }
    virtual void onDetach() {}

    virtual void update(float deltaTime) {}
    virtual void render() {}
    virtual void renderUI() {}

    virtual void onResize(int width, int height) {}

    Engine* getEngine() const { return m_Engine; }

protected:
    Engine* m_Engine = nullptr;
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

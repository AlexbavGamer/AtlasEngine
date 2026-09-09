#pragma once

// LayerStack — Walnut-style layer + overlay stack for the Atlas editor.
//
// Layers run first (in push order), overlays run after all layers, so debug
// and tool UI pushed as overlays always draws on top. Pushing attaches the
// layer immediately; popping or clearing detaches it.
//
// The stack is a template over the layer type and depends only on the C++
// standard library, so it is unit-testable without Vulkan or ImGui: any
// layer type providing onAttach/onDetach/update/render/renderUI/onResize
// works (see tests/test_layer_stack.cpp). The live specialization is
// LayerStack<EditorLayer> (see editor.h), pumped by EditorApp.

#include <cstddef>
#include <memory>
#include <utility>
#include <vector>

namespace Atlas {

template <typename TLayer>
class LayerStack {
public:
    LayerStack() = default;
    ~LayerStack() { clear(); }

    LayerStack(const LayerStack&) = delete;
    LayerStack& operator=(const LayerStack&) = delete;
    LayerStack(LayerStack&&) = default;
    LayerStack& operator=(LayerStack&&) = default;

    // Constructs a layer in place, attaches it and returns it. Layers run
    // before overlays, in push order.
    template <typename T, typename... Args>
    T* pushLayer(Args&&... args) {
        static_assert(std::is_base_of<TLayer, T>::value, "Pushed type is not a layer!");
        auto owned = std::make_unique<T>(std::forward<Args>(args)...);
        T* ptr = owned.get();
        m_Layers.insert(m_Layers.begin() + static_cast<std::ptrdiff_t>(m_LayerCount), std::move(owned));
        ++m_LayerCount;
        ptr->onAttach();
        return ptr;
    }

    // Constructs an overlay in place, attaches it and returns it. Overlays
    // run after all layers, in push order.
    template <typename T, typename... Args>
    T* pushOverlay(Args&&... args) {
        static_assert(std::is_base_of<TLayer, T>::value, "Pushed type is not a layer!");
        auto owned = std::make_unique<T>(std::forward<Args>(args)...);
        T* ptr = owned.get();
        m_Layers.push_back(std::move(owned));
        ptr->onAttach();
        return ptr;
    }

    // Detaches and removes a previously pushed layer. Returns false when the
    // pointer is not a live layer of this stack.
    bool popLayer(TLayer* layer) {
        for (size_t i = 0; i < m_LayerCount; ++i) {
            if (m_Layers[i].get() == layer) {
                m_Layers[i]->onDetach();
                m_Layers.erase(m_Layers.begin() + static_cast<std::ptrdiff_t>(i));
                --m_LayerCount;
                return true;
            }
        }
        return false;
    }

    // Detaches and removes a previously pushed overlay. Returns false when
    // the pointer is not a live overlay of this stack.
    bool popOverlay(TLayer* layer) {
        for (size_t i = m_LayerCount; i < m_Layers.size(); ++i) {
            if (m_Layers[i].get() == layer) {
                m_Layers[i]->onDetach();
                m_Layers.erase(m_Layers.begin() + static_cast<std::ptrdiff_t>(i));
                return true;
            }
        }
        return false;
    }

    void update(float deltaTime) {
        for (auto& layer : m_Layers) layer->update(deltaTime);
    }

    void render() {
        for (auto& layer : m_Layers) layer->render();
    }

    void renderUI() {
        for (auto& layer : m_Layers) layer->renderUI();
    }

    void onResize(int width, int height) {
        for (auto& layer : m_Layers) layer->onResize(width, height);
    }

    // Detaches everything (overlays first, then layers in reverse push order).
    void clear() {
        while (!m_Layers.empty()) {
            m_Layers.back()->onDetach();
            m_Layers.pop_back();
        }
        m_LayerCount = 0;
    }

    size_t size() const { return m_Layers.size(); }
    size_t layerCount() const { return m_LayerCount; }
    size_t overlayCount() const { return m_Layers.size() - m_LayerCount; }
    bool empty() const { return m_Layers.empty(); }

    typename std::vector<std::unique_ptr<TLayer>>::iterator begin() { return m_Layers.begin(); }
    typename std::vector<std::unique_ptr<TLayer>>::iterator end() { return m_Layers.end(); }
    typename std::vector<std::unique_ptr<TLayer>>::const_iterator begin() const { return m_Layers.begin(); }
    typename std::vector<std::unique_ptr<TLayer>>::const_iterator end() const { return m_Layers.end(); }

private:
    std::vector<std::unique_ptr<TLayer>> m_Layers;
    size_t m_LayerCount = 0; // layers live in [0, m_LayerCount), overlays after
};

} // namespace Atlas

#pragma once

// WorldStreamingLayer — the "World Streaming" debug window as a Walnut-style
// editor layer. Extracted from EditorApp::run() so tool/debug UI lives in the
// LayerStack instead of inline in the main loop.
//
// The layer renders nothing until the window is toggled (View menu). It
// operates on EditorApp state through a back-pointer (friend access); it owns
// no engine state itself.

#include "editor.h"

namespace Atlas {

class EditorApp;

class WorldStreamingLayer : public EditorLayer {
public:
    explicit WorldStreamingLayer(EditorApp* app);

    void renderUI() override;

private:
    EditorApp* m_App = nullptr;
};

} // namespace Atlas

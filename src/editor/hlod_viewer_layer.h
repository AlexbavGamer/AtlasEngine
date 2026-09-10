#pragma once

// HLODViewerLayer — dedicated HLOD inspector window as a Walnut-style editor
// overlay layer. Shows per-entity HLOD/LOD state in one table (level, bias,
// distance), a degraded-only filter, summary counts, and click-to-select.
//
// It operates on EditorApp state through a back-pointer (friend access); it
// owns no engine state itself. Toggled via View > HLOD Viewer.

#include "editor.h"

namespace Atlas {

class EditorApp;

class HLODViewerLayer : public EditorLayer {
public:
    explicit HLODViewerLayer(EditorApp* app);

    void renderUI() override;

private:
    EditorApp* m_App = nullptr;
    bool m_OnlyDegraded = false;
    char m_Filter[128] = {};
};

} // namespace Atlas

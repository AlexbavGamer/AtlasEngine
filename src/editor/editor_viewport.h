#pragma once

#include <imgui.h>

namespace Atlas {
class Renderer;

class EditorViewport {
public:
    ~EditorViewport();

    void setRenderer(Renderer* renderer);
    void refreshTexture();
    void releaseTexture();

    ImTextureID getTextureId() const { return m_TextureId; }

private:
    Renderer* m_Renderer = nullptr;
    ImTextureID m_TextureId = nullptr;
};

}

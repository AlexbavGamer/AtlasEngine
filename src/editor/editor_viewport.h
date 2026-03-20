#pragma once

#include <cstdint>

#include <imgui.h>
#include <vulkan/vulkan.h>

namespace Atlas {
class Renderer;

class EditorViewport {
public:
    ~EditorViewport();

    void setRenderer(Renderer* renderer);
    void refreshTexture();
    void releaseTexture();

    ImTextureID getTextureId() const;

private:
    Renderer* m_Renderer = nullptr;
    VkDescriptorSet m_TextureDescriptorSet = VK_NULL_HANDLE;
};

}

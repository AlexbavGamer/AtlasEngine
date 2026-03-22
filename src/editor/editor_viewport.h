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

    ImTextureID getSceneTextureId() const;
    ImTextureID getGameTextureId() const;

private:
    Renderer* m_Renderer = nullptr;
    VkDescriptorSet m_SceneTextureDescriptorSet = VK_NULL_HANDLE;
    VkDescriptorSet m_GameTextureDescriptorSet = VK_NULL_HANDLE;
};

}

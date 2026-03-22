#include "editor_viewport.h"

#include <cstdint>

#include <imgui_impl_vulkan.h>

#include "../renderer/renderer.h"

namespace Atlas {

namespace {
ImTextureID toImTextureId(VkDescriptorSet descriptorSet) {
    return static_cast<ImTextureID>(reinterpret_cast<uintptr_t>(descriptorSet));
}
}

EditorViewport::~EditorViewport() {
    releaseTexture();
}

void EditorViewport::setRenderer(Renderer* renderer) {
    if (m_Renderer == renderer) {
        return;
    }
    releaseTexture();
    m_Renderer = renderer;
}

void EditorViewport::refreshTexture() {
    if (!m_Renderer) {
        return;
    }

    releaseTexture();
    m_SceneTextureDescriptorSet = ImGui_ImplVulkan_AddTexture(
        m_Renderer->getOffscreenSampler(),
        m_Renderer->getOffscreenImageView(),
        VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
    m_GameTextureDescriptorSet = ImGui_ImplVulkan_AddTexture(
        m_Renderer->getGameOffscreenSampler(),
        m_Renderer->getGameOffscreenImageView(),
        VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
}

void EditorViewport::releaseTexture() {
    if (m_SceneTextureDescriptorSet != VK_NULL_HANDLE) {
        ImGui_ImplVulkan_RemoveTexture(m_SceneTextureDescriptorSet);
        m_SceneTextureDescriptorSet = VK_NULL_HANDLE;
    }
    if (m_GameTextureDescriptorSet != VK_NULL_HANDLE) {
        ImGui_ImplVulkan_RemoveTexture(m_GameTextureDescriptorSet);
        m_GameTextureDescriptorSet = VK_NULL_HANDLE;
    }
}

ImTextureID EditorViewport::getSceneTextureId() const {
    return toImTextureId(m_SceneTextureDescriptorSet);
}

ImTextureID EditorViewport::getGameTextureId() const {
    return toImTextureId(m_GameTextureDescriptorSet);
}

}

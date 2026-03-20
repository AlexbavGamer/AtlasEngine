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
    m_TextureDescriptorSet = ImGui_ImplVulkan_AddTexture(
        m_Renderer->getOffscreenSampler(),
        m_Renderer->getOffscreenImageView(),
        VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
}

void EditorViewport::releaseTexture() {
    if (m_TextureDescriptorSet != VK_NULL_HANDLE) {
        ImGui_ImplVulkan_RemoveTexture(m_TextureDescriptorSet);
        m_TextureDescriptorSet = VK_NULL_HANDLE;
    }
}

ImTextureID EditorViewport::getTextureId() const {
    return toImTextureId(m_TextureDescriptorSet);
}

}

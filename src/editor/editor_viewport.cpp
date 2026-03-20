#include "editor_viewport.h"

#include <imgui_impl_vulkan.h>

#include "../renderer/renderer.h"

namespace Atlas {

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
    m_TextureId = ImGui_ImplVulkan_AddTexture(
        m_Renderer->getOffscreenSampler(),
        m_Renderer->getOffscreenImageView(),
        VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
}

void EditorViewport::releaseTexture() {
    if (m_TextureId) {
        ImGui_ImplVulkan_RemoveTexture(static_cast<VkDescriptorSet>(m_TextureId));
        m_TextureId = nullptr;
    }
}

}

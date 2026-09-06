#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include <vulkan/vulkan.h>
#include <glm/glm.hpp>

#include "atlas_ui/ui_draw_list.h"
#include "atlas_ui/ui_font.h"

namespace Atlas::UI {

// Vulkan pipeline for the custom UI. Expects a depth attachment (D32) and a
// swapchain-color-format attachment, rendered on top of the scene. Uses
// premultiplied-alpha blending.
//
// Texture slots:
//   0 = font atlas (uploaded via uploadFontTexture)
//   1 .. kMaxTextures-1 = external textures (viewport, thumbnails, ...)
//
// Shaders are injected by the host via beginShader callback, so the library
// does not depend on the engine's shader-embedding. The callback returns the
// SPIR-V bytecode for a shader name ("ui_vert"/"ui_frag").
class UIRenderer {
public:
    static constexpr uint32_t kMaxTextures = 8;

    // Shader source provider: return nullptr/0 when unavailable.
    using ShaderLoader = const uint8_t* (*)(const char* name, size_t* outSize);

    struct CreateInfo {
        VkDevice device = VK_NULL_HANDLE;
        VkPhysicalDevice physicalDevice = VK_NULL_HANDLE;
        VkPipelineCache pipelineCache = VK_NULL_HANDLE;
        VkRenderPass renderPass = VK_NULL_HANDLE;
        VkExtent2D swapchainExtent{};
        VkFormat colorFormat = VK_FORMAT_UNDEFINED;
        VkFormat depthFormat = VK_FORMAT_UNDEFINED;
        uint32_t graphicsQueueFamily = 0;
        uint32_t maxFramesInFlight = 2;
        ShaderLoader shaderLoader = nullptr;
    };

    UIRenderer(const CreateInfo& info);
    ~UIRenderer();

    UIRenderer(const UIRenderer&) = delete;
    UIRenderer& operator=(const UIRenderer&) = delete;

    // Recreate the pipeline when the swapchain is resized.
    void recreate(VkExtent2D newExtent, VkRenderPass newRenderPass);

    // Upload a new font atlas texture from UIFont::atlasRGBA(). Occupies slot 0
    // and must be called at least once before recordDraw.
    void uploadFontTexture(const UIFont& font);

    // Register an external image (viewport, thumbnail) into a free slot >= 1.
    // Returns the slot index, or -1 if all slots are occupied. The caller keeps
    // the image view and sampler alive until releaseTexture.
    int32_t uploadTexture(VkImageView imageView, VkSampler sampler);

    // Release a previously-uploaded external texture slot. Slot 0 cannot be
    // released (resets to the font atlas fallback).
    void releaseTexture(uint32_t slot);

    // Record the UI draw commands into the given command buffer. The render
    // pass must already be active. Uses an orthographic projection matching the
    // swapchain extent.
    void recordDraw(VkCommandBuffer cmd, const UIDrawList& drawList,
                    uint32_t currentFrame);

private:
    struct Impl;
    std::unique_ptr<Impl> m_Impl;
};

} // namespace Atlas::UI
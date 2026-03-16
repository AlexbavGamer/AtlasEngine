#ifndef VULKAN_RENDER_PASS_H
#define VULKAN_RENDER_PASS_H

#include <vulkan/vulkan.h>

namespace VulkanEngine {

class VulkanRenderPass {
public:
    VulkanRenderPass();
    ~VulkanRenderPass();

    void init(VkDevice device, VkFormat swapChainImageFormat, bool enableValidationLayers);
    void cleanup(VkDevice device);

    VkRenderPass getRenderPass() const;

private:
    VkRenderPass renderPass;
    bool enableValidationLayers;
};

} // namespace VulkanEngine

#endif // VULKAN_RENDER_PASS_H
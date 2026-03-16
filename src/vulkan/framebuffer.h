#ifndef VULKAN_FRAMEBUFFER_H
#define VULKAN_FRAMEBUFFER_H

#include <vulkan/vulkan.h>
#include <vector>

namespace VulkanEngine {

class VulkanFramebuffer {
public:
    VulkanFramebuffer();
    ~VulkanFramebuffer();

    void init(VkDevice device, VkRenderPass renderPass, const std::vector<VkImageView>& swapChainImageViews, VkExtent2D swapChainExtent);
    void cleanup(VkDevice device);

    const std::vector<VkFramebuffer>& getFramebuffers() const;

private:
    std::vector<VkFramebuffer> swapChainFramebuffers;
};

} // namespace VulkanEngine

#endif // VULKAN_FRAMEBUFFER_H
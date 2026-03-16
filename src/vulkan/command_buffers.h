#ifndef VULKAN_COMMAND_BUFFERS_H
#define VULKAN_COMMAND_BUFFERS_H

#include <vulkan/vulkan.h>
#include <vector>

namespace VulkanEngine {

class VulkanCommandBuffers {
public:
    VulkanCommandBuffers();
    ~VulkanCommandBuffers();

    void init(VkDevice device, VkCommandPool commandPool, uint32_t count);
    void cleanup(VkDevice device);

    void recordCommandBuffer(VkCommandBuffer commandBuffer, uint32_t imageIndex, VkRenderPass renderPass, VkFramebuffer framebuffer, VkExtent2D swapChainExtent);
    const std::vector<VkCommandBuffer>& getCommandBuffers() const;

private:
    std::vector<VkCommandBuffer> commandBuffers;
};

} // namespace VulkanEngine

#endif // VULKAN_COMMAND_BUFFERS_H
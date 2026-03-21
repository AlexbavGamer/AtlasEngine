#pragma once

#include <vulkan/vulkan.h>
#include <GLFW/glfw3.h>

class ImGuiManager {
public:
    void init(VkInstance instance, VkPhysicalDevice physicalDevice, VkDevice device, VkQueue queue, uint32_t queueFamily, VkRenderPass renderPass, GLFWwindow* window, uint32_t imageCount);
    void newFrame();
    void render(VkCommandBuffer commandBuffer);
    void cleanup(VkDevice device);

private:
    VkDescriptorPool descriptorPool;
    VkDevice device;
    VkQueue graphicsQueue;
};
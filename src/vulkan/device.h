#ifndef VULKAN_DEVICE_H
#define VULKAN_DEVICE_H

#include <vulkan/vulkan.h>
#include "vulkan_structs.h"
#include <vector>

namespace VulkanEngine {

    const std::vector<const char*> deviceExtensions = {
        VK_KHR_SWAPCHAIN_EXTENSION_NAME
    };

class VulkanDevice {
public:
    VulkanDevice();
    ~VulkanDevice();

    void init(VkInstance instance, VkSurfaceKHR surface, bool enableValidationLayers);
    void cleanup();

    VkPhysicalDevice getPhysicalDevice() const;
    VkDevice getDevice() const;
    VkQueue getGraphicsQueue() const;
    VkQueue getPresentQueue() const;

private:
    VkPhysicalDevice physicalDevice;
    VkDevice device;

    VkQueue graphicsQueue;
    VkQueue presentQueue;

    bool enableValidationLayers;
    void pickPhysicalDevice(VkInstance instance, VkSurfaceKHR surface);
    void createLogicalDevice(VkInstance instance, VkSurfaceKHR surface);

    bool isDeviceSuitable(VkPhysicalDevice device, VkSurfaceKHR surface);
    bool checkDeviceExtensionSupport(VkPhysicalDevice device);
    SwapChainSupportDetails querySwapChainSupport(VkPhysicalDevice device, VkSurfaceKHR surface);
    QueueFamilyIndices findQueueFamilies(VkPhysicalDevice device, VkSurfaceKHR surface);
};

} // namespace VulkanEngine

#endif // VULKAN_DEVICE_H
#ifndef VULKAN_SWAPCHAIN_H
#define VULKAN_SWAPCHAIN_H

#include <vulkan/vulkan.h>
#include "vulkan_structs.h"

namespace VulkanEngine {

class VulkanSwapchain {
public:
    VulkanSwapchain();
    ~VulkanSwapchain();

    void init(VkInstance instance, VkDevice device, VkPhysicalDevice physicalDevice, VkSurfaceKHR surface, 
              uint32_t width, uint32_t height, bool enableValidationLayers);
    void cleanup(VkDevice device);

    VkSwapchainKHR getSwapchain() const;
    const std::vector<VkImage>& getImages() const;
    VkFormat getImageFormat() const;
    VkExtent2D getExtent() const;
    const std::vector<VkImageView>& getImageViews() const;

    void recreate(VkInstance instance, VkDevice device, VkPhysicalDevice physicalDevice, VkSurfaceKHR surface, 
                  uint32_t width, uint32_t height, bool enableValidationLayers);

private:
    VkInstance instance;
    VkDevice device;
    VkPhysicalDevice physicalDevice;
    VkSurfaceKHR surface;

    VkSwapchainKHR swapChain;
    std::vector<VkImage> swapChainImages;
    VkFormat swapChainImageFormat;
    VkExtent2D swapChainExtent;
    std::vector<VkImageView> swapChainImageViews;

    uint32_t width;
    uint32_t height;
    bool enableValidationLayers;

    void createSwapChain();
    void createImageViews();
    VkSurfaceFormatKHR chooseSwapSurfaceFormat(const std::vector<VkSurfaceFormatKHR>& availableFormats);
    VkPresentModeKHR chooseSwapPresentMode(const std::vector<VkPresentModeKHR>& availablePresentModes);
    VkExtent2D chooseSwapExtent(const VkSurfaceCapabilitiesKHR& capabilities);
    bool checkDeviceExtensionSupport(VkPhysicalDevice device);
    SwapChainSupportDetails querySwapChainSupport(VkPhysicalDevice device);
    QueueFamilyIndices findQueueFamilies(VkPhysicalDevice device);
};

} // namespace VulkanEngine

#endif // VULKAN_SWAPCHAIN_H
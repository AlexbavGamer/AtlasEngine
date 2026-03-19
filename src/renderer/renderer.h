#pragma once

#include <vulkan/vulkan.h>
#include <GLFW/glfw3.h>
#include <glm/glm.hpp>
#include <memory>
#include <vector>
#include <functional>
#include <string>
#include <deque>

#include "../core/base/non_copyable.h"
#include "../vulkan/vulkan_structs.h"

namespace Atlas {

class Window;
class Scene;

class Renderer : NonCopyable {
public:
    Renderer(Window* window);
    ~Renderer();

    void init();
    void shutdown();

    void beginFrame();
    void endFrame();
    void renderScene(Scene* scene);

    void recreateSwapChain();

    // Getters
    VkInstance getInstance() const { return m_Instance; }
    VkDevice getDevice() const { return m_Device; }
    VkPhysicalDevice getPhysicalDevice() const { return m_PhysicalDevice; }
    VkQueue getGraphicsQueue() const { return m_GraphicsQueue; }
    VkQueue getPresentQueue() const { return m_PresentQueue; }
    VkCommandPool getCommandPool() const { return m_CommandPool; }
    VkRenderPass getRenderPass() const { return m_RenderPass; }
    VkRenderPass getOffscreenRenderPass() const { return m_OffscreenRenderPass; }
    uint32_t getGraphicsQueueFamily() const { return m_QueueFamilyIndices.graphicsFamily.value(); }
    VkPipelineLayout getPipelineLayout() const { return m_PipelineLayout; }
    VkPipeline getGraphicsPipeline() const { return m_GraphicsPipeline; }
    VkExtent2D getSwapChainExtent() const { return m_SwapChainExtent; }
    VkFormat getSwapChainImageFormat() const { return m_SwapChainImageFormat; }
    uint32_t getSwapChainImageCount() const { return static_cast<uint32_t>(m_SwapChainImages.size()); }
    VkCommandBuffer getCurrentCommandBuffer() const { return m_CommandBuffers[m_CurrentFrame]; }
    VkImageView getOffscreenImageView() const { return m_OffscreenImageView; }
    VkSampler getOffscreenSampler() const { return m_OffscreenSampler; }

    uint32_t findMemoryType(uint32_t typeFilter, VkMemoryPropertyFlags properties);

    using ResizeCallback = std::function<void(int width, int height)>;
    void setResizeCallback(ResizeCallback callback) { m_ResizeCallback = std::move(callback); }
    
    using RenderCallback = std::function<void(VkCommandBuffer commandBuffer)>;
    void setRenderCallback(RenderCallback callback) { m_RenderCallback = std::move(callback); }

    // Clear color
    glm::vec4 getClearColor() const { return m_ClearColor; }
    void setClearColor(const glm::vec4& color) { m_ClearColor = color; }

private:
    void createInstance();
    void setupDebugMessenger();
    void createSurface();
    void pickPhysicalDevice();
    void createLogicalDevice();
    void createSwapChain();
    void createImageViews();
    void createRenderPass();
    void createDepthResources();
    void createGraphicsPipeline();
    void createFramebuffers();
    void createCommandPool();
    void createCommandBuffers();
    void createSyncObjects();
    void createOffscreenResources();
    void createOffscreenRenderPass();

    void cleanupSwapChain();
    void cleanupOffscreenResources();
    void recordCommandBuffer(VkCommandBuffer commandBuffer, uint32_t imageIndex, Scene* scene);

    bool checkValidationLayerSupport();
    bool isDeviceSuitable(VkPhysicalDevice device);
    bool checkDeviceExtensionSupport(VkPhysicalDevice device);

    QueueFamilyIndices findQueueFamilies(VkPhysicalDevice device);
    SwapChainSupportDetails querySwapChainSupport(VkPhysicalDevice device);
    VkFormat findDepthFormat();

    VkSurfaceFormatKHR chooseSwapSurfaceFormat(const std::vector<VkSurfaceFormatKHR>& availableFormats);
    VkPresentModeKHR chooseSwapPresentMode(const std::vector<VkPresentModeKHR>& availablePresentModes);
    VkExtent2D chooseSwapExtent(const VkSurfaceCapabilitiesKHR& capabilities);

    std::vector<char> readFile(const std::string& filename);
    VkShaderModule createShaderModule(const std::vector<char>& code);

    static VKAPI_ATTR VkBool32 VKAPI_CALL debugCallback(
        VkDebugUtilsMessageSeverityFlagBitsEXT messageSeverity,
        VkDebugUtilsMessageTypeFlagsEXT messageType,
        const VkDebugUtilsMessengerCallbackDataEXT* pCallbackData,
        void* pUserData);

    // Core
    Window* m_Window = nullptr;
    VkInstance m_Instance = VK_NULL_HANDLE;
    VkDebugUtilsMessengerEXT m_DebugMessenger = VK_NULL_HANDLE;
    VkSurfaceKHR m_Surface = VK_NULL_HANDLE;

    // Device
    VkPhysicalDevice m_PhysicalDevice = VK_NULL_HANDLE;
    VkDevice m_Device = VK_NULL_HANDLE;
    VkQueue m_GraphicsQueue = VK_NULL_HANDLE;
    VkQueue m_PresentQueue = VK_NULL_HANDLE;
    QueueFamilyIndices m_QueueFamilyIndices{};

    // Swapchain
    VkSwapchainKHR m_SwapChain = VK_NULL_HANDLE;
    std::vector<VkImage> m_SwapChainImages;
    VkFormat m_SwapChainImageFormat = VK_FORMAT_UNDEFINED;
    VkExtent2D m_SwapChainExtent = {};
    std::vector<VkImageView> m_SwapChainImageViews;
    std::vector<VkFramebuffer> m_SwapChainFramebuffers;

    // Depth
    VkImage m_DepthImage = VK_NULL_HANDLE;
    VkDeviceMemory m_DepthMemory = VK_NULL_HANDLE;
    VkImageView m_DepthImageView = VK_NULL_HANDLE;

    // Clear color
    glm::vec4 m_ClearColor = glm::vec4(0.5f, 0.7f, 0.9f, 1.0f);

    // Pipeline
    VkRenderPass m_RenderPass = VK_NULL_HANDLE;
    VkRenderPass m_OffscreenRenderPass = VK_NULL_HANDLE;
    VkPipelineLayout m_PipelineLayout = VK_NULL_HANDLE;
    VkPipeline m_GraphicsPipeline = VK_NULL_HANDLE;

    // Command
    VkCommandPool m_CommandPool = VK_NULL_HANDLE;
    std::vector<VkCommandBuffer> m_CommandBuffers;

    // Sync
    static constexpr uint32_t MAX_FRAMES_IN_FLIGHT = 3;
    std::vector<VkSemaphore> m_ImageAvailableSemaphores;
    std::vector<VkSemaphore> m_RenderFinishedSemaphores;
    std::vector<VkFence> m_InFlightFences;
    std::vector<VkFence> m_ImagesInFlight;
    uint32_t m_CurrentFrame = 0;

    // Offscreen
    VkImage m_OffscreenImage = VK_NULL_HANDLE;
    VkDeviceMemory m_OffscreenImageMemory = VK_NULL_HANDLE;
    VkImageView m_OffscreenImageView = VK_NULL_HANDLE;
    VkSampler m_OffscreenSampler = VK_NULL_HANDLE;
    VkFramebuffer m_OffscreenFramebuffer = VK_NULL_HANDLE;
    VkImageLayout m_OffscreenImageLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    VkImage m_OffscreenDepthImage = VK_NULL_HANDLE;
    VkDeviceMemory m_OffscreenDepthImageMemory = VK_NULL_HANDLE;
    VkImageView m_OffscreenDepthImageView = VK_NULL_HANDLE;

    bool m_FramebufferResized = false;
    ResizeCallback m_ResizeCallback;
    RenderCallback m_RenderCallback;

    struct DeletionQueue {
        std::deque<std::function<void()>> deletors;
        
        void push(std::function<void()> fn) {
            deletors.push_back(fn);
        }
        
        void flush() {
            for (auto it = deletors.rbegin(); it != deletors.rend(); it++) {
                (*it)();
            }
            deletors.clear();
        }
    };
    
    DeletionQueue m_MainQueue;
    DeletionQueue m_FrameQueue;

    static const std::vector<const char*> validationLayers;
    static const std::vector<const char*> deviceExtensions;
};

}

#pragma once

#include <vulkan/vulkan.h>
#include <GLFW/glfw3.h>
#include <glm/glm.hpp>
#include <memory>
#include <vector>
#include <functional>
#include <string>
#include <deque>
#include <cstdint>

#include "../core/base/non_copyable.h"
#include "../vulkan/vulkan_structs.h"
#include "memory/memory_manager.h"
#include "../core/profiler.h"

namespace Atlas {

struct Light {
    glm::vec3 position;
    float intensity;
    glm::vec3 color;
    float padding;
};

struct LightBuffer {
    Light lights[4];
    int lightCount;
    glm::vec3 cameraPos;
    float padding;
};

struct PushConstants {
    glm::mat4 model;
    glm::mat4 view;
    glm::mat4 proj;
    glm::vec4 baseColor;
    glm::vec4 emissiveFactor;
    float metallic;
    float roughness;
    int32_t albedoTexIndex;
    int32_t normalTexIndex;
    int32_t metallicRoughnessTexIndex;
    int32_t aoTexIndex;
    int32_t emissiveTexIndex;
    int32_t flags;
};

struct PickingPushConstants {
    glm::mat4 model;
    glm::mat4 view;
    glm::mat4 proj;
    uint32_t entityIdPlusOne;
    uint32_t _pad0;
    uint32_t _pad1;
    uint32_t _pad2;
};

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
    MemoryManager* getMemoryManager() { return m_MemoryManager.get(); }

    uint32_t findMemoryType(uint32_t typeFilter, VkMemoryPropertyFlags properties);

    using ResizeCallback = std::function<void(int width, int height)>;
    void setResizeCallback(ResizeCallback callback) { m_ResizeCallback = std::move(callback); }

    using RenderCallback = std::function<void(VkCommandBuffer commandBuffer)>;
    void setRenderCallback(RenderCallback callback) { m_RenderCallback = std::move(callback); }

    void immediateSubmit(const std::function<void(VkCommandBuffer)>& fn);
    uint32_t bindTexture(VkImageView imageView, VkSampler sampler);
    void updateTexture(uint32_t index, VkImageView imageView, VkSampler sampler);

    void setVSyncEnabled(bool enabled);
    bool isVSyncEnabled() const { return m_VSyncEnabled; }

    // Returns UINT32_MAX when nothing is hit.
    uint32_t pickEntityId(uint32_t x, uint32_t y);

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
    void createPickingRenderPass();
    void createPickingPipeline();
    void createLightBuffer();
    void createDescriptorSet();

    void destroyPipelineResources();
    void destroySwapchainResources();
    void destroyOffscreenResources();
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

    Window* m_Window = nullptr;
    VkInstance m_Instance = VK_NULL_HANDLE;
    VkDebugUtilsMessengerEXT m_DebugMessenger = VK_NULL_HANDLE;
    VkSurfaceKHR m_Surface = VK_NULL_HANDLE;

    VkPhysicalDevice m_PhysicalDevice = VK_NULL_HANDLE;
    VkDevice m_Device = VK_NULL_HANDLE;
    VkQueue m_GraphicsQueue = VK_NULL_HANDLE;
    VkQueue m_PresentQueue = VK_NULL_HANDLE;
    QueueFamilyIndices m_QueueFamilyIndices{};
    std::unique_ptr<MemoryManager> m_MemoryManager;

    VkSwapchainKHR m_SwapChain = VK_NULL_HANDLE;
    bool m_VSyncEnabled = true;
    std::vector<VkImage> m_SwapChainImages;
    VkFormat m_SwapChainImageFormat = VK_FORMAT_UNDEFINED;
    VkExtent2D m_SwapChainExtent = {};
    std::vector<VkImageView> m_SwapChainImageViews;
    std::vector<VkFramebuffer> m_SwapChainFramebuffers;

    VkImage m_DepthImage = VK_NULL_HANDLE;
    VkDeviceMemory m_DepthMemory = VK_NULL_HANDLE;
    VkImageView m_DepthImageView = VK_NULL_HANDLE;

    glm::vec4 m_ClearColor = glm::vec4(0.5f, 0.7f, 0.9f, 1.0f);

    VkRenderPass m_RenderPass = VK_NULL_HANDLE;
    VkRenderPass m_OffscreenRenderPass = VK_NULL_HANDLE;
    VkRenderPass m_PickingRenderPass = VK_NULL_HANDLE;
    VkPipelineLayout m_PipelineLayout = VK_NULL_HANDLE;
    VkPipeline m_GraphicsPipeline = VK_NULL_HANDLE;
    VkPipeline m_GraphicsPipelineBlend = VK_NULL_HANDLE;

    VkPipelineLayout m_PickingPipelineLayout = VK_NULL_HANDLE;
    VkPipeline m_PickingPipeline = VK_NULL_HANDLE;

    VkCommandPool m_CommandPool = VK_NULL_HANDLE;
    std::vector<VkCommandBuffer> m_CommandBuffers;

    static constexpr uint32_t MAX_FRAMES_IN_FLIGHT = 3;
    std::vector<VkSemaphore> m_ImageAvailableSemaphores;
    std::vector<VkSemaphore> m_RenderFinishedSemaphores;
    std::vector<VkFence> m_InFlightFences;
    std::vector<VkFence> m_ImagesInFlight;
    uint32_t m_CurrentFrame = 0;

    VkImage m_OffscreenImage = VK_NULL_HANDLE;
    VkDeviceMemory m_OffscreenImageMemory = VK_NULL_HANDLE;
    VkImageView m_OffscreenImageView = VK_NULL_HANDLE;
    VkSampler m_OffscreenSampler = VK_NULL_HANDLE;
    VkFramebuffer m_OffscreenFramebuffer = VK_NULL_HANDLE;
    VkImageLayout m_OffscreenImageLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    VkImage m_OffscreenDepthImage = VK_NULL_HANDLE;
    VkDeviceMemory m_OffscreenDepthImageMemory = VK_NULL_HANDLE;
    VkImageView m_OffscreenDepthImageView = VK_NULL_HANDLE;

    VkImage m_PickingImage = VK_NULL_HANDLE;
    VkDeviceMemory m_PickingImageMemory = VK_NULL_HANDLE;
    VkImageView m_PickingImageView = VK_NULL_HANDLE;
    VkFramebuffer m_PickingFramebuffer = VK_NULL_HANDLE;
    VkImageLayout m_PickingImageLayout = VK_IMAGE_LAYOUT_UNDEFINED;

    LightBuffer m_LightBufferData{};
    VkBuffer m_LightBuffer = VK_NULL_HANDLE;
    VkDeviceMemory m_LightBufferMemory = VK_NULL_HANDLE;
    VkDescriptorPool m_DescriptorPool = VK_NULL_HANDLE;
    VkDescriptorSet m_DescriptorSet = VK_NULL_HANDLE;
    VkDescriptorSetLayout m_DescriptorSetLayout = VK_NULL_HANDLE;
    uint32_t m_BoundTextureCount = 1;

    static constexpr uint32_t MAX_TEXTURES = 64;
    VkImage m_TextureImages[MAX_TEXTURES] = {};
    VkDeviceMemory m_TextureImageMemory[MAX_TEXTURES] = {};
    VkImageView m_TextureImageViews[MAX_TEXTURES] = {};
    VkSampler m_TextureSamplers[MAX_TEXTURES] = {};
    uint32_t m_TextureCount = 0;

    VkDescriptorPool m_TextureDescriptorPool = VK_NULL_HANDLE;
    VkDescriptorSetLayout m_TextureDescriptorSetLayout = VK_NULL_HANDLE;
    std::vector<VkDescriptorSet> m_TextureDescriptorSets;

    VkImage m_PlaceholderImage = VK_NULL_HANDLE;
    VkDeviceMemory m_PlaceholderImageMemory = VK_NULL_HANDLE;
    VkImageView m_PlaceholderImageView = VK_NULL_HANDLE;
    VkSampler m_PlaceholderSampler = VK_NULL_HANDLE;

    void createTextureDescriptorSetLayout();
    void createPlaceholderTexture();
    uint32_t createTextureFromFile(const std::string& path);

#ifdef TRACY_ENABLE
    TracyVkCtx m_TracyVkCtx = nullptr;
#endif

    bool m_FramebufferResized = false;
    ResizeCallback m_ResizeCallback;
    RenderCallback m_RenderCallback;

    struct DeletionQueue {
        std::deque<std::function<void()>> deletors;
        void push(std::function<void()> fn) { deletors.push_back(std::move(fn)); }
        void flush() {
            for (auto it = deletors.rbegin(); it != deletors.rend(); ++it) {
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

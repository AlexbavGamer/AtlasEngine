#include "renderer.h"
#include "embedded_shaders.h"
#include "../platform/window.h"
#include "../scene/scene.h"
#include "../ecs/components/components.h"
#include "../ecs/ecs.h"
#include "../renderer/vertex.h"
#include "../world/lod.h"
#include "../core/profiler.h"
#include <stdexcept>
#include <fstream>
#include <array>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <iostream>
#include <algorithm>
#include <unordered_map>
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

#ifdef NDEBUG
const bool enableValidationLayers = false;
#else
const bool enableValidationLayers = true;
#endif

namespace {
void setDebugName(VkDevice device, VkObjectType type, uint64_t handle, const char* name) {
    if (!enableValidationLayers || device == VK_NULL_HANDLE || handle == 0 || !name) {
        return;
    }

    auto fn = reinterpret_cast<PFN_vkSetDebugUtilsObjectNameEXT>(vkGetDeviceProcAddr(device, "vkSetDebugUtilsObjectNameEXT"));
    if (!fn) {
        return;
    }

    VkDebugUtilsObjectNameInfoEXT info{};
    info.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_OBJECT_NAME_INFO_EXT;
    info.objectType = type;
    info.objectHandle = handle;
    info.pObjectName = name;
    fn(device, &info);
}
}

// Phase 3b: resolve the buffers actually bound for a draw via the registry,
// the sole source of truth. Mesh carries no Vk* fields anymore (ecs.h is
// Vulkan-free). A dead/unpublished handle — including handle 0, which no
// longer has a legacy path — skips the draw. Returns false to skip.
static bool resolveMeshDrawBuffers(const Atlas::RenderResourceManager& registry, const Mesh& mesh,
                                   VkBuffer& outVB, VkBuffer& outIB, uint32_t& outIndexCount) {
    Atlas::MeshBinding binding{};
    if (!registry.getMeshData(mesh.renderMeshId, binding)) {
        return false;
    }
    if (binding.vertexBuffer == 0 || binding.indexBuffer == 0 || binding.indexCount == 0) {
        return false;
    }
    outVB = reinterpret_cast<VkBuffer>(binding.vertexBuffer);
    outIB = reinterpret_cast<VkBuffer>(binding.indexBuffer);
    outIndexCount = binding.indexCount;
    return true;
}

// Sun/sky lookup (Sun/Sky task): first component found wins; a sky must be
// enabled to draw. World-space policy lives in components.h; the renderer
// only translates to GPU state.
static const Atlas::ECS::SunComponent* findFirstSun(entt::registry& registry) {
    auto view = registry.view<Atlas::ECS::SunComponent>();
    if (view.begin() == view.end()) {
        return nullptr;
    }
    return &view.get<Atlas::ECS::SunComponent>(*view.begin());
}
static const Atlas::ECS::SkyComponent* findFirstEnabledSky(entt::registry& registry) {
    for (auto e : registry.view<Atlas::ECS::SkyComponent>()) {
        const auto& sky = registry.get<Atlas::ECS::SkyComponent>(e);
        if (sky.enabled) {
            return &sky;
        }
    }
    return nullptr;
}

namespace Atlas {

const std::vector<const char*> Atlas::Renderer::validationLayers = {
    "VK_LAYER_KHRONOS_validation"
};

const std::vector<const char*> Atlas::Renderer::deviceExtensions = {
    VK_KHR_SWAPCHAIN_EXTENSION_NAME
};

Renderer::Renderer(Window* window) : m_Window(window) {}

Renderer::~Renderer() {
    shutdown();
}

void Renderer::init()
try {
    m_InitStep = "vulkan instance/device/surface";
    createInstance();
    setupDebugMessenger();
    createSurface();
    pickPhysicalDevice();
    createLogicalDevice();

    m_InitStep = "memory manager";
    m_MemoryManager = std::make_unique<MemoryManager>(m_Instance, m_PhysicalDevice, m_Device);
    m_InitStep = "swapchain";
    createSwapChain();
    createImageViews();
    m_InitStep = "render passes";
    createRenderPass();
    createOffscreenRenderPass();
    createPickingRenderPass();
    createShadowResources();
    createDepthResources();
    m_InitStep = "pipelines";
    createBonesDescriptorSetLayout();
    createGraphicsPipeline();
    createInstancedPipelines();
    createPickingPipeline();
    createShadowPipeline();
    createOutlinePipeline();
    createSkyPipeline();
    m_InitStep = "framebuffers/resources";
    createFramebuffers();
    createOffscreenResources();
    createCommandPool();
    createCommandBuffers();
    m_InitStep = "descriptors/buffers";
    createLightBuffer();
    createDescriptorSet();
    createBonesResources();
    createInstanceBuffers();
    createImpostorQuad();
    createSyncObjects();

#ifdef TRACY_ENABLE
    // Create a one-shot command buffer for Tracy context initialization
    VkCommandBufferAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    allocInfo.commandPool = m_CommandPool;
    allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    allocInfo.commandBufferCount = 1;
    VkCommandBuffer tracyInitCmdBuf;
    vkAllocateCommandBuffers(m_Device, &allocInfo, &tracyInitCmdBuf);

    m_TracyVkCtx = PROFILE_GPU_CONTEXT(m_PhysicalDevice, m_Device, m_GraphicsQueue, tracyInitCmdBuf);

    vkFreeCommandBuffers(m_Device, m_CommandPool, 1, &tracyInitCmdBuf);
#endif
} catch (const std::exception& e) {
    throw std::runtime_error(std::string("Renderer::init failed at step '") + m_InitStep + "': " + e.what());
} catch (...) {
    throw std::runtime_error(std::string("Renderer::init failed at step '") + m_InitStep + "' (unknown, non-std exception)");
}

void Renderer::shutdown() {
    if (m_Device == VK_NULL_HANDLE) {
        return;
    }

    m_RenderCallback = nullptr;
    vkDeviceWaitIdle(m_Device);

#ifdef TRACY_ENABLE
    if (m_TracyVkCtx) {
        PROFILE_GPU_DESTROY(m_TracyVkCtx);
        m_TracyVkCtx = nullptr;
    }
#endif

    for (auto& q : m_DeletionQueues) {
        q.flush();
    }
    
    if (m_LightBuffer) vkDestroyBuffer(m_Device, m_LightBuffer, nullptr);
    if (m_LightBufferMemory) vkFreeMemory(m_Device, m_LightBufferMemory, nullptr);
    
    if (m_PlaceholderSampler) vkDestroySampler(m_Device, m_PlaceholderSampler, nullptr);
    if (m_PlaceholderImageView) vkDestroyImageView(m_Device, m_PlaceholderImageView, nullptr);
    if (m_PlaceholderImage) vkDestroyImage(m_Device, m_PlaceholderImage, nullptr);
    if (m_PlaceholderImageMemory) vkFreeMemory(m_Device, m_PlaceholderImageMemory, nullptr);

    for (uint32_t i = 0; i < m_TextureCount; i++) {
        if (m_TextureSamplers[i]) vkDestroySampler(m_Device, m_TextureSamplers[i], nullptr);
        if (m_TextureImageViews[i]) vkDestroyImageView(m_Device, m_TextureImageViews[i], nullptr);
        if (m_TextureImages[i]) vkDestroyImage(m_Device, m_TextureImages[i], nullptr);
        if (m_TextureImageMemory[i]) vkFreeMemory(m_Device, m_TextureImageMemory[i], nullptr);
    }

    for (auto semaphore : m_RenderFinishedSemaphores) {
        if (semaphore) vkDestroySemaphore(m_Device, semaphore, nullptr);
    }
    for (auto semaphore : m_ImageAvailableSemaphores) {
        if (semaphore) vkDestroySemaphore(m_Device, semaphore, nullptr);
    }
    for (auto fence : m_InFlightFences) {
        if (fence) vkDestroyFence(m_Device, fence, nullptr);
    }

    if (m_CommandPool) vkDestroyCommandPool(m_Device, m_CommandPool, nullptr);

    destroyOffscreenResources();
    destroySwapchainResources();
    destroyPipelineResources();

    for (uint32_t frame = 0; frame < MAX_FRAMES_IN_FLIGHT; ++frame) {
        if (m_BonePaletteMapped[frame] && m_BonePaletteMemories[frame]) {
            vkUnmapMemory(m_Device, m_BonePaletteMemories[frame]);
            m_BonePaletteMapped[frame] = nullptr;
        }
        if (m_BonePaletteBuffers[frame]) vkDestroyBuffer(m_Device, m_BonePaletteBuffers[frame], nullptr);
        if (m_BonePaletteMemories[frame]) vkFreeMemory(m_Device, m_BonePaletteMemories[frame], nullptr);
        m_BonePaletteBuffers[frame] = VK_NULL_HANDLE;
        m_BonePaletteMemories[frame] = VK_NULL_HANDLE;
    }

    if (m_BonesDescriptorPool) vkDestroyDescriptorPool(m_Device, m_BonesDescriptorPool, nullptr);
    if (m_BonesDescriptorSetLayout) vkDestroyDescriptorSetLayout(m_Device, m_BonesDescriptorSetLayout, nullptr);
    m_BonesDescriptorPool = VK_NULL_HANDLE;
    m_BonesDescriptorSetLayout = VK_NULL_HANDLE;

    destroyInstanceBuffers();
    destroyImpostorQuad();
    for (auto& [key, pair] : m_SimplifiedVariants) {
        (void)key;
        for (auto& v : pair) {
            if (v.vertexBuffer != VK_NULL_HANDLE) vkDestroyBuffer(m_Device, v.vertexBuffer, nullptr);
            if (v.vertexMemory != VK_NULL_HANDLE) vkFreeMemory(m_Device, v.vertexMemory, nullptr);
            if (v.indexBuffer != VK_NULL_HANDLE) vkDestroyBuffer(m_Device, v.indexBuffer, nullptr);
            if (v.indexMemory != VK_NULL_HANDLE) vkFreeMemory(m_Device, v.indexMemory, nullptr);
            v = StaticMeshBuffers{};
        }
    }
    m_SimplifiedVariants.clear();

    if (m_DescriptorPool) vkDestroyDescriptorPool(m_Device, m_DescriptorPool, nullptr);
    if (m_DescriptorSetLayout) vkDestroyDescriptorSetLayout(m_Device, m_DescriptorSetLayout, nullptr);

    m_MemoryManager.reset();

    vkDestroyDevice(m_Device, nullptr);
    m_Device = VK_NULL_HANDLE;

    if (enableValidationLayers) {
        auto func = (PFN_vkDestroyDebugUtilsMessengerEXT)vkGetInstanceProcAddr(m_Instance, "vkDestroyDebugUtilsMessengerEXT");
        if (func) {
            func(m_Instance, m_DebugMessenger, nullptr);
        }
    }

    vkDestroySurfaceKHR(m_Instance, m_Surface, nullptr);
    vkDestroyInstance(m_Instance, nullptr);
}

void Renderer::beginFrame() {
    if (m_Device == VK_NULL_HANDLE) {
        return;
    }

    // Wait for the frame slot we are about to reuse.
    vkWaitForFences(m_Device, 1, &m_InFlightFences[m_CurrentFrame], VK_TRUE, UINT64_MAX);

    // Safe point: everything that referenced resources queued for this slot is done.
    m_DeletionQueues[m_CurrentFrame].flush();

    // Command buffers are reused; reset before recording.
    if (!m_CommandBuffers.empty()) {
        vkResetCommandBuffer(m_CommandBuffers[m_CurrentFrame], 0);
    }

    // Reset bone palette arena for this frame and reserve slot 0 as identity.
    m_BonePaletteNextSlot = 0;
    if (m_BonePaletteStrideBytes != 0 && m_BonePaletteMapped[m_CurrentFrame]) {
        auto* dst = reinterpret_cast<glm::mat4*>(static_cast<char*>(m_BonePaletteMapped[m_CurrentFrame]));
        for (uint32_t i = 0; i < MAX_BONES; ++i) {
            dst[i] = glm::mat4(1.0f);
        }
        m_BonePaletteNextSlot = 1;
    }
}

void Renderer::endFrame() {
    m_CurrentFrame = (m_CurrentFrame + 1) % MAX_FRAMES_IN_FLIGHT;
    PROFILE_FRAME();
}

void Renderer::renderScene(Scene* scene) {
    PROFILE_SCOPE("RenderFrame");
    if (!scene) return;

    uint32_t imageIndex;
    VkResult result = vkAcquireNextImageKHR(m_Device, m_SwapChain, UINT64_MAX, 
        m_ImageAvailableSemaphores[m_CurrentFrame], VK_NULL_HANDLE, &imageIndex);

    if (result == VK_ERROR_OUT_OF_DATE_KHR) {
        recreateSwapChain();
        return;
    } else if (result != VK_SUCCESS && result != VK_SUBOPTIMAL_KHR) {
        throw std::runtime_error("failed to acquire swap chain image!");
    }

    // If this swapchain image is already being used by another in-flight frame, wait for it.
    if (imageIndex < m_ImagesInFlight.size() && m_ImagesInFlight[imageIndex] != VK_NULL_HANDLE) {
        vkWaitForFences(m_Device, 1, &m_ImagesInFlight[imageIndex], VK_TRUE, UINT64_MAX);
    }

    if (imageIndex < m_ImagesInFlight.size()) {
        m_ImagesInFlight[imageIndex] = m_InFlightFences[m_CurrentFrame];
    }

    vkResetFences(m_Device, 1, &m_InFlightFences[m_CurrentFrame]);

    recordCommandBuffer(m_CommandBuffers[m_CurrentFrame], imageIndex, scene);

    VkSubmitInfo submitInfo{};
    submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;

    VkSemaphore waitSemaphores[] = {m_ImageAvailableSemaphores[m_CurrentFrame]};
    VkPipelineStageFlags waitStages[] = {VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT};
    submitInfo.waitSemaphoreCount = 1;
    submitInfo.pWaitSemaphores = waitSemaphores;
    submitInfo.pWaitDstStageMask = waitStages;

    submitInfo.commandBufferCount = 1;
    submitInfo.pCommandBuffers = &m_CommandBuffers[m_CurrentFrame];

    VkSemaphore signalSemaphores[] = {m_RenderFinishedSemaphores[m_CurrentFrame]};
    submitInfo.signalSemaphoreCount = 1;
    submitInfo.pSignalSemaphores = signalSemaphores;
    
    if (vkQueueSubmit(m_GraphicsQueue, 1, &submitInfo, m_InFlightFences[m_CurrentFrame]) != VK_SUCCESS) {
        throw std::runtime_error("failed to submit draw command buffer!");
    }

    VkPresentInfoKHR presentInfo{};
    presentInfo.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
    presentInfo.waitSemaphoreCount = 1;
    presentInfo.pWaitSemaphores = &m_RenderFinishedSemaphores[m_CurrentFrame];

    VkSwapchainKHR swapChains[] = {m_SwapChain};
    presentInfo.swapchainCount = 1;
    presentInfo.pSwapchains = swapChains;
    presentInfo.pImageIndices = &imageIndex;

    result = vkQueuePresentKHR(m_PresentQueue, &presentInfo);

    if (result == VK_ERROR_OUT_OF_DATE_KHR || result == VK_SUBOPTIMAL_KHR || m_FramebufferResized) {
        m_FramebufferResized = false;
        recreateSwapChain();
    } else if (result != VK_SUCCESS) {
        throw std::runtime_error("failed to present swap chain image!");
    }

}

void Renderer::recreateSwapChain() {
    int width = 0, height = 0;
    glfwGetFramebufferSize(m_Window->getGLFWWindow(), &width, &height);
    while (width == 0 || height == 0) {
        glfwGetFramebufferSize(m_Window->getGLFWWindow(), &width, &height);
        glfwWaitEvents();
    }

    vkDeviceWaitIdle(m_Device);

    for (auto semaphore : m_RenderFinishedSemaphores) {
        if (semaphore) vkDestroySemaphore(m_Device, semaphore, nullptr);
    }
    for (auto semaphore : m_ImageAvailableSemaphores) {
        if (semaphore) vkDestroySemaphore(m_Device, semaphore, nullptr);
    }
    for (auto fence : m_InFlightFences) {
        if (fence) vkDestroyFence(m_Device, fence, nullptr);
    }
    m_RenderFinishedSemaphores.clear();
    m_ImageAvailableSemaphores.clear();
    m_InFlightFences.clear();
    m_ImagesInFlight.clear();

    if (!m_CommandBuffers.empty()) {
        vkFreeCommandBuffers(m_Device, m_CommandPool, static_cast<uint32_t>(m_CommandBuffers.size()), m_CommandBuffers.data());
        m_CommandBuffers.clear();
    }

    destroyOffscreenResources();
    destroySwapchainResources();
    destroyPipelineResources();

    createSwapChain();
    createImageViews();
    createRenderPass();
    createOffscreenRenderPass();
    createPickingRenderPass();
    createDepthResources();
    createGraphicsPipeline();
    createInstancedPipelines();
    createPickingPipeline();
    createOutlinePipeline();
    createSkyPipeline();
    createFramebuffers();
    createOffscreenResources();
    createCommandBuffers();
    createSyncObjects();
    m_CurrentFrame = 0;

    if (m_ResizeCallback) {
        m_ResizeCallback(static_cast<int>(m_SwapChainExtent.width), static_cast<int>(m_SwapChainExtent.height));
    }
}

uint32_t Renderer::findMemoryType(uint32_t typeFilter, VkMemoryPropertyFlags properties) {
    VkPhysicalDeviceMemoryProperties memProperties;
    vkGetPhysicalDeviceMemoryProperties(m_PhysicalDevice, &memProperties);

    for (uint32_t i = 0; i < memProperties.memoryTypeCount; i++) {
        if ((typeFilter & (1 << i)) && (memProperties.memoryTypes[i].propertyFlags & properties) == properties) {
            return i;
        }
    }
    throw std::runtime_error("failed to find suitable memory type!");
}

void Renderer::setVSyncEnabled(bool enabled) {
    if (m_VSyncEnabled == enabled) {
        return;
    }
    m_VSyncEnabled = enabled;

    if (m_Device == VK_NULL_HANDLE) {
        return;
    }

    recreateSwapChain();
}

void Renderer::defer(std::function<void()> fn) {
    if (!fn) return;

    // Queue for the current frame slot; it will flush when this slot is reused.
    m_DeletionQueues[m_CurrentFrame].push(std::move(fn));
}

void Renderer::immediateSubmit(const std::function<void(VkCommandBuffer)>& fn) {
    if (!fn) return;

    VkCommandPool commandPool = VK_NULL_HANDLE;
    VkCommandBuffer commandBuffer = VK_NULL_HANDLE;

    VkCommandPoolCreateInfo poolInfo{};
    poolInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    poolInfo.flags = VK_COMMAND_POOL_CREATE_TRANSIENT_BIT;
    poolInfo.queueFamilyIndex = m_QueueFamilyIndices.graphicsFamily.value();

    if (vkCreateCommandPool(m_Device, &poolInfo, nullptr, &commandPool) != VK_SUCCESS) {
        throw std::runtime_error("failed to create immediate command pool!");
    }

    VkCommandBufferAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    allocInfo.commandPool = commandPool;
    allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    allocInfo.commandBufferCount = 1;

    if (vkAllocateCommandBuffers(m_Device, &allocInfo, &commandBuffer) != VK_SUCCESS) {
        vkDestroyCommandPool(m_Device, commandPool, nullptr);
        throw std::runtime_error("failed to allocate immediate command buffer!");
    }

    VkCommandBufferBeginInfo beginInfo{};
    beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;

    if (vkBeginCommandBuffer(commandBuffer, &beginInfo) != VK_SUCCESS) {
        vkFreeCommandBuffers(m_Device, commandPool, 1, &commandBuffer);
        vkDestroyCommandPool(m_Device, commandPool, nullptr);
        throw std::runtime_error("failed to begin immediate command buffer!");
    }

    fn(commandBuffer);

    if (vkEndCommandBuffer(commandBuffer) != VK_SUCCESS) {
        vkFreeCommandBuffers(m_Device, commandPool, 1, &commandBuffer);
        vkDestroyCommandPool(m_Device, commandPool, nullptr);
        throw std::runtime_error("failed to end immediate command buffer!");
    }

    VkFence fence = VK_NULL_HANDLE;
    VkFenceCreateInfo fenceInfo{};
    fenceInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;

    if (vkCreateFence(m_Device, &fenceInfo, nullptr, &fence) != VK_SUCCESS) {
        vkFreeCommandBuffers(m_Device, commandPool, 1, &commandBuffer);
        vkDestroyCommandPool(m_Device, commandPool, nullptr);
        throw std::runtime_error("failed to create immediate fence!");
    }

    VkSubmitInfo submitInfo{};
    submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submitInfo.commandBufferCount = 1;
    submitInfo.pCommandBuffers = &commandBuffer;

    if (vkQueueSubmit(m_GraphicsQueue, 1, &submitInfo, fence) != VK_SUCCESS) {
        vkDestroyFence(m_Device, fence, nullptr);
        vkFreeCommandBuffers(m_Device, commandPool, 1, &commandBuffer);
        vkDestroyCommandPool(m_Device, commandPool, nullptr);
        throw std::runtime_error("failed to submit immediate command buffer!");
    }

    vkWaitForFences(m_Device, 1, &fence, VK_TRUE, UINT64_MAX);

    vkDestroyFence(m_Device, fence, nullptr);
    vkFreeCommandBuffers(m_Device, commandPool, 1, &commandBuffer);
    vkDestroyCommandPool(m_Device, commandPool, nullptr);
}

uint32_t Renderer::bindTexture(VkImageView imageView, VkSampler sampler) {
    if (imageView == VK_NULL_HANDLE || sampler == VK_NULL_HANDLE) {
        return 0;
    }
    if (m_BoundTextureCount >= MAX_TEXTURES) {
        return 0;
    }

    uint32_t index = m_BoundTextureCount++;

    VkDescriptorImageInfo imageInfo{};
    imageInfo.sampler = sampler;
    imageInfo.imageView = imageView;
    imageInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

    VkWriteDescriptorSet write{};
    write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    write.dstSet = m_DescriptorSet;
    write.dstBinding = 1;
    write.dstArrayElement = index;
    write.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    write.descriptorCount = 1;
    write.pImageInfo = &imageInfo;

    vkUpdateDescriptorSets(m_Device, 1, &write, 0, nullptr);

    return index;
}

void Renderer::updateTexture(uint32_t index, VkImageView imageView, VkSampler sampler) {
    if (index == 0 || index >= MAX_TEXTURES) {
        return;
    }
    if (imageView == VK_NULL_HANDLE || sampler == VK_NULL_HANDLE) {
        return;
    }

    VkDescriptorImageInfo imageInfo{};
    imageInfo.sampler = sampler;
    imageInfo.imageView = imageView;
    imageInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

    VkWriteDescriptorSet write{};
    write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    write.dstSet = m_DescriptorSet;
    write.dstBinding = 1;
    write.dstArrayElement = index;
    write.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    write.descriptorCount = 1;
    write.pImageInfo = &imageInfo;

    vkUpdateDescriptorSets(m_Device, 1, &write, 0, nullptr);
}

uint32_t Renderer::pickEntityId(uint32_t x, uint32_t y) {
    if (m_PickingImage == VK_NULL_HANDLE || m_PickingImageMemory == VK_NULL_HANDLE) {
        return UINT32_MAX;
    }

    if (x >= m_SwapChainExtent.width || y >= m_SwapChainExtent.height) {
        return UINT32_MAX;
    }

    VkBuffer stagingBuffer = VK_NULL_HANDLE;
    VkDeviceMemory stagingMemory = VK_NULL_HANDLE;

    VkBufferCreateInfo bufferInfo{};
    bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    bufferInfo.size = sizeof(uint32_t);
    bufferInfo.usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT;
    bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    if (vkCreateBuffer(m_Device, &bufferInfo, nullptr, &stagingBuffer) != VK_SUCCESS) {
        return UINT32_MAX;
    }

    VkMemoryRequirements memRequirements{};
    vkGetBufferMemoryRequirements(m_Device, stagingBuffer, &memRequirements);

    VkMemoryAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    allocInfo.allocationSize = memRequirements.size;
    allocInfo.memoryTypeIndex = findMemoryType(memRequirements.memoryTypeBits,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);

    if (vkAllocateMemory(m_Device, &allocInfo, nullptr, &stagingMemory) != VK_SUCCESS) {
        vkDestroyBuffer(m_Device, stagingBuffer, nullptr);
        return UINT32_MAX;
    }

    vkBindBufferMemory(m_Device, stagingBuffer, stagingMemory, 0);

    immediateSubmit([&](VkCommandBuffer commandBuffer) {
        VkImageMemoryBarrier barrier{};
        barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
        barrier.oldLayout = m_PickingImageLayout;
        barrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
        barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.image = m_PickingImage;
        barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        barrier.subresourceRange.baseMipLevel = 0;
        barrier.subresourceRange.levelCount = 1;
        barrier.subresourceRange.baseArrayLayer = 0;
        barrier.subresourceRange.layerCount = 1;

        VkPipelineStageFlags srcStage = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
        barrier.srcAccessMask = 0;
        if (m_PickingImageLayout == VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL) {
            srcStage = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
            barrier.srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
        }

        barrier.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;

        vkCmdPipelineBarrier(commandBuffer, srcStage, VK_PIPELINE_STAGE_TRANSFER_BIT, 0,
            0, nullptr, 0, nullptr, 1, &barrier);

        VkBufferImageCopy region{};
        region.bufferOffset = 0;
        region.bufferRowLength = 0;
        region.bufferImageHeight = 0;
        region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        region.imageSubresource.mipLevel = 0;
        region.imageSubresource.baseArrayLayer = 0;
        region.imageSubresource.layerCount = 1;
        region.imageOffset = {static_cast<int32_t>(x), static_cast<int32_t>(y), 0};
        region.imageExtent = {1, 1, 1};

        vkCmdCopyImageToBuffer(commandBuffer, m_PickingImage, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
            stagingBuffer, 1, &region);

        barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
        barrier.newLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
        barrier.srcAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
        barrier.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;

        vkCmdPipelineBarrier(commandBuffer, VK_PIPELINE_STAGE_TRANSFER_BIT,
            VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, 0, 0, nullptr, 0, nullptr, 1, &barrier);
    });

    m_PickingImageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

    uint32_t picked = 0;
    void* data = nullptr;
    if (vkMapMemory(m_Device, stagingMemory, 0, sizeof(uint32_t), 0, &data) == VK_SUCCESS && data) {
        picked = *reinterpret_cast<uint32_t*>(data);
        vkUnmapMemory(m_Device, stagingMemory);
    }

    vkFreeMemory(m_Device, stagingMemory, nullptr);
    vkDestroyBuffer(m_Device, stagingBuffer, nullptr);

    if (picked == 0) {
        return UINT32_MAX;
    }

    return picked - 1u;
}

void Renderer::createInstance() {
    if (enableValidationLayers && !checkValidationLayerSupport()) {
        throw std::runtime_error("validation layers requested, but not available!");
    }

    VkApplicationInfo appInfo{};
    appInfo.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
    appInfo.pApplicationName = "Atlas Engine";
    appInfo.applicationVersion = VK_MAKE_VERSION(1, 0, 0);
    appInfo.pEngineName = "Atlas";
    appInfo.engineVersion = VK_MAKE_VERSION(1, 0, 0);
    appInfo.apiVersion = VK_API_VERSION_1_0;

    VkInstanceCreateInfo createInfo{};
    createInfo.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
    createInfo.pApplicationInfo = &appInfo;

    uint32_t glfwExtensionCount = 0;
    const char** glfwExtensions = glfwGetRequiredInstanceExtensions(&glfwExtensionCount);
    std::vector<const char*> extensions(glfwExtensions, glfwExtensions + glfwExtensionCount);

    if (enableValidationLayers) {
        extensions.push_back(VK_EXT_DEBUG_UTILS_EXTENSION_NAME);
        extensions.push_back(VK_EXT_VALIDATION_FEATURES_EXTENSION_NAME);
    }

    createInfo.enabledExtensionCount = static_cast<uint32_t>(extensions.size());
    createInfo.ppEnabledExtensionNames = extensions.data();

    VkDebugUtilsMessengerCreateInfoEXT debugCreateInfo{};
    VkValidationFeaturesEXT validationFeatures{};
    std::array<VkValidationFeatureEnableEXT, 1> enabledValidationFeatures{};

    if (enableValidationLayers) {
        createInfo.enabledLayerCount = static_cast<uint32_t>(validationLayers.size());
        createInfo.ppEnabledLayerNames = validationLayers.data();

        debugCreateInfo = {};
        debugCreateInfo.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT;
        // WARNING|ERROR + VALIDATION|PERFORMANCE only: drops VERBOSE/GENERAL
        // loader spam (ICD discovery, OBS hook, registry GUIDs) while keeping
        // every real warning and error.
        debugCreateInfo.messageSeverity =
            VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT;
        debugCreateInfo.messageType =
            VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT;
        debugCreateInfo.pfnUserCallback = debugCallback;

        // Synchronization validation only. BEST_PRACTICES is intentionally off:
        // small dedicated allocations (light/uniform buffers) are a deliberate
        // choice until the renderer migrates fully to the VMA sub-allocator.
        enabledValidationFeatures[0] = VK_VALIDATION_FEATURE_ENABLE_SYNCHRONIZATION_VALIDATION_EXT;

        validationFeatures.sType = VK_STRUCTURE_TYPE_VALIDATION_FEATURES_EXT;
        validationFeatures.enabledValidationFeatureCount = static_cast<uint32_t>(enabledValidationFeatures.size());
        validationFeatures.pEnabledValidationFeatures = enabledValidationFeatures.data();
        validationFeatures.pNext = &debugCreateInfo;

        createInfo.pNext = &validationFeatures;
    } else {
        createInfo.enabledLayerCount = 0;
        createInfo.pNext = nullptr;
    }

    if (vkCreateInstance(&createInfo, nullptr, &m_Instance) != VK_SUCCESS) {
        throw std::runtime_error("failed to create instance!");
    }
}

void Renderer::setupDebugMessenger() {
    if (!enableValidationLayers) return;

    VkDebugUtilsMessengerCreateInfoEXT createInfo{};
    createInfo.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT;
    // Keep in sync with createInstance(): WARNING|ERROR + VALIDATION|PERFORMANCE.
    createInfo.messageSeverity =
        VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT;
    createInfo.messageType =
        VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT;
    createInfo.pfnUserCallback = debugCallback;

    auto func = (PFN_vkCreateDebugUtilsMessengerEXT)vkGetInstanceProcAddr(m_Instance, "vkCreateDebugUtilsMessengerEXT");
    if (func && func(m_Instance, &createInfo, nullptr, &m_DebugMessenger) != VK_SUCCESS) {
        throw std::runtime_error("failed to set up debug messenger!");
    }
}

void Renderer::createSurface() {
    if (glfwCreateWindowSurface(m_Instance, m_Window->getGLFWWindow(), nullptr, &m_Surface) != VK_SUCCESS) {
        throw std::runtime_error("failed to create window surface!");
    }
}

void Renderer::pickPhysicalDevice() {
    uint32_t deviceCount = 0;
    vkEnumeratePhysicalDevices(m_Instance, &deviceCount, nullptr);
    if (deviceCount == 0) {
        throw std::runtime_error("failed to find GPUs with Vulkan support!");
    }

    std::vector<VkPhysicalDevice> devices(deviceCount);
    vkEnumeratePhysicalDevices(m_Instance, &deviceCount, devices.data());

    for (const auto& device : devices) {
        if (isDeviceSuitable(device)) {
            m_PhysicalDevice = device;
            break;
        }
    }

    if (m_PhysicalDevice == VK_NULL_HANDLE) {
        throw std::runtime_error("failed to find a suitable GPU!");
    }
}

void Renderer::createLogicalDevice() {
    m_QueueFamilyIndices = findQueueFamilies(m_PhysicalDevice);
    QueueFamilyIndices indices = m_QueueFamilyIndices;

    std::vector<VkDeviceQueueCreateInfo> queueCreateInfos;
    std::set<uint32_t> uniqueQueueFamilies = {indices.graphicsFamily.value(), indices.presentFamily.value()};

    float queuePriority = 1.0f;
    for (uint32_t queueFamily : uniqueQueueFamilies) {
        VkDeviceQueueCreateInfo queueCreateInfo{};
        queueCreateInfo.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
        queueCreateInfo.queueFamilyIndex = queueFamily;
        queueCreateInfo.queueCount = 1;
        queueCreateInfo.pQueuePriorities = &queuePriority;
        queueCreateInfos.push_back(queueCreateInfo);
    }

    VkPhysicalDeviceFeatures supportedFeatures{};
    vkGetPhysicalDeviceFeatures(m_PhysicalDevice, &supportedFeatures);

    VkPhysicalDeviceFeatures deviceFeatures{};
    deviceFeatures.samplerAnisotropy = supportedFeatures.samplerAnisotropy;

    VkDeviceCreateInfo createInfo{};
    createInfo.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
    createInfo.queueCreateInfoCount = static_cast<uint32_t>(queueCreateInfos.size());
    createInfo.pQueueCreateInfos = queueCreateInfos.data();
    createInfo.pEnabledFeatures = &deviceFeatures;
    createInfo.enabledExtensionCount = static_cast<uint32_t>(deviceExtensions.size());
    createInfo.ppEnabledExtensionNames = deviceExtensions.data();

    // Device layers are deprecated since Vulkan 1.0 — only instance layers are valid.
    // See https://docs.vulkan.org/spec/latest/appendices/legacy.html#legacy-devicelayers
    // and VUID-VkDeviceCreateInfo-enabledLayerCount-12384.
    createInfo.enabledLayerCount = 0;
    createInfo.ppEnabledLayerNames = nullptr;

    if (vkCreateDevice(m_PhysicalDevice, &createInfo, nullptr, &m_Device) != VK_SUCCESS) {
        throw std::runtime_error("failed to create logical device!");
    }

    vkGetDeviceQueue(m_Device, indices.graphicsFamily.value(), 0, &m_GraphicsQueue);
    vkGetDeviceQueue(m_Device, indices.presentFamily.value(), 0, &m_PresentQueue);
}

void Renderer::createSwapChain() {
    SwapChainSupportDetails swapChainSupport = querySwapChainSupport(m_PhysicalDevice);

    VkSurfaceFormatKHR surfaceFormat = chooseSwapSurfaceFormat(swapChainSupport.formats);
    VkPresentModeKHR presentMode = chooseSwapPresentMode(swapChainSupport.presentModes);
    VkExtent2D extent = chooseSwapExtent(swapChainSupport.capabilities);

    uint32_t imageCount = swapChainSupport.capabilities.minImageCount + 1;
    if (swapChainSupport.capabilities.maxImageCount > 0 && imageCount > swapChainSupport.capabilities.maxImageCount) {
        imageCount = swapChainSupport.capabilities.maxImageCount;
    }

    VkSwapchainCreateInfoKHR createInfo{};
    createInfo.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
    createInfo.surface = m_Surface;
    createInfo.minImageCount = imageCount;
    createInfo.imageFormat = surfaceFormat.format;
    createInfo.imageColorSpace = surfaceFormat.colorSpace;
    createInfo.imageExtent = extent;
    createInfo.imageArrayLayers = 1;
    createInfo.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;

    QueueFamilyIndices indices = findQueueFamilies(m_PhysicalDevice);
    uint32_t queueFamilyIndices[] = {indices.graphicsFamily.value(), indices.presentFamily.value()};

    if (indices.graphicsFamily != indices.presentFamily) {
        createInfo.imageSharingMode = VK_SHARING_MODE_CONCURRENT;
        createInfo.queueFamilyIndexCount = 2;
        createInfo.pQueueFamilyIndices = queueFamilyIndices;
    } else {
        createInfo.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
    }

    createInfo.preTransform = swapChainSupport.capabilities.currentTransform;
    createInfo.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
    createInfo.presentMode = presentMode;
    createInfo.clipped = VK_TRUE;

    if (vkCreateSwapchainKHR(m_Device, &createInfo, nullptr, &m_SwapChain) != VK_SUCCESS) {
        throw std::runtime_error("failed to create swap chain!");
    }

    setDebugName(m_Device, VK_OBJECT_TYPE_SWAPCHAIN_KHR, reinterpret_cast<uint64_t>(m_SwapChain), "Swapchain");

    vkGetSwapchainImagesKHR(m_Device, m_SwapChain, &imageCount, nullptr);
    m_SwapChainImages.resize(imageCount);
    vkGetSwapchainImagesKHR(m_Device, m_SwapChain, &imageCount, m_SwapChainImages.data());

    for (size_t i = 0; i < m_SwapChainImages.size(); i++) {
        std::string name = std::string("SwapchainImage[") + std::to_string(i) + "]";
        setDebugName(m_Device, VK_OBJECT_TYPE_IMAGE, reinterpret_cast<uint64_t>(m_SwapChainImages[i]), name.c_str());
    }

    m_SwapChainImageFormat = surfaceFormat.format;
    m_SwapChainExtent = extent;
}

void Renderer::createImageViews() {
    m_SwapChainImageViews.resize(m_SwapChainImages.size());

    for (size_t i = 0; i < m_SwapChainImages.size(); i++) {
        VkImageViewCreateInfo createInfo{};
        createInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        createInfo.image = m_SwapChainImages[i];
        createInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
        createInfo.format = m_SwapChainImageFormat;
        createInfo.components.r = VK_COMPONENT_SWIZZLE_IDENTITY;
        createInfo.components.g = VK_COMPONENT_SWIZZLE_IDENTITY;
        createInfo.components.b = VK_COMPONENT_SWIZZLE_IDENTITY;
        createInfo.components.a = VK_COMPONENT_SWIZZLE_IDENTITY;
        createInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        createInfo.subresourceRange.baseMipLevel = 0;
        createInfo.subresourceRange.levelCount = 1;
        createInfo.subresourceRange.baseArrayLayer = 0;
        createInfo.subresourceRange.layerCount = 1;

        if (vkCreateImageView(m_Device, &createInfo, nullptr, &m_SwapChainImageViews[i]) != VK_SUCCESS) {
            throw std::runtime_error("failed to create image views!");
        }

        {
            std::string name = std::string("SwapchainImageView[") + std::to_string(i) + "]";
            setDebugName(m_Device, VK_OBJECT_TYPE_IMAGE_VIEW, reinterpret_cast<uint64_t>(m_SwapChainImageViews[i]), name.c_str());
        }
    }
}

namespace {
// Canonical two-way subpass dependencies shared IDENTICALLY by every render
// pass (swapchain, offscreen, picking). Attachments are reused across frames
// and sampled/copied afterwards, so the store + layout transition out of the
// pass must chain against both later sampling/transfer reads and the next
// frame's layout transition (fixes sync-validation WRITE_AFTER_WRITE hazards
// on vkCmdBeginRenderPass). Masks must stay identical everywhere: validation
// compares pDependencies between a pipeline's render pass and the pass
// instance it is used in (vkCmdDraw*). Superset stages/accesses are harmless.
void sharedSubpassDependencies(VkSubpassDependency (&deps)[2]) {
    deps[0].srcSubpass = VK_SUBPASS_EXTERNAL;
    deps[0].dstSubpass = 0;
    deps[0].srcStageMask = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT | VK_PIPELINE_STAGE_TRANSFER_BIT |
        VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT | VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
    deps[0].srcAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_TRANSFER_READ_BIT |
        VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
    deps[0].dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT | VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
    deps[0].dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
    deps[0].dependencyFlags = 0;

    deps[1].srcSubpass = 0;
    deps[1].dstSubpass = VK_SUBPASS_EXTERNAL;
    deps[1].srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT | VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT;
    deps[1].srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
    deps[1].dstStageMask = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT | VK_PIPELINE_STAGE_TRANSFER_BIT |
        VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT | VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
    deps[1].dstAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_TRANSFER_READ_BIT |
        VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
    deps[1].dependencyFlags = 0;
}
} // namespace

void Renderer::createRenderPass() {
    VkAttachmentDescription colorAttachment{};
    colorAttachment.format = m_SwapChainImageFormat;
    colorAttachment.samples = VK_SAMPLE_COUNT_1_BIT;
    colorAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    colorAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    colorAttachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    colorAttachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    colorAttachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    colorAttachment.finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;

    VkAttachmentDescription depthAttachment{};
    depthAttachment.format = VK_FORMAT_D32_SFLOAT;
    depthAttachment.samples = VK_SAMPLE_COUNT_1_BIT;
    depthAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    depthAttachment.storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    depthAttachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    depthAttachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    depthAttachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    depthAttachment.finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

    VkAttachmentReference colorAttachmentRef{};
    colorAttachmentRef.attachment = 0;
    colorAttachmentRef.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

    VkAttachmentReference depthAttachmentRef{};
    depthAttachmentRef.attachment = 1;
    depthAttachmentRef.layout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

    VkSubpassDescription subpass{};
    subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
    subpass.colorAttachmentCount = 1;
    subpass.pColorAttachments = &colorAttachmentRef;
    subpass.pDepthStencilAttachment = &depthAttachmentRef;

    // Two-way dependencies shared IDENTICALLY by all render passes (swapchain,
    // offscreen, picking): attachments are reused across frames and sampled /
    // copied afterwards, so the store + layout transition must chain against
    // both later sampling and the next frame's transition (fixes sync-validation
    // WRITE_AFTER_WRITE hazards). Identical masks keep every pipeline compatible
    // with every pass (validation compares pDependencies on vkCmdDraw*). See
    // sharedSubpassDependencies() below for the canonical pattern.

    VkSubpassDependency dependencies[2];
    sharedSubpassDependencies(dependencies);

    VkAttachmentDescription attachments[] = {colorAttachment, depthAttachment};
    VkRenderPassCreateInfo renderPassInfo{};
    renderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
    renderPassInfo.attachmentCount = 2;
    renderPassInfo.pAttachments = attachments;
    renderPassInfo.subpassCount = 1;
    renderPassInfo.pSubpasses = &subpass;
    renderPassInfo.dependencyCount = 2;
    renderPassInfo.pDependencies = dependencies;

    if (vkCreateRenderPass(m_Device, &renderPassInfo, nullptr, &m_RenderPass) != VK_SUCCESS) {
        throw std::runtime_error("failed to create render pass!");
    }

    setDebugName(m_Device, VK_OBJECT_TYPE_RENDER_PASS, reinterpret_cast<uint64_t>(m_RenderPass), "SwapchainRenderPass");
}

VkFormat Renderer::findDepthFormat() {
    return VK_FORMAT_D32_SFLOAT;
}

void Renderer::createDepthResources() {
    VkFormat depthFormat = findDepthFormat();
    
    VkImageCreateInfo imageInfo{};
    imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    imageInfo.imageType = VK_IMAGE_TYPE_2D;
    imageInfo.extent.width = m_SwapChainExtent.width;
    imageInfo.extent.height = m_SwapChainExtent.height;
    imageInfo.extent.depth = 1;
    imageInfo.mipLevels = 1;
    imageInfo.arrayLayers = 1;
    imageInfo.format = depthFormat;
    imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
    imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    imageInfo.usage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;
    imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
    imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    imageInfo.flags = 0;

    if (vkCreateImage(m_Device, &imageInfo, nullptr, &m_DepthImage) != VK_SUCCESS) {
        throw std::runtime_error("failed to create depth image!");
    }

    setDebugName(m_Device, VK_OBJECT_TYPE_IMAGE, reinterpret_cast<uint64_t>(m_DepthImage), "DepthImage");

    VkMemoryRequirements memRequirements;
    vkGetImageMemoryRequirements(m_Device, m_DepthImage, &memRequirements);

    VkMemoryAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    allocInfo.allocationSize = memRequirements.size;
    allocInfo.memoryTypeIndex = findMemoryType(memRequirements.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);

    if (vkAllocateMemory(m_Device, &allocInfo, nullptr, &m_DepthMemory) != VK_SUCCESS) {
        throw std::runtime_error("failed to allocate depth image memory!");
    }

    vkBindImageMemory(m_Device, m_DepthImage, m_DepthMemory, 0);

    VkImageViewCreateInfo viewInfo{};
    viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    viewInfo.image = m_DepthImage;
    viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
    viewInfo.format = depthFormat;
    viewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
    viewInfo.subresourceRange.baseMipLevel = 0;
    viewInfo.subresourceRange.levelCount = 1;
    viewInfo.subresourceRange.baseArrayLayer = 0;
    viewInfo.subresourceRange.layerCount = 1;

    if (vkCreateImageView(m_Device, &viewInfo, nullptr, &m_DepthImageView) != VK_SUCCESS) {
        throw std::runtime_error("failed to create depth image view!");
    }

    setDebugName(m_Device, VK_OBJECT_TYPE_IMAGE_VIEW, reinterpret_cast<uint64_t>(m_DepthImageView), "DepthImageView");
}

void Renderer::createOffscreenRenderPass() {
    VkAttachmentDescription colorAttachment{};
    colorAttachment.format = m_SwapChainImageFormat;
    colorAttachment.samples = VK_SAMPLE_COUNT_1_BIT;
    colorAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    colorAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    colorAttachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    colorAttachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    colorAttachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    colorAttachment.finalLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

    VkAttachmentDescription depthAttachment{};
    depthAttachment.format = VK_FORMAT_D32_SFLOAT;
    depthAttachment.samples = VK_SAMPLE_COUNT_1_BIT;
    depthAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    depthAttachment.storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    depthAttachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    depthAttachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    depthAttachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    depthAttachment.finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

    VkAttachmentReference colorAttachmentRef{};
    colorAttachmentRef.attachment = 0;
    colorAttachmentRef.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

    VkAttachmentReference depthAttachmentRef{};
    depthAttachmentRef.attachment = 1;
    depthAttachmentRef.layout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

    VkSubpassDescription subpass{};
    subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
    subpass.colorAttachmentCount = 1;
    subpass.pColorAttachments = &colorAttachmentRef;
    subpass.pDepthStencilAttachment = &depthAttachmentRef;

    // Same canonical dependencies as every other pass (see above).
    VkSubpassDependency dependencies[2];
    sharedSubpassDependencies(dependencies);

    VkAttachmentDescription attachments[] = {colorAttachment, depthAttachment};
    VkRenderPassCreateInfo renderPassInfo{};
    renderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
    renderPassInfo.attachmentCount = 2;
    renderPassInfo.pAttachments = attachments;
    renderPassInfo.subpassCount = 1;
    renderPassInfo.pSubpasses = &subpass;
    renderPassInfo.dependencyCount = 2;
    renderPassInfo.pDependencies = dependencies;

    if (vkCreateRenderPass(m_Device, &renderPassInfo, nullptr, &m_OffscreenRenderPass) != VK_SUCCESS) {
        throw std::runtime_error("failed to create offscreen render pass!");
    }

    setDebugName(m_Device, VK_OBJECT_TYPE_RENDER_PASS, reinterpret_cast<uint64_t>(m_OffscreenRenderPass), "OffscreenRenderPass");
}

void Renderer::createPickingRenderPass() {
    VkAttachmentDescription pickingAttachment{};
    pickingAttachment.format = VK_FORMAT_R32_UINT;
    pickingAttachment.samples = VK_SAMPLE_COUNT_1_BIT;
    pickingAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    pickingAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    pickingAttachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    pickingAttachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    pickingAttachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    pickingAttachment.finalLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

    VkAttachmentDescription depthAttachment{};
    depthAttachment.format = VK_FORMAT_D32_SFLOAT;
    depthAttachment.samples = VK_SAMPLE_COUNT_1_BIT;
    depthAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    depthAttachment.storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    depthAttachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    depthAttachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    depthAttachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    depthAttachment.finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

    VkAttachmentReference colorRef{};
    colorRef.attachment = 0;
    colorRef.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

    VkAttachmentReference depthRef{};
    depthRef.attachment = 1;
    depthRef.layout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

    VkSubpassDescription subpass{};
    subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
    subpass.colorAttachmentCount = 1;
    subpass.pColorAttachments = &colorRef;
    subpass.pDepthStencilAttachment = &depthRef;

    // Same canonical dependencies as every other pass (see above).
    VkSubpassDependency dependencies[2];
    sharedSubpassDependencies(dependencies);

    VkAttachmentDescription attachments[] = {pickingAttachment, depthAttachment};
    VkRenderPassCreateInfo renderPassInfo{};
    renderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
    renderPassInfo.attachmentCount = 2;
    renderPassInfo.pAttachments = attachments;
    renderPassInfo.subpassCount = 1;
    renderPassInfo.pSubpasses = &subpass;
    renderPassInfo.dependencyCount = 2;
    renderPassInfo.pDependencies = dependencies;

    if (vkCreateRenderPass(m_Device, &renderPassInfo, nullptr, &m_PickingRenderPass) != VK_SUCCESS) {
        throw std::runtime_error("failed to create picking render pass!");
    }

    setDebugName(m_Device, VK_OBJECT_TYPE_RENDER_PASS, reinterpret_cast<uint64_t>(m_PickingRenderPass), "PickingRenderPass");
}

void Renderer::createGraphicsPipeline() {
    auto vertShaderCode = readFile("shaders/pbr_vert.spv");
    auto fragShaderCode = readFile("shaders/pbr_frag.spv");

    VkShaderModule vertShaderModule = createShaderModule(vertShaderCode);
    VkShaderModule fragShaderModule = createShaderModule(fragShaderCode);

    VkPipelineShaderStageCreateInfo vertShaderStageInfo{};
    vertShaderStageInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    vertShaderStageInfo.stage = VK_SHADER_STAGE_VERTEX_BIT;
    vertShaderStageInfo.module = vertShaderModule;
    vertShaderStageInfo.pName = "main";

    VkPipelineShaderStageCreateInfo fragShaderStageInfo{};
    fragShaderStageInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    fragShaderStageInfo.stage = VK_SHADER_STAGE_FRAGMENT_BIT;
    fragShaderStageInfo.module = fragShaderModule;
    fragShaderStageInfo.pName = "main";

    VkPipelineShaderStageCreateInfo shaderStages[] = {vertShaderStageInfo, fragShaderStageInfo};

    const auto bindingDescription = Vertex::getBindingDescription();
    const auto attributeDescriptions = Vertex::getAttributeDescriptions();

    VkPipelineVertexInputStateCreateInfo vertexInputInfo{};
    vertexInputInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
    vertexInputInfo.vertexBindingDescriptionCount = 1;
    vertexInputInfo.pVertexBindingDescriptions = &bindingDescription;
    vertexInputInfo.vertexAttributeDescriptionCount = static_cast<uint32_t>(attributeDescriptions.size());
    vertexInputInfo.pVertexAttributeDescriptions = attributeDescriptions.data();

    VkPipelineInputAssemblyStateCreateInfo inputAssembly{};
    inputAssembly.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
    inputAssembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    inputAssembly.primitiveRestartEnable = VK_FALSE;

    VkViewport viewport{};
    viewport.x = 0.0f;
    viewport.y = 0.0f;
    viewport.width = static_cast<float>(m_SwapChainExtent.width);
    viewport.height = static_cast<float>(m_SwapChainExtent.height);
    viewport.minDepth = 0.0f;
    viewport.maxDepth = 1.0f;

    VkRect2D scissor{};
    scissor.offset = {0, 0};
    scissor.extent = m_SwapChainExtent;

    VkPipelineViewportStateCreateInfo viewportState{};
    viewportState.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
    viewportState.viewportCount = 1;
    viewportState.pViewports = &viewport;
    viewportState.scissorCount = 1;
    viewportState.pScissors = &scissor;

    VkPipelineRasterizationStateCreateInfo rasterizer{};
    rasterizer.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
    rasterizer.depthClampEnable = VK_FALSE;
    rasterizer.rasterizerDiscardEnable = VK_FALSE;
    rasterizer.polygonMode = VK_POLYGON_MODE_FILL;
    rasterizer.lineWidth = 1.0f;
    rasterizer.cullMode = VK_CULL_MODE_BACK_BIT;
    rasterizer.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
    rasterizer.depthBiasEnable = VK_FALSE;

    VkPipelineDepthStencilStateCreateInfo depthStencil{};
    depthStencil.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
    depthStencil.depthTestEnable = VK_TRUE;
    depthStencil.depthWriteEnable = VK_TRUE;
    depthStencil.depthCompareOp = VK_COMPARE_OP_LESS;
    depthStencil.depthBoundsTestEnable = VK_FALSE;
    depthStencil.stencilTestEnable = VK_FALSE;
    depthStencil.minDepthBounds = 0.0f;
    depthStencil.maxDepthBounds = 1.0f;

    VkPipelineMultisampleStateCreateInfo multisampling{};
    multisampling.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
    multisampling.sampleShadingEnable = VK_FALSE;
    multisampling.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

    VkPipelineColorBlendAttachmentState colorBlendAttachment{};
    colorBlendAttachment.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | 
        VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
    colorBlendAttachment.blendEnable = VK_FALSE;

    VkPipelineColorBlendStateCreateInfo colorBlending{};
    colorBlending.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
    colorBlending.logicOpEnable = VK_FALSE;
    colorBlending.attachmentCount = 1;
    colorBlending.pAttachments = &colorBlendAttachment;

    VkPushConstantRange pushConstantRange{};
    pushConstantRange.stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
    pushConstantRange.offset = 0;
    pushConstantRange.size = sizeof(PushConstants);

    VkDescriptorSetLayoutBinding lightBinding{};
    lightBinding.binding = 0;
    lightBinding.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    lightBinding.descriptorCount = 1;
    lightBinding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

    VkDescriptorSetLayoutBinding samplerBinding{};
    samplerBinding.binding = 1;
    samplerBinding.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    samplerBinding.descriptorCount = MAX_TEXTURES;
    samplerBinding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

    VkDescriptorSetLayoutBinding shadowBinding{};
    shadowBinding.binding = 2;
    shadowBinding.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    shadowBinding.descriptorCount = 1;
    shadowBinding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

    std::array<VkDescriptorSetLayoutBinding, 3> bindings = {lightBinding, samplerBinding, shadowBinding};

    VkDescriptorSetLayoutCreateInfo layoutInfo{};
    layoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    layoutInfo.bindingCount = static_cast<uint32_t>(bindings.size());
    layoutInfo.pBindings = bindings.data();

    if (m_DescriptorSetLayout == VK_NULL_HANDLE && vkCreateDescriptorSetLayout(m_Device, &layoutInfo, nullptr, &m_DescriptorSetLayout) != VK_SUCCESS) {
        throw std::runtime_error("failed to create descriptor set layout!");
    }

    VkPipelineLayoutCreateInfo pipelineLayoutInfo{};
    VkDescriptorSetLayout setLayouts[] = {m_DescriptorSetLayout, m_BonesDescriptorSetLayout};

    pipelineLayoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    pipelineLayoutInfo.setLayoutCount = 2;
    pipelineLayoutInfo.pSetLayouts = setLayouts;
    pipelineLayoutInfo.pushConstantRangeCount = 1;
    pipelineLayoutInfo.pPushConstantRanges = &pushConstantRange;

    if (vkCreatePipelineLayout(m_Device, &pipelineLayoutInfo, nullptr, &m_PipelineLayout) != VK_SUCCESS) {
        throw std::runtime_error("failed to create pipeline layout!");
    }

    VkGraphicsPipelineCreateInfo pipelineInfo{};
    pipelineInfo.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
    pipelineInfo.stageCount = 2;
    pipelineInfo.pStages = shaderStages;
    pipelineInfo.pVertexInputState = &vertexInputInfo;
    pipelineInfo.pInputAssemblyState = &inputAssembly;
    pipelineInfo.pViewportState = &viewportState;
    pipelineInfo.pRasterizationState = &rasterizer;
    pipelineInfo.pDepthStencilState = &depthStencil;
    pipelineInfo.pMultisampleState = &multisampling;
    pipelineInfo.pColorBlendState = &colorBlending;
    pipelineInfo.layout = m_PipelineLayout;
    pipelineInfo.renderPass = m_RenderPass;
    pipelineInfo.subpass = 0;

    if (vkCreateGraphicsPipelines(m_Device, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &m_GraphicsPipeline) != VK_SUCCESS) {
        throw std::runtime_error("failed to create graphics pipeline!");
    }

    setDebugName(m_Device, VK_OBJECT_TYPE_PIPELINE, reinterpret_cast<uint64_t>(m_GraphicsPipeline), "PBRPipeline");

    VkPipelineRasterizationStateCreateInfo rasterFrontCull = rasterizer;
    rasterFrontCull.cullMode = VK_CULL_MODE_FRONT_BIT;
    pipelineInfo.pRasterizationState = &rasterFrontCull;

    if (vkCreateGraphicsPipelines(m_Device, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &m_GraphicsPipelineFrontCull) != VK_SUCCESS) {
        throw std::runtime_error("failed to create front-cull graphics pipeline!");
    }

    setDebugName(m_Device, VK_OBJECT_TYPE_PIPELINE, reinterpret_cast<uint64_t>(m_GraphicsPipelineFrontCull), "PBRPipeline_FrontCull");

    VkPipelineRasterizationStateCreateInfo rasterNoCull = rasterizer;
    rasterNoCull.cullMode = VK_CULL_MODE_NONE;
    pipelineInfo.pRasterizationState = &rasterNoCull;

    if (vkCreateGraphicsPipelines(m_Device, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &m_GraphicsPipelineNoCull) != VK_SUCCESS) {
        throw std::runtime_error("failed to create no-cull graphics pipeline!");
    }

    setDebugName(m_Device, VK_OBJECT_TYPE_PIPELINE, reinterpret_cast<uint64_t>(m_GraphicsPipelineNoCull), "PBRPipeline_NoCull");

    pipelineInfo.pRasterizationState = &rasterizer;

    // Transparent pipeline: alpha blending + depth test, but no depth writes.
    colorBlendAttachment.blendEnable = VK_TRUE;
    colorBlendAttachment.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
    colorBlendAttachment.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
    colorBlendAttachment.colorBlendOp = VK_BLEND_OP_ADD;
    colorBlendAttachment.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
    colorBlendAttachment.dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
    colorBlendAttachment.alphaBlendOp = VK_BLEND_OP_ADD;

    depthStencil.depthWriteEnable = VK_FALSE;

    if (vkCreateGraphicsPipelines(m_Device, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &m_GraphicsPipelineBlend) != VK_SUCCESS) {
        throw std::runtime_error("failed to create transparent graphics pipeline!");
    }

    setDebugName(m_Device, VK_OBJECT_TYPE_PIPELINE, reinterpret_cast<uint64_t>(m_GraphicsPipelineBlend), "PBRPipeline_AlphaBlend");

    pipelineInfo.pRasterizationState = &rasterFrontCull;

    if (vkCreateGraphicsPipelines(m_Device, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &m_GraphicsPipelineBlendFrontCull) != VK_SUCCESS) {
        throw std::runtime_error("failed to create transparent front-cull graphics pipeline!");
    }

    setDebugName(m_Device, VK_OBJECT_TYPE_PIPELINE, reinterpret_cast<uint64_t>(m_GraphicsPipelineBlendFrontCull), "PBRPipeline_AlphaBlend_FrontCull");

    pipelineInfo.pRasterizationState = &rasterNoCull;

    if (vkCreateGraphicsPipelines(m_Device, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &m_GraphicsPipelineBlendNoCull) != VK_SUCCESS) {
        throw std::runtime_error("failed to create transparent no-cull graphics pipeline!");
    }

    setDebugName(m_Device, VK_OBJECT_TYPE_PIPELINE, reinterpret_cast<uint64_t>(m_GraphicsPipelineBlendNoCull), "PBRPipeline_AlphaBlend_NoCull");

    vkDestroyShaderModule(m_Device, fragShaderModule, nullptr);
    vkDestroyShaderModule(m_Device, vertShaderModule, nullptr);
}

// TDD §6: instanced PBR pipelines (opaque cull modes). Same push constants,
// descriptor sets and render pass as the per-entity pipelines; only the vertex
// input gains binding 1 (per-instance mat4 @ locations 6-9) and the vertex
// shader (pbr_instanced_vert) consumes it. Reuses m_PipelineLayout.
void Renderer::createInstancedPipelines() {
    auto vertShaderCode = readFile("shaders/pbr_instanced_vert.spv");
    auto fragShaderCode = readFile("shaders/pbr_frag.spv");

    VkShaderModule vertShaderModule = createShaderModule(vertShaderCode);
    VkShaderModule fragShaderModule = createShaderModule(fragShaderCode);

    VkPipelineShaderStageCreateInfo vertShaderStageInfo{};
    vertShaderStageInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    vertShaderStageInfo.stage = VK_SHADER_STAGE_VERTEX_BIT;
    vertShaderStageInfo.module = vertShaderModule;
    vertShaderStageInfo.pName = "main";

    VkPipelineShaderStageCreateInfo fragShaderStageInfo{};
    fragShaderStageInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    fragShaderStageInfo.stage = VK_SHADER_STAGE_FRAGMENT_BIT;
    fragShaderStageInfo.module = fragShaderModule;
    fragShaderStageInfo.pName = "main";

    VkPipelineShaderStageCreateInfo shaderStages[] = {vertShaderStageInfo, fragShaderStageInfo};

    const auto bindingDescription = Vertex::getBindingDescription();
    const auto attributeDescriptions = Vertex::getAttributeDescriptions();

    VkVertexInputBindingDescription bindings[2]{};
    bindings[0] = bindingDescription;
    bindings[1].binding = 1;
    bindings[1].stride = sizeof(glm::mat4);
    bindings[1].inputRate = VK_VERTEX_INPUT_RATE_INSTANCE;

    std::array<VkVertexInputAttributeDescription, 10> attributes{};
    for (size_t i = 0; i < attributeDescriptions.size(); ++i) {
        attributes[i] = attributeDescriptions[i];
    }
    for (uint32_t col = 0; col < 4; ++col) {
        auto& a = attributes[6 + col];
        a.binding = 1;
        a.location = 6 + col;
        a.format = VK_FORMAT_R32G32B32A32_SFLOAT;
        a.offset = col * 16u;
    }

    VkPipelineVertexInputStateCreateInfo vertexInputInfo{};
    vertexInputInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
    vertexInputInfo.vertexBindingDescriptionCount = 2;
    vertexInputInfo.pVertexBindingDescriptions = bindings;
    vertexInputInfo.vertexAttributeDescriptionCount = static_cast<uint32_t>(attributes.size());
    vertexInputInfo.pVertexAttributeDescriptions = attributes.data();

    VkPipelineInputAssemblyStateCreateInfo inputAssembly{};
    inputAssembly.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
    inputAssembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    inputAssembly.primitiveRestartEnable = VK_FALSE;

    VkViewport viewport{};
    viewport.x = 0.0f;
    viewport.y = 0.0f;
    viewport.width = static_cast<float>(m_SwapChainExtent.width);
    viewport.height = static_cast<float>(m_SwapChainExtent.height);
    viewport.minDepth = 0.0f;
    viewport.maxDepth = 1.0f;

    VkRect2D scissor{};
    scissor.offset = {0, 0};
    scissor.extent = m_SwapChainExtent;

    VkPipelineViewportStateCreateInfo viewportState{};
    viewportState.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
    viewportState.viewportCount = 1;
    viewportState.pViewports = &viewport;
    viewportState.scissorCount = 1;
    viewportState.pScissors = &scissor;

    VkPipelineRasterizationStateCreateInfo rasterizer{};
    rasterizer.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
    rasterizer.depthClampEnable = VK_FALSE;
    rasterizer.rasterizerDiscardEnable = VK_FALSE;
    rasterizer.polygonMode = VK_POLYGON_MODE_FILL;
    rasterizer.lineWidth = 1.0f;
    rasterizer.cullMode = VK_CULL_MODE_BACK_BIT;
    rasterizer.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
    rasterizer.depthBiasEnable = VK_FALSE;

    VkPipelineDepthStencilStateCreateInfo depthStencil{};
    depthStencil.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
    depthStencil.depthTestEnable = VK_TRUE;
    depthStencil.depthWriteEnable = VK_TRUE;
    depthStencil.depthCompareOp = VK_COMPARE_OP_LESS;
    depthStencil.depthBoundsTestEnable = VK_FALSE;
    depthStencil.stencilTestEnable = VK_FALSE;
    depthStencil.minDepthBounds = 0.0f;
    depthStencil.maxDepthBounds = 1.0f;

    VkPipelineMultisampleStateCreateInfo multisampling{};
    multisampling.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
    multisampling.sampleShadingEnable = VK_FALSE;
    multisampling.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

    VkPipelineColorBlendAttachmentState colorBlendAttachment{};
    colorBlendAttachment.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
        VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
    colorBlendAttachment.blendEnable = VK_FALSE;

    VkPipelineColorBlendStateCreateInfo colorBlending{};
    colorBlending.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
    colorBlending.logicOpEnable = VK_FALSE;
    colorBlending.attachmentCount = 1;
    colorBlending.pAttachments = &colorBlendAttachment;

    VkGraphicsPipelineCreateInfo pipelineInfo{};
    pipelineInfo.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
    pipelineInfo.stageCount = 2;
    pipelineInfo.pStages = shaderStages;
    pipelineInfo.pVertexInputState = &vertexInputInfo;
    pipelineInfo.pInputAssemblyState = &inputAssembly;
    pipelineInfo.pViewportState = &viewportState;
    pipelineInfo.pRasterizationState = &rasterizer;
    pipelineInfo.pDepthStencilState = &depthStencil;
    pipelineInfo.pMultisampleState = &multisampling;
    pipelineInfo.pColorBlendState = &colorBlending;
    pipelineInfo.layout = m_PipelineLayout;
    pipelineInfo.renderPass = m_RenderPass;
    pipelineInfo.subpass = 0;

    if (vkCreateGraphicsPipelines(m_Device, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &m_GraphicsPipelineInstanced) != VK_SUCCESS) {
        vkDestroyShaderModule(m_Device, fragShaderModule, nullptr);
        vkDestroyShaderModule(m_Device, vertShaderModule, nullptr);
        throw std::runtime_error("failed to create instanced graphics pipeline!");
    }
    setDebugName(m_Device, VK_OBJECT_TYPE_PIPELINE, reinterpret_cast<uint64_t>(m_GraphicsPipelineInstanced), "PBRPipeline_Instanced");

    VkPipelineRasterizationStateCreateInfo rasterFrontCull = rasterizer;
    rasterFrontCull.cullMode = VK_CULL_MODE_FRONT_BIT;
    pipelineInfo.pRasterizationState = &rasterFrontCull;

    if (vkCreateGraphicsPipelines(m_Device, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &m_GraphicsPipelineInstancedFrontCull) != VK_SUCCESS) {
        vkDestroyShaderModule(m_Device, fragShaderModule, nullptr);
        vkDestroyShaderModule(m_Device, vertShaderModule, nullptr);
        throw std::runtime_error("failed to create front-cull instanced graphics pipeline!");
    }
    setDebugName(m_Device, VK_OBJECT_TYPE_PIPELINE, reinterpret_cast<uint64_t>(m_GraphicsPipelineInstancedFrontCull), "PBRPipeline_Instanced_FrontCull");

    VkPipelineRasterizationStateCreateInfo rasterNoCull = rasterizer;
    rasterNoCull.cullMode = VK_CULL_MODE_NONE;
    pipelineInfo.pRasterizationState = &rasterNoCull;

    if (vkCreateGraphicsPipelines(m_Device, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &m_GraphicsPipelineInstancedNoCull) != VK_SUCCESS) {
        vkDestroyShaderModule(m_Device, fragShaderModule, nullptr);
        vkDestroyShaderModule(m_Device, vertShaderModule, nullptr);
        throw std::runtime_error("failed to create no-cull instanced graphics pipeline!");
    }
    setDebugName(m_Device, VK_OBJECT_TYPE_PIPELINE, reinterpret_cast<uint64_t>(m_GraphicsPipelineInstancedNoCull), "PBRPipeline_Instanced_NoCull");

    vkDestroyShaderModule(m_Device, fragShaderModule, nullptr);
    vkDestroyShaderModule(m_Device, vertShaderModule, nullptr);
}

void Renderer::createInstanceBuffers() {
    destroyInstanceBuffers();
    const VkDeviceSize bufferSize = static_cast<VkDeviceSize>(MAX_INSTANCES_PER_FRAME) * sizeof(glm::mat4);
    for (uint32_t frame = 0; frame < MAX_FRAMES_IN_FLIGHT; ++frame) {
        VkBufferCreateInfo bufferInfo{};
        bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
        bufferInfo.size = bufferSize;
        bufferInfo.usage = VK_BUFFER_USAGE_VERTEX_BUFFER_BIT;
        bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

        if (vkCreateBuffer(m_Device, &bufferInfo, nullptr, &m_InstanceBuffers[frame]) != VK_SUCCESS) {
            throw std::runtime_error("failed to create instance staging buffer!");
        }

        VkMemoryRequirements memRequirements{};
        vkGetBufferMemoryRequirements(m_Device, m_InstanceBuffers[frame], &memRequirements);

        VkMemoryAllocateInfo allocInfo{};
        allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
        allocInfo.allocationSize = memRequirements.size;
        allocInfo.memoryTypeIndex = findMemoryType(memRequirements.memoryTypeBits,
            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);

        if (vkAllocateMemory(m_Device, &allocInfo, nullptr, &m_InstanceMemories[frame]) != VK_SUCCESS) {
            throw std::runtime_error("failed to allocate instance staging memory!");
        }

        vkBindBufferMemory(m_Device, m_InstanceBuffers[frame], m_InstanceMemories[frame], 0);

        void* mapped = nullptr;
        if (vkMapMemory(m_Device, m_InstanceMemories[frame], 0, bufferSize, 0, &mapped) != VK_SUCCESS) {
            throw std::runtime_error("failed to map instance staging memory!");
        }
        m_InstanceMapped[frame] = mapped;

        char name[48];
        std::snprintf(name, sizeof(name), "InstanceStagingBuffer[%u]", frame);
        setDebugName(m_Device, VK_OBJECT_TYPE_BUFFER, reinterpret_cast<uint64_t>(m_InstanceBuffers[frame]), name);
    }
}

void Renderer::destroyInstanceBuffers() {
    for (uint32_t frame = 0; frame < MAX_FRAMES_IN_FLIGHT; ++frame) {
        if (m_InstanceMapped[frame] && m_InstanceMemories[frame]) {
            vkUnmapMemory(m_Device, m_InstanceMemories[frame]);
            m_InstanceMapped[frame] = nullptr;
        }
        if (m_InstanceBuffers[frame]) { vkDestroyBuffer(m_Device, m_InstanceBuffers[frame], nullptr); m_InstanceBuffers[frame] = VK_NULL_HANDLE; }
        if (m_InstanceMemories[frame]) { vkFreeMemory(m_Device, m_InstanceMemories[frame], nullptr); m_InstanceMemories[frame] = VK_NULL_HANDLE; }
    }
}

// TDD §4.2/§5.2: unit quad for HLOD1 impostor billboards. Matches the Vertex
// layout (pos/color/uv/normal/joints/weights); normal faces +Z, billboarded
// CPU-side so the quad always faces the camera.
void Renderer::createImpostorQuad() {
    destroyImpostorQuad();
    struct QuadVert { glm::vec3 pos; glm::vec3 color; glm::vec2 uv; glm::vec3 normal; glm::uvec4 joints; glm::vec4 weights; };
    const QuadVert verts[4] = {
        {{-0.5f, -0.5f, 0.0f}, {1,1,1}, {0,0}, {0,0,1}, {0,0,0,0}, {1,0,0,0}},
        {{ 0.5f, -0.5f, 0.0f}, {1,1,1}, {1,0}, {0,0,1}, {0,0,0,0}, {1,0,0,0}},
        {{ 0.5f,  0.5f, 0.0f}, {1,1,1}, {1,1}, {0,0,1}, {0,0,0,0}, {1,0,0,0}},
        {{-0.5f,  0.5f, 0.0f}, {1,1,1}, {0,1}, {0,0,1}, {0,0,0,0}, {1,0,0,0}},
    };
    const uint32_t indices[6] = {0, 1, 2, 2, 3, 0};

    auto makeBuffer = [&](VkDeviceSize size, VkBufferUsageFlags usage, VkBuffer& outBuf, VkDeviceMemory& outMem, const void* src) {
        VkBufferCreateInfo bufferInfo{};
        bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
        bufferInfo.size = size;
        bufferInfo.usage = usage;
        bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
        if (vkCreateBuffer(m_Device, &bufferInfo, nullptr, &outBuf) != VK_SUCCESS) {
            throw std::runtime_error("failed to create impostor quad buffer!");
        }
        VkMemoryRequirements memReq{};
        vkGetBufferMemoryRequirements(m_Device, outBuf, &memReq);
        VkMemoryAllocateInfo allocInfo{};
        allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
        allocInfo.allocationSize = memReq.size;
        allocInfo.memoryTypeIndex = findMemoryType(memReq.memoryTypeBits,
            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
        if (vkAllocateMemory(m_Device, &allocInfo, nullptr, &outMem) != VK_SUCCESS) {
            throw std::runtime_error("failed to allocate impostor quad memory!");
        }
        vkBindBufferMemory(m_Device, outBuf, outMem, 0);
        void* mapped = nullptr;
        vkMapMemory(m_Device, outMem, 0, size, 0, &mapped);
        std::memcpy(mapped, src, static_cast<size_t>(size));
        vkUnmapMemory(m_Device, outMem);
    };
    makeBuffer(sizeof(verts), VK_BUFFER_USAGE_VERTEX_BUFFER_BIT, m_ImpostorQuadVB, m_ImpostorQuadVBMem, verts);
    makeBuffer(sizeof(indices), VK_BUFFER_USAGE_INDEX_BUFFER_BIT, m_ImpostorQuadIB, m_ImpostorQuadIBMem, indices);
    setDebugName(m_Device, VK_OBJECT_TYPE_BUFFER, reinterpret_cast<uint64_t>(m_ImpostorQuadVB), "ImpostorQuadVB");
}

// TDD §5 auto-LOD: upload CPU-side simplified geometry to device-local
// buffers (staging pattern, mirrors ModelLoader without the Assimp layer).
Renderer::StaticMeshBuffers Renderer::uploadStaticMesh(const void* verts, size_t vertSize, size_t vertCount,
                                                       const uint32_t* indices, size_t indexCount) {
    StaticMeshBuffers out;
    if (!verts || !indices || vertSize == 0 || vertCount == 0 || indexCount == 0) {
        return out;
    }
    const VkDeviceSize vertexBytes = static_cast<VkDeviceSize>(vertSize * vertCount);
    const VkDeviceSize indexBytes = static_cast<VkDeviceSize>(sizeof(uint32_t) * indexCount);
    auto uploadOne = [&](VkDeviceSize byteSize, const void* src, VkBufferUsageFlags dstUsage,
                         VkBuffer& outBuf, VkDeviceMemory& outMem) -> bool {
        VkBufferCreateInfo stagingInfo{};
        stagingInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
        stagingInfo.size = byteSize;
        stagingInfo.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
        stagingInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
        VkBuffer staging = VK_NULL_HANDLE;
        VkDeviceMemory stagingMem = VK_NULL_HANDLE;
        if (vkCreateBuffer(m_Device, &stagingInfo, nullptr, &staging) != VK_SUCCESS) return false;
        VkMemoryRequirements req{};
        vkGetBufferMemoryRequirements(m_Device, staging, &req);
        VkMemoryAllocateInfo allocInfo{};
        allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
        allocInfo.allocationSize = req.size;
        allocInfo.memoryTypeIndex = findMemoryType(req.memoryTypeBits,
            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
        if (vkAllocateMemory(m_Device, &allocInfo, nullptr, &stagingMem) != VK_SUCCESS) {
            vkDestroyBuffer(m_Device, staging, nullptr);
            return false;
        }
        vkBindBufferMemory(m_Device, staging, stagingMem, 0);
        void* mapped = nullptr;
        vkMapMemory(m_Device, stagingMem, 0, byteSize, 0, &mapped);
        std::memcpy(mapped, src, static_cast<size_t>(byteSize));
        vkUnmapMemory(m_Device, stagingMem);

        VkBufferCreateInfo dstInfo{};
        dstInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
        dstInfo.size = byteSize;
        dstInfo.usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT | dstUsage;
        dstInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
        if (vkCreateBuffer(m_Device, &dstInfo, nullptr, &outBuf) != VK_SUCCESS) {
            vkFreeMemory(m_Device, stagingMem, nullptr);
            vkDestroyBuffer(m_Device, staging, nullptr);
            return false;
        }
        vkGetBufferMemoryRequirements(m_Device, outBuf, &req);
        allocInfo.allocationSize = req.size;
        allocInfo.memoryTypeIndex = findMemoryType(req.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
        if (vkAllocateMemory(m_Device, &allocInfo, nullptr, &outMem) != VK_SUCCESS) {
            vkDestroyBuffer(m_Device, outBuf, nullptr);
            outBuf = VK_NULL_HANDLE;
            vkFreeMemory(m_Device, stagingMem, nullptr);
            vkDestroyBuffer(m_Device, staging, nullptr);
            return false;
        }
        vkBindBufferMemory(m_Device, outBuf, outMem, 0);
        immediateSubmit([&](VkCommandBuffer cb) {
            VkBufferCopy region{};
            region.srcOffset = 0;
            region.dstOffset = 0;
            region.size = byteSize;
            vkCmdCopyBuffer(cb, staging, outBuf, 1, &region);
        });
        vkFreeMemory(m_Device, stagingMem, nullptr);
        vkDestroyBuffer(m_Device, staging, nullptr);
        return true;
    };
    if (!uploadOne(vertexBytes, verts, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT, out.vertexBuffer, out.vertexMemory)) {
        return StaticMeshBuffers{};
    }
    if (!uploadOne(indexBytes, indices, VK_BUFFER_USAGE_INDEX_BUFFER_BIT, out.indexBuffer, out.indexMemory)) {
        vkDestroyBuffer(m_Device, out.vertexBuffer, nullptr);
        vkFreeMemory(m_Device, out.vertexMemory, nullptr);
        return StaticMeshBuffers{};
    }
    out.vertexCount = static_cast<uint32_t>(vertCount);
    out.indexCount = static_cast<uint32_t>(indexCount);
    setDebugName(m_Device, VK_OBJECT_TYPE_BUFFER, reinterpret_cast<uint64_t>(out.vertexBuffer), "SimplifiedVB");
    return out;
}

void Renderer::cacheSimplifiedVariant(VkBuffer srcVB, VkBuffer srcIB, int level, StaticMeshBuffers buffers) {
    if (srcVB == VK_NULL_HANDLE || srcIB == VK_NULL_HANDLE) return;
    if (level < 1 || level > 2) return;
    if (buffers.vertexBuffer == VK_NULL_HANDLE || buffers.indexBuffer == VK_NULL_HANDLE) return;
    SimplifiedKey key{srcVB, srcIB};
    auto& slot = m_SimplifiedVariants[key]; // creates empty pair on demand
    StaticMeshBuffers& prev = slot[static_cast<size_t>(level - 1)];
    if (prev.vertexBuffer != VK_NULL_HANDLE) {
        vkDestroyBuffer(m_Device, prev.vertexBuffer, nullptr);
        vkFreeMemory(m_Device, prev.vertexMemory, nullptr);
        vkDestroyBuffer(m_Device, prev.indexBuffer, nullptr);
        vkFreeMemory(m_Device, prev.indexMemory, nullptr);
    }
    slot[static_cast<size_t>(level - 1)] = buffers;
}

bool Renderer::findSimplifiedVariant(VkBuffer srcVB, VkBuffer srcIB, int level, StaticMeshBuffers* out) const {
    if (!m_AutoLODEnabled || level < 1 || level > 2 || !out) return false;
    SimplifiedKey key{srcVB, srcIB};
    auto it = m_SimplifiedVariants.find(key);
    if (it == m_SimplifiedVariants.end()) return false;
    const StaticMeshBuffers& v = it->second[static_cast<size_t>(level - 1)];
    if (v.vertexBuffer == VK_NULL_HANDLE || v.indexBuffer == VK_NULL_HANDLE || v.indexCount == 0) return false;
    *out = v;
    return true;
}

uint32_t Renderer::getSimplifiedVariantCount() const {
    uint32_t n = 0;
    for (const auto& [key, pair] : m_SimplifiedVariants) {
        (void)key;
        for (const auto& v : pair) {
            if (v.vertexBuffer != VK_NULL_HANDLE) n++;
        }
    }
    return n;
}

void Renderer::destroyImpostorQuad() {
    if (m_ImpostorQuadVB) { vkDestroyBuffer(m_Device, m_ImpostorQuadVB, nullptr); m_ImpostorQuadVB = VK_NULL_HANDLE; }
    if (m_ImpostorQuadVBMem) { vkFreeMemory(m_Device, m_ImpostorQuadVBMem, nullptr); m_ImpostorQuadVBMem = VK_NULL_HANDLE; }
    if (m_ImpostorQuadIB) { vkDestroyBuffer(m_Device, m_ImpostorQuadIB, nullptr); m_ImpostorQuadIB = VK_NULL_HANDLE; }
    if (m_ImpostorQuadIBMem) { vkFreeMemory(m_Device, m_ImpostorQuadIBMem, nullptr); m_ImpostorQuadIBMem = VK_NULL_HANDLE; }
}

void Renderer::createPickingPipeline() {
    auto vertShaderCode = readFile("shaders/picking_vert.spv");
    auto fragShaderCode = readFile("shaders/picking_frag.spv");

    VkShaderModule vertShaderModule = createShaderModule(vertShaderCode);
    VkShaderModule fragShaderModule = createShaderModule(fragShaderCode);

    VkPipelineShaderStageCreateInfo vertShaderStageInfo{};
    vertShaderStageInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    vertShaderStageInfo.stage = VK_SHADER_STAGE_VERTEX_BIT;
    vertShaderStageInfo.module = vertShaderModule;
    vertShaderStageInfo.pName = "main";

    VkPipelineShaderStageCreateInfo fragShaderStageInfo{};
    fragShaderStageInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    fragShaderStageInfo.stage = VK_SHADER_STAGE_FRAGMENT_BIT;
    fragShaderStageInfo.module = fragShaderModule;
    fragShaderStageInfo.pName = "main";

    VkPipelineShaderStageCreateInfo shaderStages[] = {vertShaderStageInfo, fragShaderStageInfo};

    const auto bindingDescription = Vertex::getBindingDescription();
    const auto attributeDescriptions = Vertex::getAttributeDescriptions();

    VkPipelineVertexInputStateCreateInfo vertexInputInfo{};
    vertexInputInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
    vertexInputInfo.vertexBindingDescriptionCount = 1;
    vertexInputInfo.pVertexBindingDescriptions = &bindingDescription;
    vertexInputInfo.vertexAttributeDescriptionCount = static_cast<uint32_t>(attributeDescriptions.size());
    vertexInputInfo.pVertexAttributeDescriptions = attributeDescriptions.data();

    VkPipelineInputAssemblyStateCreateInfo inputAssembly{};
    inputAssembly.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
    inputAssembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    inputAssembly.primitiveRestartEnable = VK_FALSE;

    VkViewport viewport{};
    viewport.x = 0.0f;
    viewport.y = 0.0f;
    viewport.width = static_cast<float>(m_SwapChainExtent.width);
    viewport.height = static_cast<float>(m_SwapChainExtent.height);
    viewport.minDepth = 0.0f;
    viewport.maxDepth = 1.0f;

    VkRect2D scissor{};
    scissor.offset = {0, 0};
    scissor.extent = m_SwapChainExtent;

    VkPipelineViewportStateCreateInfo viewportState{};
    viewportState.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
    viewportState.viewportCount = 1;
    viewportState.pViewports = &viewport;
    viewportState.scissorCount = 1;
    viewportState.pScissors = &scissor;

    VkPipelineRasterizationStateCreateInfo rasterizer{};
    rasterizer.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
    rasterizer.depthClampEnable = VK_FALSE;
    rasterizer.rasterizerDiscardEnable = VK_FALSE;
    rasterizer.polygonMode = VK_POLYGON_MODE_FILL;
    rasterizer.lineWidth = 1.0f;
    rasterizer.cullMode = VK_CULL_MODE_BACK_BIT;
    rasterizer.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
    rasterizer.depthBiasEnable = VK_FALSE;

    VkPipelineDepthStencilStateCreateInfo depthStencil{};
    depthStencil.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
    depthStencil.depthTestEnable = VK_TRUE;
    depthStencil.depthWriteEnable = VK_TRUE;
    depthStencil.depthCompareOp = VK_COMPARE_OP_LESS;
    depthStencil.depthBoundsTestEnable = VK_FALSE;
    depthStencil.stencilTestEnable = VK_FALSE;

    VkPipelineMultisampleStateCreateInfo multisampling{};
    multisampling.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
    multisampling.sampleShadingEnable = VK_FALSE;
    multisampling.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

    VkPipelineColorBlendAttachmentState colorBlendAttachment{};
    colorBlendAttachment.colorWriteMask = VK_COLOR_COMPONENT_R_BIT;
    colorBlendAttachment.blendEnable = VK_FALSE;

    VkPipelineColorBlendStateCreateInfo colorBlending{};
    colorBlending.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
    colorBlending.logicOpEnable = VK_FALSE;
    colorBlending.attachmentCount = 1;
    colorBlending.pAttachments = &colorBlendAttachment;

    VkPushConstantRange pushConstantRange{};
    pushConstantRange.stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
    pushConstantRange.offset = 0;
    pushConstantRange.size = sizeof(PickingPushConstants);

    VkPipelineLayoutCreateInfo pipelineLayoutInfo{};
    VkDescriptorSetLayout setLayouts[] = {m_DescriptorSetLayout, m_BonesDescriptorSetLayout};

    pipelineLayoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    pipelineLayoutInfo.setLayoutCount = 2;
    pipelineLayoutInfo.pSetLayouts = setLayouts;
    pipelineLayoutInfo.pushConstantRangeCount = 1;
    pipelineLayoutInfo.pPushConstantRanges = &pushConstantRange;

    if (vkCreatePipelineLayout(m_Device, &pipelineLayoutInfo, nullptr, &m_PickingPipelineLayout) != VK_SUCCESS) {
        vkDestroyShaderModule(m_Device, fragShaderModule, nullptr);
        vkDestroyShaderModule(m_Device, vertShaderModule, nullptr);
        throw std::runtime_error("failed to create picking pipeline layout!");
    }

    VkGraphicsPipelineCreateInfo pipelineInfo{};
    pipelineInfo.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
    pipelineInfo.stageCount = 2;
    pipelineInfo.pStages = shaderStages;
    pipelineInfo.pVertexInputState = &vertexInputInfo;
    pipelineInfo.pInputAssemblyState = &inputAssembly;
    pipelineInfo.pViewportState = &viewportState;
    pipelineInfo.pRasterizationState = &rasterizer;
    pipelineInfo.pDepthStencilState = &depthStencil;
    pipelineInfo.pMultisampleState = &multisampling;
    pipelineInfo.pColorBlendState = &colorBlending;
    pipelineInfo.layout = m_PickingPipelineLayout;
    pipelineInfo.renderPass = m_PickingRenderPass;
    pipelineInfo.subpass = 0;

    if (vkCreateGraphicsPipelines(m_Device, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &m_PickingPipeline) != VK_SUCCESS) {
        vkDestroyShaderModule(m_Device, fragShaderModule, nullptr);
        vkDestroyShaderModule(m_Device, vertShaderModule, nullptr);
        throw std::runtime_error("failed to create picking pipeline!");
    }

    setDebugName(m_Device, VK_OBJECT_TYPE_PIPELINE, reinterpret_cast<uint64_t>(m_PickingPipeline), "PickingPipeline");

    VkPipelineRasterizationStateCreateInfo rasterFrontCull = rasterizer;
    rasterFrontCull.cullMode = VK_CULL_MODE_FRONT_BIT;
    pipelineInfo.pRasterizationState = &rasterFrontCull;

    if (vkCreateGraphicsPipelines(m_Device, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &m_PickingPipelineFrontCull) != VK_SUCCESS) {
        vkDestroyShaderModule(m_Device, fragShaderModule, nullptr);
        vkDestroyShaderModule(m_Device, vertShaderModule, nullptr);
        throw std::runtime_error("failed to create front-cull picking pipeline!");
    }

    setDebugName(m_Device, VK_OBJECT_TYPE_PIPELINE, reinterpret_cast<uint64_t>(m_PickingPipelineFrontCull), "PickingPipeline_FrontCull");

    VkPipelineRasterizationStateCreateInfo rasterNoCull = rasterizer;
    rasterNoCull.cullMode = VK_CULL_MODE_NONE;
    pipelineInfo.pRasterizationState = &rasterNoCull;

    if (vkCreateGraphicsPipelines(m_Device, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &m_PickingPipelineNoCull) != VK_SUCCESS) {
        vkDestroyShaderModule(m_Device, fragShaderModule, nullptr);
        vkDestroyShaderModule(m_Device, vertShaderModule, nullptr);
        throw std::runtime_error("failed to create no-cull picking pipeline!");
    }

    setDebugName(m_Device, VK_OBJECT_TYPE_PIPELINE, reinterpret_cast<uint64_t>(m_PickingPipelineNoCull), "PickingPipeline_NoCull");

    vkDestroyShaderModule(m_Device, fragShaderModule, nullptr);
    vkDestroyShaderModule(m_Device, vertShaderModule, nullptr);
}

void Renderer::createShadowResources() {
    // Fixed-size depth image, sampled later by the PBR pass (binding 2).
    VkImageCreateInfo imageInfo{};
    imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    imageInfo.imageType = VK_IMAGE_TYPE_2D;
    imageInfo.extent.width = kShadowMapSize;
    imageInfo.extent.height = kShadowMapSize;
    imageInfo.extent.depth = 1;
    imageInfo.mipLevels = 1;
    imageInfo.arrayLayers = 1;
    imageInfo.format = VK_FORMAT_D32_SFLOAT;
    imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
    imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    imageInfo.usage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
    imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
    imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    if (vkCreateImage(m_Device, &imageInfo, nullptr, &m_ShadowImage) != VK_SUCCESS) {
        throw std::runtime_error("failed to create shadow image!");
    }
    setDebugName(m_Device, VK_OBJECT_TYPE_IMAGE, reinterpret_cast<uint64_t>(m_ShadowImage), "ShadowMapImage");

    VkMemoryRequirements memRequirements;
    vkGetImageMemoryRequirements(m_Device, m_ShadowImage, &memRequirements);

    VkMemoryAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    allocInfo.allocationSize = memRequirements.size;
    allocInfo.memoryTypeIndex = findMemoryType(memRequirements.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);

    if (vkAllocateMemory(m_Device, &allocInfo, nullptr, &m_ShadowMemory) != VK_SUCCESS) {
        throw std::runtime_error("failed to allocate shadow image memory!");
    }
    vkBindImageMemory(m_Device, m_ShadowImage, m_ShadowMemory, 0);

    VkImageViewCreateInfo viewInfo{};
    viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    viewInfo.image = m_ShadowImage;
    viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
    viewInfo.format = VK_FORMAT_D32_SFLOAT;
    viewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
    viewInfo.subresourceRange.baseMipLevel = 0;
    viewInfo.subresourceRange.levelCount = 1;
    viewInfo.subresourceRange.baseArrayLayer = 0;
    viewInfo.subresourceRange.layerCount = 1;

    if (vkCreateImageView(m_Device, &viewInfo, nullptr, &m_ShadowView) != VK_SUCCESS) {
        throw std::runtime_error("failed to create shadow image view!");
    }
    setDebugName(m_Device, VK_OBJECT_TYPE_IMAGE_VIEW, reinterpret_cast<uint64_t>(m_ShadowView), "ShadowMapView");

    // Comparison sampler: hardware PCF 2x2; outside the frustum reads lit.
    VkSamplerCreateInfo samplerInfo{};
    samplerInfo.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
    samplerInfo.magFilter = VK_FILTER_LINEAR;
    samplerInfo.minFilter = VK_FILTER_LINEAR;
    samplerInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST;
    samplerInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER;
    samplerInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER;
    samplerInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER;
    samplerInfo.borderColor = VK_BORDER_COLOR_FLOAT_OPAQUE_WHITE;
    samplerInfo.compareEnable = VK_TRUE;
    samplerInfo.compareOp = VK_COMPARE_OP_LESS_OR_EQUAL;
    samplerInfo.unnormalizedCoordinates = VK_FALSE;

    if (vkCreateSampler(m_Device, &samplerInfo, nullptr, &m_ShadowSampler) != VK_SUCCESS) {
        throw std::runtime_error("failed to create shadow sampler!");
    }
    setDebugName(m_Device, VK_OBJECT_TYPE_SAMPLER, reinterpret_cast<uint64_t>(m_ShadowSampler), "ShadowMapSampler");

    // Depth-only pass: UNDEFINED -> clear -> SHADER_READ_ONLY. The canonical
    // subpass dependencies cover both transitions, so no manual barriers.
    VkAttachmentDescription depthAttachment{};
    depthAttachment.format = VK_FORMAT_D32_SFLOAT;
    depthAttachment.samples = VK_SAMPLE_COUNT_1_BIT;
    depthAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    depthAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    depthAttachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    depthAttachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    depthAttachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    depthAttachment.finalLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

    VkAttachmentReference depthRef{};
    depthRef.attachment = 0;
    depthRef.layout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

    VkSubpassDescription subpass{};
    subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
    subpass.colorAttachmentCount = 0;
    subpass.pDepthStencilAttachment = &depthRef;

    VkSubpassDependency dependencies[2];
    sharedSubpassDependencies(dependencies);

    VkRenderPassCreateInfo renderPassInfo{};
    renderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
    renderPassInfo.attachmentCount = 1;
    renderPassInfo.pAttachments = &depthAttachment;
    renderPassInfo.subpassCount = 1;
    renderPassInfo.pSubpasses = &subpass;
    renderPassInfo.dependencyCount = 2;
    renderPassInfo.pDependencies = dependencies;

    if (vkCreateRenderPass(m_Device, &renderPassInfo, nullptr, &m_ShadowRenderPass) != VK_SUCCESS) {
        throw std::runtime_error("failed to create shadow render pass!");
    }
    setDebugName(m_Device, VK_OBJECT_TYPE_RENDER_PASS, reinterpret_cast<uint64_t>(m_ShadowRenderPass), "ShadowRenderPass");

    VkFramebufferCreateInfo framebufferInfo{};
    framebufferInfo.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
    framebufferInfo.renderPass = m_ShadowRenderPass;
    framebufferInfo.attachmentCount = 1;
    framebufferInfo.pAttachments = &m_ShadowView;
    framebufferInfo.width = kShadowMapSize;
    framebufferInfo.height = kShadowMapSize;
    framebufferInfo.layers = 1;

    if (vkCreateFramebuffer(m_Device, &framebufferInfo, nullptr, &m_ShadowFramebuffer) != VK_SUCCESS) {
        throw std::runtime_error("failed to create shadow framebuffer!");
    }
}

void Renderer::destroyShadowResources() {
    if (m_ShadowPipeline) { vkDestroyPipeline(m_Device, m_ShadowPipeline, nullptr); m_ShadowPipeline = VK_NULL_HANDLE; }
    if (m_ShadowPipelineLayout) { vkDestroyPipelineLayout(m_Device, m_ShadowPipelineLayout, nullptr); m_ShadowPipelineLayout = VK_NULL_HANDLE; }
    if (m_ShadowFramebuffer) { vkDestroyFramebuffer(m_Device, m_ShadowFramebuffer, nullptr); m_ShadowFramebuffer = VK_NULL_HANDLE; }
    if (m_ShadowRenderPass) { vkDestroyRenderPass(m_Device, m_ShadowRenderPass, nullptr); m_ShadowRenderPass = VK_NULL_HANDLE; }
    if (m_ShadowSampler) { vkDestroySampler(m_Device, m_ShadowSampler, nullptr); m_ShadowSampler = VK_NULL_HANDLE; }
    if (m_ShadowView) { vkDestroyImageView(m_Device, m_ShadowView, nullptr); m_ShadowView = VK_NULL_HANDLE; }
    if (m_ShadowImage) { vkDestroyImage(m_Device, m_ShadowImage, nullptr); m_ShadowImage = VK_NULL_HANDLE; }
    if (m_ShadowMemory) { vkFreeMemory(m_Device, m_ShadowMemory, nullptr); m_ShadowMemory = VK_NULL_HANDLE; }
    m_ShadowEnabledFrame = false;
}

void Renderer::createShadowPipeline() {
    auto vertShaderCode = readFile("shaders/shadow.spv");
    VkShaderModule vertShaderModule = createShaderModule(vertShaderCode);

    VkPipelineShaderStageCreateInfo vertShaderStageInfo{};
    vertShaderStageInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    vertShaderStageInfo.stage = VK_SHADER_STAGE_VERTEX_BIT;
    vertShaderStageInfo.module = vertShaderModule;
    vertShaderStageInfo.pName = "main";

    const auto bindingDescription = Vertex::getBindingDescription();
    const auto attributeDescriptions = Vertex::getAttributeDescriptions();

    VkPipelineVertexInputStateCreateInfo vertexInputInfo{};
    vertexInputInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
    vertexInputInfo.vertexBindingDescriptionCount = 1;
    vertexInputInfo.pVertexBindingDescriptions = &bindingDescription;
    vertexInputInfo.vertexAttributeDescriptionCount = static_cast<uint32_t>(attributeDescriptions.size());
    vertexInputInfo.pVertexAttributeDescriptions = attributeDescriptions.data();

    VkPipelineInputAssemblyStateCreateInfo inputAssembly{};
    inputAssembly.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
    inputAssembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    inputAssembly.primitiveRestartEnable = VK_FALSE;

    // Static 2048 viewport: never stale on swapchain resize.
    VkViewport viewport{};
    viewport.x = 0.0f;
    viewport.y = 0.0f;
    viewport.width = static_cast<float>(kShadowMapSize);
    viewport.height = static_cast<float>(kShadowMapSize);
    viewport.minDepth = 0.0f;
    viewport.maxDepth = 1.0f;

    VkRect2D scissor{};
    scissor.offset = {0, 0};
    scissor.extent = {kShadowMapSize, kShadowMapSize};

    VkPipelineViewportStateCreateInfo viewportState{};
    viewportState.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
    viewportState.viewportCount = 1;
    viewportState.pViewports = &viewport;
    viewportState.scissorCount = 1;
    viewportState.pScissors = &scissor;

    VkPipelineRasterizationStateCreateInfo rasterizer{};
    rasterizer.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
    rasterizer.depthClampEnable = VK_FALSE;
    rasterizer.rasterizerDiscardEnable = VK_FALSE;
    rasterizer.polygonMode = VK_POLYGON_MODE_FILL;
    rasterizer.lineWidth = 1.0f;
    rasterizer.cullMode = VK_CULL_MODE_BACK_BIT;
    rasterizer.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
    rasterizer.depthBiasEnable = VK_FALSE; // bias applied in pbr_frag instead

    VkPipelineDepthStencilStateCreateInfo depthStencil{};
    depthStencil.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
    depthStencil.depthTestEnable = VK_TRUE;
    depthStencil.depthWriteEnable = VK_TRUE;
    depthStencil.depthCompareOp = VK_COMPARE_OP_LESS;
    depthStencil.depthBoundsTestEnable = VK_FALSE;
    depthStencil.stencilTestEnable = VK_FALSE;

    VkPipelineMultisampleStateCreateInfo multisampling{};
    multisampling.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
    multisampling.sampleShadingEnable = VK_FALSE;
    multisampling.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

    VkPushConstantRange pushConstantRange{};
    pushConstantRange.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;
    pushConstantRange.offset = 0;
    pushConstantRange.size = sizeof(ShadowPushConstants);

    VkPipelineLayoutCreateInfo pipelineLayoutInfo{};
    pipelineLayoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    pipelineLayoutInfo.setLayoutCount = 0; // position-only shader, no descriptors
    pipelineLayoutInfo.pushConstantRangeCount = 1;
    pipelineLayoutInfo.pPushConstantRanges = &pushConstantRange;

    if (vkCreatePipelineLayout(m_Device, &pipelineLayoutInfo, nullptr, &m_ShadowPipelineLayout) != VK_SUCCESS) {
        vkDestroyShaderModule(m_Device, vertShaderModule, nullptr);
        throw std::runtime_error("failed to create shadow pipeline layout!");
    }

    VkGraphicsPipelineCreateInfo pipelineInfo{};
    pipelineInfo.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
    pipelineInfo.stageCount = 1;
    pipelineInfo.pStages = &vertShaderStageInfo;
    pipelineInfo.pVertexInputState = &vertexInputInfo;
    pipelineInfo.pInputAssemblyState = &inputAssembly;
    pipelineInfo.pViewportState = &viewportState;
    pipelineInfo.pRasterizationState = &rasterizer;
    pipelineInfo.pDepthStencilState = &depthStencil;
    pipelineInfo.pMultisampleState = &multisampling;
    pipelineInfo.pColorBlendState = nullptr; // depth-only: no color attachments
    pipelineInfo.layout = m_ShadowPipelineLayout;
    pipelineInfo.renderPass = m_ShadowRenderPass;
    pipelineInfo.subpass = 0;

    if (vkCreateGraphicsPipelines(m_Device, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &m_ShadowPipeline) != VK_SUCCESS) {
        vkDestroyShaderModule(m_Device, vertShaderModule, nullptr);
        throw std::runtime_error("failed to create shadow pipeline!");
    }
    setDebugName(m_Device, VK_OBJECT_TYPE_PIPELINE, reinterpret_cast<uint64_t>(m_ShadowPipeline), "ShadowPipeline");

    vkDestroyShaderModule(m_Device, vertShaderModule, nullptr);
}

void Renderer::updateLightsAndShadow(Scene* scene) {
    m_ShadowEnabledFrame = false;
    m_LightBufferData.shadowParams = glm::vec4(0.0f);
    m_LightBufferData.shadowViewProj = glm::mat4(1.0f);

    // Active camera for cameraPos upload + shadow-frustum centering (mirrors
    // the per-pass camera preference: primary game camera, else editor).
    glm::vec3 cameraPos(0.0f, 0.0f, 5.0f);
    glm::vec3 cameraTarget(0.0f);
    if (scene) {
        auto& registry = scene->getRegistry();
        entt::entity camEntity = entt::null;
        for (auto e : registry.view<Camera, ECS::GameCameraComponent>()) {
            if (camEntity == entt::null) camEntity = e;
            if (registry.get<ECS::GameCameraComponent>(e).primary) { camEntity = e; break; }
        }
        if (camEntity == entt::null) {
            auto editorView = registry.view<EditorCamera>();
            if (editorView.begin() != editorView.end()) camEntity = *editorView.begin();
        }
        if (camEntity == entt::null) {
            auto camView = registry.view<Camera>();
            if (camView.begin() != camView.end()) camEntity = *camView.begin();
        }
        if (camEntity != entt::null) {
            if (auto* c = registry.try_get<Camera>(camEntity)) {
                cameraPos = c->position;
                cameraTarget = c->target;
            } else if (auto* ec = registry.try_get<EditorCamera>(camEntity)) {
                cameraPos = ec->position;
                cameraTarget = ec->target;
            }
        }
    }
    m_LightBufferData.cameraPos = cameraPos;

    // Sun (Sun/Sky task): the first SunComponent owns slot 0 as the scene
    // directional light + shadow caster, ahead of ad-hoc LightComponents.
    // Absent sun + absent lights = legacy hardcoded default below.
    int slot = 0;
    entt::entity casterEntity = entt::null;
    glm::vec3 shadowDir(0.0f, -1.0f, 0.0f);
    // Shadow frustum half-extent (m): sun-owned when the sun casts (slider
    // in the Sun inspector), legacy fixed extent for ad-hoc light casters.
    float shadowExtent = kShadowOrthoExtent;

    // Caster resolution: the frag shadows slot 0 only, so slot 0 MUST be the
    // caster when one exists. Priority: casting sun first, then the first
    // cast-flagged directional light. Shadows only ever come from components
    // (a sun that doesn't cast yields to a directional that does).
    const ECS::SunComponent* sun = nullptr;
    entt::entity sunEntity = entt::null;
    float sunNightFade = 1.0f;
    bool sunCasts = false;
    entt::entity dirCaster = entt::null;
    if (scene) {
        auto& registry = scene->getRegistry();
        if (const ECS::SunComponent* s = findFirstSun(registry)) {
            sun = s;
            for (auto e : registry.view<ECS::SunComponent>()) { sunEntity = e; break; }
            const glm::vec3 toSun = sun->sunDirection();
            // Night fade: a sun below the horizon contributes no light
            // (0 at/below -0.08, full above +0.08).
            sunNightFade = glm::clamp((toSun.y + 0.08f) / 0.16f, 0.0f, 1.0f);
            sunCasts = sun->castShadows && toSun.y > 0.0f;
        }
        if (!sunCasts) {
            for (auto e : registry.view<ECS::LightComponent>()) {
                const auto& lc = registry.get<ECS::LightComponent>(e);
                if (lc.type == ECS::LightComponent::Type::Directional && lc.castShadows) {
                    dirCaster = e;
                    break;
                }
            }
        }
    }
    casterEntity = sunCasts ? sunEntity : dirCaster;

    // Sun fill (slot system: shadowed slot 0 when it casts, deferred slot 1
    // when yielding to a directional caster, lone slot 0 when unshadowed).
    auto fillSun = [&]() {
        Light& dst = m_LightBufferData.lights[slot];
        dst.color = sun->color;
        dst.intensity = sun->intensity * sunNightFade;
        dst.type = 1; // directional
        dst.direction = sun->lightDirection();
        dst.position = cameraTarget - dst.direction * 100.0f;
        ++slot;
    };
    bool sunPlaced = false;
    if (sunCasts) {
        fillSun();
        shadowDir = sun->lightDirection();
        shadowExtent = glm::clamp(sun->shadowRange, 5.0f, 250.0f);
        sunPlaced = true;
    }

    // Gather scene lights: shadow-casting directional first (slot 0), rest after.
    auto fillLight = [&](entt::registry& registry, entt::entity e, bool directional) {
        auto& src = registry.get<ECS::LightComponent>(e);
        Light& dst = m_LightBufferData.lights[slot];
        dst.color = src.color;
        dst.intensity = src.intensity;
        dst.type = directional ? 1 : 0;
        glm::vec3 pos(0.0f);
        glm::vec3 dir(0.0f, -1.0f, 0.0f);
        if (const auto* t = registry.try_get<Transform>(e)) {
            pos = t->position;
            glm::mat4 rot(1.0f);
            rot = glm::rotate(rot, glm::radians(t->rotation.x), glm::vec3(1.0f, 0.0f, 0.0f));
            rot = glm::rotate(rot, glm::radians(t->rotation.y), glm::vec3(0.0f, 1.0f, 0.0f));
            rot = glm::rotate(rot, glm::radians(t->rotation.z), glm::vec3(0.0f, 0.0f, 1.0f));
            dir = glm::normalize(glm::vec3(rot * glm::vec4(0.0f, 0.0f, -1.0f, 0.0f)));
        }
        dst.position = pos;
        dst.direction = dir;
        if (directional) shadowDir = dir;
        ++slot;
    };
    if (scene) {
        auto& registry = scene->getRegistry();
        if (dirCaster != entt::null && !sunCasts) {
            // Caster claims shadowed slot 0 (fillLight sets shadowDir from
            // the entity Transform). Skipped below to avoid double-fill.
            fillLight(registry, dirCaster, true);
        }
        if (sun != nullptr && !sunPlaced) {
            // Deferred sun: slot 1 behind a directional caster, or lone
            // unshadowed slot 0 when nothing casts.
            fillSun();
        }
        for (auto e : registry.view<ECS::LightComponent>()) {
            if (e == casterEntity) continue;
            if (slot >= 4) break;
            const auto& lc = registry.get<ECS::LightComponent>(e);
            fillLight(registry, e, lc.type == ECS::LightComponent::Type::Directional);
        }
    }
    if (slot == 0) {
        // Legacy default: preserves the exact look of scenes without lights.
        m_LightBufferData.lights[0].position = glm::vec3(5.0f, 5.0f, 5.0f);
        m_LightBufferData.lights[0].color = glm::vec3(1.0f, 1.0f, 1.0f);
        m_LightBufferData.lights[0].intensity = 50.0f;
        m_LightBufferData.lights[0].direction = glm::vec3(0.0f, -1.0f, 0.0f);
        m_LightBufferData.lights[0].type = 0;
        m_LightBufferData.lightCount = 1;
    } else {
        m_LightBufferData.lightCount = slot;
    }

    if (m_ShadowsEnabled && casterEntity != entt::null) {
        // Fixed ortho frustum around the main-camera target (v1; extent from
        // the sun slider, see ARCHITECTURE.md).
        const float e = shadowExtent;
        glm::vec3 center = cameraTarget;
        const glm::vec3 up = (std::abs(shadowDir.y) > 0.99f) ? glm::vec3(0.0f, 0.0f, -1.0f) : glm::vec3(0.0f, 1.0f, 0.0f);
        // Texel snapping: lock shadow-map texels to the world so orbiting the
        // camera doesn't make shadows swim/crawl relative to their casters.
        // Only the frustum origin shifts (< 1 texel); orientation is untouched.
        {
            const glm::mat4 unsnapped = glm::lookAt(center - shadowDir * (e * 1.5f), center, up);
            const float texel = (2.0f * e) / static_cast<float>(kShadowMapSize);
            glm::vec4 cLS = unsnapped * glm::vec4(center, 1.0f);
            cLS.x = std::floor(cLS.x / texel) * texel;
            cLS.y = std::floor(cLS.y / texel) * texel;
            center = glm::vec3(glm::inverse(unsnapped) * cLS);
        }
        const glm::vec3 lightPos = center - shadowDir * (e * 1.5f);
        const glm::mat4 view = glm::lookAt(lightPos, center, up);
        glm::mat4 proj = glm::ortho(-e, e, -e, e, 1.0f, e * 4.0f);
        proj[1][1] *= -1.0f; // Vulkan Y-flip, same convention as the main passes
        // Depth: glm::ortho maps to OpenGL [-1,1] (no GLM_FORCE_DEPTH_ZERO_TO_ONE
        // in this project). Vulkan clips z<0, which emptied the whole map (the
        // scene sits in the negative half). Remap to [0,1] here. (Main-pass
        // perspective has the same quirk but survives: its negative range only
        // covers a sub-near sliver. See ARCHITECTURE.md.)
        proj[2][2] *= 0.5f;
        proj[3][2] = proj[3][2] * 0.5f + 0.5f;
        m_ShadowViewProj = proj * view;
        m_LightBufferData.shadowViewProj = m_ShadowViewProj;
        m_LightBufferData.shadowParams = glm::vec4(1.0f, kShadowDepthBias, static_cast<float>(kShadowMapSize), kShadowSlopeScale);
        m_ShadowEnabledFrame = true;
    }

    // Upload (host-visible + coherent, same pattern as creation).
    void* data = nullptr;
    if (vkMapMemory(m_Device, m_LightBufferMemory, 0, sizeof(LightBuffer), 0, &data) == VK_SUCCESS) {
        memcpy(data, &m_LightBufferData, sizeof(LightBuffer));
        vkUnmapMemory(m_Device, m_LightBufferMemory);
    }
}

void Renderer::recordShadowPass(VkCommandBuffer commandBuffer, Scene* scene) {
    const bool canRun = m_ShadowEnabledFrame && scene &&
        m_ShadowRenderPass != VK_NULL_HANDLE && m_ShadowFramebuffer != VK_NULL_HANDLE &&
        m_ShadowPipeline != VK_NULL_HANDLE;
    if (!canRun) {
        // No shadow pass this frame: still guarantee the SHADER_READ_ONLY
        // layout that main-pass binding 2 was written with (first frame only;
        // afterwards the image already sits in that layout).
        if (m_ShadowImageLayout == VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL) return;
        VkImageMemoryBarrier barrier{};
        barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
        barrier.oldLayout = m_ShadowImageLayout;
        barrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.image = m_ShadowImage;
        barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
        barrier.subresourceRange.baseMipLevel = 0;
        barrier.subresourceRange.levelCount = 1;
        barrier.subresourceRange.baseArrayLayer = 0;
        barrier.subresourceRange.layerCount = 1;
        barrier.srcAccessMask = 0;
        barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
        vkCmdPipelineBarrier(commandBuffer, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
            VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0, 0, nullptr, 0, nullptr, 1, &barrier);
        m_ShadowImageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        return;
    }

    VkRenderPassBeginInfo passInfo{};
    passInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
    passInfo.renderPass = m_ShadowRenderPass;
    passInfo.framebuffer = m_ShadowFramebuffer;
    passInfo.renderArea.offset = {0, 0};
    passInfo.renderArea.extent = {kShadowMapSize, kShadowMapSize};
    VkClearValue clear{};
    clear.depthStencil = {1.0f, 0};
    passInfo.clearValueCount = 1;
    passInfo.pClearValues = &clear;

    vkCmdBeginRenderPass(commandBuffer, &passInfo, VK_SUBPASS_CONTENTS_INLINE);
    vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, m_ShadowPipeline);

    auto& registry = scene->getRegistry();
    auto meshView = registry.view<Mesh>();
    for (auto entity : meshView) {
        if (registry.all_of<ECS::EditorHiddenComponent>(entity)) continue;
        auto& mesh = registry.get<Mesh>(entity);
        // Phase 3b: draw buffers resolve solely via the registry.
        VkBuffer drawVB = VK_NULL_HANDLE;
        VkBuffer drawIB = VK_NULL_HANDLE;
        uint32_t drawIndexCount = 0;
        if (!resolveMeshDrawBuffers(m_meshRegistry, mesh, drawVB, drawIB, drawIndexCount)) continue;
        glm::mat4 model(1.0f);
        if (scene->hasTransform(entity)) model = scene->getCachedWorldTransform(entity);
        ShadowPushConstants pc{};
        pc.model = model;
        pc.viewProj = m_ShadowViewProj;
        vkCmdPushConstants(commandBuffer, m_ShadowPipelineLayout, VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(pc), &pc);
        VkBuffer vertexBuffers[] = {drawVB};
        VkDeviceSize offsets[] = {0};
        vkCmdBindVertexBuffers(commandBuffer, 0, 1, vertexBuffers, offsets);
        vkCmdBindIndexBuffer(commandBuffer, drawIB, 0, VK_INDEX_TYPE_UINT32);
        vkCmdDrawIndexed(commandBuffer, drawIndexCount, 1, 0, 0, 0);
    }
    vkCmdEndRenderPass(commandBuffer);
    // The pass finalLayout transitioned the image; track it.
    m_ShadowImageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
}

void Renderer::createOutlinePipeline() {
    auto vertShaderCode = readFile("shaders/outline_vert.spv");
    auto fragShaderCode = readFile("shaders/outline_frag.spv");

    VkShaderModule vertShaderModule = createShaderModule(vertShaderCode);
    VkShaderModule fragShaderModule = createShaderModule(fragShaderCode);

    VkPipelineShaderStageCreateInfo vertShaderStageInfo{};
    vertShaderStageInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    vertShaderStageInfo.stage = VK_SHADER_STAGE_VERTEX_BIT;
    vertShaderStageInfo.module = vertShaderModule;
    vertShaderStageInfo.pName = "main";

    VkPipelineShaderStageCreateInfo fragShaderStageInfo{};
    fragShaderStageInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    fragShaderStageInfo.stage = VK_SHADER_STAGE_FRAGMENT_BIT;
    fragShaderStageInfo.module = fragShaderModule;
    fragShaderStageInfo.pName = "main";

    VkPipelineShaderStageCreateInfo shaderStages[] = {vertShaderStageInfo, fragShaderStageInfo};

    const auto bindingDescription = Vertex::getBindingDescription();
    const auto attributeDescriptions = Vertex::getAttributeDescriptions();

    VkPipelineVertexInputStateCreateInfo vertexInputInfo{};
    vertexInputInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
    vertexInputInfo.vertexBindingDescriptionCount = 1;
    vertexInputInfo.pVertexBindingDescriptions = &bindingDescription;
    vertexInputInfo.vertexAttributeDescriptionCount = static_cast<uint32_t>(attributeDescriptions.size());
    vertexInputInfo.pVertexAttributeDescriptions = attributeDescriptions.data();

    VkPipelineInputAssemblyStateCreateInfo inputAssembly{};
    inputAssembly.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
    inputAssembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    inputAssembly.primitiveRestartEnable = VK_FALSE;

    VkViewport viewport{};
    viewport.x = 0.0f;
    viewport.y = 0.0f;
    viewport.width = static_cast<float>(m_SwapChainExtent.width);
    viewport.height = static_cast<float>(m_SwapChainExtent.height);
    viewport.minDepth = 0.0f;
    viewport.maxDepth = 1.0f;

    VkRect2D scissor{};
    scissor.offset = {0, 0};
    scissor.extent = m_SwapChainExtent;

    VkPipelineViewportStateCreateInfo viewportState{};
    viewportState.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
    viewportState.viewportCount = 1;
    viewportState.pViewports = &viewport;
    viewportState.scissorCount = 1;
    viewportState.pScissors = &scissor;

    VkPipelineRasterizationStateCreateInfo rasterizer{};
    rasterizer.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
    rasterizer.depthClampEnable = VK_FALSE;
    rasterizer.rasterizerDiscardEnable = VK_FALSE;
    rasterizer.polygonMode = VK_POLYGON_MODE_FILL;
    rasterizer.lineWidth = 1.0f;
    rasterizer.cullMode = VK_CULL_MODE_FRONT_BIT;
    rasterizer.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
    rasterizer.depthBiasEnable = VK_FALSE;

    VkPipelineDepthStencilStateCreateInfo depthStencil{};
    depthStencil.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
    depthStencil.depthTestEnable = VK_TRUE;
    depthStencil.depthWriteEnable = VK_FALSE;
    depthStencil.depthCompareOp = VK_COMPARE_OP_LESS_OR_EQUAL;
    depthStencil.depthBoundsTestEnable = VK_FALSE;
    depthStencil.stencilTestEnable = VK_FALSE;

    VkPipelineMultisampleStateCreateInfo multisampling{};
    multisampling.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
    multisampling.sampleShadingEnable = VK_FALSE;
    multisampling.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

    VkPipelineColorBlendAttachmentState colorBlendAttachment{};
    colorBlendAttachment.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
        VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
    colorBlendAttachment.blendEnable = VK_FALSE;

    VkPipelineColorBlendStateCreateInfo colorBlending{};
    colorBlending.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
    colorBlending.logicOpEnable = VK_FALSE;
    colorBlending.attachmentCount = 1;
    colorBlending.pAttachments = &colorBlendAttachment;

    VkPushConstantRange pushConstantRange{};
    pushConstantRange.stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
    pushConstantRange.offset = 0;
    pushConstantRange.size = sizeof(OutlinePushConstants);

    VkPipelineLayoutCreateInfo pipelineLayoutInfo{};
    VkDescriptorSetLayout setLayouts[] = {m_DescriptorSetLayout, m_BonesDescriptorSetLayout};

    pipelineLayoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    pipelineLayoutInfo.setLayoutCount = 2;
    pipelineLayoutInfo.pSetLayouts = setLayouts;
    pipelineLayoutInfo.pushConstantRangeCount = 1;
    pipelineLayoutInfo.pPushConstantRanges = &pushConstantRange;

    if (vkCreatePipelineLayout(m_Device, &pipelineLayoutInfo, nullptr, &m_OutlinePipelineLayout) != VK_SUCCESS) {
        vkDestroyShaderModule(m_Device, fragShaderModule, nullptr);
        vkDestroyShaderModule(m_Device, vertShaderModule, nullptr);
        throw std::runtime_error("failed to create outline pipeline layout!");
    }

    VkGraphicsPipelineCreateInfo pipelineInfo{};
    pipelineInfo.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
    pipelineInfo.stageCount = 2;
    pipelineInfo.pStages = shaderStages;
    pipelineInfo.pVertexInputState = &vertexInputInfo;
    pipelineInfo.pInputAssemblyState = &inputAssembly;
    pipelineInfo.pViewportState = &viewportState;
    pipelineInfo.pRasterizationState = &rasterizer;
    pipelineInfo.pDepthStencilState = &depthStencil;
    pipelineInfo.pMultisampleState = &multisampling;
    pipelineInfo.pColorBlendState = &colorBlending;
    pipelineInfo.layout = m_OutlinePipelineLayout;
    pipelineInfo.renderPass = m_OffscreenRenderPass;
    pipelineInfo.subpass = 0;

    if (vkCreateGraphicsPipelines(m_Device, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &m_OutlinePipeline) != VK_SUCCESS) {
        vkDestroyShaderModule(m_Device, fragShaderModule, nullptr);
        vkDestroyShaderModule(m_Device, vertShaderModule, nullptr);
        throw std::runtime_error("failed to create outline pipeline!");
    }

    setDebugName(m_Device, VK_OBJECT_TYPE_PIPELINE, reinterpret_cast<uint64_t>(m_OutlinePipeline), "OutlinePipeline");

    vkDestroyShaderModule(m_Device, fragShaderModule, nullptr);
    vkDestroyShaderModule(m_Device, vertShaderModule, nullptr);
}

// Procedural sky: fullscreen triangle (no vertex buffers — positions come
// from gl_VertexIndex), drawn first at the far plane with depth writes off
// so opaques overdraw it. No descriptors; everything via SkyPushConstants.
void Renderer::createSkyPipeline() {
    auto vertShaderCode = readFile("shaders/sky_vert.spv");
    auto fragShaderCode = readFile("shaders/sky_frag.spv");

    VkShaderModule vertShaderModule = createShaderModule(vertShaderCode);
    VkShaderModule fragShaderModule = createShaderModule(fragShaderCode);

    VkPipelineShaderStageCreateInfo vertShaderStageInfo{};
    vertShaderStageInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    vertShaderStageInfo.stage = VK_SHADER_STAGE_VERTEX_BIT;
    vertShaderStageInfo.module = vertShaderModule;
    vertShaderStageInfo.pName = "main";

    VkPipelineShaderStageCreateInfo fragShaderStageInfo{};
    fragShaderStageInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    fragShaderStageInfo.stage = VK_SHADER_STAGE_FRAGMENT_BIT;
    fragShaderStageInfo.module = fragShaderModule;
    fragShaderStageInfo.pName = "main";

    VkPipelineShaderStageCreateInfo shaderStages[] = {vertShaderStageInfo, fragShaderStageInfo};

    VkPipelineVertexInputStateCreateInfo vertexInputInfo{};
    vertexInputInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
    vertexInputInfo.vertexBindingDescriptionCount = 0;
    vertexInputInfo.vertexAttributeDescriptionCount = 0;

    VkPipelineInputAssemblyStateCreateInfo inputAssembly{};
    inputAssembly.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
    inputAssembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    inputAssembly.primitiveRestartEnable = VK_FALSE;

    VkViewport viewport{};
    viewport.x = 0.0f;
    viewport.y = 0.0f;
    viewport.width = static_cast<float>(m_SwapChainExtent.width);
    viewport.height = static_cast<float>(m_SwapChainExtent.height);
    viewport.minDepth = 0.0f;
    viewport.maxDepth = 1.0f;

    VkRect2D scissor{};
    scissor.offset = {0, 0};
    scissor.extent = m_SwapChainExtent;

    VkPipelineViewportStateCreateInfo viewportState{};
    viewportState.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
    viewportState.viewportCount = 1;
    viewportState.pViewports = &viewport;
    viewportState.scissorCount = 1;
    viewportState.pScissors = &scissor;

    VkPipelineRasterizationStateCreateInfo rasterizer{};
    rasterizer.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
    rasterizer.depthClampEnable = VK_FALSE;
    rasterizer.rasterizerDiscardEnable = VK_FALSE;
    rasterizer.polygonMode = VK_POLYGON_MODE_FILL;
    rasterizer.lineWidth = 1.0f;
    rasterizer.cullMode = VK_CULL_MODE_NONE;
    rasterizer.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
    rasterizer.depthBiasEnable = VK_FALSE;

    VkPipelineDepthStencilStateCreateInfo depthStencil{};
    depthStencil.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
    depthStencil.depthTestEnable = VK_TRUE;
    depthStencil.depthWriteEnable = VK_FALSE;
    depthStencil.depthCompareOp = VK_COMPARE_OP_LESS_OR_EQUAL;
    depthStencil.depthBoundsTestEnable = VK_FALSE;
    depthStencil.stencilTestEnable = VK_FALSE;

    VkPipelineMultisampleStateCreateInfo multisampling{};
    multisampling.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
    multisampling.sampleShadingEnable = VK_FALSE;
    multisampling.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

    VkPipelineColorBlendAttachmentState colorBlendAttachment{};
    colorBlendAttachment.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
        VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
    colorBlendAttachment.blendEnable = VK_FALSE;

    VkPipelineColorBlendStateCreateInfo colorBlending{};
    colorBlending.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
    colorBlending.logicOpEnable = VK_FALSE;
    colorBlending.attachmentCount = 1;
    colorBlending.pAttachments = &colorBlendAttachment;

    VkPushConstantRange pushConstantRange{};
    pushConstantRange.stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
    pushConstantRange.offset = 0;
    pushConstantRange.size = sizeof(SkyPushConstants);

    VkPipelineLayoutCreateInfo pipelineLayoutInfo{};
    pipelineLayoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    pipelineLayoutInfo.setLayoutCount = 0;
    pipelineLayoutInfo.pushConstantRangeCount = 1;
    pipelineLayoutInfo.pPushConstantRanges = &pushConstantRange;

    if (vkCreatePipelineLayout(m_Device, &pipelineLayoutInfo, nullptr, &m_SkyPipelineLayout) != VK_SUCCESS) {
        vkDestroyShaderModule(m_Device, fragShaderModule, nullptr);
        vkDestroyShaderModule(m_Device, vertShaderModule, nullptr);
        throw std::runtime_error("failed to create sky pipeline layout!");
    }

    VkGraphicsPipelineCreateInfo pipelineInfo{};
    pipelineInfo.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
    pipelineInfo.stageCount = 2;
    pipelineInfo.pStages = shaderStages;
    pipelineInfo.pVertexInputState = &vertexInputInfo;
    pipelineInfo.pInputAssemblyState = &inputAssembly;
    pipelineInfo.pViewportState = &viewportState;
    pipelineInfo.pRasterizationState = &rasterizer;
    pipelineInfo.pDepthStencilState = &depthStencil;
    pipelineInfo.pMultisampleState = &multisampling;
    pipelineInfo.pColorBlendState = &colorBlending;
    pipelineInfo.layout = m_SkyPipelineLayout;
    pipelineInfo.renderPass = m_OffscreenRenderPass;
    pipelineInfo.subpass = 0;

    if (vkCreateGraphicsPipelines(m_Device, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &m_SkyPipeline) != VK_SUCCESS) {
        vkDestroyShaderModule(m_Device, fragShaderModule, nullptr);
        vkDestroyShaderModule(m_Device, vertShaderModule, nullptr);
        throw std::runtime_error("failed to create sky pipeline!");
    }

    setDebugName(m_Device, VK_OBJECT_TYPE_PIPELINE, reinterpret_cast<uint64_t>(m_SkyPipeline), "SkyPipeline");

    vkDestroyShaderModule(m_Device, fragShaderModule, nullptr);
    vkDestroyShaderModule(m_Device, vertShaderModule, nullptr);
}

void Renderer::createLightBuffer() {
    m_LightBufferData.lightCount = 1;
    m_LightBufferData.lights[0].position = glm::vec3(5.0f, 5.0f, 5.0f);
    m_LightBufferData.lights[0].color = glm::vec3(1.0f, 1.0f, 1.0f);
    m_LightBufferData.lights[0].intensity = 50.0f;
    m_LightBufferData.cameraPos = glm::vec3(0.0f, 0.0f, 5.0f);

    VkDeviceSize bufferSize = sizeof(LightBuffer);

    VkBufferCreateInfo bufferInfo{};
    bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    bufferInfo.size = bufferSize;
    bufferInfo.usage = VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT;
    bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    if (vkCreateBuffer(m_Device, &bufferInfo, nullptr, &m_LightBuffer) != VK_SUCCESS) {
        throw std::runtime_error("failed to create light buffer!");
    }

    setDebugName(m_Device, VK_OBJECT_TYPE_BUFFER, reinterpret_cast<uint64_t>(m_LightBuffer), "LightBuffer");

    VkMemoryRequirements memRequirements;
    vkGetBufferMemoryRequirements(m_Device, m_LightBuffer, &memRequirements);

    VkMemoryAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    allocInfo.allocationSize = memRequirements.size;
    allocInfo.memoryTypeIndex = findMemoryType(memRequirements.memoryTypeBits, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);

    if (vkAllocateMemory(m_Device, &allocInfo, nullptr, &m_LightBufferMemory) != VK_SUCCESS) {
        throw std::runtime_error("failed to allocate light buffer memory!");
    }

    vkBindBufferMemory(m_Device, m_LightBuffer, m_LightBufferMemory, 0);

    void* data;
    vkMapMemory(m_Device, m_LightBufferMemory, 0, sizeof(LightBuffer), 0, &data);
    memcpy(data, &m_LightBufferData, sizeof(LightBuffer));
    vkUnmapMemory(m_Device, m_LightBufferMemory);
}

void Renderer::createDescriptorSet() {
    createPlaceholderTexture();
    m_BoundTextureCount = 1;

    std::array<VkDescriptorPoolSize, 3> poolSizes{};
    poolSizes[0].type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    poolSizes[0].descriptorCount = 1;
    poolSizes[1].type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    poolSizes[1].descriptorCount = MAX_TEXTURES;
    poolSizes[2].type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    poolSizes[2].descriptorCount = 1; // shadow map (binding 2)

    VkDescriptorPoolCreateInfo poolInfo{};
    poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    poolInfo.poolSizeCount = static_cast<uint32_t>(poolSizes.size());
    poolInfo.pPoolSizes = poolSizes.data();
    poolInfo.maxSets = 1;

    if (vkCreateDescriptorPool(m_Device, &poolInfo, nullptr, &m_DescriptorPool) != VK_SUCCESS) {
        throw std::runtime_error("failed to create descriptor pool!");
    }

    VkDescriptorSetAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    allocInfo.descriptorPool = m_DescriptorPool;
    allocInfo.descriptorSetCount = 1;
    allocInfo.pSetLayouts = &m_DescriptorSetLayout;

    if (vkAllocateDescriptorSets(m_Device, &allocInfo, &m_DescriptorSet) != VK_SUCCESS) {
        throw std::runtime_error("failed to allocate descriptor sets!");
    }

    VkDescriptorBufferInfo bufferInfo{};
    bufferInfo.buffer = m_LightBuffer;
    bufferInfo.offset = 0;
    bufferInfo.range = sizeof(LightBuffer);

    std::array<VkDescriptorImageInfo, MAX_TEXTURES> imageInfos{};
    for (uint32_t i = 0; i < MAX_TEXTURES; i++) {
        imageInfos[i].sampler = m_PlaceholderSampler;
        imageInfos[i].imageView = m_PlaceholderImageView;
        imageInfos[i].imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    }

    std::array<VkWriteDescriptorSet, 3> descriptorWrites{};
    descriptorWrites[0].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    descriptorWrites[0].dstSet = m_DescriptorSet;
    descriptorWrites[0].dstBinding = 0;
    descriptorWrites[0].dstArrayElement = 0;
    descriptorWrites[0].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    descriptorWrites[0].descriptorCount = 1;
    descriptorWrites[0].pBufferInfo = &bufferInfo;

    descriptorWrites[1].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    descriptorWrites[1].dstSet = m_DescriptorSet;
    descriptorWrites[1].dstBinding = 1;
    descriptorWrites[1].dstArrayElement = 0;
    descriptorWrites[1].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    descriptorWrites[1].descriptorCount = MAX_TEXTURES;
    descriptorWrites[1].pImageInfo = imageInfos.data();

    VkDescriptorImageInfo shadowImageInfo{};
    shadowImageInfo.sampler = m_ShadowSampler;
    shadowImageInfo.imageView = m_ShadowView;
    shadowImageInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

    descriptorWrites[2].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    descriptorWrites[2].dstSet = m_DescriptorSet;
    descriptorWrites[2].dstBinding = 2;
    descriptorWrites[2].dstArrayElement = 0;
    descriptorWrites[2].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    descriptorWrites[2].descriptorCount = 1;
    descriptorWrites[2].pImageInfo = &shadowImageInfo;

    vkUpdateDescriptorSets(m_Device, static_cast<uint32_t>(descriptorWrites.size()), descriptorWrites.data(), 0, nullptr);
}

void Renderer::createBonesDescriptorSetLayout() {
    VkDescriptorSetLayoutBinding bonesBinding{};
    bonesBinding.binding = 0;
    bonesBinding.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC;
    bonesBinding.descriptorCount = 1;
    bonesBinding.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;

    VkDescriptorSetLayoutCreateInfo layoutInfo{};
    layoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    layoutInfo.bindingCount = 1;
    layoutInfo.pBindings = &bonesBinding;

    if (vkCreateDescriptorSetLayout(m_Device, &layoutInfo, nullptr, &m_BonesDescriptorSetLayout) != VK_SUCCESS) {
        throw std::runtime_error("failed to create bones descriptor set layout!");
    }

    setDebugName(m_Device, VK_OBJECT_TYPE_DESCRIPTOR_SET_LAYOUT, reinterpret_cast<uint64_t>(m_BonesDescriptorSetLayout), "BonesDescriptorSetLayout");
}

static VkDeviceSize alignUp(VkDeviceSize value, VkDeviceSize alignment) {
    if (alignment == 0) return value;
    return (value + alignment - 1) & ~(alignment - 1);
}

void Renderer::createBonesResources() {
    VkDescriptorPoolSize poolSize{};
    poolSize.type = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC;
    poolSize.descriptorCount = MAX_FRAMES_IN_FLIGHT;

    VkDescriptorPoolCreateInfo poolInfo{};
    poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    poolInfo.poolSizeCount = 1;
    poolInfo.pPoolSizes = &poolSize;
    poolInfo.maxSets = MAX_FRAMES_IN_FLIGHT;

    if (vkCreateDescriptorPool(m_Device, &poolInfo, nullptr, &m_BonesDescriptorPool) != VK_SUCCESS) {
        throw std::runtime_error("failed to create bones descriptor pool!");
    }

    std::array<VkDescriptorSetLayout, MAX_FRAMES_IN_FLIGHT> layouts{};
    layouts.fill(m_BonesDescriptorSetLayout);

    VkDescriptorSetAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    allocInfo.descriptorPool = m_BonesDescriptorPool;
    allocInfo.descriptorSetCount = MAX_FRAMES_IN_FLIGHT;
    allocInfo.pSetLayouts = layouts.data();

    if (vkAllocateDescriptorSets(m_Device, &allocInfo, m_BonesDescriptorSets.data()) != VK_SUCCESS) {
        throw std::runtime_error("failed to allocate bones descriptor sets!");
    }

    VkPhysicalDeviceProperties props{};
    vkGetPhysicalDeviceProperties(m_PhysicalDevice, &props);

    const VkDeviceSize alignment = props.limits.minStorageBufferOffsetAlignment;
    m_BonePaletteStrideBytes = alignUp(sizeof(glm::mat4) * MAX_BONES, alignment);

    const VkDeviceSize bufferSize = m_BonePaletteStrideBytes * MAX_SKINNED_INSTANCES;

    for (uint32_t frame = 0; frame < MAX_FRAMES_IN_FLIGHT; ++frame) {
        VkBufferCreateInfo bufferInfo{};
        bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
        bufferInfo.size = bufferSize;
        bufferInfo.usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;
        bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

        if (vkCreateBuffer(m_Device, &bufferInfo, nullptr, &m_BonePaletteBuffers[frame]) != VK_SUCCESS) {
            throw std::runtime_error("failed to create bone palette buffer!");
        }

        VkMemoryRequirements memRequirements{};
        vkGetBufferMemoryRequirements(m_Device, m_BonePaletteBuffers[frame], &memRequirements);

        VkMemoryAllocateInfo memAlloc{};
        memAlloc.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
        memAlloc.allocationSize = memRequirements.size;
        memAlloc.memoryTypeIndex = findMemoryType(memRequirements.memoryTypeBits,
            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);

        if (vkAllocateMemory(m_Device, &memAlloc, nullptr, &m_BonePaletteMemories[frame]) != VK_SUCCESS) {
            throw std::runtime_error("failed to allocate bone palette memory!");
        }

        vkBindBufferMemory(m_Device, m_BonePaletteBuffers[frame], m_BonePaletteMemories[frame], 0);

        void* mapped = nullptr;
        if (vkMapMemory(m_Device, m_BonePaletteMemories[frame], 0, bufferSize, 0, &mapped) != VK_SUCCESS) {
            throw std::runtime_error("failed to map bone palette memory!");
        }
        m_BonePaletteMapped[frame] = mapped;

        char name[64];
        std::snprintf(name, sizeof(name), "BonePaletteBuffer[%u]", frame);
        setDebugName(m_Device, VK_OBJECT_TYPE_BUFFER, reinterpret_cast<uint64_t>(m_BonePaletteBuffers[frame]), name);

        VkDescriptorBufferInfo bonesInfo{};
        bonesInfo.buffer = m_BonePaletteBuffers[frame];
        bonesInfo.offset = 0;
        bonesInfo.range = bufferSize;

        VkWriteDescriptorSet write{};
        write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        write.dstSet = m_BonesDescriptorSets[frame];
        write.dstBinding = 0;
        write.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC;
        write.descriptorCount = 1;
        write.pBufferInfo = &bonesInfo;

        vkUpdateDescriptorSets(m_Device, 1, &write, 0, nullptr);
    }
}

uint32_t Renderer::uploadBonePalette(const glm::mat4* matrices, uint32_t boneCount) {
    if (m_BonePaletteStrideBytes == 0) {
        return 0;
    }

    void* mapped = m_BonePaletteMapped[m_CurrentFrame];
    if (!mapped) {
        return 0;
    }

    if (m_BonePaletteNextSlot >= MAX_SKINNED_INSTANCES) {
        return 0;
    }

    const uint32_t slot = m_BonePaletteNextSlot++;
    const VkDeviceSize offset = static_cast<VkDeviceSize>(slot) * m_BonePaletteStrideBytes;

    auto* dst = reinterpret_cast<glm::mat4*>(static_cast<char*>(mapped) + offset);
    const uint32_t count = std::min<uint32_t>(boneCount, MAX_BONES);

    for (uint32_t i = 0; i < count; ++i) {
        dst[i] = matrices[i];
    }
    for (uint32_t i = count; i < MAX_BONES; ++i) {
        dst[i] = glm::mat4(1.0f);
    }

    return static_cast<uint32_t>(offset);
}

void Renderer::createPlaceholderTexture() {
    // Upload a small white texture and transition to SHADER_READ_ONLY.
    VkDeviceSize imageSize = 4 * 4 * 4;
    std::vector<uint8_t> pixels(static_cast<size_t>(imageSize), 255);

    VkBuffer stagingBuffer = VK_NULL_HANDLE;
    VkDeviceMemory stagingMemory = VK_NULL_HANDLE;

    VkBufferCreateInfo bufferInfo{};
    bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    bufferInfo.size = imageSize;
    bufferInfo.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
    bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    if (vkCreateBuffer(m_Device, &bufferInfo, nullptr, &stagingBuffer) != VK_SUCCESS) {
        throw std::runtime_error("failed to create placeholder staging buffer!");
    }

    VkMemoryRequirements memRequirements;
    vkGetBufferMemoryRequirements(m_Device, stagingBuffer, &memRequirements);

    VkMemoryAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    allocInfo.allocationSize = memRequirements.size;
    allocInfo.memoryTypeIndex = findMemoryType(
        memRequirements.memoryTypeBits,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT
    );

    if (vkAllocateMemory(m_Device, &allocInfo, nullptr, &stagingMemory) != VK_SUCCESS) {
        vkDestroyBuffer(m_Device, stagingBuffer, nullptr);
        throw std::runtime_error("failed to allocate placeholder staging memory!");
    }

    vkBindBufferMemory(m_Device, stagingBuffer, stagingMemory, 0);

    void* data = nullptr;
    vkMapMemory(m_Device, stagingMemory, 0, imageSize, 0, &data);
    memcpy(data, pixels.data(), static_cast<size_t>(imageSize));
    vkUnmapMemory(m_Device, stagingMemory);

    VkImageCreateInfo imageInfo{};
    imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    imageInfo.imageType = VK_IMAGE_TYPE_2D;
    imageInfo.extent.width = 4;
    imageInfo.extent.height = 4;
    imageInfo.extent.depth = 1;
    imageInfo.mipLevels = 1;
    imageInfo.arrayLayers = 1;
    imageInfo.format = VK_FORMAT_R8G8B8A8_SRGB;
    imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
    imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    imageInfo.usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
    imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
    imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    if (vkCreateImage(m_Device, &imageInfo, nullptr, &m_PlaceholderImage) != VK_SUCCESS) {
        vkFreeMemory(m_Device, stagingMemory, nullptr);
        vkDestroyBuffer(m_Device, stagingBuffer, nullptr);
        throw std::runtime_error("failed to create placeholder image!");
    }

    vkGetImageMemoryRequirements(m_Device, m_PlaceholderImage, &memRequirements);

    allocInfo.allocationSize = memRequirements.size;
    allocInfo.memoryTypeIndex = findMemoryType(memRequirements.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);

    if (vkAllocateMemory(m_Device, &allocInfo, nullptr, &m_PlaceholderImageMemory) != VK_SUCCESS) {
        vkDestroyImage(m_Device, m_PlaceholderImage, nullptr);
        vkFreeMemory(m_Device, stagingMemory, nullptr);
        vkDestroyBuffer(m_Device, stagingBuffer, nullptr);
        throw std::runtime_error("failed to allocate placeholder image memory!");
    }

    vkBindImageMemory(m_Device, m_PlaceholderImage, m_PlaceholderImageMemory, 0);

    immediateSubmit([&](VkCommandBuffer commandBuffer) {
        VkImageMemoryBarrier barrier{};
        barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
        barrier.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        barrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
        barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.image = m_PlaceholderImage;
        barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        barrier.subresourceRange.baseMipLevel = 0;
        barrier.subresourceRange.levelCount = 1;
        barrier.subresourceRange.baseArrayLayer = 0;
        barrier.subresourceRange.layerCount = 1;
        barrier.srcAccessMask = 0;
        barrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;

        vkCmdPipelineBarrier(commandBuffer, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
            VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 0, nullptr, 1, &barrier);

        VkBufferImageCopy region{};
        region.bufferOffset = 0;
        region.bufferRowLength = 0;
        region.bufferImageHeight = 0;
        region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        region.imageSubresource.mipLevel = 0;
        region.imageSubresource.baseArrayLayer = 0;
        region.imageSubresource.layerCount = 1;
        region.imageOffset = {0, 0, 0};
        region.imageExtent = {4, 4, 1};

        vkCmdCopyBufferToImage(commandBuffer, stagingBuffer, m_PlaceholderImage,
            VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);

        barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
        barrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;

        vkCmdPipelineBarrier(commandBuffer, VK_PIPELINE_STAGE_TRANSFER_BIT,
            VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0, 0, nullptr, 0, nullptr, 1, &barrier);
    });

    VkImageViewCreateInfo viewInfo{};
    viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    viewInfo.image = m_PlaceholderImage;
    viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
    viewInfo.format = VK_FORMAT_R8G8B8A8_SRGB;
    viewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    viewInfo.subresourceRange.baseMipLevel = 0;
    viewInfo.subresourceRange.levelCount = 1;
    viewInfo.subresourceRange.baseArrayLayer = 0;
    viewInfo.subresourceRange.layerCount = 1;

    if (vkCreateImageView(m_Device, &viewInfo, nullptr, &m_PlaceholderImageView) != VK_SUCCESS) {
        throw std::runtime_error("failed to create placeholder image view!");
    }

    VkSamplerCreateInfo samplerInfo{};
    samplerInfo.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
    samplerInfo.magFilter = VK_FILTER_LINEAR;
    samplerInfo.minFilter = VK_FILTER_LINEAR;
    samplerInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
    samplerInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_REPEAT;
    samplerInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_REPEAT;
    samplerInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_REPEAT;
    samplerInfo.anisotropyEnable = VK_FALSE;
    samplerInfo.borderColor = VK_BORDER_COLOR_INT_OPAQUE_BLACK;
    samplerInfo.unnormalizedCoordinates = VK_FALSE;
    samplerInfo.compareEnable = VK_FALSE;
    samplerInfo.compareOp = VK_COMPARE_OP_ALWAYS;

    if (vkCreateSampler(m_Device, &samplerInfo, nullptr, &m_PlaceholderSampler) != VK_SUCCESS) {
        throw std::runtime_error("failed to create placeholder sampler!");
    }

    vkFreeMemory(m_Device, stagingMemory, nullptr);
    vkDestroyBuffer(m_Device, stagingBuffer, nullptr);
}

void Renderer::createFramebuffers() {
    m_SwapChainFramebuffers.resize(m_SwapChainImageViews.size());

    for (size_t i = 0; i < m_SwapChainImageViews.size(); i++) {
        VkImageView attachments[] = {m_SwapChainImageViews[i], m_DepthImageView};

        VkFramebufferCreateInfo framebufferInfo{};
        framebufferInfo.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
        framebufferInfo.renderPass = m_RenderPass;
        framebufferInfo.attachmentCount = 2;
        framebufferInfo.pAttachments = attachments;
        framebufferInfo.width = m_SwapChainExtent.width;
        framebufferInfo.height = m_SwapChainExtent.height;
        framebufferInfo.layers = 1;

        if (vkCreateFramebuffer(m_Device, &framebufferInfo, nullptr, &m_SwapChainFramebuffers[i]) != VK_SUCCESS) {
            throw std::runtime_error("failed to create framebuffer!");
        }
    }
}

void Renderer::createCommandPool() {
    QueueFamilyIndices queueFamilyIndices = findQueueFamilies(m_PhysicalDevice);

    VkCommandPoolCreateInfo poolInfo{};
    poolInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    poolInfo.queueFamilyIndex = queueFamilyIndices.graphicsFamily.value();
    poolInfo.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;

    if (vkCreateCommandPool(m_Device, &poolInfo, nullptr, &m_CommandPool) != VK_SUCCESS) {
        throw std::runtime_error("failed to create command pool!");
    }
}

void Renderer::createCommandBuffers() {
    m_CommandBuffers.resize(MAX_FRAMES_IN_FLIGHT);

    VkCommandBufferAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    allocInfo.commandPool = m_CommandPool;
    allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    allocInfo.commandBufferCount = static_cast<uint32_t>(m_CommandBuffers.size());

    if (vkAllocateCommandBuffers(m_Device, &allocInfo, m_CommandBuffers.data()) != VK_SUCCESS) {
        throw std::runtime_error("failed to allocate command buffers!");
    }
}

void Renderer::createSyncObjects() {
    const uint32_t imageCount = static_cast<uint32_t>(m_SwapChainImages.size());

    m_ImageAvailableSemaphores.resize(MAX_FRAMES_IN_FLIGHT);
    m_RenderFinishedSemaphores.resize(MAX_FRAMES_IN_FLIGHT);
    m_InFlightFences.resize(MAX_FRAMES_IN_FLIGHT);
    m_ImagesInFlight.assign(imageCount, VK_NULL_HANDLE);

    VkSemaphoreCreateInfo semaphoreInfo{};
    semaphoreInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;

    VkFenceCreateInfo fenceInfo{};
    fenceInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
    fenceInfo.flags = VK_FENCE_CREATE_SIGNALED_BIT;

    for (size_t i = 0; i < MAX_FRAMES_IN_FLIGHT; i++) {
        if (vkCreateSemaphore(m_Device, &semaphoreInfo, nullptr, &m_ImageAvailableSemaphores[i]) != VK_SUCCESS ||
            vkCreateSemaphore(m_Device, &semaphoreInfo, nullptr, &m_RenderFinishedSemaphores[i]) != VK_SUCCESS ||
            vkCreateFence(m_Device, &fenceInfo, nullptr, &m_InFlightFences[i]) != VK_SUCCESS) {
            throw std::runtime_error("failed to create synchronization objects!");
        }
    }
}

void Renderer::createOffscreenResources() {
    VkImageCreateInfo imageInfo{};
    imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    imageInfo.imageType = VK_IMAGE_TYPE_2D;
    imageInfo.extent.width = m_SwapChainExtent.width;
    imageInfo.extent.height = m_SwapChainExtent.height;
    imageInfo.extent.depth = 1;
    imageInfo.mipLevels = 1;
    imageInfo.arrayLayers = 1;
    imageInfo.format = m_SwapChainImageFormat;
    imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
    imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    imageInfo.usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
    imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;

    if (vkCreateImage(m_Device, &imageInfo, nullptr, &m_OffscreenImage) != VK_SUCCESS) {
        throw std::runtime_error("failed to create offscreen image!");
    }

    VkMemoryRequirements memRequirements;
    vkGetImageMemoryRequirements(m_Device, m_OffscreenImage, &memRequirements);

    VkMemoryAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    allocInfo.allocationSize = memRequirements.size;
    allocInfo.memoryTypeIndex = findMemoryType(memRequirements.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);

    if (vkAllocateMemory(m_Device, &allocInfo, nullptr, &m_OffscreenImageMemory) != VK_SUCCESS) {
        throw std::runtime_error("failed to allocate offscreen image memory!");
    }

    vkBindImageMemory(m_Device, m_OffscreenImage, m_OffscreenImageMemory, 0);
    setDebugName(m_Device, VK_OBJECT_TYPE_IMAGE, reinterpret_cast<uint64_t>(m_OffscreenImage), "OffscreenColorImage");

    VkImageViewCreateInfo viewInfo{};
    viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    viewInfo.image = m_OffscreenImage;
    viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
    viewInfo.format = m_SwapChainImageFormat;
    viewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    viewInfo.subresourceRange.baseMipLevel = 0;
    viewInfo.subresourceRange.levelCount = 1;
    viewInfo.subresourceRange.baseArrayLayer = 0;
    viewInfo.subresourceRange.layerCount = 1;

    if (vkCreateImageView(m_Device, &viewInfo, nullptr, &m_OffscreenImageView) != VK_SUCCESS) {
        throw std::runtime_error("failed to create offscreen image view!");
    }

    VkSamplerCreateInfo samplerInfo{};
    samplerInfo.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
    samplerInfo.magFilter = VK_FILTER_LINEAR;
    samplerInfo.minFilter = VK_FILTER_LINEAR;
    samplerInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    samplerInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    samplerInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    samplerInfo.anisotropyEnable = VK_FALSE;
    samplerInfo.borderColor = VK_BORDER_COLOR_INT_OPAQUE_BLACK;
    samplerInfo.unnormalizedCoordinates = VK_FALSE;

    if (vkCreateSampler(m_Device, &samplerInfo, nullptr, &m_OffscreenSampler) != VK_SUCCESS) {
        throw std::runtime_error("failed to create offscreen sampler!");
    }

    VkImageCreateInfo depthImageInfo{};
    depthImageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    depthImageInfo.imageType = VK_IMAGE_TYPE_2D;
    depthImageInfo.extent.width = m_SwapChainExtent.width;
    depthImageInfo.extent.height = m_SwapChainExtent.height;
    depthImageInfo.extent.depth = 1;
    depthImageInfo.mipLevels = 1;
    depthImageInfo.arrayLayers = 1;
    depthImageInfo.format = VK_FORMAT_D32_SFLOAT;
    depthImageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
    depthImageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    depthImageInfo.usage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;
    depthImageInfo.samples = VK_SAMPLE_COUNT_1_BIT;

    if (vkCreateImage(m_Device, &depthImageInfo, nullptr, &m_OffscreenDepthImage) != VK_SUCCESS) {
        throw std::runtime_error("failed to create offscreen depth image!");
    }

    VkMemoryRequirements depthMemRequirements;
    vkGetImageMemoryRequirements(m_Device, m_OffscreenDepthImage, &depthMemRequirements);

    VkMemoryAllocateInfo depthAllocInfo{};
    depthAllocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    depthAllocInfo.allocationSize = depthMemRequirements.size;
    depthAllocInfo.memoryTypeIndex = findMemoryType(depthMemRequirements.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);

    if (vkAllocateMemory(m_Device, &depthAllocInfo, nullptr, &m_OffscreenDepthImageMemory) != VK_SUCCESS) {
        throw std::runtime_error("failed to allocate offscreen depth image memory!");
    }

    vkBindImageMemory(m_Device, m_OffscreenDepthImage, m_OffscreenDepthImageMemory, 0);
    setDebugName(m_Device, VK_OBJECT_TYPE_IMAGE, reinterpret_cast<uint64_t>(m_OffscreenDepthImage), "OffscreenDepthImage");

    VkImageViewCreateInfo depthViewInfo{};
    depthViewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    depthViewInfo.image = m_OffscreenDepthImage;
    depthViewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
    depthViewInfo.format = VK_FORMAT_D32_SFLOAT;
    depthViewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
    depthViewInfo.subresourceRange.baseMipLevel = 0;
    depthViewInfo.subresourceRange.levelCount = 1;
    depthViewInfo.subresourceRange.baseArrayLayer = 0;
    depthViewInfo.subresourceRange.layerCount = 1;

    if (vkCreateImageView(m_Device, &depthViewInfo, nullptr, &m_OffscreenDepthImageView) != VK_SUCCESS) {
        throw std::runtime_error("failed to create offscreen depth image view!");
    }

    // Picking image
    VkImageCreateInfo pickingImageInfo{};
    pickingImageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    pickingImageInfo.imageType = VK_IMAGE_TYPE_2D;
    pickingImageInfo.extent.width = m_SwapChainExtent.width;
    pickingImageInfo.extent.height = m_SwapChainExtent.height;
    pickingImageInfo.extent.depth = 1;
    pickingImageInfo.mipLevels = 1;
    pickingImageInfo.arrayLayers = 1;
    pickingImageInfo.format = VK_FORMAT_R32_UINT;
    pickingImageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
    pickingImageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    pickingImageInfo.usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
    pickingImageInfo.samples = VK_SAMPLE_COUNT_1_BIT;

    if (vkCreateImage(m_Device, &pickingImageInfo, nullptr, &m_PickingImage) != VK_SUCCESS) {
        throw std::runtime_error("failed to create picking image!");
    }

    VkMemoryRequirements pickingMemRequirements;
    vkGetImageMemoryRequirements(m_Device, m_PickingImage, &pickingMemRequirements);

    VkMemoryAllocateInfo pickingAllocInfo{};
    pickingAllocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    pickingAllocInfo.allocationSize = pickingMemRequirements.size;
    pickingAllocInfo.memoryTypeIndex = findMemoryType(pickingMemRequirements.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);

    if (vkAllocateMemory(m_Device, &pickingAllocInfo, nullptr, &m_PickingImageMemory) != VK_SUCCESS) {
        throw std::runtime_error("failed to allocate picking image memory!");
    }

    vkBindImageMemory(m_Device, m_PickingImage, m_PickingImageMemory, 0);
    setDebugName(m_Device, VK_OBJECT_TYPE_IMAGE, reinterpret_cast<uint64_t>(m_PickingImage), "PickingColorImage");

    VkImageViewCreateInfo pickingViewInfo{};
    pickingViewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    pickingViewInfo.image = m_PickingImage;
    pickingViewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
    pickingViewInfo.format = VK_FORMAT_R32_UINT;
    pickingViewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    pickingViewInfo.subresourceRange.baseMipLevel = 0;
    pickingViewInfo.subresourceRange.levelCount = 1;
    pickingViewInfo.subresourceRange.baseArrayLayer = 0;
    pickingViewInfo.subresourceRange.layerCount = 1;

    if (vkCreateImageView(m_Device, &pickingViewInfo, nullptr, &m_PickingImageView) != VK_SUCCESS) {
        throw std::runtime_error("failed to create picking image view!");
    }

        // Picking depth image
    VkImageCreateInfo pickingDepthInfo{};
    pickingDepthInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    pickingDepthInfo.imageType = VK_IMAGE_TYPE_2D;
    pickingDepthInfo.extent.width = m_SwapChainExtent.width;
    pickingDepthInfo.extent.height = m_SwapChainExtent.height;
    pickingDepthInfo.extent.depth = 1;
    pickingDepthInfo.mipLevels = 1;
    pickingDepthInfo.arrayLayers = 1;
    pickingDepthInfo.format = VK_FORMAT_D32_SFLOAT;
    pickingDepthInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
    pickingDepthInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    pickingDepthInfo.usage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;
    pickingDepthInfo.samples = VK_SAMPLE_COUNT_1_BIT;

    if (vkCreateImage(m_Device, &pickingDepthInfo, nullptr, &m_PickingDepthImage) != VK_SUCCESS) {
        throw std::runtime_error("failed to create picking depth image!");
    }

    VkMemoryRequirements pickingDepthMemReq{};
    vkGetImageMemoryRequirements(m_Device, m_PickingDepthImage, &pickingDepthMemReq);

    VkMemoryAllocateInfo pickingDepthAlloc{};
    pickingDepthAlloc.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    pickingDepthAlloc.allocationSize = pickingDepthMemReq.size;
    pickingDepthAlloc.memoryTypeIndex = findMemoryType(pickingDepthMemReq.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);

    if (vkAllocateMemory(m_Device, &pickingDepthAlloc, nullptr, &m_PickingDepthImageMemory) != VK_SUCCESS) {
        throw std::runtime_error("failed to allocate picking depth image memory!");
    }

    vkBindImageMemory(m_Device, m_PickingDepthImage, m_PickingDepthImageMemory, 0);
    setDebugName(m_Device, VK_OBJECT_TYPE_IMAGE, reinterpret_cast<uint64_t>(m_PickingDepthImage), "PickingDepthImage");

    VkImageViewCreateInfo pickingDepthView{};
    pickingDepthView.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    pickingDepthView.image = m_PickingDepthImage;
    pickingDepthView.viewType = VK_IMAGE_VIEW_TYPE_2D;
    pickingDepthView.format = VK_FORMAT_D32_SFLOAT;
    pickingDepthView.subresourceRange.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
    pickingDepthView.subresourceRange.baseMipLevel = 0;
    pickingDepthView.subresourceRange.levelCount = 1;
    pickingDepthView.subresourceRange.baseArrayLayer = 0;
    pickingDepthView.subresourceRange.layerCount = 1;

    if (vkCreateImageView(m_Device, &pickingDepthView, nullptr, &m_PickingDepthImageView) != VK_SUCCESS) {
        throw std::runtime_error("failed to create picking depth image view!");
    }

    VkImageView pickingAttachments[] = {m_PickingImageView, m_PickingDepthImageView};
    VkFramebufferCreateInfo pickingFramebufferInfo{};
    pickingFramebufferInfo.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
    pickingFramebufferInfo.renderPass = m_PickingRenderPass;
    pickingFramebufferInfo.attachmentCount = 2;
    pickingFramebufferInfo.pAttachments = pickingAttachments;
    pickingFramebufferInfo.width = m_SwapChainExtent.width;
    pickingFramebufferInfo.height = m_SwapChainExtent.height;
    pickingFramebufferInfo.layers = 1;

    if (vkCreateFramebuffer(m_Device, &pickingFramebufferInfo, nullptr, &m_PickingFramebuffer) != VK_SUCCESS) {
        throw std::runtime_error("failed to create picking framebuffer!");
    }

    m_PickingImageLayout = VK_IMAGE_LAYOUT_UNDEFINED;

    VkImageView offscreenAttachments[] = {m_OffscreenImageView, m_OffscreenDepthImageView};
    VkFramebufferCreateInfo framebufferInfo{};
    framebufferInfo.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
    framebufferInfo.renderPass = m_OffscreenRenderPass;
    framebufferInfo.attachmentCount = 2;
    framebufferInfo.pAttachments = offscreenAttachments;
    framebufferInfo.width = m_SwapChainExtent.width;
    framebufferInfo.height = m_SwapChainExtent.height;
    framebufferInfo.layers = 1;

    if (vkCreateFramebuffer(m_Device, &framebufferInfo, nullptr, &m_OffscreenFramebuffer) != VK_SUCCESS) {
        throw std::runtime_error("failed to create offscreen framebuffer!");
    }

    if (vkCreateImage(m_Device, &imageInfo, nullptr, &m_GameOffscreenImage) != VK_SUCCESS) {
        throw std::runtime_error("failed to create game offscreen image!");
    }

    vkGetImageMemoryRequirements(m_Device, m_GameOffscreenImage, &memRequirements);
    allocInfo.allocationSize = memRequirements.size;
    allocInfo.memoryTypeIndex = findMemoryType(memRequirements.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);

    if (vkAllocateMemory(m_Device, &allocInfo, nullptr, &m_GameOffscreenImageMemory) != VK_SUCCESS) {
        throw std::runtime_error("failed to allocate game offscreen image memory!");
    }

    vkBindImageMemory(m_Device, m_GameOffscreenImage, m_GameOffscreenImageMemory, 0);
    setDebugName(m_Device, VK_OBJECT_TYPE_IMAGE, reinterpret_cast<uint64_t>(m_GameOffscreenImage), "GameOffscreenColorImage");

    viewInfo.image = m_GameOffscreenImage;
    if (vkCreateImageView(m_Device, &viewInfo, nullptr, &m_GameOffscreenImageView) != VK_SUCCESS) {
        throw std::runtime_error("failed to create game offscreen image view!");
    }

    if (vkCreateSampler(m_Device, &samplerInfo, nullptr, &m_GameOffscreenSampler) != VK_SUCCESS) {
        throw std::runtime_error("failed to create game offscreen sampler!");
    }

    if (vkCreateImage(m_Device, &depthImageInfo, nullptr, &m_GameOffscreenDepthImage) != VK_SUCCESS) {
        throw std::runtime_error("failed to create game offscreen depth image!");
    }

    vkGetImageMemoryRequirements(m_Device, m_GameOffscreenDepthImage, &depthMemRequirements);
    depthAllocInfo.allocationSize = depthMemRequirements.size;
    depthAllocInfo.memoryTypeIndex = findMemoryType(depthMemRequirements.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);

    if (vkAllocateMemory(m_Device, &depthAllocInfo, nullptr, &m_GameOffscreenDepthImageMemory) != VK_SUCCESS) {
        throw std::runtime_error("failed to allocate game offscreen depth image memory!");
    }

    vkBindImageMemory(m_Device, m_GameOffscreenDepthImage, m_GameOffscreenDepthImageMemory, 0);
    setDebugName(m_Device, VK_OBJECT_TYPE_IMAGE, reinterpret_cast<uint64_t>(m_GameOffscreenDepthImage), "GameOffscreenDepthImage");

    depthViewInfo.image = m_GameOffscreenDepthImage;
    if (vkCreateImageView(m_Device, &depthViewInfo, nullptr, &m_GameOffscreenDepthImageView) != VK_SUCCESS) {
        throw std::runtime_error("failed to create game offscreen depth image view!");
    }

    VkImageView gameOffscreenAttachments[] = {m_GameOffscreenImageView, m_GameOffscreenDepthImageView};
    framebufferInfo.pAttachments = gameOffscreenAttachments;
    if (vkCreateFramebuffer(m_Device, &framebufferInfo, nullptr, &m_GameOffscreenFramebuffer) != VK_SUCCESS) {
        throw std::runtime_error("failed to create game offscreen framebuffer!");
    }
}


void Renderer::destroyPipelineResources() {
    if (m_SkyPipeline) { vkDestroyPipeline(m_Device, m_SkyPipeline, nullptr); m_SkyPipeline = VK_NULL_HANDLE; }
    if (m_SkyPipelineLayout) { vkDestroyPipelineLayout(m_Device, m_SkyPipelineLayout, nullptr); m_SkyPipelineLayout = VK_NULL_HANDLE; }
    if (m_OutlinePipeline) { vkDestroyPipeline(m_Device, m_OutlinePipeline, nullptr); m_OutlinePipeline = VK_NULL_HANDLE; }
    if (m_OutlinePipelineLayout) { vkDestroyPipelineLayout(m_Device, m_OutlinePipelineLayout, nullptr); m_OutlinePipelineLayout = VK_NULL_HANDLE; }

    destroyShadowResources();
    if (m_PickingPipelineNoCull) { vkDestroyPipeline(m_Device, m_PickingPipelineNoCull, nullptr); m_PickingPipelineNoCull = VK_NULL_HANDLE; }
    if (m_PickingPipelineFrontCull) { vkDestroyPipeline(m_Device, m_PickingPipelineFrontCull, nullptr); m_PickingPipelineFrontCull = VK_NULL_HANDLE; }
    if (m_PickingPipeline) { vkDestroyPipeline(m_Device, m_PickingPipeline, nullptr); m_PickingPipeline = VK_NULL_HANDLE; }
    if (m_PickingPipelineLayout) { vkDestroyPipelineLayout(m_Device, m_PickingPipelineLayout, nullptr); m_PickingPipelineLayout = VK_NULL_HANDLE; }

    if (m_GraphicsPipelineBlendNoCull) { vkDestroyPipeline(m_Device, m_GraphicsPipelineBlendNoCull, nullptr); m_GraphicsPipelineBlendNoCull = VK_NULL_HANDLE; }
    if (m_GraphicsPipelineBlendFrontCull) { vkDestroyPipeline(m_Device, m_GraphicsPipelineBlendFrontCull, nullptr); m_GraphicsPipelineBlendFrontCull = VK_NULL_HANDLE; }
    if (m_GraphicsPipelineBlend) { vkDestroyPipeline(m_Device, m_GraphicsPipelineBlend, nullptr); m_GraphicsPipelineBlend = VK_NULL_HANDLE; }
    if (m_GraphicsPipelineNoCull) { vkDestroyPipeline(m_Device, m_GraphicsPipelineNoCull, nullptr); m_GraphicsPipelineNoCull = VK_NULL_HANDLE; }
    if (m_GraphicsPipelineFrontCull) { vkDestroyPipeline(m_Device, m_GraphicsPipelineFrontCull, nullptr); m_GraphicsPipelineFrontCull = VK_NULL_HANDLE; }
    if (m_GraphicsPipeline) { vkDestroyPipeline(m_Device, m_GraphicsPipeline, nullptr); m_GraphicsPipeline = VK_NULL_HANDLE; }
    if (m_GraphicsPipelineInstancedNoCull) { vkDestroyPipeline(m_Device, m_GraphicsPipelineInstancedNoCull, nullptr); m_GraphicsPipelineInstancedNoCull = VK_NULL_HANDLE; }
    if (m_GraphicsPipelineInstancedFrontCull) { vkDestroyPipeline(m_Device, m_GraphicsPipelineInstancedFrontCull, nullptr); m_GraphicsPipelineInstancedFrontCull = VK_NULL_HANDLE; }
    if (m_GraphicsPipelineInstanced) { vkDestroyPipeline(m_Device, m_GraphicsPipelineInstanced, nullptr); m_GraphicsPipelineInstanced = VK_NULL_HANDLE; }
    if (m_PipelineLayout) { vkDestroyPipelineLayout(m_Device, m_PipelineLayout, nullptr); m_PipelineLayout = VK_NULL_HANDLE; }

    if (m_PickingRenderPass) { vkDestroyRenderPass(m_Device, m_PickingRenderPass, nullptr); m_PickingRenderPass = VK_NULL_HANDLE; }
    if (m_RenderPass) { vkDestroyRenderPass(m_Device, m_RenderPass, nullptr); m_RenderPass = VK_NULL_HANDLE; }
    if (m_OffscreenRenderPass) { vkDestroyRenderPass(m_Device, m_OffscreenRenderPass, nullptr); m_OffscreenRenderPass = VK_NULL_HANDLE; }
}

void Renderer::destroySwapchainResources() {
    for (auto framebuffer : m_SwapChainFramebuffers) {
        if (framebuffer) vkDestroyFramebuffer(m_Device, framebuffer, nullptr);
    }
    m_SwapChainFramebuffers.clear();

    for (auto imageView : m_SwapChainImageViews) {
        if (imageView) vkDestroyImageView(m_Device, imageView, nullptr);
    }
    m_SwapChainImageViews.clear();

    if (m_DepthImageView) { vkDestroyImageView(m_Device, m_DepthImageView, nullptr); m_DepthImageView = VK_NULL_HANDLE; }
    if (m_DepthImage) { vkDestroyImage(m_Device, m_DepthImage, nullptr); m_DepthImage = VK_NULL_HANDLE; }
    if (m_DepthMemory) { vkFreeMemory(m_Device, m_DepthMemory, nullptr); m_DepthMemory = VK_NULL_HANDLE; }

    if (m_SwapChain) { vkDestroySwapchainKHR(m_Device, m_SwapChain, nullptr); m_SwapChain = VK_NULL_HANDLE; }
    m_SwapChainImages.clear();
}

void Renderer::destroyOffscreenResources() {
    if (m_PickingFramebuffer) vkDestroyFramebuffer(m_Device, m_PickingFramebuffer, nullptr);
    if (m_PickingImageView) vkDestroyImageView(m_Device, m_PickingImageView, nullptr);
    if (m_PickingImage) vkDestroyImage(m_Device, m_PickingImage, nullptr);
    if (m_PickingImageMemory) vkFreeMemory(m_Device, m_PickingImageMemory, nullptr);
    if (m_PickingDepthImageView) vkDestroyImageView(m_Device, m_PickingDepthImageView, nullptr);
    if (m_PickingDepthImage) vkDestroyImage(m_Device, m_PickingDepthImage, nullptr);
    if (m_PickingDepthImageMemory) vkFreeMemory(m_Device, m_PickingDepthImageMemory, nullptr);

    if (m_OffscreenFramebuffer) vkDestroyFramebuffer(m_Device, m_OffscreenFramebuffer, nullptr);
    if (m_OffscreenSampler) vkDestroySampler(m_Device, m_OffscreenSampler, nullptr);
    if (m_OffscreenImageView) vkDestroyImageView(m_Device, m_OffscreenImageView, nullptr);
    if (m_OffscreenImage) vkDestroyImage(m_Device, m_OffscreenImage, nullptr);
    if (m_OffscreenImageMemory) vkFreeMemory(m_Device, m_OffscreenImageMemory, nullptr);
    if (m_OffscreenDepthImageView) vkDestroyImageView(m_Device, m_OffscreenDepthImageView, nullptr);
    if (m_OffscreenDepthImage) vkDestroyImage(m_Device, m_OffscreenDepthImage, nullptr);
    if (m_OffscreenDepthImageMemory) vkFreeMemory(m_Device, m_OffscreenDepthImageMemory, nullptr);

    if (m_GameOffscreenFramebuffer) vkDestroyFramebuffer(m_Device, m_GameOffscreenFramebuffer, nullptr);
    if (m_GameOffscreenSampler) vkDestroySampler(m_Device, m_GameOffscreenSampler, nullptr);
    if (m_GameOffscreenImageView) vkDestroyImageView(m_Device, m_GameOffscreenImageView, nullptr);
    if (m_GameOffscreenImage) vkDestroyImage(m_Device, m_GameOffscreenImage, nullptr);
    if (m_GameOffscreenImageMemory) vkFreeMemory(m_Device, m_GameOffscreenImageMemory, nullptr);
    if (m_GameOffscreenDepthImageView) vkDestroyImageView(m_Device, m_GameOffscreenDepthImageView, nullptr);
    if (m_GameOffscreenDepthImage) vkDestroyImage(m_Device, m_GameOffscreenDepthImage, nullptr);
    if (m_GameOffscreenDepthImageMemory) vkFreeMemory(m_Device, m_GameOffscreenDepthImageMemory, nullptr);

    m_OffscreenFramebuffer = VK_NULL_HANDLE;
    m_OffscreenSampler = VK_NULL_HANDLE;
    m_OffscreenImageView = VK_NULL_HANDLE;
    m_OffscreenImage = VK_NULL_HANDLE;
    m_OffscreenImageMemory = VK_NULL_HANDLE;
    m_OffscreenImageLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    m_OffscreenDepthImageView = VK_NULL_HANDLE;
    m_OffscreenDepthImage = VK_NULL_HANDLE;
    m_OffscreenDepthImageMemory = VK_NULL_HANDLE;

    m_GameOffscreenFramebuffer = VK_NULL_HANDLE;
    m_GameOffscreenSampler = VK_NULL_HANDLE;
    m_GameOffscreenImageView = VK_NULL_HANDLE;
    m_GameOffscreenImage = VK_NULL_HANDLE;
    m_GameOffscreenImageMemory = VK_NULL_HANDLE;
    m_GameOffscreenImageLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    m_GameOffscreenDepthImageView = VK_NULL_HANDLE;
    m_GameOffscreenDepthImage = VK_NULL_HANDLE;
    m_GameOffscreenDepthImageMemory = VK_NULL_HANDLE;

    m_PickingFramebuffer = VK_NULL_HANDLE;
    m_PickingImageView = VK_NULL_HANDLE;
    m_PickingImage = VK_NULL_HANDLE;
    m_PickingImageMemory = VK_NULL_HANDLE;
    m_PickingImageLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    m_PickingDepthImageView = VK_NULL_HANDLE;
    m_PickingDepthImage = VK_NULL_HANDLE;
    m_PickingDepthImageMemory = VK_NULL_HANDLE;
}

void Renderer::recordCommandBuffer(VkCommandBuffer commandBuffer, uint32_t imageIndex, Scene* scene) {
    VkCommandBufferBeginInfo beginInfo{};
    beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;

    if (vkBeginCommandBuffer(commandBuffer, &beginInfo) != VK_SUCCESS) {
        throw std::runtime_error("failed to begin recording command buffer!");
    }

    // TDD §12 frame metrics.
    m_FrameDrawCalls = 0;
    m_FrameTriangles = 0;
    m_FrameInstancedDraws = 0;
    m_FrameInstancedInstances = 0;
    m_FrameSimplifiedDraws = 0;

#ifdef TRACY_ENABLE
    TracyVkCollect(m_TracyVkCtx, commandBuffer);
#endif

    {
#ifdef TRACY_ENABLE
    TracyVkZone(m_TracyVkCtx, commandBuffer, "RenderFrame_GPU");
#endif

    // Offscreen image layout is defined by the offscreen render pass itself.

        std::unordered_map<uint32_t, uint32_t> boneOffsetCache;
    boneOffsetCache.reserve(256);
    std::vector<glm::mat4> bonePaletteScratch;
    std::vector<glm::mat4> boneGlobalsScratch;

    auto getBoneOffsetBytes = [&](entt::registry& registry, entt::entity entity) -> uint32_t {
        if (!registry.valid(entity) || !registry.all_of<ECS::SkinnedMeshComponent>(entity)) {
            return 0;
        }

        auto& skinned = registry.get<ECS::SkinnedMeshComponent>(entity);
        const uint32_t cacheKey = static_cast<uint32_t>(entity);
        if (auto it = boneOffsetCache.find(cacheKey); it != boneOffsetCache.end()) {
            skinned.bonePaletteOffsetBytes = it->second;
            return it->second;
        }

        entt::entity skelEntity = (skinned.skeletonEntity != entt::null) ? skinned.skeletonEntity : entity;
        if (!registry.valid(skelEntity) || !registry.all_of<ECS::SkeletonComponent>(skelEntity)) {
            return 0;
        }

        const auto& skc = registry.get<ECS::SkeletonComponent>(skelEntity);
        if (!skc.skeleton) {
            return 0;
        }

        const Atlas::Anim::Skeleton* skelPtr = skc.skeleton.get();
        if (!skelPtr) {
            return 0;
        }

        const Atlas::Anim::AnimationClip* clip = nullptr;
        float tSeconds = 0.0f;
        int32_t lockedBoneIndex = -1;
        bool lockTranslation = false;
        bool lockRotation = false;
        if (registry.all_of<ECS::AnimationPlayerComponent>(skelEntity)) {
            const auto& ap = registry.get<ECS::AnimationPlayerComponent>(skelEntity).player;
            tSeconds = ap.timeSeconds;

            if (ap.clipIndex >= 0 && static_cast<size_t>(ap.clipIndex) < skc.clips.size()) {
                clip = &skc.clips[static_cast<size_t>(ap.clipIndex)];
            }

            if (ap.enableRootMotion && skc.skeleton && skc.skeleton->rootMotionBoneIndex >= 0) {
                lockedBoneIndex = skc.skeleton->rootMotionBoneIndex;
                lockTranslation = true;
                lockRotation = ap.rootMotionApplyRotation;
            }
        }

        Atlas::Anim::PoseOverrides overrides;
        if (registry.all_of<ECS::BonePoseOverrideComponent>(skelEntity)) {
            const auto& o = registry.get<ECS::BonePoseOverrideComponent>(skelEntity);
            if (o.enabled) {
                overrides.hasRotation = &o.hasRotation;
                overrides.rotation = &o.rotation;
            }
        }

        Atlas::Anim::evaluateGlobals(*skelPtr, clip, tSeconds, overrides, boneGlobalsScratch,
            lockedBoneIndex, lockTranslation, lockRotation);

        uint32_t count = static_cast<uint32_t>(boneGlobalsScratch.size());
        if (count > MAX_BONES) {
            count = MAX_BONES;
        }

        bonePaletteScratch.resize(count);
        for (uint32_t i = 0; i < count; ++i) {
            glm::mat4 invBind = (i < skelPtr->inverseBind.size()) ? skelPtr->inverseBind[i] : glm::mat4(1.0f);
            bonePaletteScratch[i] = boneGlobalsScratch[i] * invBind;
        }

        const uint32_t offsetBytes = uploadBonePalette(bonePaletteScratch.data(), count);
        boneOffsetCache[cacheKey] = offsetBytes;
        skinned.bonePaletteOffsetBytes = offsetBytes;
        return offsetBytes;
    };

    // Render scene IDs to picking buffer
    // Lights UBO + shadow map (when a directional+castShadows light exists).
    updateLightsAndShadow(scene);
    recordShadowPass(commandBuffer, scene);

    if (scene && m_PickingRenderPass != VK_NULL_HANDLE && m_PickingFramebuffer != VK_NULL_HANDLE && m_PickingPipeline != VK_NULL_HANDLE) {
        if (m_PickingImageLayout != VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL) {
            VkPipelineStageFlags srcStage = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
            VkAccessFlags srcAccess = 0;
            if (m_PickingImageLayout == VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL) {
                srcStage = VK_PIPELINE_STAGE_TRANSFER_BIT;
                srcAccess = VK_ACCESS_TRANSFER_READ_BIT;
            }

            VkImageMemoryBarrier barrier{};
            barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
            barrier.oldLayout = m_PickingImageLayout;
            barrier.newLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
            barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            barrier.image = m_PickingImage;
            barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
            barrier.subresourceRange.baseMipLevel = 0;
            barrier.subresourceRange.levelCount = 1;
            barrier.subresourceRange.baseArrayLayer = 0;
            barrier.subresourceRange.layerCount = 1;
            barrier.srcAccessMask = srcAccess;
            barrier.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;

            vkCmdPipelineBarrier(commandBuffer, srcStage, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, 0,
                0, nullptr, 0, nullptr, 1, &barrier);

            m_PickingImageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
        }

        VkRenderPassBeginInfo pickingPassInfo{};
        pickingPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
        pickingPassInfo.renderPass = m_PickingRenderPass;
        pickingPassInfo.framebuffer = m_PickingFramebuffer;
        pickingPassInfo.renderArea.offset = {0, 0};
        pickingPassInfo.renderArea.extent = m_SwapChainExtent;

        VkClearValue pickingClearValues[2];
        pickingClearValues[0].color.uint32[0] = 0;
        pickingClearValues[0].color.uint32[1] = 0;
        pickingClearValues[0].color.uint32[2] = 0;
        pickingClearValues[0].color.uint32[3] = 0;
        pickingClearValues[1].depthStencil = {1.0f, 0};
        pickingPassInfo.clearValueCount = 2;
        pickingPassInfo.pClearValues = pickingClearValues;

        vkCmdBeginRenderPass(commandBuffer, &pickingPassInfo, VK_SUBPASS_CONTENTS_INLINE);

        // Render scene entities (ID only)
        auto& registry = scene->getRegistry();
        auto meshView = registry.view<Mesh>();

        if (!meshView.empty()) {
            glm::mat4 view = glm::mat4(1.0f);
            glm::mat4 proj = glm::mat4(1.0f);

            auto chooseCameraEntity = [&]() -> entt::entity {
                if (m_PreferGameCamera) {
                    auto gameView = registry.view<Camera, ECS::GameCameraComponent>();
                    entt::entity fallback = entt::null;
                    for (auto e : gameView) {
                        const auto& gcc = gameView.get<ECS::GameCameraComponent>(e);
                        if (fallback == entt::null) {
                            fallback = e;
                        }
                        if (gcc.primary) {
                            return e;
                        }
                    }
                    if (fallback != entt::null) {
                        return fallback;
                    }
                }

                if (!m_PreferGameCamera) {
                    auto editorCameraView = registry.view<EditorCamera>();
                    if (editorCameraView.begin() != editorCameraView.end()) {
                        return *editorCameraView.begin();
                    }
                }

                auto cameraView = registry.view<Camera>();
                if (cameraView.begin() != cameraView.end()) {
                    return *cameraView.begin();
                }

                return entt::null;
            };

            entt::entity cameraEntity = chooseCameraEntity();
            if (cameraEntity != entt::null) {
                if (registry.all_of<Camera>(cameraEntity)) {
                    auto& camera = registry.get<Camera>(cameraEntity);
                    camera.aspectRatio = static_cast<float>(m_SwapChainExtent.width) / static_cast<float>(m_SwapChainExtent.height);

                    // Respect Near/Far from Camera component
                    proj = glm::perspective(glm::radians(camera.fov), camera.aspectRatio, camera.nearPlane, camera.farPlane);
                    proj[1][1] = -proj[1][1];

                    view = camera.getViewMatrix();
                } else if (registry.all_of<EditorCamera>(cameraEntity)) {
                    auto& camera = registry.get<EditorCamera>(cameraEntity);
                    camera.aspectRatio = static_cast<float>(m_SwapChainExtent.width) / static_cast<float>(m_SwapChainExtent.height);
                    proj = glm::perspective(glm::radians(camera.fov), camera.aspectRatio, camera.nearPlane, camera.farPlane);
                    proj[1][1] = -proj[1][1];
                    view = camera.getViewMatrix();
                }
            }

            VkPipeline activePipeline = VK_NULL_HANDLE;

            for (auto entity : meshView) {
                if (registry.all_of<ECS::EditorHiddenComponent>(entity)) {
                    continue;
                }

                auto& mesh = registry.get<Mesh>(entity);
                // Phase 3a: buffers come from the registry (legacy Mesh::Vk*
                // only when renderMeshId == 0).
                VkBuffer pickVB = VK_NULL_HANDLE;
                VkBuffer pickIB = VK_NULL_HANDLE;
                uint32_t pickIndexCount = 0;
                if (!resolveMeshDrawBuffers(m_meshRegistry, mesh, pickVB, pickIB, pickIndexCount)) {
                    continue;
                }

                if (registry.all_of<Renderable>(entity)) {
                    auto& renderable = registry.get<Renderable>(entity);
                    if (!renderable.visible) continue;
                }

                bool doubleSided = false;
                if (registry.all_of<ECS::MaterialComponent>(entity)) {
                    auto& material = registry.get<ECS::MaterialComponent>(entity);
                    doubleSided = material.doubleSided;
                }

                bool invertCulling = false;
                if (registry.all_of<ECS::MaterialComponent>(entity)) {
                    auto& material = registry.get<ECS::MaterialComponent>(entity);
                    invertCulling = material.invertCulling;
                }

                VkPipeline desired = m_PickingPipeline;
                if (doubleSided && m_PickingPipelineNoCull != VK_NULL_HANDLE) {
                    desired = m_PickingPipelineNoCull;
                } else if (invertCulling && m_PickingPipelineFrontCull != VK_NULL_HANDLE) {
                    desired = m_PickingPipelineFrontCull;
                }

                if (desired != VK_NULL_HANDLE && desired != activePipeline) {
                    vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, desired);
                    activePipeline = desired;

                }

                glm::mat4 model = glm::mat4(1.0f);
                if (scene->hasTransform(entity)) {
                    model = scene->getCachedWorldTransform(entity);
                }

                PickingPushConstants pc{};
                pc.model = model;
                pc.view = view;
                pc.proj = proj;
                pc.entityIdPlusOne = static_cast<uint32_t>(entity) + 1u;

                vkCmdPushConstants(commandBuffer, m_PickingPipelineLayout,
                    VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(PickingPushConstants), &pc);

                uint32_t boneOffsetBytes = getBoneOffsetBytes(registry, entity);
                VkDescriptorSet sets[] = {m_DescriptorSet, m_BonesDescriptorSets[m_CurrentFrame]};
                vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, m_PickingPipelineLayout, 0, 2, sets, 1, &boneOffsetBytes);

                VkBuffer vertexBuffers[] = {pickVB};
                VkDeviceSize offsets[] = {0};
                vkCmdBindVertexBuffers(commandBuffer, 0, 1, vertexBuffers, offsets);
                vkCmdBindIndexBuffer(commandBuffer, pickIB, 0, VK_INDEX_TYPE_UINT32);
                vkCmdDrawIndexed(commandBuffer, pickIndexCount, 1, 0, 0, 0);
            }
        }

        vkCmdEndRenderPass(commandBuffer);
        m_PickingImageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    }

    auto choosePreviewCameraEntity = [&](entt::registry& registry, bool preferGameCamera) -> entt::entity {
        if (preferGameCamera) {
            auto gameView = registry.view<Camera, ECS::GameCameraComponent>();
            entt::entity fallback = entt::null;
            for (auto e : gameView) {
                const auto& gcc = gameView.get<ECS::GameCameraComponent>(e);
                if (fallback == entt::null) {
                    fallback = e;
                }
                if (gcc.primary) {
                    return e;
                }
            }
            if (fallback != entt::null) {
                return fallback;
            }
        } else {
            auto editorCameraView = registry.view<EditorCamera>();
            if (editorCameraView.begin() != editorCameraView.end()) {
                return *editorCameraView.begin();
            }
        }

        auto cameraView = registry.view<Camera>();
        if (cameraView.begin() != cameraView.end()) {
            return *cameraView.begin();
        }

        if (preferGameCamera) {
            auto editorCameraView = registry.view<EditorCamera>();
            if (editorCameraView.begin() != editorCameraView.end()) {
                return *editorCameraView.begin();
            }
        }

        return entt::null;
    };

    auto renderPreviewPass = [&](VkRenderPass renderPass, VkFramebuffer framebuffer, VkImageLayout& imageLayout, bool preferGameCamera) {
        VkRenderPassBeginInfo renderPassInfo{};
        renderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
        renderPassInfo.renderPass = renderPass;
        renderPassInfo.framebuffer = framebuffer;
        renderPassInfo.renderArea.offset = {0, 0};
        renderPassInfo.renderArea.extent = m_SwapChainExtent;

        VkClearValue offscreenClearValues[2];
        offscreenClearValues[0].color = {{m_ClearColor.r, m_ClearColor.g, m_ClearColor.b, m_ClearColor.a}};
        offscreenClearValues[1].depthStencil = {1.0f, 0};
        renderPassInfo.clearValueCount = 2;
        renderPassInfo.pClearValues = offscreenClearValues;

        vkCmdBeginRenderPass(commandBuffer, &renderPassInfo, VK_SUBPASS_CONTENTS_INLINE);
        imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

        if (scene) {
            auto& registry = scene->getRegistry();
            auto meshView = registry.view<Mesh>();

            if (!meshView.empty()) {
                glm::mat4 view = glm::mat4(1.0f);
                glm::mat4 proj = glm::mat4(1.0f);
                glm::vec3 cameraPos = glm::vec3(0.0f);

                if (!preferGameCamera && m_ScenePreviewCameraOverrideEnabled) {
                    view = m_ScenePreviewView;
                    proj = m_ScenePreviewProj;
                    cameraPos = m_ScenePreviewPosition;
                } else {
                    entt::entity cameraEntity = choosePreviewCameraEntity(registry, preferGameCamera);
                    if (cameraEntity != entt::null) {
                        if (registry.all_of<Camera>(cameraEntity)) {
                            auto& camera = registry.get<Camera>(cameraEntity);
                            camera.aspectRatio = static_cast<float>(m_SwapChainExtent.width) / static_cast<float>(m_SwapChainExtent.height);
                            proj = glm::perspective(glm::radians(camera.fov), camera.aspectRatio, camera.nearPlane, camera.farPlane);
                            proj[1][1] = -proj[1][1];
                            view = camera.getViewMatrix();
                            cameraPos = camera.position;
                        } else if (registry.all_of<EditorCamera>(cameraEntity)) {
                            auto& camera = registry.get<EditorCamera>(cameraEntity);
                            camera.aspectRatio = static_cast<float>(m_SwapChainExtent.width) / static_cast<float>(m_SwapChainExtent.height);
                            proj = glm::perspective(glm::radians(camera.fov), camera.aspectRatio, camera.nearPlane, camera.farPlane);
                            proj[1][1] = -proj[1][1];
                            view = camera.getViewMatrix();
                            cameraPos = camera.position;
                        }
                    }
                }

                struct DrawItem {
                    entt::entity entity;
                    float distSq;
                };

                // Procedural sky (Sun/Sky task): fullscreen triangle at the
                // far plane, drawn first so all geometry overdraws it.
                // Skipped without an enabled SkyComponent (legacy clear
                // color). The sun disk tracks the scene Sun, if any.
                if (m_SkyPipeline != VK_NULL_HANDLE) {
                    if (const Atlas::ECS::SkyComponent* sky = findFirstEnabledSky(registry)) {
                        glm::vec3 toSun(0.0f, 1.0f, 0.0f);
                        if (const Atlas::ECS::SunComponent* sun = findFirstSun(registry)) {
                            toSun = sun->sunDirection();
                        }
                        const glm::mat4 viewRot = glm::mat4(glm::mat3(view));
                        SkyPushConstants spc{};
                        spc.invViewProj = glm::inverse(proj * viewRot);
                        spc.sunDir = glm::vec4(toSun, 0.0f);
                        spc.horizonColor = glm::vec4(sky->horizonColor, 1.0f);
                        spc.zenithColor = glm::vec4(sky->zenithColor, 1.0f);
                        spc.groundColor = glm::vec4(sky->groundColor, 1.0f);
                        spc.sunColorSize = glm::vec4(sky->sunColor, sky->sunDiskSizeDeg);
                        spc.params = glm::vec4(sky->sunGlow, 0.0f, 0.0f, 0.0f);
                        vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, m_SkyPipeline);
                        vkCmdPushConstants(commandBuffer, m_SkyPipelineLayout,
                            VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0,
                            sizeof(SkyPushConstants), &spc);
                        vkCmdDraw(commandBuffer, 3, 1, 0, 0);
                    }
                }

                std::vector<entt::entity> opaqueCull;
                std::vector<entt::entity> opaqueFrontCull;
                std::vector<entt::entity> opaqueNoCull;
                std::vector<DrawItem> transparentCull;
                std::vector<DrawItem> transparentFrontCull;
                std::vector<DrawItem> transparentNoCull;

                for (auto entity : meshView) {
                    if (registry.all_of<ECS::EditorHiddenComponent>(entity)) {
                        continue;
                    }

                    auto& mesh = registry.get<Mesh>(entity);
                    // Phase 3b: skip decision reads the registry (no Mesh::Vk*).
                    {
                        VkBuffer filterVB = VK_NULL_HANDLE;
                        VkBuffer filterIB = VK_NULL_HANDLE;
                        uint32_t filterIndexCount = 0;
                        if (!resolveMeshDrawBuffers(
                                m_meshRegistry, mesh, filterVB, filterIB, filterIndexCount)) {
                            continue;
                        }
                    }

                    if (registry.all_of<Renderable>(entity)) {
                        auto& renderable = registry.get<Renderable>(entity);
                        if (!renderable.visible) continue;
                    }

                    bool isBlend = false;
                    bool doubleSided = false;
                    bool invertCulling = false;
                    if (registry.all_of<ECS::MaterialComponent>(entity)) {
                        auto& material = registry.get<ECS::MaterialComponent>(entity);
                        isBlend = (material.alphaMode == ECS::MaterialComponent::AlphaMode::Blend);
                        doubleSided = material.doubleSided;
                        invertCulling = material.invertCulling;
                    }

                    if (isBlend) {
                        glm::vec3 pos = glm::vec3(0.0f);
                        if (scene && scene->hasTransform(entity)) {
                            glm::mat4 model = scene->getCachedWorldTransform(entity);
                            pos = glm::vec3(model[3]);
                        }
                        glm::vec3 d = pos - cameraPos;
                        DrawItem item{entity, glm::dot(d, d)};
                        if (doubleSided) {
                            transparentNoCull.push_back(item);
                        } else if (invertCulling) {
                            transparentFrontCull.push_back(item);
                        } else {
                            transparentCull.push_back(item);
                        }
                    } else {
                        if (doubleSided) {
                            opaqueNoCull.push_back(entity);
                        } else if (invertCulling) {
                            opaqueFrontCull.push_back(entity);
                        } else {
                            opaqueCull.push_back(entity);
                        }
                    }
                }

                std::sort(transparentCull.begin(), transparentCull.end(), [](const DrawItem& a, const DrawItem& b) { return a.distSq > b.distSq; });
                std::sort(transparentFrontCull.begin(), transparentFrontCull.end(), [](const DrawItem& a, const DrawItem& b) { return a.distSq > b.distSq; });
                std::sort(transparentNoCull.begin(), transparentNoCull.end(), [](const DrawItem& a, const DrawItem& b) { return a.distSq > b.distSq; });

                // Auto-LOD (§5): swap dense meshes for their simplified GPU
                // variant when the LOD system selected LOD1/LOD2 and a variant
                // was generated at import. Falls back to the full mesh.
                auto resolveSimplified = [&](entt::entity entity, VkBuffer baseVB, VkBuffer baseIB,
                                              uint32_t baseIndexCount, VkBuffer& outVB, VkBuffer& outIB,
                                              uint32_t& outIndexCount) -> bool {
                    outVB = baseVB;
                    outIB = baseIB;
                    outIndexCount = baseIndexCount;
                    const Atlas::LODComponent* lod = registry.try_get<Atlas::LODComponent>(entity);
                    if (!lod) return false;
                    int level = 0;
                    if (lod->currentLevel() == Atlas::LODLevel::LOD1) level = 1;
                    else if (lod->currentLevel() == Atlas::LODLevel::LOD2) level = 2;
                    else return false;
                    StaticMeshBuffers variant;
                    if (!findSimplifiedVariant(baseVB, baseIB, level, &variant)) return false;
                    outVB = variant.vertexBuffer;
                    outIB = variant.indexBuffer;
                    outIndexCount = variant.indexCount;
                    return true;
                };

                auto drawEntity = [&](entt::entity entity) {
                    auto& mesh = registry.get<Mesh>(entity);
                    // Phase 3b: draw buffers come only from the registry
                    // (::Mesh holds no Vk* fields; ecs.h is Vulkan-free).
                    VkBuffer baseVB = VK_NULL_HANDLE;
                    VkBuffer baseIB = VK_NULL_HANDLE;
                    uint32_t baseIndexCount = 0;
                    if (!resolveMeshDrawBuffers(m_meshRegistry, mesh, baseVB, baseIB, baseIndexCount)) return;
                    VkBuffer drawVB = baseVB;
                    VkBuffer drawIB = baseIB;
                    uint32_t drawIndexCount = baseIndexCount;
                    const bool drewSimplified = resolveSimplified(entity, baseVB, baseIB, baseIndexCount,
                                                                  drawVB, drawIB, drawIndexCount);
                    glm::mat4 model = glm::mat4(1.0f);
                    glm::vec4 baseColor = glm::vec4(1.0f);
                    glm::vec4 emissiveFactor = glm::vec4(0.0f);
                    float metallic = 0.0f;
                    float roughness = 0.5f;
                    float alphaCutoff = 0.5f;
                    int32_t albedoTexIndex = 0;
                    int32_t normalTexIndex = 0;
                    int32_t metallicRoughnessTexIndex = 0;
                    int32_t aoTexIndex = 0;
                    int32_t emissiveTexIndex = 0;
                    int32_t flags = 0;

                    if (scene && scene->hasTransform(entity)) {
                        model = scene->getCachedWorldTransform(entity);
                    }

                    if (registry.all_of<ECS::MaterialComponent>(entity)) {
                        auto& material = registry.get<ECS::MaterialComponent>(entity);
                        baseColor = material.baseColor;
                        metallic = material.metallic;
                        roughness = material.roughness;
                        emissiveFactor = glm::vec4(material.emissiveFactor, 0.0f);
                        alphaCutoff = material.alphaCutoff;
                        if (material.useAlbedoTexture && material.albedoTextureIndex >= 0) { flags |= (1 << 0); albedoTexIndex = material.albedoTextureIndex; }
                        if (material.useNormalTexture && material.normalTextureIndex >= 0) { flags |= (1 << 1); normalTexIndex = material.normalTextureIndex; }
                        if (material.useMetallicRoughnessTexture && material.metallicRoughnessTextureIndex >= 0) { flags |= (1 << 2); metallicRoughnessTexIndex = material.metallicRoughnessTextureIndex; }
                        if (material.useAOTexture && material.aoTextureIndex >= 0) { flags |= (1 << 3); aoTexIndex = material.aoTextureIndex; }
                        if (material.useEmissiveTexture && material.emissiveTextureIndex >= 0) { flags |= (1 << 4); emissiveTexIndex = material.emissiveTextureIndex; }
                        if (material.doubleSided) { flags |= (1 << 5); }
                        flags |= (static_cast<int32_t>(material.alphaMode) & 3) << 8;
                    }

                    uint32_t boneOffsetBytes = getBoneOffsetBytes(registry, entity);
                    VkDescriptorSet sets[] = {m_DescriptorSet, m_BonesDescriptorSets[m_CurrentFrame]};
                    vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, m_PipelineLayout, 0, 2, sets, 1, &boneOffsetBytes);

                    PushConstants pushConstants{};
                    pushConstants.model = model;
                    pushConstants.viewProj = proj * view;
                    pushConstants.baseColor = baseColor;
                    pushConstants.emissiveFactor = emissiveFactor;
                    pushConstants.metallic = metallic;
                    pushConstants.roughness = roughness;
                    pushConstants.alphaCutoff = alphaCutoff;
                    pushConstants.albedoTexIndex = albedoTexIndex;
                    pushConstants.normalTexIndex = normalTexIndex;
                    pushConstants.metallicRoughnessTexIndex = metallicRoughnessTexIndex;
                    pushConstants.aoTexIndex = aoTexIndex;
                    pushConstants.emissiveTexIndex = emissiveTexIndex;
                    pushConstants.flags = flags;
                    vkCmdPushConstants(commandBuffer, m_PipelineLayout, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(PushConstants), &pushConstants);

                    VkBuffer vertexBuffers[] = {drawVB};
                    VkDeviceSize offsets[] = {0};
                    vkCmdBindVertexBuffers(commandBuffer, 0, 1, vertexBuffers, offsets);
                    vkCmdBindIndexBuffer(commandBuffer, drawIB, 0, VK_INDEX_TYPE_UINT32);
                    vkCmdDrawIndexed(commandBuffer, drawIndexCount, 1, 0, 0, 0);
                    m_FrameDrawCalls++;
                    m_FrameTriangles += drawIndexCount / 3u;
                    if (drewSimplified) m_FrameSimplifiedDraws++;
                };

                // TDD §6: group opaque entities sharing (mesh buffers + full
                // material state) into GPU-instanced batches. Transparent,
                // skinned and picking/outline paths stay per-entity.
                struct InstanceMatKey {
                    glm::vec4 baseColor{1.0f};
                    glm::vec4 emissive{0.0f};
                    float metallic = 0.0f;
                    float roughness = 0.5f;
                    float alphaCutoff = 0.5f;
                    int32_t tex[5] = {0, 0, 0, 0, 0};
                    int32_t flags = 0;
                    bool operator==(const InstanceMatKey& o) const {
                        return baseColor == o.baseColor && emissive == o.emissive &&
                               metallic == o.metallic && roughness == o.roughness &&
                               alphaCutoff == o.alphaCutoff && flags == o.flags &&
                               tex[0] == o.tex[0] && tex[1] == o.tex[1] && tex[2] == o.tex[2] &&
                               tex[3] == o.tex[3] && tex[4] == o.tex[4];
                    }
                };
                struct InstanceBatchKey {
                    VkBuffer vertexBuffer = VK_NULL_HANDLE;
                    VkBuffer indexBuffer = VK_NULL_HANDLE;
                    InstanceMatKey mat;
                    bool operator==(const InstanceBatchKey& o) const {
                        return vertexBuffer == o.vertexBuffer && indexBuffer == o.indexBuffer && mat == o.mat;
                    }
                };
                struct InstanceBatchKeyHash {
                    size_t operator()(const InstanceBatchKey& k) const noexcept {
                        size_t h = std::hash<uint64_t>{}(reinterpret_cast<uint64_t>(k.vertexBuffer));
                        h ^= std::hash<uint64_t>{}(reinterpret_cast<uint64_t>(k.indexBuffer) + 0x9e3779b97f4a7c15ULL + (h << 6) + (h >> 2));
                        const uint32_t* words = reinterpret_cast<const uint32_t*>(&k.mat);
                        for (size_t i = 0; i < sizeof(InstanceMatKey) / 4; ++i) {
                            h ^= std::hash<uint32_t>{}(words[i] + 0x9e3779b9u + static_cast<uint32_t>(h) + (h << 6) + (h >> 2));
                        }
                        return h;
                    }
                };
                struct InstanceBatch {
                    InstanceBatchKey key;
                    uint32_t indexCount = 0;
                    bool isSimplified = false;
                    std::vector<glm::mat4> matrices;
                    std::vector<entt::entity> members;
                };

                auto readInstanceKey = [&](entt::entity entity, const Mesh& mesh, InstanceBatchKey& outKey,
                                           uint32_t& outIndexCount, bool& outSimplified) -> bool {
                    // Phase 3a: batch key groups by the buffers actually
                    // drawn (registry first, legacy fallback only when
                    // renderMeshId == 0); dead/unpublished → per-entity path.
                    VkBuffer baseVB = VK_NULL_HANDLE;
                    VkBuffer baseIB = VK_NULL_HANDLE;
                    uint32_t baseIndexCount = 0;
                    if (!resolveMeshDrawBuffers(m_meshRegistry, mesh, baseVB, baseIB, baseIndexCount)) return false;
                    // Skinned meshes need per-entity bone palettes: not instanceable.
                    if (registry.all_of<ECS::SkeletonComponent>(entity) ||
                        registry.all_of<ECS::SkinnedMeshComponent>(entity)) {
                        return false;
                    }
                    // Resolve auto-LOD variant first so batches group by the
                    // buffers actually drawn (variants batch among themselves).
                    VkBuffer vb = baseVB;
                    VkBuffer ib = baseIB;
                    outIndexCount = baseIndexCount;
                    outSimplified = resolveSimplified(entity, baseVB, baseIB, baseIndexCount,
                                                       vb, ib, outIndexCount);
                    outKey.vertexBuffer = vb;
                    outKey.indexBuffer = ib;
                    InstanceMatKey m;
                    if (registry.all_of<ECS::MaterialComponent>(entity)) {
                        auto& material = registry.get<ECS::MaterialComponent>(entity);
                        m.baseColor = material.baseColor;
                        m.metallic = material.metallic;
                        m.roughness = material.roughness;
                        m.emissive = glm::vec4(material.emissiveFactor, 0.0f);
                        m.alphaCutoff = material.alphaCutoff;
                        if (material.useAlbedoTexture && material.albedoTextureIndex >= 0) { m.flags |= (1 << 0); m.tex[0] = material.albedoTextureIndex; }
                        if (material.useNormalTexture && material.normalTextureIndex >= 0) { m.flags |= (1 << 1); m.tex[1] = material.normalTextureIndex; }
                        if (material.useMetallicRoughnessTexture && material.metallicRoughnessTextureIndex >= 0) { m.flags |= (1 << 2); m.tex[2] = material.metallicRoughnessTextureIndex; }
                        if (material.useAOTexture && material.aoTextureIndex >= 0) { m.flags |= (1 << 3); m.tex[3] = material.aoTextureIndex; }
                        if (material.useEmissiveTexture && material.emissiveTextureIndex >= 0) { m.flags |= (1 << 4); m.tex[4] = material.emissiveTextureIndex; }
                        if (material.doubleSided) { m.flags |= (1 << 5); }
                        m.flags |= (static_cast<int32_t>(material.alphaMode) & 3) << 8;
                    }
                    outKey.mat = m;
                    return true;
                };

                uint32_t instCursor = 0;
                void* instMapped = m_InstanceMapped[m_CurrentFrame];
                VkBuffer instBuffer = m_InstanceBuffers[m_CurrentFrame];

                auto drawOpaqueList = [&](const std::vector<entt::entity>& list, VkPipeline singlePipe, VkPipeline instPipe) {
                    if (list.empty() || singlePipe == VK_NULL_HANDLE) {
                        return;
                    }
                    const bool canInstance = m_InstancingEnabled && instPipe != VK_NULL_HANDLE &&
                                             instMapped != nullptr && instBuffer != VK_NULL_HANDLE;

                    std::unordered_map<InstanceBatchKey, size_t, InstanceBatchKeyHash> batchIndex;
                    std::vector<InstanceBatch> batches;
                    std::vector<entt::entity> singles;
                    if (canInstance) {
                        batchIndex.reserve(list.size());
                        for (auto e : list) {
                            if (!registry.valid(e) || !registry.all_of<Mesh>(e)) {
                                singles.push_back(e);
                                continue;
                            }
                            const auto& mesh = registry.get<Mesh>(e);
                            InstanceBatchKey key;
                            uint32_t resolvedCount = 0;
                            bool resolvedSimplified = false;
                            if (!readInstanceKey(e, mesh, key, resolvedCount, resolvedSimplified)) {
                                singles.push_back(e);
                                continue;
                            }
                            auto it = batchIndex.find(key);
                            if (it == batchIndex.end()) {
                                size_t bi = batches.size();
                                batchIndex.emplace(key, bi);
                                InstanceBatch b;
                                b.key = key;
                                b.indexCount = resolvedCount;
                                b.isSimplified = resolvedSimplified;
                                batches.push_back(std::move(b));
                                it = batchIndex.find(key);
                            }
                            glm::mat4 model(1.0f);
                            if (scene && scene->hasTransform(e)) {
                                model = scene->getCachedWorldTransform(e);
                            }
                            batches[it->second].matrices.push_back(model);
                            batches[it->second].members.push_back(e);
                        }
                    } else {
                        singles = list;
                    }

                    if (!singles.empty()) {
                        vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, singlePipe);
                        for (auto e : singles) drawEntity(e);
                    }

                    for (auto& batch : batches) {
                        const uint32_t count = static_cast<uint32_t>(batch.matrices.size());
                        const bool fits = (instCursor + count) <= MAX_INSTANCES_PER_FRAME;
                        if (count < MIN_INSTANCES_PER_BATCH || !fits) {
                            // Below threshold or staging full: per-entity fallback.
                            vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, singlePipe);
                            for (auto e : batch.members) drawEntity(e);
                            continue;
                        }

                        vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, instPipe);

                        PushConstants pc{};
                        pc.model = glm::mat4(1.0f);
                        pc.viewProj = proj * view;
                        pc.baseColor = batch.key.mat.baseColor;
                        pc.emissiveFactor = batch.key.mat.emissive;
                        pc.metallic = batch.key.mat.metallic;
                        pc.roughness = batch.key.mat.roughness;
                        pc.alphaCutoff = batch.key.mat.alphaCutoff;
                        pc.albedoTexIndex = batch.key.mat.tex[0];
                        pc.normalTexIndex = batch.key.mat.tex[1];
                        pc.metallicRoughnessTexIndex = batch.key.mat.tex[2];
                        pc.aoTexIndex = batch.key.mat.tex[3];
                        pc.emissiveTexIndex = batch.key.mat.tex[4];
                        pc.flags = batch.key.mat.flags;
                        vkCmdPushConstants(commandBuffer, m_PipelineLayout,
                            VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(PushConstants), &pc);

                        uint32_t zeroOffset = 0;
                        VkDescriptorSet sets[] = {m_DescriptorSet, m_BonesDescriptorSets[m_CurrentFrame]};
                        vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, m_PipelineLayout, 0, 2, sets, 1, &zeroOffset);

                        auto* dst = reinterpret_cast<glm::mat4*>(static_cast<char*>(instMapped) + instCursor * sizeof(glm::mat4));
                        std::memcpy(dst, batch.matrices.data(), count * sizeof(glm::mat4));

                        VkBuffer vbs[] = {batch.key.vertexBuffer, instBuffer};
                        VkDeviceSize offs[] = {0, static_cast<VkDeviceSize>(instCursor) * sizeof(glm::mat4)};
                        vkCmdBindVertexBuffers(commandBuffer, 0, 2, vbs, offs);
                        vkCmdBindIndexBuffer(commandBuffer, batch.key.indexBuffer, 0, VK_INDEX_TYPE_UINT32);
                        vkCmdDrawIndexed(commandBuffer, batch.indexCount, count, 0, 0, 0);
                        m_FrameDrawCalls++;
                        m_FrameTriangles += (batch.indexCount / 3u) * count;
                        m_FrameInstancedDraws++;
                        m_FrameInstancedInstances += count;
                        if (batch.isSimplified) m_FrameSimplifiedDraws++;
                        instCursor += count;
                    }
                };

                drawOpaqueList(opaqueCull, m_GraphicsPipeline, m_GraphicsPipelineInstanced);
                if (m_GraphicsPipelineFrontCull != VK_NULL_HANDLE) {
                    drawOpaqueList(opaqueFrontCull, m_GraphicsPipelineFrontCull, m_GraphicsPipelineInstancedFrontCull);
                }
                if (m_GraphicsPipelineNoCull != VK_NULL_HANDLE) {
                    drawOpaqueList(opaqueNoCull, m_GraphicsPipelineNoCull, m_GraphicsPipelineInstancedNoCull);
                }
                // TDD §4.2/§5.2 HLOD1 impostors: billboard quads tinted per cell.
                // Opaque -> drawn with the no-cull PBR pipeline right after opaques.
                if (!m_ImpostorDraws.empty() && m_ImpostorQuadVB != VK_NULL_HANDLE &&
                    m_ImpostorQuadIB != VK_NULL_HANDLE && m_GraphicsPipelineNoCull != VK_NULL_HANDLE) {
                    vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, m_GraphicsPipelineNoCull);
                    for (const auto& imp : m_ImpostorDraws) {
                        PushConstants pc{};
                        pc.model = imp.model;
                        pc.viewProj = proj * view;
                        pc.baseColor = imp.color;
                        pc.emissiveFactor = glm::vec4(0.0f);
                        pc.metallic = 0.0f;
                        pc.roughness = 1.0f;
                        pc.alphaCutoff = 0.5f;
                        vkCmdPushConstants(commandBuffer, m_PipelineLayout,
                            VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(PushConstants), &pc);

                        uint32_t zeroOffset = 0;
                        VkDescriptorSet sets[] = {m_DescriptorSet, m_BonesDescriptorSets[m_CurrentFrame]};
                        vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, m_PipelineLayout, 0, 2, sets, 1, &zeroOffset);

                        VkBuffer vbs[] = {m_ImpostorQuadVB};
                        VkDeviceSize offs[] = {0};
                        vkCmdBindVertexBuffers(commandBuffer, 0, 1, vbs, offs);
                        vkCmdBindIndexBuffer(commandBuffer, m_ImpostorQuadIB, 0, VK_INDEX_TYPE_UINT32);
                        vkCmdDrawIndexed(commandBuffer, 6, 1, 0, 0, 0);
                        m_FrameDrawCalls++;
                        m_FrameTriangles += 2;
                    }
                }
                if (!transparentCull.empty() && m_GraphicsPipelineBlend != VK_NULL_HANDLE) {
                    vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, m_GraphicsPipelineBlend);
                    for (const auto& item : transparentCull) drawEntity(item.entity);
                }
                if (!transparentFrontCull.empty() && m_GraphicsPipelineBlendFrontCull != VK_NULL_HANDLE) {
                    vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, m_GraphicsPipelineBlendFrontCull);
                    for (const auto& item : transparentFrontCull) drawEntity(item.entity);
                }
                if (!transparentNoCull.empty() && m_GraphicsPipelineBlendNoCull != VK_NULL_HANDLE) {
                    vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, m_GraphicsPipelineBlendNoCull);
                    for (const auto& item : transparentNoCull) drawEntity(item.entity);
                }

                if (!preferGameCamera && !m_SelectedEntityIds.empty() && m_OutlinePipeline != VK_NULL_HANDLE) {
                    vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, m_OutlinePipeline);
                    for (uint32_t selId : m_SelectedEntityIds) {
                        if (selId == UINT32_MAX) continue;
                        entt::entity selected = static_cast<entt::entity>(selId);
                        if (!registry.valid(selected) || !registry.all_of<Mesh>(selected)) continue;
                        auto& selMesh = registry.get<Mesh>(selected);
                        // Phase 3a: buffers come from the registry (legacy
                        // Mesh::Vk* only when renderMeshId == 0).
                        VkBuffer selVB = VK_NULL_HANDLE;
                        VkBuffer selIB = VK_NULL_HANDLE;
                        uint32_t selIndexCount = 0;
                        if (!resolveMeshDrawBuffers(m_meshRegistry, selMesh, selVB, selIB, selIndexCount)) continue;

                        glm::mat4 selModel = glm::mat4(1.0f);
                        if (scene && scene->hasTransform(selected)) {
                            selModel = scene->getCachedWorldTransform(selected);
                        }

                        OutlinePushConstants pc{};
                        pc.model = selModel;
                        pc.view = view;
                        pc.proj = proj;
                        pc.color = glm::vec4(1.0f, 0.7f, 0.1f, 1.0f);
                        pc.width = 0.015f;
                        vkCmdPushConstants(commandBuffer, m_OutlinePipelineLayout, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(OutlinePushConstants), &pc);

                        uint32_t boneOffsetBytes = getBoneOffsetBytes(registry, selected);
                        VkDescriptorSet sets[] = {m_DescriptorSet, m_BonesDescriptorSets[m_CurrentFrame]};
                        vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, m_OutlinePipelineLayout, 0, 2, sets, 1, &boneOffsetBytes);

                        VkBuffer vertexBuffers[] = {selVB};
                        VkDeviceSize offsets[] = {0};
                        vkCmdBindVertexBuffers(commandBuffer, 0, 1, vertexBuffers, offsets);
                        vkCmdBindIndexBuffer(commandBuffer, selIB, 0, VK_INDEX_TYPE_UINT32);
                        vkCmdDrawIndexed(commandBuffer, selIndexCount, 1, 0, 0, 0);
                    }
                }
            }
        }

        vkCmdEndRenderPass(commandBuffer);
        imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    };

    if (m_GameMode) {
        // Standalone game: render the scene directly to the swapchain with the
        // game camera and skip the editor UI pass entirely.
        VkImageLayout dummyLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        renderPreviewPass(m_RenderPass, m_SwapChainFramebuffers[imageIndex], dummyLayout, true);
    } else {
        renderPreviewPass(m_OffscreenRenderPass, m_OffscreenFramebuffer, m_OffscreenImageLayout, false);
        renderPreviewPass(m_OffscreenRenderPass, m_GameOffscreenFramebuffer, m_GameOffscreenImageLayout, true);

        // Render UI to swapchain
        VkRenderPassBeginInfo renderPassInfo{};
        renderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
        renderPassInfo.renderPass = m_RenderPass;
        renderPassInfo.framebuffer = m_SwapChainFramebuffers[imageIndex];
        renderPassInfo.renderArea.offset = {0, 0};
        renderPassInfo.renderArea.extent = m_SwapChainExtent;
        VkClearValue clearValues[2];
        clearValues[0].color = {{m_ClearColor.r, m_ClearColor.g, m_ClearColor.b, m_ClearColor.a}};
        clearValues[1].depthStencil = {1.0f, 0};
        renderPassInfo.clearValueCount = 2;
        renderPassInfo.pClearValues = clearValues;
        vkCmdBeginRenderPass(commandBuffer, &renderPassInfo, VK_SUBPASS_CONTENTS_INLINE);

        if (m_RenderCallback) {
           m_RenderCallback(commandBuffer);
        }

        vkCmdEndRenderPass(commandBuffer);
    }

    } // end TracyVkZone scope

    m_LastDrawCalls = m_FrameDrawCalls;
    m_LastTriangles = m_FrameTriangles;
    m_LastInstancedDraws = m_FrameInstancedDraws;
    m_LastInstancedInstances = m_FrameInstancedInstances;
    m_LastSimplifiedDraws = m_FrameSimplifiedDraws;

    if (vkEndCommandBuffer(commandBuffer) != VK_SUCCESS) {
        throw std::runtime_error("failed to record command buffer!");
    }
}

bool Renderer::checkValidationLayerSupport() {
    uint32_t layerCount;
    vkEnumerateInstanceLayerProperties(&layerCount, nullptr);

    std::vector<VkLayerProperties> availableLayers(layerCount);
    vkEnumerateInstanceLayerProperties(&layerCount, availableLayers.data());

    for (const char* layerName : validationLayers) {
        bool layerFound = false;
        for (const auto& layerProperties : availableLayers) {
            if (strcmp(layerName, layerProperties.layerName) == 0) {
                layerFound = true;
                break;
            }
        }
        if (!layerFound) return false;
    }
    return true;
}

bool Renderer::isDeviceSuitable(VkPhysicalDevice device) {
    QueueFamilyIndices indices = findQueueFamilies(device);
    bool extensionsSupported = checkDeviceExtensionSupport(device);

    bool swapChainAdequate = false;
    if (extensionsSupported) {
        SwapChainSupportDetails swapChainSupport = querySwapChainSupport(device);
        swapChainAdequate = !swapChainSupport.formats.empty() && !swapChainSupport.presentModes.empty();
    }

    return indices.isComplete() && extensionsSupported && swapChainAdequate;
}

bool Renderer::checkDeviceExtensionSupport(VkPhysicalDevice device) {
    uint32_t extensionCount;
    vkEnumerateDeviceExtensionProperties(device, nullptr, &extensionCount, nullptr);

    std::vector<VkExtensionProperties> availableExtensions(extensionCount);
    vkEnumerateDeviceExtensionProperties(device, nullptr, &extensionCount, availableExtensions.data());

    std::set<std::string> requiredExtensions(deviceExtensions.begin(), deviceExtensions.end());
    for (const auto& extension : availableExtensions) {
        requiredExtensions.erase(extension.extensionName);
    }
    return requiredExtensions.empty();
}

QueueFamilyIndices Renderer::findQueueFamilies(VkPhysicalDevice device) {
    QueueFamilyIndices indices;

    uint32_t queueFamilyCount = 0;
    vkGetPhysicalDeviceQueueFamilyProperties(device, &queueFamilyCount, nullptr);

    std::vector<VkQueueFamilyProperties> queueFamilies(queueFamilyCount);
    vkGetPhysicalDeviceQueueFamilyProperties(device, &queueFamilyCount, queueFamilies.data());

    int i = 0;
    for (const auto& queueFamily : queueFamilies) {
        if (queueFamily.queueFlags & VK_QUEUE_GRAPHICS_BIT) {
            indices.graphicsFamily = i;
        }

        VkBool32 presentSupport = false;
        vkGetPhysicalDeviceSurfaceSupportKHR(device, i, m_Surface, &presentSupport);
        if (presentSupport) {
            indices.presentFamily = i;
        }

        if (indices.isComplete()) break;
        i++;
    }

    return indices;
}

SwapChainSupportDetails Renderer::querySwapChainSupport(VkPhysicalDevice device) {
    SwapChainSupportDetails details;
    vkGetPhysicalDeviceSurfaceCapabilitiesKHR(device, m_Surface, &details.capabilities);

    uint32_t formatCount;
    vkGetPhysicalDeviceSurfaceFormatsKHR(device, m_Surface, &formatCount, nullptr);
    if (formatCount != 0) {
        details.formats.resize(formatCount);
        vkGetPhysicalDeviceSurfaceFormatsKHR(device, m_Surface, &formatCount, details.formats.data());
    }

    uint32_t presentModeCount;
    vkGetPhysicalDeviceSurfacePresentModesKHR(device, m_Surface, &presentModeCount, nullptr);
    if (presentModeCount != 0) {
        details.presentModes.resize(presentModeCount);
        vkGetPhysicalDeviceSurfacePresentModesKHR(device, m_Surface, &presentModeCount, details.presentModes.data());
    }

    return details;
}

VkSurfaceFormatKHR Renderer::chooseSwapSurfaceFormat(const std::vector<VkSurfaceFormatKHR>& availableFormats) {
    for (const auto& availableFormat : availableFormats) {
        if (availableFormat.format == VK_FORMAT_B8G8R8A8_SRGB && 
            availableFormat.colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR) {
            return availableFormat;
        }
    }
    return availableFormats[0];
}

VkPresentModeKHR Renderer::chooseSwapPresentMode(const std::vector<VkPresentModeKHR>& availablePresentModes) {
    bool hasMailbox = false;
    bool hasImmediate = false;

    for (const auto& mode : availablePresentModes) {
        if (mode == VK_PRESENT_MODE_MAILBOX_KHR) hasMailbox = true;
        if (mode == VK_PRESENT_MODE_IMMEDIATE_KHR) hasImmediate = true;
    }

    if (m_VSyncEnabled) {
        return VK_PRESENT_MODE_FIFO_KHR; // guaranteed support
    }

    if (hasImmediate) {
        return VK_PRESENT_MODE_IMMEDIATE_KHR; // no v-sync (tearing)
    }
    if (hasMailbox) {
        return VK_PRESENT_MODE_MAILBOX_KHR; // best-effort when immediate unavailable
    }
    return VK_PRESENT_MODE_FIFO_KHR;
}

VkExtent2D Renderer::chooseSwapExtent(const VkSurfaceCapabilitiesKHR& capabilities) {
    if (capabilities.currentExtent.width != UINT32_MAX) {
        return capabilities.currentExtent;
    } else {
        int width, height;
        glfwGetFramebufferSize(m_Window->getGLFWWindow(), &width, &height);

        VkExtent2D actualExtent = {
            static_cast<uint32_t>(width),
            static_cast<uint32_t>(height)
        };

        // Parenthesized (std::max)/(std::min): immune to windows.h max/min macros.
        actualExtent.width = (std::max)(capabilities.minImageExtent.width,
            (std::min)(capabilities.maxImageExtent.width, actualExtent.width));
        actualExtent.height = (std::max)(capabilities.minImageExtent.height,
            (std::min)(capabilities.maxImageExtent.height, actualExtent.height));

        return actualExtent;
    }
}

std::vector<char> Renderer::readFile(const std::string& filename) {
    namespace fs = std::filesystem;
    std::vector<fs::path> candidates = {
        fs::path(filename),
        fs::current_path() / filename
    };
#ifdef _WIN32
    char buffer[MAX_PATH] = {};
    DWORD len = GetModuleFileNameA(nullptr, buffer, MAX_PATH);
    if (len > 0 && len < MAX_PATH) {
        candidates.push_back(fs::path(std::string(buffer, len)).parent_path() / filename);
    }
#elif defined(__linux__)
    std::error_code ec;
    fs::path exePath = fs::read_symlink("/proc/self/exe", ec);
    if (!ec) {
        candidates.push_back(exePath.parent_path() / filename);
    }
#endif

    for (const auto& candidate : candidates) {
        std::ifstream file(candidate, std::ios::ate | std::ios::binary);
        if (!file.is_open()) {
            continue;
        }

        const size_t fileSize = static_cast<size_t>(file.tellg());
        std::vector<char> buffer(fileSize);
        file.seekg(0);
        file.read(buffer.data(), static_cast<std::streamsize>(fileSize));
        return buffer;
    }

    // Fallback to embedded shaders (when available).
    if (const auto* embedded = EmbeddedShaders::find(filename.c_str())) {
        const char* begin = reinterpret_cast<const char*>(embedded->data);
        return std::vector<char>(begin, begin + embedded->size);
    }

    const std::string baseName = fs::path(filename).filename().string();
    if (baseName != filename) {
        if (const auto* embedded = EmbeddedShaders::find(baseName.c_str())) {
            const char* begin = reinterpret_cast<const char*>(embedded->data);
            return std::vector<char>(begin, begin + embedded->size);
        }
    }

    throw std::runtime_error("failed to open shader file: " + filename);
}

VkShaderModule Renderer::createShaderModule(const std::vector<char>& code) {
    VkShaderModuleCreateInfo createInfo{};
    createInfo.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
    createInfo.codeSize = code.size();
    createInfo.pCode = reinterpret_cast<const uint32_t*>(code.data());

    VkShaderModule shaderModule;
    if (vkCreateShaderModule(m_Device, &createInfo, nullptr, &shaderModule) != VK_SUCCESS) {
        throw std::runtime_error("failed to create shader module!");
    }
    return shaderModule;
}

VKAPI_ATTR VkBool32 VKAPI_CALL Renderer::debugCallback(
    VkDebugUtilsMessageSeverityFlagBitsEXT messageSeverity,
    VkDebugUtilsMessageTypeFlagsEXT messageType,
    const VkDebugUtilsMessengerCallbackDataEXT* pCallbackData,
    void* pUserData) {
    (void)messageSeverity;
    (void)messageType;
    (void)pUserData;
    if (pCallbackData && pCallbackData->pMessage) {
        // Benign Tracy GPU-profiler polling: the query simply isn't ready yet
        // this frame. Not an error — filter it so the log stays meaningful.
        if (std::strstr(pCallbackData->pMessage, "vkGetQueryPoolResults(): Returned VK_NOT_READY") != nullptr) {
            return VK_FALSE;
        }
    }
    std::cerr << "validation layer: " << pCallbackData->pMessage << std::endl;
    return VK_FALSE;
}

}

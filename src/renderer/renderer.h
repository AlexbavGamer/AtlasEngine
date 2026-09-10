#pragma once

#include <vulkan/vulkan.h>
#include <GLFW/glfw3.h>
#include <glm/glm.hpp>
#include <memory>
#include <vector>
#include <unordered_map>
#include <functional>
#include <string>
#include <deque>
#include <array>
#include <cstddef>
#include <cstdint>

#include "../core/base/non_copyable.h"
#include "../vulkan/vulkan_structs.h"
#include "memory/memory_manager.h"
#include "render_resources.h"
#include "../core/profiler.h"

namespace Atlas {

struct Light {
    glm::vec3 position;
    float intensity;
    glm::vec3 color;
    float pad0 = 0.0f;
    glm::vec3 direction = glm::vec3(0.0f, -1.0f, 0.0f);
    // 0 = point, 1 = directional (spot falls back to point in v1)
    int32_t type = 0;
};
static_assert(sizeof(Light) == 48, "Light must match pbr_frag.glsl std140 layout");

struct LightBuffer {
    Light lights[4];
    int32_t lightCount = 0;
    // Explicit pad: std140 aligns the following vec3 to 16 bytes.
    float _pad0 = 0.0f, _pad1 = 0.0f, _pad2 = 0.0f;
    glm::vec3 cameraPos = glm::vec3(0.0f);
    float padding = 0.0f;
    glm::mat4 shadowViewProj{1.0f};
    // x: shadow enabled (0/1), y: depth bias, z: map size, w: reserved
    glm::vec4 shadowParams = glm::vec4(0.0f);
};
static_assert(sizeof(LightBuffer) == 304, "LightBuffer must match pbr_frag.glsl std140 layout");

struct PushConstants {
    glm::mat4 model;
    glm::mat4 viewProj;
    glm::vec4 baseColor;
    glm::vec4 emissiveFactor;
    float metallic;
    float roughness;
    float alphaCutoff;
    int32_t albedoTexIndex;
    int32_t normalTexIndex;
    int32_t metallicRoughnessTexIndex;
    int32_t aoTexIndex;
    int32_t emissiveTexIndex;
    int32_t flags;
};

struct ShadowPushConstants {
    glm::mat4 model;
    glm::mat4 viewProj;
};
static_assert(sizeof(ShadowPushConstants) <= 128, "ShadowPushConstants must stay within 128 bytes");

// Procedural sky (fullscreen triangle at far plane). Layout MUST match the
// SkyPC block in shaders/sky_vert.glsl + sky_frag.glsl exactly (std430).
struct SkyPushConstants {
    glm::mat4 invViewProj;   // 0: inverse of (proj * rotation-only view)
    glm::vec4 sunDir;        // 64: xyz = toward sun (normalized), w unused
    glm::vec4 horizonColor;  // 80
    glm::vec4 zenithColor;   // 96
    glm::vec4 groundColor;   // 112
    glm::vec4 sunColorSize;  // 128: rgb + disk size in degrees
    glm::vec4 params;        // 144: x = glow strength, yzw reserved
};
static_assert(sizeof(SkyPushConstants) <= 256, "SkyPushConstants exceeds maxPushConstantsSize (256)");
static_assert(offsetof(SkyPushConstants, sunDir) == 64, "SkyPushConstants.sunDir offset drifted from GLSL");
static_assert(offsetof(SkyPushConstants, horizonColor) == 80, "SkyPushConstants.horizonColor drifted from GLSL");
static_assert(offsetof(SkyPushConstants, sunColorSize) == 128, "SkyPushConstants.sunColorSize drifted from GLSL");
static_assert(sizeof(SkyPushConstants) == 160, "SkyPushConstants size drifted from GLSL std430 layout (160B)");

struct PickingPushConstants {
    glm::mat4 model;
    glm::mat4 view;
    glm::mat4 proj;
    uint32_t entityIdPlusOne;
    uint32_t _pad0;
    uint32_t _pad1;
    uint32_t _pad2;
};

struct OutlinePushConstants {
    glm::mat4 model;
    glm::mat4 view;
    glm::mat4 proj;
    glm::vec4 color;
    float width;
    float _pad0;
    float _pad1;
    float _pad2;
};

static_assert(sizeof(PushConstants) <= 256, "PushConstants exceeds maxPushConstantsSize (256)");
// Pin the C++ layout to the GLSL push blocks in pbr_vert/frag.glsl and
// pbr_instanced_vert.glsl (std430): field order/types must stay identical.
// A drift here silently shifts every uniform after it (this exact bug hid
// all geometry: vert read proj/view from the wrong offsets). Update GLSL
// together with this struct.
static_assert(offsetof(PushConstants, model) == 0, "PushConstants.model offset drifted from GLSL");
static_assert(offsetof(PushConstants, viewProj) == 64, "PushConstants.viewProj offset drifted from GLSL");
static_assert(offsetof(PushConstants, baseColor) == 128, "PushConstants.baseColor offset drifted from GLSL");
static_assert(offsetof(PushConstants, emissiveFactor) == 144, "PushConstants.emissiveFactor offset drifted from GLSL");
static_assert(offsetof(PushConstants, metallic) == 160, "PushConstants.metallic offset drifted from GLSL");
static_assert(offsetof(PushConstants, flags) == 192, "PushConstants.flags offset drifted from GLSL");
static_assert(sizeof(PushConstants) == 196, "PushConstants size drifted from GLSL std430 layout (196B)");
static_assert(sizeof(PickingPushConstants) <= 256, "PickingPushConstants exceeds limit");
static_assert(sizeof(OutlinePushConstants) <= 256, "OutlinePushConstants exceeds limit");

// TDD §4.2/§5.2 HLOD1 impostor draw: view-aligned quad tinted per cell.
struct ImpostorDraw {
    glm::mat4 model{1.0f};
    glm::vec4 color{1.0f};
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
    VkImageView getGameOffscreenImageView() const { return m_GameOffscreenImageView; }
    VkSampler getGameOffscreenSampler() const { return m_GameOffscreenSampler; }
    MemoryManager* getMemoryManager() { return m_MemoryManager.get(); }

    // Phase 2 of ECS<->Vulkan decoupling: registry of opaque handles, one per
    // GPU mesh allocation backing ::Mesh components (see render_resources.h).
    // Renderer-owned; editor/game allocate on upload, free on destroy.
    RenderResourceManager& getMeshRegistry() { return m_meshRegistry; }
    const RenderResourceManager& getMeshRegistry() const { return m_meshRegistry; }
    void setShadowsEnabled(bool enabled) { m_ShadowsEnabled = enabled; }
    bool isShadowsEnabled() const { return m_ShadowsEnabled; }
    // Debug aid: live mesh-handle count (leak detection for Phase-2 wiring).
    uint32_t getLiveMeshHandleCount() const { return m_meshRegistry.liveMeshCount(); }

    uint32_t findMemoryType(uint32_t typeFilter, VkMemoryPropertyFlags properties);

    using ResizeCallback = std::function<void(int width, int height)>;
    void setResizeCallback(ResizeCallback callback) { m_ResizeCallback = std::move(callback); }

    using RenderCallback = std::function<void(VkCommandBuffer commandBuffer)>;
    void setRenderCallback(RenderCallback callback) { m_RenderCallback = std::move(callback); }
    void setPreferGameCamera(bool prefer) { m_PreferGameCamera = prefer; }

    // When set, the renderer renders the scene directly to the swapchain
    // (instead of an offscreen image) and skips the UI pass. Used by the
    // standalone game executable.
    void setGameMode(bool mode) { m_GameMode = mode; }
    void setScenePreviewCameraOverride(bool enabled, const glm::mat4& view, const glm::mat4& proj, const glm::vec3& position) {
        m_ScenePreviewCameraOverrideEnabled = enabled;
        m_ScenePreviewView = view;
        m_ScenePreviewProj = proj;
        m_ScenePreviewPosition = position;
    }

    void immediateSubmit(const std::function<void(VkCommandBuffer)>& fn);
    uint32_t bindTexture(VkImageView imageView, VkSampler sampler);
    void updateTexture(uint32_t index, VkImageView imageView, VkSampler sampler);

    // Defer destruction until GPU is done with in-flight frames.
    void defer(std::function<void()> fn);

    void setVSyncEnabled(bool enabled);
    bool isVSyncEnabled() const { return m_VSyncEnabled; }

    // Returns UINT32_MAX when nothing is hit.
    uint32_t pickEntityId(uint32_t x, uint32_t y);

    void setSelectedEntityId(uint32_t entityId) {
        m_SelectedEntityId = entityId;
        m_SelectedEntityIds.clear();
        if (entityId != UINT32_MAX) {
            m_SelectedEntityIds.push_back(entityId);
        }
    }

    void setSelectedEntityIds(const std::vector<uint32_t>& entityIds) {
        m_SelectedEntityIds = entityIds;
        m_SelectedEntityId = m_SelectedEntityIds.empty() ? UINT32_MAX : m_SelectedEntityIds[0];
    }

    glm::vec4 getClearColor() const { return m_ClearColor; }
    void setClearColor(const glm::vec4& color) { m_ClearColor = color; }

    void setInstancingEnabled(bool enabled) { m_InstancingEnabled = enabled; }
    bool isInstancingEnabled() const { return m_InstancingEnabled; }
    void setImpostorDraws(std::vector<ImpostorDraw> draws) { m_ImpostorDraws = std::move(draws); }
    size_t getImpostorDrawCount() const { return m_ImpostorDraws.size(); }
    uint32_t getLastDrawCalls() const { return m_LastDrawCalls; }
    uint32_t getLastTriangles() const { return m_LastTriangles; }
    uint32_t getLastInstancedDraws() const { return m_LastInstancedDraws; }
    uint32_t getLastInstancedInstances() const { return m_LastInstancedInstances; }

    // Auto-LOD (§5): simplified GPU variants of dense static meshes.
    // Keyed by source vertex buffer; level 1 = LOD1, 2 = LOD2.
    struct StaticMeshBuffers {
        VkBuffer vertexBuffer = VK_NULL_HANDLE;
        VkDeviceMemory vertexMemory = VK_NULL_HANDLE;
        VkBuffer indexBuffer = VK_NULL_HANDLE;
        VkDeviceMemory indexMemory = VK_NULL_HANDLE;
        uint32_t vertexCount = 0;
        uint32_t indexCount = 0;
    };
    StaticMeshBuffers uploadStaticMesh(const void* verts, size_t vertSize, size_t vertCount,
                                       const uint32_t* indices, size_t indexCount);
    void cacheSimplifiedVariant(VkBuffer srcVB, VkBuffer srcIB, int level, StaticMeshBuffers buffers);
    bool findSimplifiedVariant(VkBuffer srcVB, VkBuffer srcIB, int level, StaticMeshBuffers* out) const;
    void setAutoLODEnabled(bool enabled) { m_AutoLODEnabled = enabled; }
    bool isAutoLODEnabled() const { return m_AutoLODEnabled; }
    uint32_t getSimplifiedVariantCount() const;
    uint32_t getLastSimplifiedDraws() const { return m_LastSimplifiedDraws; }

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
    // TDD §6: GPU instancing (per-instance mat4 @ binding 1, locations 6-9).
    // Opaque pipelines only; transparent (sorted) + picking + outline stay per-entity.
    void createInstancedPipelines();
    void createInstanceBuffers();
    void destroyInstanceBuffers();
    // TDD §4 HLOD1 impostor quad (unit quad, billboarded CPU-side per draw).
    void createImpostorQuad();
    void destroyImpostorQuad();
    void createFramebuffers();
    void createCommandPool();
    void createCommandBuffers();
    void createSyncObjects();
    void createOffscreenResources();
    void createOffscreenRenderPass();
    void createPickingRenderPass();
    void createPickingPipeline();
    void createShadowResources();
    void destroyShadowResources();
    void createShadowPipeline();
    // Gathers scene lights into the LightBuffer UBO (cameraPos always) and
    // computes the shadow matrix when a directional+castShadows light exists.
    void updateLightsAndShadow(Scene* scene);
    void recordShadowPass(VkCommandBuffer commandBuffer, Scene* scene);
    void createOutlinePipeline();
    void createSkyPipeline();
    void createLightBuffer();
    void createDescriptorSet();
    void createBonesDescriptorSetLayout();
    void createBonesResources();
    uint32_t uploadBonePalette(const glm::mat4* matrices, uint32_t boneCount);

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

    // See getMeshRegistry(). Must outlive any ::Mesh referencing its handles;
    // Renderer is destroyed after the scene in both editor and game flows.
    RenderResourceManager m_meshRegistry;
    // Current init() phase; reported in fatal-error context on failure.
    std::string m_InitStep = "begin";

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
    VkPipeline m_GraphicsPipelineFrontCull = VK_NULL_HANDLE;
    VkPipeline m_GraphicsPipelineNoCull = VK_NULL_HANDLE;
    VkPipeline m_GraphicsPipelineBlend = VK_NULL_HANDLE;
    VkPipeline m_GraphicsPipelineBlendFrontCull = VK_NULL_HANDLE;
    VkPipeline m_GraphicsPipelineBlendNoCull = VK_NULL_HANDLE;

    // TDD §6 instanced variants (opaque only, same layout/descriptors).
    VkPipeline m_GraphicsPipelineInstanced = VK_NULL_HANDLE;
    VkPipeline m_GraphicsPipelineInstancedFrontCull = VK_NULL_HANDLE;
    VkPipeline m_GraphicsPipelineInstancedNoCull = VK_NULL_HANDLE;

    // TDD §6 instancing toggle + batch threshold.
    bool m_InstancingEnabled = true;
    static constexpr uint32_t MIN_INSTANCES_PER_BATCH = 2;
    static constexpr uint32_t MAX_INSTANCES_PER_FRAME = 8192;

    // TDD §12 frame metrics (filled during recordCommandBuffer).
    uint32_t m_FrameDrawCalls = 0;
    uint32_t m_FrameTriangles = 0;
    uint32_t m_FrameInstancedDraws = 0;
    uint32_t m_FrameInstancedInstances = 0;
    uint32_t m_LastDrawCalls = 0;
    uint32_t m_LastTriangles = 0;
    uint32_t m_LastInstancedDraws = 0;
    uint32_t m_LastInstancedInstances = 0;

    VkPipelineLayout m_PickingPipelineLayout = VK_NULL_HANDLE;
    VkPipeline m_PickingPipeline = VK_NULL_HANDLE;
    VkPipeline m_PickingPipelineFrontCull = VK_NULL_HANDLE;
    VkPipeline m_PickingPipelineNoCull = VK_NULL_HANDLE;

    VkPipelineLayout m_OutlinePipelineLayout = VK_NULL_HANDLE;
    VkPipeline m_OutlinePipeline = VK_NULL_HANDLE;

    // Procedural sky (fullscreen triangle, drawn first in the main pass).
    // No vertex buffers, no descriptor sets — everything via push constants.
    VkPipelineLayout m_SkyPipelineLayout = VK_NULL_HANDLE;
    VkPipeline m_SkyPipeline = VK_NULL_HANDLE;

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

    VkImage m_GameOffscreenImage = VK_NULL_HANDLE;
    VkDeviceMemory m_GameOffscreenImageMemory = VK_NULL_HANDLE;
    VkImageView m_GameOffscreenImageView = VK_NULL_HANDLE;
    VkSampler m_GameOffscreenSampler = VK_NULL_HANDLE;
    VkFramebuffer m_GameOffscreenFramebuffer = VK_NULL_HANDLE;
    VkImageLayout m_GameOffscreenImageLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    VkImage m_GameOffscreenDepthImage = VK_NULL_HANDLE;
    VkDeviceMemory m_GameOffscreenDepthImageMemory = VK_NULL_HANDLE;
    VkImageView m_GameOffscreenDepthImageView = VK_NULL_HANDLE;

    VkImage m_PickingImage = VK_NULL_HANDLE;
    VkDeviceMemory m_PickingImageMemory = VK_NULL_HANDLE;
    VkImageView m_PickingImageView = VK_NULL_HANDLE;
    VkFramebuffer m_PickingFramebuffer = VK_NULL_HANDLE;
    VkImageLayout m_PickingImageLayout = VK_IMAGE_LAYOUT_UNDEFINED;

    VkImage m_PickingDepthImage = VK_NULL_HANDLE;
    VkDeviceMemory m_PickingDepthImageMemory = VK_NULL_HANDLE;
    VkImageView m_PickingDepthImageView = VK_NULL_HANDLE;

    LightBuffer m_LightBufferData{};
    VkBuffer m_LightBuffer = VK_NULL_HANDLE;
    VkDeviceMemory m_LightBufferMemory = VK_NULL_HANDLE;
    VkDescriptorPool m_DescriptorPool = VK_NULL_HANDLE;
    VkDescriptorSet m_DescriptorSet = VK_NULL_HANDLE;
    VkDescriptorSetLayout m_DescriptorSetLayout = VK_NULL_HANDLE;

    // Directional shadow mapping (single fixed shadow map, v1 — see ARCHITECTURE.md).
    static constexpr uint32_t kShadowMapSize = 2048;
    static constexpr float kShadowOrthoExtent = 80.0f;
    static constexpr float kShadowDepthBias = 0.0015f;
    // Slope-scaled bias (NDC units, added as (1-NdotL)*scale). Kept small:
    // acne is handled mainly by the receiver normal offset in sampleShadow,
    // a large depth bias detaches shadows at low sun (peter-panning).
    static constexpr float kShadowSlopeScale = 0.002f;
    VkImage m_ShadowImage = VK_NULL_HANDLE;
    VkDeviceMemory m_ShadowMemory = VK_NULL_HANDLE;
    VkImageView m_ShadowView = VK_NULL_HANDLE;
    VkSampler m_ShadowSampler = VK_NULL_HANDLE;
    VkRenderPass m_ShadowRenderPass = VK_NULL_HANDLE;
    VkFramebuffer m_ShadowFramebuffer = VK_NULL_HANDLE;
    VkPipelineLayout m_ShadowPipelineLayout = VK_NULL_HANDLE;
    VkPipeline m_ShadowPipeline = VK_NULL_HANDLE;
    bool m_ShadowsEnabled = true; // global shadow-map switch (per-light/caster flags gate the rest)
    bool m_ShadowEnabledFrame = false;
    // Tracks the shadow image layout across frames: the render pass moves it
    // to SHADER_READ_ONLY when it runs; otherwise recordShadowPass issues a
    // one-time barrier so main-pass binding 2 is always sampling-valid.
    VkImageLayout m_ShadowImageLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    glm::mat4 m_ShadowViewProj{1.0f};

    // Bones palette (dynamic SSBO) used by vertex shaders (set=1,binding=0).
    static constexpr uint32_t MAX_BONES = 256;
    static constexpr uint32_t MAX_SKINNED_INSTANCES = 256;

    VkDescriptorPool m_BonesDescriptorPool = VK_NULL_HANDLE;
    VkDescriptorSetLayout m_BonesDescriptorSetLayout = VK_NULL_HANDLE;
    std::array<VkDescriptorSet, MAX_FRAMES_IN_FLIGHT> m_BonesDescriptorSets{};

    std::array<VkBuffer, MAX_FRAMES_IN_FLIGHT> m_BonePaletteBuffers{};
    std::array<VkDeviceMemory, MAX_FRAMES_IN_FLIGHT> m_BonePaletteMemories{};
    std::array<void*, MAX_FRAMES_IN_FLIGHT> m_BonePaletteMapped{};
    VkDeviceSize m_BonePaletteStrideBytes = 0;
    uint32_t m_BonePaletteNextSlot = 0;

    // Per-flight instance-transform staging (host-visible). One slot per frame
    // in flight; written during record, consumed by the GPU draw.
    std::array<VkBuffer, MAX_FRAMES_IN_FLIGHT> m_InstanceBuffers{};
    std::array<VkDeviceMemory, MAX_FRAMES_IN_FLIGHT> m_InstanceMemories{};
    std::array<void*, MAX_FRAMES_IN_FLIGHT> m_InstanceMapped{};

    // TDD §4.2/§5.2 HLOD1 impostors: view-aligned quads tinted per cell.
    // Auto-LOD variant cache: (sourceVB, sourceIB) -> [LOD1, LOD2] GPU meshes.
    struct SimplifiedKey {
        VkBuffer vertexBuffer = VK_NULL_HANDLE;
        VkBuffer indexBuffer = VK_NULL_HANDLE;
        bool operator==(const SimplifiedKey& o) const {
            return vertexBuffer == o.vertexBuffer && indexBuffer == o.indexBuffer;
        }
    };
    struct SimplifiedKeyHash {
        size_t operator()(const SimplifiedKey& k) const noexcept {
            const size_t a = static_cast<size_t>(reinterpret_cast<uintptr_t>(k.vertexBuffer));
            const size_t b = static_cast<size_t>(reinterpret_cast<uintptr_t>(k.indexBuffer));
            return a * 1315423911u + b * 1566083941u;
        }
    };
    std::unordered_map<SimplifiedKey, std::array<StaticMeshBuffers, 2>, SimplifiedKeyHash> m_SimplifiedVariants;
    bool m_AutoLODEnabled = true;
    uint32_t m_FrameSimplifiedDraws = 0;
    uint32_t m_LastSimplifiedDraws = 0;
    std::vector<ImpostorDraw> m_ImpostorDraws;
    VkBuffer m_ImpostorQuadVB = VK_NULL_HANDLE;
    VkDeviceMemory m_ImpostorQuadVBMem = VK_NULL_HANDLE;
    VkBuffer m_ImpostorQuadIB = VK_NULL_HANDLE;
    VkDeviceMemory m_ImpostorQuadIBMem = VK_NULL_HANDLE;

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

    uint32_t m_SelectedEntityId = UINT32_MAX;
    std::vector<uint32_t> m_SelectedEntityIds;

    void createTextureDescriptorSetLayout();
    void createPlaceholderTexture();
    uint32_t createTextureFromFile(const std::string& path);

#ifdef TRACY_ENABLE
    TracyVkCtx m_TracyVkCtx = nullptr;
#endif

    bool m_FramebufferResized = false;
    ResizeCallback m_ResizeCallback;
    RenderCallback m_RenderCallback;
    bool m_PreferGameCamera = false;
    bool m_GameMode = false;
    bool m_ScenePreviewCameraOverrideEnabled = false;
    glm::mat4 m_ScenePreviewView = glm::mat4(1.0f);
    glm::mat4 m_ScenePreviewProj = glm::mat4(1.0f);
    glm::vec3 m_ScenePreviewPosition{0.0f};

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

    std::array<DeletionQueue, MAX_FRAMES_IN_FLIGHT> m_DeletionQueues{};

    static const std::vector<const char*> validationLayers;
    static const std::vector<const char*> deviceExtensions;
};

}

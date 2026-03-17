#pragma once

#include <vector>
#include <vulkan/vulkan.h>
#include "../ecs/ecs.h"
#include "../ecs/vertex.h"
#include "../utils/model_loader.h"
#include <glm/glm.hpp>

class RenderableInterface {
public:
    virtual void render(VkCommandBuffer commandBuffer) = 0;
};

class Scene {
public:
    Scene(entt::registry* world);
    ~Scene();

    void init(VkDevice device, VkPhysicalDevice physicalDevice);
    void loadModel(const std::string& path, VkDevice device, VkPhysicalDevice physicalDevice, uint32_t (*findMemoryType)(uint32_t, VkMemoryPropertyFlags, VkPhysicalDeviceMemoryProperties*));
    void addRenderable(RenderableInterface* renderable);
    void render(VkCommandBuffer commandBuffer, VkPipelineLayout pipelineLayout, VkDevice device);

    entt::registry* getWorld() { return ecsWorld; }

    VkBuffer getVertexBuffer() const { return vertexBuffer; }
    VkBuffer getIndexBuffer() const { return indexBuffer; }
    uint32_t getIndexCount() const { return static_cast<uint32_t>(indices.size()); }

private:
    std::vector<RenderableInterface*> renderables;
    entt::registry* ecsWorld;

    std::vector<Vertex> vertices;
    std::vector<uint32_t> indices;
    VkBuffer vertexBuffer = VK_NULL_HANDLE;
    VkDeviceMemory vertexMemory = VK_NULL_HANDLE;
    VkBuffer indexBuffer = VK_NULL_HANDLE;
    VkDeviceMemory indexMemory = VK_NULL_HANDLE;

    MeshData loadedModel;

    void createGeometry(VkDevice device, VkPhysicalDevice physicalDevice, uint32_t (*findMemoryType)(uint32_t, VkMemoryPropertyFlags, VkPhysicalDeviceMemoryProperties*));
};

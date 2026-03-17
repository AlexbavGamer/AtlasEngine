#include "scene.h"
#include "../ecs/components.h"
#include "../utils/model_loader.h"

Scene::Scene(entt::registry* world) : ecsWorld(world) {
    vertices = {
        {{-0.5f, -0.5f, 0.0f}, {1.0f, 0.0f, 0.0f}, {0.0f, 0.0f}, {0.0f, 0.0f, 1.0f}},
        {{ 0.5f, -0.5f, 0.0f}, {0.0f, 1.0f, 0.0f}, {1.0f, 0.0f}, {0.0f, 0.0f, 1.0f}},
        {{ 0.5f,  0.5f, 0.0f}, {0.0f, 0.0f, 1.0f}, {1.0f, 1.0f}, {0.0f, 0.0f, 1.0f}},
        {{-0.5f,  0.5f, 0.0f}, {1.0f, 1.0f, 1.0f}, {0.0f, 1.0f}, {0.0f, 0.0f, 1.0f}}
    };
    indices = {0, 1, 2, 2, 3, 0};
}

Scene::~Scene() {}

void Scene::addRenderable(RenderableInterface* renderable) {
    renderables.push_back(renderable);
}

void Scene::loadModel(const std::string& path, VkDevice device, VkPhysicalDevice physicalDevice, uint32_t (*findMemoryType)(uint32_t, VkMemoryPropertyFlags, VkPhysicalDeviceMemoryProperties*)) {
    loadedModel = ModelLoader::loadModel(path, device, physicalDevice, findMemoryType);
}

static uint32_t findMemoryTypeImpl(uint32_t typeFilter, VkMemoryPropertyFlags properties, VkPhysicalDeviceMemoryProperties* memProperties) {
    for (uint32_t i = 0; i < memProperties->memoryTypeCount; i++) {
        if ((typeFilter & (1 << i)) && (memProperties->memoryTypes[i].propertyFlags & properties) == properties) {
            return i;
        }
    }
    return uint32_t(~0);
}

void Scene::init(VkDevice device, VkPhysicalDevice physicalDevice) {
    createGeometry(device, physicalDevice, findMemoryTypeImpl);
    
    loadedModel.vertexBuffer = VK_NULL_HANDLE;
    loadedModel.indexBuffer = VK_NULL_HANDLE;
}

void Scene::createGeometry(VkDevice device, VkPhysicalDevice physicalDevice, uint32_t (*findMemoryType)(uint32_t, VkMemoryPropertyFlags, VkPhysicalDeviceMemoryProperties*)) {
    VkPhysicalDeviceMemoryProperties memProperties;
    vkGetPhysicalDeviceMemoryProperties(physicalDevice, &memProperties);

    VkBufferCreateInfo bufferInfo{};
    bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    bufferInfo.size = sizeof(vertices[0]) * vertices.size();
    bufferInfo.usage = VK_BUFFER_USAGE_VERTEX_BUFFER_BIT;
    bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    if (vkCreateBuffer(device, &bufferInfo, nullptr, &vertexBuffer) != VK_SUCCESS) {
        throw std::runtime_error("failed to create vertex buffer!");
    }

    VkMemoryRequirements memRequirements;
    vkGetBufferMemoryRequirements(device, vertexBuffer, &memRequirements);

    VkMemoryAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    allocInfo.allocationSize = memRequirements.size;
    allocInfo.memoryTypeIndex = findMemoryType(memRequirements.memoryTypeBits, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, &memProperties);

    if (vkAllocateMemory(device, &allocInfo, nullptr, &vertexMemory) != VK_SUCCESS) {
        throw std::runtime_error("failed to allocate vertex buffer memory!");
    }

    vkBindBufferMemory(device, vertexBuffer, vertexMemory, 0);

    void* data;
    vkMapMemory(device, vertexMemory, 0, bufferInfo.size, 0, &data);
    memcpy(data, vertices.data(), bufferInfo.size);
    vkUnmapMemory(device, vertexMemory);

    bufferInfo.usage = VK_BUFFER_USAGE_INDEX_BUFFER_BIT;
    bufferInfo.size = sizeof(indices[0]) * indices.size();

    if (vkCreateBuffer(device, &bufferInfo, nullptr, &indexBuffer) != VK_SUCCESS) {
        throw std::runtime_error("failed to create index buffer!");
    }

    vkGetBufferMemoryRequirements(device, indexBuffer, &memRequirements);
    allocInfo.allocationSize = memRequirements.size;
    allocInfo.memoryTypeIndex = findMemoryType(memRequirements.memoryTypeBits, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, &memProperties);

    if (vkAllocateMemory(device, &allocInfo, nullptr, &indexMemory) != VK_SUCCESS) {
        throw std::runtime_error("failed to allocate index buffer memory!");
    }

    vkBindBufferMemory(device, indexBuffer, indexMemory, 0);

    vkMapMemory(device, indexMemory, 0, bufferInfo.size, 0, &data);
    memcpy(data, indices.data(), bufferInfo.size);
    vkUnmapMemory(device, indexMemory);
}

void Scene::render(VkCommandBuffer commandBuffer, VkPipelineLayout pipelineLayout, VkDevice device) {
    if (!ecsWorld || vertexBuffer == VK_NULL_HANDLE) return;

    VkBuffer currentVertexBuffer = vertexBuffer;
    VkBuffer currentIndexBuffer = indexBuffer;
    uint32_t currentIndexCount = static_cast<uint32_t>(indices.size());

    if (loadedModel.vertexBuffer != VK_NULL_HANDLE) {
        currentVertexBuffer = loadedModel.vertexBuffer;
        currentIndexBuffer = loadedModel.indexBuffer;
        currentIndexCount = loadedModel.indexCount;
    }

    VkBuffer vertexBuffers[] = {currentVertexBuffer};
    VkDeviceSize offsets[] = {0};
    vkCmdBindVertexBuffers(commandBuffer, 0, 1, vertexBuffers, offsets);
    vkCmdBindIndexBuffer(commandBuffer, currentIndexBuffer, 0, VK_INDEX_TYPE_UINT32);

    glm::mat4 viewMatrix = glm::mat4(1.0f);
    glm::mat4 projMatrix = glm::mat4(1.0f);

    auto cameraView = ecsWorld->view<Camera>();
    if (!cameraView.empty()) {
        Entity cameraEntity = cameraView.front();
        auto& camera = ecsWorld->get<Camera>(cameraEntity);
        viewMatrix = camera.getViewMatrix();
        projMatrix = camera.getProjectionMatrix();
    }

    auto view = ecsWorld->view<Transform, Renderable>();
    for (auto entity : view) {
        auto& transform = view.get<Transform>(entity);
        auto& renderable = view.get<Renderable>(entity);

        if (!renderable.visible) continue;

        glm::mat4 modelMatrix = transform.getModelMatrix();
        glm::mat4 mvp = projMatrix * viewMatrix * modelMatrix;
        
        vkCmdPushConstants(commandBuffer, pipelineLayout, VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(glm::mat4), &mvp);

        vkCmdDrawIndexed(commandBuffer, currentIndexCount, 1, 0, 0, 0);
    }
}

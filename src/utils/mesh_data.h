#pragma once

// MeshData + ModelData — extracted from utils/model_loader.h (Fase 2).
// CPU-side mesh containers + GPU-handle ownership. Needs Vulkan types
// and Vertex/animation declarations, but NOT assimp/stb — prefer this
// header whenever you only need the structs (primitives, colliders,
// editor headers). ModelLoader:: methods stay in model_loader.h.

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include <glm/glm.hpp>
#include <vulkan/vulkan.h>

#include "../renderer/vertex.h"
#include "../animation/animation.h"

struct MeshData {
    std::vector<Vertex> vertices;
    std::vector<uint32_t> indices;

    VkBuffer vertexBuffer = VK_NULL_HANDLE;
    VkDeviceMemory vertexMemory = VK_NULL_HANDLE;
    VkBuffer indexBuffer = VK_NULL_HANDLE;
    VkDeviceMemory indexMemory = VK_NULL_HANDLE;

    uint32_t vertexCount = 0;
    uint32_t indexCount = 0;
    uint32_t vertexOffset = 0;
    VkDevice ownerDevice = VK_NULL_HANDLE;
    std::string name;

    glm::vec4 baseColor = glm::vec4(1.0f);
    glm::vec3 emissiveFactor = glm::vec3(0.0f);
    float metallic = 0.0f;
    float roughness = 0.5f;

    // Since we import with aiProcess_PreTransformVertices, vertex positions are baked into a common space.
    // Keep a per-mesh pivot so editor gizmos start at the mesh location.
    glm::vec3 pivotPosition = glm::vec3(0.0f);

    // For skinned assets (no PreTransform): original mesh node global transform in model space.
    glm::mat4 meshNodeGlobal = glm::mat4(1.0f);
    bool hasMeshNodeGlobal = false;

    std::string baseColorTexturePath;
    std::string normalTexturePath;
    std::string metallicRoughnessTexturePath;
    std::string aoTexturePath;
    std::string emissiveTexturePath;

    int alphaMode = 0; // 0=Opaque, 1=Mask, 2=Blend
    float alphaCutoff = 0.5f;
    bool doubleSided = false;

    MeshData() = default;

    MeshData(const MeshData&) = delete;
    MeshData& operator=(const MeshData&) = delete;

    MeshData(MeshData&& other) noexcept
        : vertices(std::move(other.vertices)),
          indices(std::move(other.indices)),
          vertexBuffer(other.vertexBuffer),
          vertexMemory(other.vertexMemory),
          indexBuffer(other.indexBuffer),
          indexMemory(other.indexMemory),
          vertexCount(other.vertexCount),
          indexCount(other.indexCount),
          vertexOffset(other.vertexOffset),
          ownerDevice(other.ownerDevice),
          name(std::move(other.name)),
          baseColor(other.baseColor),
          emissiveFactor(other.emissiveFactor),
          pivotPosition(other.pivotPosition),
          metallic(other.metallic),
          roughness(other.roughness),
          baseColorTexturePath(std::move(other.baseColorTexturePath)),
          normalTexturePath(std::move(other.normalTexturePath)),
          metallicRoughnessTexturePath(std::move(other.metallicRoughnessTexturePath)),
          aoTexturePath(std::move(other.aoTexturePath)),
          emissiveTexturePath(std::move(other.emissiveTexturePath)),
          alphaMode(other.alphaMode),
          alphaCutoff(other.alphaCutoff),
          doubleSided(other.doubleSided)
    {
        other.vertexBuffer = VK_NULL_HANDLE;
        other.vertexMemory = VK_NULL_HANDLE;
        other.indexBuffer = VK_NULL_HANDLE;
        other.indexMemory = VK_NULL_HANDLE;
        other.vertexCount = 0;
        other.indexCount = 0;
        other.vertexOffset = 0;
        other.ownerDevice = VK_NULL_HANDLE;
        other.baseColor = glm::vec4(1.0f);
        other.emissiveFactor = glm::vec3(0.0f);
        other.pivotPosition = glm::vec3(0.0f);
        other.metallic = 0.0f;
        other.roughness = 0.5f;
        other.baseColorTexturePath.clear();
        other.normalTexturePath.clear();
        other.metallicRoughnessTexturePath.clear();
        other.aoTexturePath.clear();
        other.emissiveTexturePath.clear();
        other.alphaMode = 0;
        other.alphaCutoff = 0.5f;
        other.doubleSided = false;
    }

    MeshData& operator=(MeshData&& other) noexcept {
        if (this != &other) {
            clearVulkanResources();
            vertices = std::move(other.vertices);
            indices = std::move(other.indices);

            vertexBuffer = other.vertexBuffer;
            vertexMemory = other.vertexMemory;
            indexBuffer = other.indexBuffer;
            indexMemory = other.indexMemory;

            vertexCount = other.vertexCount;
            indexCount = other.indexCount;
            vertexOffset = other.vertexOffset;
            ownerDevice = other.ownerDevice;
            name = std::move(other.name);
            baseColor = other.baseColor;
            emissiveFactor = other.emissiveFactor;
            pivotPosition = other.pivotPosition;
            metallic = other.metallic;
            roughness = other.roughness;
            baseColorTexturePath = std::move(other.baseColorTexturePath);
            normalTexturePath = std::move(other.normalTexturePath);
            metallicRoughnessTexturePath = std::move(other.metallicRoughnessTexturePath);
            aoTexturePath = std::move(other.aoTexturePath);
            emissiveTexturePath = std::move(other.emissiveTexturePath);
            alphaMode = other.alphaMode;
            alphaCutoff = other.alphaCutoff;
            doubleSided = other.doubleSided;

            other.vertexBuffer = VK_NULL_HANDLE;
            other.vertexMemory = VK_NULL_HANDLE;
            other.indexBuffer = VK_NULL_HANDLE;
            other.indexMemory = VK_NULL_HANDLE;
            other.vertexCount = 0;
            other.indexCount = 0;
            other.vertexOffset = 0;
            other.ownerDevice = VK_NULL_HANDLE;
            other.baseColor = glm::vec4(1.0f);
            other.emissiveFactor = glm::vec3(0.0f);
            other.pivotPosition = glm::vec3(0.0f);
            other.metallic = 0.0f;
            other.roughness = 0.5f;
            other.baseColorTexturePath.clear();
            other.normalTexturePath.clear();
            other.metallicRoughnessTexturePath.clear();
            other.aoTexturePath.clear();
            other.emissiveTexturePath.clear();
            other.alphaMode = 0;
            other.alphaCutoff = 0.5f;
            other.doubleSided = false;
        }
        return *this;
    }

    ~MeshData() {
        clearVulkanResources();
    }

    void clearVulkanResources(VkDevice device = VK_NULL_HANDLE) {
        if (device == VK_NULL_HANDLE) {
            device = ownerDevice;
        }
        if (device != VK_NULL_HANDLE) {
            if (vertexBuffer != VK_NULL_HANDLE) {
                vkDestroyBuffer(device, vertexBuffer, nullptr);
                vertexBuffer = VK_NULL_HANDLE;
            }
            if (vertexMemory != VK_NULL_HANDLE) {
                vkFreeMemory(device, vertexMemory, nullptr);
                vertexMemory = VK_NULL_HANDLE;
            }
            if (indexBuffer != VK_NULL_HANDLE) {
                vkDestroyBuffer(device, indexBuffer, nullptr);
                indexBuffer = VK_NULL_HANDLE;
            }
            if (indexMemory != VK_NULL_HANDLE) {
                vkFreeMemory(device, indexMemory, nullptr);
                indexMemory = VK_NULL_HANDLE;
            }
        }
        vertexCount = 0;
        indexCount = 0;
        ownerDevice = VK_NULL_HANDLE;
    }
    void freeCPUMemory() {
        std::vector<Vertex>().swap(vertices);
        std::vector<uint32_t>().swap(indices);
    }
};

struct ModelData {
    std::vector<MeshData> meshes;
    std::string rootName;
    uint32_t totalVertices = 0;
    uint32_t totalIndices = 0;
    VkDevice device = VK_NULL_HANDLE;

    // Skeletal animation (optional)
    std::shared_ptr<Atlas::Anim::Skeleton> skeleton;
    std::vector<Atlas::Anim::AnimationClip> clips;

    ModelData() = default;
    explicit ModelData(VkDevice dev) : device(dev) {}
    ModelData(const ModelData&) = delete;
    ModelData& operator=(const ModelData&) = delete;

    ModelData(ModelData&& other) noexcept 
        : meshes(std::move(other.meshes)),
          rootName(std::move(other.rootName)),
          totalVertices(other.totalVertices),
          totalIndices(other.totalIndices),
          device(other.device),
          skeleton(std::move(other.skeleton)),
          clips(std::move(other.clips)) {
        other.device = VK_NULL_HANDLE;
        other.rootName.clear();
        other.totalVertices = 0;
        other.totalIndices = 0;
    }
    
    ModelData& operator=(ModelData&& other) noexcept {
        if (this != &other) {
            device = other.device;
            totalVertices = other.totalVertices;
            totalIndices = other.totalIndices;
            meshes = std::move(other.meshes);
            rootName = std::move(other.rootName);
            skeleton = std::move(other.skeleton);
            clips = std::move(other.clips);

            other.rootName.clear();
            other.skeleton.reset();
            other.clips.clear();
            other.device = VK_NULL_HANDLE;
            other.totalVertices = 0;
            other.totalIndices = 0;
        }
        return *this;
    }

    ~ModelData() {
        clear();
    }

    void clear() {
        if (device != VK_NULL_HANDLE) {
            for (auto& mesh : meshes) {
                mesh.clearVulkanResources(device);
            }
        }
        meshes.clear();
        rootName.clear();
        skeleton.reset();
        clips.clear();
        totalVertices = 0;
        totalIndices = 0;
        device = VK_NULL_HANDLE;
    }

    void clear(VkDevice dev) {
        device = dev;
        clear();
    }

    void freeAllCPUMemory() {
        for (auto& mesh : meshes) {
            mesh.freeCPUMemory();
        }
    }
};

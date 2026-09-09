#pragma once

#include <string>
#include <vector>
#include <array>
#include <iostream>
#include <chrono>
#include <functional>
#include <unordered_map>
#include <cstdint>
#include <memory>
#include <filesystem>
#include <fstream>
#include <cstdlib>
#include <vulkan/vulkan.h>
#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>
#include <glm/gtc/constants.hpp>
#include <assimp/mesh.h>
#include <assimp/scene.h>
#include <assimp/Importer.hpp>
#include <assimp/postprocess.h>
#include <assimp/material.h>
#include <assimp/GltfMaterial.h>

#include <stb_image.h>

#include "../renderer/vertex.h"
#include "../animation/animation.h"

#include "mesh_data.h"


class ModelLoader {
public:
    static MeshData createCube(float size = 1.0f, VkDevice device = VK_NULL_HANDLE, VkPhysicalDevice physicalDevice = VK_NULL_HANDLE, uint32_t (*findMemoryType)(uint32_t, VkMemoryPropertyFlags, VkPhysicalDeviceMemoryProperties*) = nullptr);

    static MeshData loadModel(const std::string& path, VkDevice device, VkPhysicalDevice physicalDevice, uint32_t (*findMemoryType)(uint32_t, VkMemoryPropertyFlags, VkPhysicalDeviceMemoryProperties*));

    static void loadModelMultiMesh(
        const std::string& path, 
        VkDevice device, 
        VkPhysicalDevice physicalDevice, 
        uint32_t (*findMemoryType)(uint32_t, VkMemoryPropertyFlags, VkPhysicalDeviceMemoryProperties*), 
        ModelData* outData, 
        bool mergeAll = false,
        bool loadTextures = true);

    static void createBuffers(MeshData& meshData, VkDevice device, VkPhysicalDevice physicalDevice, uint32_t (*findMemoryType)(uint32_t, VkMemoryPropertyFlags, VkPhysicalDeviceMemoryProperties*));

    static void cleanupModel(ModelData& modelData, VkDevice device);

    static void cleanup(MeshData& meshData, VkDevice device);
};

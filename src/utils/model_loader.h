#pragma once

#include <string>
#include <vector>
#include <vulkan/vulkan.h>
#include <glm/glm.hpp>
#include <assimp/mesh.h>
#include <assimp/scene.h>
#include <assimp/Importer.hpp>
#include <assimp/postprocess.h>
#include "../ecs/vertex.h"

struct MeshData {
    std::vector<Vertex> vertices;
    std::vector<uint32_t> indices;
    VkBuffer vertexBuffer = VK_NULL_HANDLE;
    VkDeviceMemory vertexMemory = VK_NULL_HANDLE;
    VkBuffer indexBuffer = VK_NULL_HANDLE;
    VkDeviceMemory indexMemory = VK_NULL_HANDLE;
    uint32_t indexCount = 0;
};

class ModelLoader {
public:
    static MeshData loadModel(const std::string& path, VkDevice device, VkPhysicalDevice physicalDevice, uint32_t (*findMemoryType)(uint32_t, VkMemoryPropertyFlags, VkPhysicalDeviceMemoryProperties*)) {
        Assimp::Importer importer;
        
        const aiScene* scene = importer.ReadFile(path, aiProcess_Triangulate | aiProcess_FlipUVs | aiProcess_CalcTangentSpace | aiProcess_JoinIdenticalVertices | aiProcess_FlipWindingOrder);
        
        if (!scene || scene->mFlags & AI_SCENE_FLAGS_INCOMPLETE || !scene->mRootNode) {
            throw std::runtime_error("Failed to load model: " + std::string(importer.GetErrorString()));
        }

        MeshData meshData;

        uint32_t vertexOffset = 0;

        for (unsigned int i = 0; i < scene->mNumMeshes; i++) {
            aiMesh* aiMesh = scene->mMeshes[i];
            
            for (unsigned int j = 0; j < aiMesh->mNumVertices; j++) {
                Vertex vertex;
                
                vertex.pos.x = aiMesh->mVertices[j].x;
                vertex.pos.y = aiMesh->mVertices[j].y;
                vertex.pos.z = aiMesh->mVertices[j].z;

                vertex.color.r = 1.0f;
                vertex.color.g = 1.0f;
                vertex.color.b = 1.0f;

                if (aiMesh->mTextureCoords[0]) {
                    vertex.texCoord.x = aiMesh->mTextureCoords[0][j].x;
                    vertex.texCoord.y = aiMesh->mTextureCoords[0][j].y;
                } else {
                    vertex.texCoord = {0.0f, 0.0f};
                }

                if (aiMesh->mNormals) {
                    vertex.normal.x = aiMesh->mNormals[j].x;
                    vertex.normal.y = aiMesh->mNormals[j].y;
                    vertex.normal.z = aiMesh->mNormals[j].z;
                } else {
                    vertex.normal = {0.0f, 1.0f, 0.0f};
                }

                meshData.vertices.push_back(vertex);
            }

            for (unsigned int j = 0; j < aiMesh->mNumFaces; j++) {
                aiFace face = aiMesh->mFaces[j];
                for (unsigned int k = 0; k < face.mNumIndices; k++) {
                    meshData.indices.push_back(face.mIndices[k] + vertexOffset);
                }
            }

            vertexOffset += aiMesh->mNumVertices;
        }

        meshData.indexCount = static_cast<uint32_t>(meshData.indices.size());

        createBuffers(meshData, device, physicalDevice, findMemoryType);

        return meshData;
    }

    static void createBuffers(MeshData& meshData, VkDevice device, VkPhysicalDevice physicalDevice, uint32_t (*findMemoryType)(uint32_t, VkMemoryPropertyFlags, VkPhysicalDeviceMemoryProperties*)) {
        VkPhysicalDeviceMemoryProperties memProperties;
        vkGetPhysicalDeviceMemoryProperties(physicalDevice, &memProperties);

        VkBufferCreateInfo bufferInfo{};
        bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
        bufferInfo.size = sizeof(meshData.vertices[0]) * meshData.vertices.size();
        bufferInfo.usage = VK_BUFFER_USAGE_VERTEX_BUFFER_BIT;
        bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

        if (vkCreateBuffer(device, &bufferInfo, nullptr, &meshData.vertexBuffer) != VK_SUCCESS) {
            throw std::runtime_error("failed to create vertex buffer!");
        }

        VkMemoryRequirements memRequirements;
        vkGetBufferMemoryRequirements(device, meshData.vertexBuffer, &memRequirements);

        VkMemoryAllocateInfo allocInfo{};
        allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
        allocInfo.allocationSize = memRequirements.size;
        allocInfo.memoryTypeIndex = findMemoryType(memRequirements.memoryTypeBits, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, &memProperties);

        if (vkAllocateMemory(device, &allocInfo, nullptr, &meshData.vertexMemory) != VK_SUCCESS) {
            throw std::runtime_error("failed to allocate vertex buffer memory!");
        }

        vkBindBufferMemory(device, meshData.vertexBuffer, meshData.vertexMemory, 0);

        void* data;
        vkMapMemory(device, meshData.vertexMemory, 0, bufferInfo.size, 0, &data);
        memcpy(data, meshData.vertices.data(), bufferInfo.size);
        vkUnmapMemory(device, meshData.vertexMemory);

        bufferInfo.usage = VK_BUFFER_USAGE_INDEX_BUFFER_BIT;
        bufferInfo.size = sizeof(meshData.indices[0]) * meshData.indices.size();

        if (vkCreateBuffer(device, &bufferInfo, nullptr, &meshData.indexBuffer) != VK_SUCCESS) {
            throw std::runtime_error("failed to create index buffer!");
        }

        vkGetBufferMemoryRequirements(device, meshData.indexBuffer, &memRequirements);
        allocInfo.allocationSize = memRequirements.size;
        allocInfo.memoryTypeIndex = findMemoryType(memRequirements.memoryTypeBits, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, &memProperties);

        if (vkAllocateMemory(device, &allocInfo, nullptr, &meshData.indexMemory) != VK_SUCCESS) {
            throw std::runtime_error("failed to allocate index buffer memory!");
        }

        vkBindBufferMemory(device, meshData.indexBuffer, meshData.indexMemory, 0);

        vkMapMemory(device, meshData.indexMemory, 0, bufferInfo.size, 0, &data);
        memcpy(data, meshData.indices.data(), bufferInfo.size);
        vkUnmapMemory(device, meshData.indexMemory);
    }

    static void cleanup(MeshData& meshData, VkDevice device) {
        if (meshData.vertexBuffer != VK_NULL_HANDLE) {
            vkDestroyBuffer(device, meshData.vertexBuffer, nullptr);
            vkFreeMemory(device, meshData.vertexMemory, nullptr);
        }
        if (meshData.indexBuffer != VK_NULL_HANDLE) {
            vkDestroyBuffer(device, meshData.indexBuffer, nullptr);
            vkFreeMemory(device, meshData.indexMemory, nullptr);
        }
    }
};

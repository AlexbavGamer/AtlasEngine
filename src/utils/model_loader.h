#pragma once

#include <string>
#include <vector>
#include <iostream>
#include <chrono>
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
    uint32_t vertexOffset = 0;
    std::string name;
};

struct ModelData {
    std::vector<MeshData> meshes;
    uint32_t totalVertices = 0;
    uint32_t totalIndices = 0;
    
    ModelData() = default;
    ~ModelData() = default;
    ModelData(ModelData&&) = default;
    ModelData& operator=(ModelData&&) = default;
};

class ModelLoader {
public:
    static MeshData createCube(float size = 1.0f, VkDevice device = VK_NULL_HANDLE, VkPhysicalDevice physicalDevice = VK_NULL_HANDLE, uint32_t (*findMemoryType)(uint32_t, VkMemoryPropertyFlags, VkPhysicalDeviceMemoryProperties*) = nullptr) {
        MeshData meshData;
        float h = size / 2.0f;
        
        meshData.vertices = {
            // Front face (+Z) - CCW from outside
            Vertex{{-h, -h,  h}, {1,1,1}, {0,0}, {0,0,1}},
            Vertex{{ h, -h,  h}, {1,1,1}, {1,0}, {0,0,1}},
            Vertex{{ h,  h,  h}, {1,1,1}, {1,1}, {0,0,1}},
            Vertex{{-h,  h,  h}, {1,1,1}, {0,1}, {0,0,1}},
            
            // Back face (-Z) - CCW from outside
            Vertex{{ h, -h, -h}, {1,0,0}, {0,0}, {0,0,-1}},
            Vertex{{-h, -h, -h}, {1,0,0}, {1,0}, {0,0,-1}},
            Vertex{{-h,  h, -h}, {1,0,0}, {1,1}, {0,0,-1}},
            Vertex{{ h,  h, -h}, {1,0,0}, {0,1}, {0,0,-1}},
            
            // Right face (+X) - CCW from outside
            Vertex{{ h, -h,  h}, {0,1,0}, {0,0}, {1,0,0}},
            Vertex{{ h, -h, -h}, {0,1,0}, {1,0}, {1,0,0}},
            Vertex{{ h,  h, -h}, {0,1,0}, {1,1}, {1,0,0}},
            Vertex{{ h,  h,  h}, {0,1,0}, {0,1}, {1,0,0}},
            
            // Left face (-X) - CCW from outside
            Vertex{{-h, -h, -h}, {0,0,1}, {0,0}, {-1,0,0}},
            Vertex{{-h, -h,  h}, {0,0,1}, {1,0}, {-1,0,0}},
            Vertex{{-h,  h,  h}, {0,0,1}, {1,1}, {-1,0,0}},
            Vertex{{-h,  h, -h}, {0,0,1}, {0,1}, {-1,0,0}},
            
            // Top face (+Y) - CCW from outside
            Vertex{{-h,  h,  h}, {1,1,0}, {0,0}, {0,1,0}},
            Vertex{{ h,  h,  h}, {1,1,0}, {1,0}, {0,1,0}},
            Vertex{{ h,  h, -h}, {1,1,0}, {1,1}, {0,1,0}},
            Vertex{{-h,  h, -h}, {1,1,0}, {0,1}, {0,1,0}},
            
            // Bottom face (-Y) - CCW from outside
            Vertex{{-h, -h, -h}, {0,1,1}, {0,0}, {0,-1,0}},
            Vertex{{ h, -h, -h}, {0,1,1}, {1,0}, {0,-1,0}},
            Vertex{{ h, -h,  h}, {0,1,1}, {1,1}, {0,-1,0}},
            Vertex{{-h, -h,  h}, {0,1,1}, {0,1}, {0,-1,0}},
        };
        
        meshData.indices = {
            // Front
            0, 1, 2, 2, 3, 0,
            // Back
            4, 5, 6, 6, 7, 4,
            // Right
            8, 9, 10, 10, 11, 8,
            // Left
            12, 13, 14, 14, 15, 12,
            // Top
            16, 17, 18, 18, 19, 16,
            // Bottom
            20, 21, 22, 22, 23, 20
        };
        
        meshData.indexCount = static_cast<uint32_t>(meshData.indices.size());
        
        if (device != VK_NULL_HANDLE && physicalDevice != VK_NULL_HANDLE && findMemoryType != nullptr) {
            createBuffers(meshData, device, physicalDevice, findMemoryType);
        }
        
        return meshData;
    }
    
    static glm::mat4 convertAssimpMatrix(aiMatrix4x4 matrix) {
        glm::mat4 result;
        result[0][0] = matrix.a1; result[0][1] = matrix.b1; result[0][2] = matrix.c1; result[0][3] = matrix.d1;
        result[1][0] = matrix.a2; result[1][1] = matrix.b2; result[1][2] = matrix.c2; result[1][3] = matrix.d2;
        result[2][0] = matrix.a3; result[2][1] = matrix.b3; result[2][2] = matrix.c3; result[2][3] = matrix.d3;
        result[3][0] = matrix.a4; result[3][1] = matrix.b4; result[3][2] = matrix.c4; result[3][3] = matrix.d4;
        return result;
    }
    
    static void applyCoordinateCorrection(std::vector<Vertex>& vertices) {
        glm::mat4 correction = glm::rotate(glm::mat4(1.0f), glm::radians(-90.0f), glm::vec3(1.0f, 0.0f, 0.0f));
        
        for (auto& vertex : vertices) {
            glm::vec4 pos(vertex.pos, 1.0f);
            pos = correction * pos;
            vertex.pos = glm::vec3(pos);
            
            glm::vec4 normal(vertex.normal, 0.0f);
            normal = correction * normal;
            vertex.normal = glm::vec3(normal);
        }
    }
    
    static void flipTriangles(std::vector<uint32_t>& indices) {
        for (size_t i = 0; i < indices.size(); i += 3) {
            std::swap(indices[i], indices[i + 2]);
        }
    }
    
    static MeshData loadModel(const std::string& path, VkDevice device, VkPhysicalDevice physicalDevice, uint32_t (*findMemoryType)(uint32_t, VkMemoryPropertyFlags, VkPhysicalDeviceMemoryProperties*)) {
        Assimp::Importer importer;
        
        const aiScene* scene = importer.ReadFile(path, 
            aiProcess_Triangulate | 
            aiProcess_FixInfacingNormals);
        
        if (!scene || scene->mFlags & AI_SCENE_FLAGS_INCOMPLETE || !scene->mRootNode) {
            throw std::runtime_error("Failed to load model: " + std::string(importer.GetErrorString()));
        }

        MeshData meshData;
        uint32_t vertexOffset = 0;
        
        uint32_t totalVerts = 0, totalIndices = 0;
        for (unsigned int i = 0; i < scene->mNumMeshes; i++) {
            totalVerts += scene->mMeshes[i]->mNumVertices;
            totalIndices += scene->mMeshes[i]->mNumFaces * 3;
        }
        meshData.vertices.reserve(totalVerts);
        meshData.indices.reserve(totalIndices);

        for (unsigned int i = 0; i < scene->mNumMeshes; i++) {
            aiMesh* aiMesh = scene->mMeshes[i];
            
            for (unsigned int j = 0; j < aiMesh->mNumVertices; j++) {
                Vertex vertex;
                
                vertex.pos.x = aiMesh->mVertices[j].x;
                vertex.pos.y = aiMesh->mVertices[j].y;
                vertex.pos.z = aiMesh->mVertices[j].z;

                vertex.color = {1.0f, 1.0f, 1.0f};

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
    
    static void loadModelMultiMesh(const std::string& path, VkDevice device, VkPhysicalDevice physicalDevice, uint32_t (*findMemoryType)(uint32_t, VkMemoryPropertyFlags, VkPhysicalDeviceMemoryProperties*), ModelData& outData, bool mergeAll = false) {
        auto startTotal = std::chrono::high_resolution_clock::now();
        
        Assimp::Importer importer;
        auto startRead = std::chrono::high_resolution_clock::now();
        const aiScene* scene = importer.ReadFile(path, 
            aiProcess_Triangulate | 
            aiProcess_FixInfacingNormals);
        auto endRead = std::chrono::high_resolution_clock::now();
        std::cout << "  [TIMING] Assimp ReadFile: " << std::chrono::duration<double, std::milli>(endRead - startRead).count() << "ms" << std::endl;
        
        if (!scene || scene->mFlags & AI_SCENE_FLAGS_INCOMPLETE || !scene->mRootNode) {
            throw std::runtime_error("Failed to load model: " + std::string(importer.GetErrorString()));
        }

        ModelData modelData;
        
        if (mergeAll || scene->mNumMeshes > 32) {
            MeshData merged;
            merged.name = "merged";
            
            uint32_t totalVerts = 0, totalInds = 0;
            for (unsigned int i = 0; i < scene->mNumMeshes; i++) {
                totalVerts += scene->mMeshes[i]->mNumVertices;
                totalInds += scene->mMeshes[i]->mNumFaces * 3;
            }
            merged.vertices.reserve(totalVerts);
            merged.indices.reserve(totalInds);
            
            uint32_t vertexOffset = 0;
            for (unsigned int i = 0; i < scene->mNumMeshes; i++) {
                aiMesh* aiMesh = scene->mMeshes[i];
                std::cout << "  Merging mesh " << i << ": " << (aiMesh->mName.length > 0 ? aiMesh->mName.C_Str() : "unnamed") << std::endl;
                
                for (unsigned int j = 0; j < aiMesh->mNumVertices; j++) {
                    Vertex vertex;
                    vertex.pos.x = aiMesh->mVertices[j].x;
                    vertex.pos.y = aiMesh->mVertices[j].y;
                    vertex.pos.z = aiMesh->mVertices[j].z;
                    vertex.color = {1.0f, 1.0f, 1.0f};
                    
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
                    
                    merged.vertices.push_back(vertex);
                }
                
                for (unsigned int j = 0; j < aiMesh->mNumFaces; j++) {
                    aiFace face = aiMesh->mFaces[j];
                    for (unsigned int k = 0; k < face.mNumIndices; k++) {
                        merged.indices.push_back(face.mIndices[k] + vertexOffset);
                    }
                }
                
                vertexOffset += aiMesh->mNumVertices;
            }
            
            merged.indexCount = static_cast<uint32_t>(merged.indices.size());
            
            auto startBuffer = std::chrono::high_resolution_clock::now();
            createBuffers(merged, device, physicalDevice, findMemoryType);
            auto endBuffer = std::chrono::high_resolution_clock::now();
            std::cout << "  [TIMING] Merged buffer creation: " << std::chrono::duration<double, std::milli>(endBuffer - startBuffer).count() << "ms" << std::endl;
            
            modelData.meshes.push_back(std::move(merged));
        } else {
            auto startParse = std::chrono::high_resolution_clock::now();
            for (unsigned int i = 0; i < scene->mNumMeshes; i++) {
                aiMesh* aiMesh = scene->mMeshes[i];
                MeshData meshData;
                
                meshData.name = aiMesh->mName.length > 0 ? std::string(aiMesh->mName.C_Str()) : "Part_" + std::to_string(i);
                meshData.vertexOffset = modelData.totalVertices;
                
                meshData.vertices.reserve(aiMesh->mNumVertices);
                meshData.indices.reserve(aiMesh->mNumFaces * 3);
                
                for (unsigned int j = 0; j < aiMesh->mNumVertices; j++) {
                    Vertex vertex;
                    vertex.pos.x = aiMesh->mVertices[j].x;
                    vertex.pos.y = aiMesh->mVertices[j].y;
                    vertex.pos.z = aiMesh->mVertices[j].z;
                    vertex.color = {1.0f, 1.0f, 1.0f};
                    
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
                        meshData.indices.push_back(face.mIndices[k]);
                    }
                }
                
                meshData.indexCount = static_cast<uint32_t>(meshData.indices.size());
                
                auto startBuffer = std::chrono::high_resolution_clock::now();
                createBuffers(meshData, device, physicalDevice, findMemoryType);
                auto endBuffer = std::chrono::high_resolution_clock::now();
                std::cout << "  [TIMING] Mesh " << i << " (" << meshData.name << ") buffers: " 
                          << std::chrono::duration<double, std::milli>(endBuffer - startBuffer).count() << "ms" << std::endl;
                
                modelData.totalVertices += aiMesh->mNumVertices;
                modelData.totalIndices += meshData.indexCount;
                modelData.meshes.push_back(std::move(meshData));
            }
            auto endParse = std::chrono::high_resolution_clock::now();
            std::cout << "  [TIMING] Parse + Buffer creation: " << std::chrono::duration<double, std::milli>(endParse - startParse).count() << "ms" << std::endl;
        }
        
        auto endTotal = std::chrono::high_resolution_clock::now();
        std::cout << "  [TIMING] TOTAL loadModelMultiMesh: " << std::chrono::duration<double, std::milli>(endTotal - startTotal).count() << "ms" << std::endl;
        std::cout << "  [LOADER] About to return ModelData with " << modelData.meshes.size() << " meshes" << std::endl;
        std::cout.flush();
        
        outData = std::move(modelData);
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

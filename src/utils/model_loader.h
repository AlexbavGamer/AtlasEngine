#pragma once

#include <string>
#include <vector>
#include <iostream>
#include <chrono>
#include <functional>
#include <unordered_map>
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

#include "../ecs/vertex.h"

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

    ModelData() = default;
    explicit ModelData(VkDevice dev) : device(dev) {}
    ModelData(const ModelData&) = delete;
    ModelData& operator=(const ModelData&) = delete;

    ModelData(ModelData&& other) noexcept 
        : meshes(std::move(other.meshes)),
          rootName(std::move(other.rootName)),
          totalVertices(other.totalVertices),
          totalIndices(other.totalIndices),
          device(other.device) {
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
            other.rootName.clear();
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

class ModelLoader {
public:
    static MeshData createCube(float size = 1.0f, VkDevice device = VK_NULL_HANDLE, VkPhysicalDevice physicalDevice = VK_NULL_HANDLE, uint32_t (*findMemoryType)(uint32_t, VkMemoryPropertyFlags, VkPhysicalDeviceMemoryProperties*) = nullptr) {
        MeshData meshData;
        float h = size / 2.0f;
        
        meshData.vertices = {
            Vertex{{-h, -h,  h}, {1,1,1}, {0,0}, {0,0,1}},
            Vertex{{ h, -h,  h}, {1,1,1}, {1,0}, {0,0,1}},
            Vertex{{ h,  h,  h}, {1,1,1}, {1,1}, {0,0,1}},
            Vertex{{-h,  h,  h}, {1,1,1}, {0,1}, {0,0,1}},
            Vertex{{ h, -h, -h}, {1,0,0}, {0,0}, {0,0,-1}},
            Vertex{{-h, -h, -h}, {1,0,0}, {1,0}, {0,0,-1}},
            Vertex{{-h,  h, -h}, {1,0,0}, {1,1}, {0,0,-1}},
            Vertex{{ h,  h, -h}, {1,0,0}, {0,1}, {0,0,-1}},
            Vertex{{ h, -h,  h}, {0,1,0}, {0,0}, {1,0,0}},
            Vertex{{ h, -h, -h}, {0,1,0}, {1,0}, {1,0,0}},
            Vertex{{ h,  h, -h}, {0,1,0}, {1,1}, {1,0,0}},
            Vertex{{ h,  h,  h}, {0,1,0}, {0,1}, {1,0,0}},
            Vertex{{-h, -h, -h}, {0,0,1}, {0,0}, {-1,0,0}},
            Vertex{{-h, -h,  h}, {0,0,1}, {1,0}, {-1,0,0}},
            Vertex{{-h,  h,  h}, {0,0,1}, {1,1}, {-1,0,0}},
            Vertex{{-h,  h, -h}, {0,0,1}, {0,1}, {-1,0,0}},
            Vertex{{-h,  h,  h}, {1,1,0}, {0,0}, {0,1,0}},
            Vertex{{ h,  h,  h}, {1,1,0}, {1,0}, {0,1,0}},
            Vertex{{ h,  h, -h}, {1,1,0}, {1,1}, {0,1,0}},
            Vertex{{-h,  h, -h}, {1,1,0}, {0,1}, {0,1,0}},
            Vertex{{-h, -h, -h}, {0,1,1}, {0,0}, {0,-1,0}},
            Vertex{{ h, -h, -h}, {0,1,1}, {1,0}, {0,-1,0}},
            Vertex{{ h, -h,  h}, {0,1,1}, {1,1}, {0,-1,0}},
            Vertex{{-h, -h,  h}, {0,1,1}, {0,1}, {0,-1,0}},
        };
        
        meshData.indices = {
            0, 1, 2, 2, 3, 0,
            4, 5, 6, 6, 7, 4,
            8, 9, 10, 10, 11, 8,
            12, 13, 14, 14, 15, 12,
            16, 17, 18, 18, 19, 16,
            20, 21, 22, 22, 23, 20
        };
        
        meshData.vertexCount = static_cast<uint32_t>(meshData.vertices.size());
        meshData.indexCount = static_cast<uint32_t>(meshData.indices.size());
        
        if (device != VK_NULL_HANDLE && physicalDevice != VK_NULL_HANDLE && findMemoryType != nullptr) {
            createBuffers(meshData, device, physicalDevice, findMemoryType);
            meshData.freeCPUMemory();
        }
        
        return meshData;
    }

    static MeshData loadModel(const std::string& path, VkDevice device, VkPhysicalDevice physicalDevice, uint32_t (*findMemoryType)(uint32_t, VkMemoryPropertyFlags, VkPhysicalDeviceMemoryProperties*)) {
        Assimp::Importer importer;
        
        const aiScene* scene = importer.ReadFile(path, 
            aiProcess_Triangulate |
            aiProcess_FixInfacingNormals |
            aiProcess_PreTransformVertices |
            aiProcess_ConvertToLeftHanded |
            aiProcess_FlipUVs |
            aiProcess_GenNormals);
        
        if (!scene || scene->mFlags & AI_SCENE_FLAGS_INCOMPLETE || !scene->mRootNode) {
            throw std::runtime_error("Failed to load model: " + std::string(importer.GetErrorString()));
        }

        MeshData meshData;
        
        uint32_t totalVerts = 0, totalIndices = 0;
        for (unsigned int i = 0; i < scene->mNumMeshes; i++) {
            totalVerts += scene->mMeshes[i]->mNumVertices;
            totalIndices += scene->mMeshes[i]->mNumFaces * 3;
        }
        meshData.vertices.reserve(totalVerts);
        meshData.indices.reserve(totalIndices);

        uint32_t vertexOffset = 0;
        
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

        meshData.vertexCount = static_cast<uint32_t>(meshData.vertices.size());
        meshData.indexCount = static_cast<uint32_t>(meshData.indices.size());

        createBuffers(meshData, device, physicalDevice, findMemoryType);
        meshData.freeCPUMemory();

        return meshData;
    }

    static void loadModelMultiMesh(
        const std::string& path, 
        VkDevice device, 
        VkPhysicalDevice physicalDevice, 
        uint32_t (*findMemoryType)(uint32_t, VkMemoryPropertyFlags, VkPhysicalDeviceMemoryProperties*), 
        ModelData* outData, 
        bool mergeAll = false) 
    {
        if (outData == nullptr) {
            throw std::runtime_error("outData pointer is null!");
        }

        outData->clear(device);

        auto startTotal = std::chrono::high_resolution_clock::now();
        
        Assimp::Importer importer;
        const aiScene* scene = importer.ReadFile(path, 
            aiProcess_Triangulate |
            aiProcess_FixInfacingNormals |
            aiProcess_PreTransformVertices |
            aiProcess_ConvertToLeftHanded |
            aiProcess_FlipUVs |
            aiProcess_GenNormals);
        
        if (!scene || scene->mFlags & AI_SCENE_FLAGS_INCOMPLETE || !scene->mRootNode) {
            throw std::runtime_error("Failed to load model: " + std::string(importer.GetErrorString()));
        }

        std::filesystem::path modelPath(path);
        std::filesystem::path modelDir = modelPath.parent_path();

        auto trimWs = [](std::string& s) {
            auto isWs = [](unsigned char c) -> bool {
                return c == ' ' || c == '\t' || c == '\n' || c == '\r';
            };
            while (!s.empty() && isWs(static_cast<unsigned char>(s.front()))) {
                s.erase(s.begin());
            }
            while (!s.empty() && isWs(static_cast<unsigned char>(s.back()))) {
                s.pop_back();
            }
        };

        auto prettify = [&](std::string s) -> std::string {
            trimWs(s);
            // FBX often prefixes names like "Model::Foo".
            size_t pos = s.rfind("::");
            if (pos != std::string::npos && pos + 2 < s.size()) {
                s = s.substr(pos + 2);
                trimWs(s);
            }
            return s;
        };

        auto isGeneric = [](const std::string& name) -> bool {
            if (name.empty()) return true;

            std::string n;
            n.reserve(name.size());
            for (unsigned char c : name) {
                if (c >= 'A' && c <= 'Z') n.push_back(static_cast<char>(c - 'A' + 'a'));
                else n.push_back(static_cast<char>(c));
            }

            return n == "rootnode" || n == "root" || n == "scene";
        };

        // Name for the imported object root (prefer the model's internal root node name).
        {
            outData->rootName.clear();

            if (scene->mRootNode) {
                outData->rootName = prettify(scene->mRootNode->mName.C_Str());
            }

            // If root node is generic, pick the first meaningful node name in the hierarchy.
            if (scene->mRootNode && isGeneric(outData->rootName)) {
                std::function<void(const aiNode*)> walk;
                walk = [&](const aiNode* node) {
                    if (!node || !outData->rootName.empty()) return;

                    std::string cand = prettify(node->mName.C_Str());
                    if (!isGeneric(cand)) {
                        outData->rootName = std::move(cand);
                        return;
                    }

                    for (unsigned int i = 0; i < node->mNumChildren; ++i) {
                        walk(node->mChildren[i]);
                        if (!outData->rootName.empty()) return;
                    }
                };

                // Start from children to avoid immediately matching the generic root again.
                for (unsigned int i = 0; i < scene->mRootNode->mNumChildren; ++i) {
                    walk(scene->mRootNode->mChildren[i]);
                    if (!outData->rootName.empty()) break;
                }
            }

            // If root name looks like the filename, prefer a neutral group name.
            {
                auto toLower = [](std::string s) {
                    for (char& c : s) {
                        if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
                    }
                    return s;
                };

                std::string fileName = modelPath.filename().string();
                std::string stem = modelPath.stem().string();
                std::string rn = toLower(outData->rootName);
                if (!fileName.empty()) {
                    std::string fn = toLower(fileName);
                    std::string st = toLower(stem);
                    if (!rn.empty() && (rn == fn || rn == st)) {
                        outData->rootName.clear();
                    }
                }
            }

            if (isGeneric(outData->rootName)) {
                outData->rootName.clear();
            }
            if (outData->rootName.empty()) {
                outData->rootName = "Scene";
            }
        }

        // Prefer extracting/copying textures into MyProject/assets/textures when available.
        std::filesystem::path extractedDir = std::filesystem::path("MyProject") / "assets" / "textures";
        std::error_code ec;
        if (!std::filesystem::exists(extractedDir, ec)) {
            extractedDir = modelDir.parent_path() / "textures";
        }
        std::filesystem::create_directories(extractedDir, ec);

        auto extractEmbeddedTexture = [&](int index) -> std::string {
            if (index < 0 || static_cast<unsigned int>(index) >= scene->mNumTextures) {
                return std::string();
            }

            const aiTexture* tex = scene->mTextures[index];
            std::string hint = tex->achFormatHint;
            if (hint.empty()) hint = "png";

            for (auto& ch : hint) {
                if (ch >= 'A' && ch <= 'Z') ch = static_cast<char>(ch - 'A' + 'a');
            }

            std::string fileStem = modelPath.stem().string() + "_embedded_" + std::to_string(index);

            auto pickUniquePath = [&](const std::string& ext, uintmax_t expectedSize) -> std::filesystem::path {
                std::filesystem::path base = extractedDir / (fileStem + ext);
                if (!std::filesystem::exists(base)) return base;

                if (expectedSize > 0) {
                    std::error_code fsEc;
                    auto sz = std::filesystem::file_size(base, fsEc);
                    if (!fsEc && sz == expectedSize) {
                        return base;
                    }
                }

                for (int v = 2; v < 1000; ++v) {
                    std::filesystem::path cand = extractedDir / (fileStem + "_v" + std::to_string(v) + ext);
                    if (!std::filesystem::exists(cand)) return cand;
                    if (expectedSize > 0) {
                        std::error_code fsEc;
                        auto sz = std::filesystem::file_size(cand, fsEc);
                        if (!fsEc && sz == expectedSize) {
                            return cand;
                        }
                    }
                }

                return base;
            };

            std::filesystem::path outPath;
            if (tex->mHeight == 0) {
                // Compressed data; mWidth is byte count.
                outPath = pickUniquePath("." + hint, static_cast<uintmax_t>(tex->mWidth));
                if (!std::filesystem::exists(outPath)) {
                    std::ofstream out(outPath, std::ios::binary);
                    out.write(reinterpret_cast<const char*>(tex->pcData), static_cast<std::streamsize>(tex->mWidth));
                }
            } else {
                // Uncompressed data (aiTexel array).
                outPath = pickUniquePath(".ppm", 0);
                if (!std::filesystem::exists(outPath)) {
                    std::ofstream out(outPath, std::ios::binary);
                    out << "P6\n" << tex->mWidth << " " << tex->mHeight << "\n255\n";
                    for (unsigned int y = 0; y < tex->mHeight; ++y) {
                        for (unsigned int x = 0; x < tex->mWidth; ++x) {
                            const aiTexel& t = tex->pcData[y * tex->mWidth + x];
                            unsigned char rgb[3] = {t.r, t.g, t.b};
                            out.write(reinterpret_cast<const char*>(rgb), 3);
                        }
                    }
                }
            }

            return outPath.string();
        };

        auto copyExternalTexture = [&](const std::string& rawPath) -> std::string {
            if (rawPath.empty()) return std::string();

            std::filesystem::path input(rawPath);
            std::filesystem::path src;

            std::error_code fsEc;
            if (input.is_absolute() || std::filesystem::exists(input, fsEc)) {
                src = input;
            } else {
                src = (modelDir / input).lexically_normal();
            }

            fsEc.clear();
            if (!std::filesystem::exists(src, fsEc) || !std::filesystem::is_regular_file(src, fsEc)) {
                return rawPath;
            }

            auto computeTargetDir = [&](const std::filesystem::path& resolvedSrc) -> std::filesystem::path {
                // If the source is under .../assets/models/textures, redirect to .../assets/textures.
                std::filesystem::path p = resolvedSrc.parent_path();
                if (p.filename() == "textures") {
                    std::filesystem::path models = p.parent_path();
                    if (models.filename() == "models") {
                        std::filesystem::path assets = models.parent_path();
                        if (assets.filename() == "assets") {
                            return assets / "textures";
                        }
                    }
                }
                return extractedDir;
            };

            std::filesystem::path targetDir = computeTargetDir(src);
            fsEc.clear();
            std::filesystem::create_directories(targetDir, fsEc);

            // If the texture is already in the target folder, keep it.
            if (src.parent_path() == targetDir) {
                return src.lexically_normal().string();
            }

            std::string ext = src.extension().string();
            if (ext.empty()) {
                ext = ".png";
            }

            std::string fileStem = modelPath.stem().string() + "_" + src.stem().string();

            auto pickUniquePath = [&](uintmax_t expectedSize) -> std::filesystem::path {
                std::filesystem::path base = targetDir / (fileStem + ext);
                if (!std::filesystem::exists(base)) return base;

                if (expectedSize > 0) {
                    std::error_code szEc;
                    auto sz = std::filesystem::file_size(base, szEc);
                    if (!szEc && sz == expectedSize) {
                        return base;
                    }
                }

                for (int v = 2; v < 1000; ++v) {
                    std::filesystem::path cand = targetDir / (fileStem + "_v" + std::to_string(v) + ext);
                    if (!std::filesystem::exists(cand)) return cand;

                    if (expectedSize > 0) {
                        std::error_code szEc;
                        auto sz = std::filesystem::file_size(cand, szEc);
                        if (!szEc && sz == expectedSize) {
                            return cand;
                        }
                    }
                }

                return base;
            };

            uintmax_t srcSize = 0;
            {
                std::error_code szEc;
                srcSize = std::filesystem::file_size(src, szEc);
                if (szEc) srcSize = 0;
            }

            std::filesystem::path dst = pickUniquePath(srcSize);

            if (!std::filesystem::exists(dst)) {
                std::error_code copyEc;
                std::filesystem::copy_file(src, dst, std::filesystem::copy_options::skip_existing, copyEc);
                if (copyEc) {
                    return rawPath;
                }
            }

            return dst.lexically_normal().string();
        };

        auto resolveTexturePath = [&](const aiString& texPath) -> std::string {
            const char* cstr = texPath.C_Str();
            if (!cstr || cstr[0] == '\0') return std::string();
            if (cstr[0] == '*') {
                int idx = std::atoi(cstr + 1);
                return extractEmbeddedTexture(idx);
            }

            return copyExternalTexture(std::string(cstr));
        };

        auto recenterVertices = [&](std::vector<Vertex>& verts, glm::vec3& outPivot) {
            if (verts.empty()) {
                outPivot = glm::vec3(0.0f);
                return;
            }

            glm::vec3 minP = verts[0].pos;
            glm::vec3 maxP = verts[0].pos;
            for (const auto& v : verts) {
                minP = glm::min(minP, v.pos);
                maxP = glm::max(maxP, v.pos);
            }

            outPivot = (minP + maxP) * 0.5f;
            for (auto& v : verts) {
                v.pos -= outPivot;
            }
        };

        ModelData modelData(device);
        modelData.rootName = outData->rootName;

        if (mergeAll) {
            // Chunk by material (opt-in) to reduce entity count.
            struct ChunkBuild {
                std::vector<Vertex> vertices;
                std::vector<uint32_t> indices;
                uint32_t vertexOffset = 0;
                uint32_t partIndex = 0;
                glm::vec4 baseColor = glm::vec4(1.0f);
                glm::vec3 emissiveFactor = glm::vec3(0.0f);
                float metallic = 0.0f;
                float roughness = 0.5f;
                std::string baseColorTexturePath;
                std::string normalTexturePath;
                std::string metallicRoughnessTexturePath;
                std::string aoTexturePath;
                std::string emissiveTexturePath;
                int alphaMode = 0;
                float alphaCutoff = 0.5f;
                bool doubleSided = false;
                bool materialInitialized = false;
            };

            std::unordered_map<unsigned int, ChunkBuild> builds;

            auto initMaterialFor = [&](unsigned int materialIndex, ChunkBuild& build) {
                if (build.materialInitialized) return;
                build.materialInitialized = true;

                if (materialIndex >= scene->mNumMaterials) return;
                aiMaterial* mat = scene->mMaterials[materialIndex];

                aiColor4D color;
                if (aiGetMaterialColor(mat, AI_MATKEY_BASE_COLOR, &color) == AI_SUCCESS ||
                    aiGetMaterialColor(mat, AI_MATKEY_COLOR_DIFFUSE, &color) == AI_SUCCESS) {
                    build.baseColor = glm::vec4(color.r, color.g, color.b, color.a);
                }

                aiColor4D emissive(0.0f, 0.0f, 0.0f, 1.0f);
                if (aiGetMaterialColor(mat, AI_MATKEY_COLOR_EMISSIVE, &emissive) == AI_SUCCESS) {
                    build.emissiveFactor = glm::vec3(emissive.r, emissive.g, emissive.b);
                }

                ai_real value = 0.0f;
                if (aiGetMaterialFloat(mat, AI_MATKEY_METALLIC_FACTOR, &value) == AI_SUCCESS) {
                    build.metallic = static_cast<float>(value);
                }
                if (aiGetMaterialFloat(mat, AI_MATKEY_ROUGHNESS_FACTOR, &value) == AI_SUCCESS) {
                    build.roughness = static_cast<float>(value);
                }

                int twoSided = 0;
                if (aiGetMaterialInteger(mat, AI_MATKEY_TWOSIDED, &twoSided) == AI_SUCCESS) {
                    build.doubleSided = (twoSided != 0);
                }

                // Alpha mode / opacity
                build.alphaMode = 0;
                aiString alphaModeStr;
                if (aiGetMaterialString(mat, AI_MATKEY_GLTF_ALPHAMODE, &alphaModeStr) == AI_SUCCESS) {
                    std::string s = alphaModeStr.C_Str();
                    if (s == "MASK") build.alphaMode = 1;
                    if (s == "BLEND") build.alphaMode = 2;
                }

                if (aiGetMaterialFloat(mat, AI_MATKEY_GLTF_ALPHACUTOFF, &value) == AI_SUCCESS) {
                    build.alphaCutoff = static_cast<float>(value);
                }

                if (aiGetMaterialFloat(mat, AI_MATKEY_OPACITY, &value) == AI_SUCCESS) {
                    if (static_cast<float>(value) < 0.999f) {
                        build.alphaMode = 2;
                    }
                    build.baseColor.a *= static_cast<float>(value);
                }

                auto tryTex = [&](aiTextureType type, std::string& dst) {
                    aiString p;
                    if (mat->GetTexture(type, 0, &p) == AI_SUCCESS) {
                        dst = resolveTexturePath(p);
                    }
                };

                tryTex(aiTextureType_BASE_COLOR, build.baseColorTexturePath);
                if (build.baseColorTexturePath.empty()) tryTex(aiTextureType_DIFFUSE, build.baseColorTexturePath);

                tryTex(aiTextureType_NORMALS, build.normalTexturePath);
                if (build.normalTexturePath.empty()) tryTex(aiTextureType_HEIGHT, build.normalTexturePath);

                // glTF metallic-roughness is usually exposed as METALNESS by Assimp.
                tryTex(aiTextureType_METALNESS, build.metallicRoughnessTexturePath);
                if (build.metallicRoughnessTexturePath.empty()) tryTex(aiTextureType_DIFFUSE_ROUGHNESS, build.metallicRoughnessTexturePath);

                tryTex(aiTextureType_AMBIENT_OCCLUSION, build.aoTexturePath);
                if (build.aoTexturePath.empty()) tryTex(aiTextureType_LIGHTMAP, build.aoTexturePath);

                tryTex(aiTextureType_EMISSIVE, build.emissiveTexturePath);

                // If an explicit opacity texture exists, treat as BLEND.
                aiString opacityTex;
                if (mat->GetTexture(aiTextureType_OPACITY, 0, &opacityTex) == AI_SUCCESS) {
                    build.alphaMode = 2;
                }
            };

            auto flushBuild = [&](unsigned int materialIndex, ChunkBuild& build) {
                if (build.vertices.empty() || build.indices.empty()) return;

                MeshData chunk;
                chunk.name = "mat_" + std::to_string(materialIndex) + "_chunk_" + std::to_string(build.partIndex++);
                chunk.vertices = std::move(build.vertices);
                chunk.indices = std::move(build.indices);
                chunk.vertexCount = static_cast<uint32_t>(chunk.vertices.size());
                chunk.indexCount = static_cast<uint32_t>(chunk.indices.size());

                recenterVertices(chunk.vertices, chunk.pivotPosition);

                chunk.baseColor = build.baseColor;
                chunk.emissiveFactor = build.emissiveFactor;
                chunk.metallic = build.metallic;
                chunk.roughness = build.roughness;
                chunk.baseColorTexturePath = build.baseColorTexturePath;
                chunk.normalTexturePath = build.normalTexturePath;
                chunk.metallicRoughnessTexturePath = build.metallicRoughnessTexturePath;
                chunk.aoTexturePath = build.aoTexturePath;
                chunk.emissiveTexturePath = build.emissiveTexturePath;
                chunk.alphaMode = build.alphaMode;
                chunk.alphaCutoff = build.alphaCutoff;
                chunk.doubleSided = build.doubleSided;

                if (device != VK_NULL_HANDLE && physicalDevice != VK_NULL_HANDLE && findMemoryType != nullptr) {
                    createBuffers(chunk, device, physicalDevice, findMemoryType);
                    chunk.freeCPUMemory();
                }

                modelData.totalVertices += chunk.vertexCount;
                modelData.totalIndices += chunk.indexCount;
                modelData.meshes.push_back(std::move(chunk));

                build.vertices.clear();
                build.indices.clear();
                build.vertexOffset = 0;
            };

            const size_t kVertexThreshold = 100000;

            for (unsigned int i = 0; i < scene->mNumMeshes; i++) {
                aiMesh* mesh = scene->mMeshes[i];
                unsigned int materialIndex = mesh->mMaterialIndex;

                ChunkBuild& build = builds[materialIndex];
                initMaterialFor(materialIndex, build);

                for (unsigned int j = 0; j < mesh->mNumVertices; j++) {
                    Vertex vtx;
                    vtx.pos.x = mesh->mVertices[j].x;
                    vtx.pos.y = mesh->mVertices[j].y;
                    vtx.pos.z = mesh->mVertices[j].z;
                    vtx.color = {1.0f, 1.0f, 1.0f};

                    if (mesh->mTextureCoords[0]) {
                        vtx.texCoord.x = mesh->mTextureCoords[0][j].x;
                        vtx.texCoord.y = mesh->mTextureCoords[0][j].y;
                    } else {
                        vtx.texCoord = {0.0f, 0.0f};
                    }

                    if (mesh->mNormals) {
                        vtx.normal.x = mesh->mNormals[j].x;
                        vtx.normal.y = mesh->mNormals[j].y;
                        vtx.normal.z = mesh->mNormals[j].z;
                    } else {
                        vtx.normal = {0.0f, 1.0f, 0.0f};
                    }

                    build.vertices.push_back(vtx);
                }

                for (unsigned int j = 0; j < mesh->mNumFaces; j++) {
                    aiFace face = mesh->mFaces[j];
                    for (unsigned int k = 0; k < face.mNumIndices; k++) {
                        build.indices.push_back(face.mIndices[k] + build.vertexOffset);
                    }
                }

                build.vertexOffset += mesh->mNumVertices;

                if (build.vertices.size() >= kVertexThreshold) {
                    flushBuild(materialIndex, build);
                }
            }

            for (auto& kv : builds) {
                flushBuild(kv.first, kv.second);
            }

            std::cout << "  [CHUNK] Loaded " << modelData.meshes.size() << " material chunks" << std::endl;
        } else {
            // Standard non-chunk path: one mesh per Assimp mesh (pretransformed).
            // Build a mapping from mesh index -> node/object name for better TagComponent names.
            std::unordered_map<unsigned int, std::string> meshIndexToNodeName;
            {
                std::function<void(const aiNode*)> walkNode;
                walkNode = [&](const aiNode* node) {
                    if (!node) return;

                    std::string base = prettify(node->mName.C_Str());
                    if (!isGeneric(base)) {
                        for (unsigned int m = 0; m < node->mNumMeshes; ++m) {
                            unsigned int meshIdx = node->mMeshes[m];
                            if (meshIdx >= scene->mNumMeshes) continue;
                            if (meshIndexToNodeName.find(meshIdx) != meshIndexToNodeName.end()) continue;

                            std::string n = base;
                            if (node->mNumMeshes > 1) {
                                n = base + "_" + std::to_string(m + 1);
                            }
                            meshIndexToNodeName[meshIdx] = std::move(n);
                        }
                    }

                    for (unsigned int i = 0; i < node->mNumChildren; ++i) {
                        walkNode(node->mChildren[i]);
                    }
                };

                walkNode(scene->mRootNode);
            }

            for (unsigned int i = 0; i < scene->mNumMeshes; i++) {
                aiMesh* mesh = scene->mMeshes[i];
                MeshData meshData;

                if (auto it = meshIndexToNodeName.find(i); it != meshIndexToNodeName.end()) {
                    meshData.name = it->second;
                } else {
                    meshData.name = prettify(mesh->mName.C_Str());
                }

                if (isGeneric(meshData.name)) {
                    meshData.name.clear();
                }
                if (meshData.name.empty()) {
                    meshData.name = std::string("Mesh_") + std::to_string(i);
                }

                if (mesh->mMaterialIndex < scene->mNumMaterials) {
                    aiMaterial* mat = scene->mMaterials[mesh->mMaterialIndex];

                    aiColor4D color;
                    if (aiGetMaterialColor(mat, AI_MATKEY_BASE_COLOR, &color) == AI_SUCCESS ||
                        aiGetMaterialColor(mat, AI_MATKEY_COLOR_DIFFUSE, &color) == AI_SUCCESS) {
                        meshData.baseColor = glm::vec4(color.r, color.g, color.b, color.a);
                    }

                    ai_real value = 0.0f;
                    if (aiGetMaterialFloat(mat, AI_MATKEY_METALLIC_FACTOR, &value) == AI_SUCCESS) {
                        meshData.metallic = static_cast<float>(value);
                    }
                    if (aiGetMaterialFloat(mat, AI_MATKEY_ROUGHNESS_FACTOR, &value) == AI_SUCCESS) {
                        meshData.roughness = static_cast<float>(value);
                    }

                    aiColor4D emissive(0.0f, 0.0f, 0.0f, 1.0f);
                    if (aiGetMaterialColor(mat, AI_MATKEY_COLOR_EMISSIVE, &emissive) == AI_SUCCESS) {
                        meshData.emissiveFactor = glm::vec3(emissive.r, emissive.g, emissive.b);
                    }

                    int twoSided = 0;
                    if (aiGetMaterialInteger(mat, AI_MATKEY_TWOSIDED, &twoSided) == AI_SUCCESS) {
                        meshData.doubleSided = (twoSided != 0);
                    }

                    // Alpha mode / opacity
                    meshData.alphaMode = 0;
                    aiString alphaModeStr;
                    if (aiGetMaterialString(mat, AI_MATKEY_GLTF_ALPHAMODE, &alphaModeStr) == AI_SUCCESS) {
                        std::string s = alphaModeStr.C_Str();
                        if (s == "MASK") meshData.alphaMode = 1;
                        if (s == "BLEND") meshData.alphaMode = 2;
                    }

                    if (aiGetMaterialFloat(mat, AI_MATKEY_GLTF_ALPHACUTOFF, &value) == AI_SUCCESS) {
                        meshData.alphaCutoff = static_cast<float>(value);
                    }

                    if (aiGetMaterialFloat(mat, AI_MATKEY_OPACITY, &value) == AI_SUCCESS) {
                        if (static_cast<float>(value) < 0.999f) {
                            meshData.alphaMode = 2;
                        }
                        meshData.baseColor.a *= static_cast<float>(value);
                    }

                    auto tryTex = [&](aiTextureType type, std::string& dst) {
                        aiString p;
                        if (mat->GetTexture(type, 0, &p) == AI_SUCCESS) {
                            dst = resolveTexturePath(p);
                        }
                    };

                    tryTex(aiTextureType_BASE_COLOR, meshData.baseColorTexturePath);
                    if (meshData.baseColorTexturePath.empty()) tryTex(aiTextureType_DIFFUSE, meshData.baseColorTexturePath);
                    if (meshData.baseColorTexturePath.empty()) tryTex(aiTextureType_UNKNOWN, meshData.baseColorTexturePath);

                    tryTex(aiTextureType_NORMALS, meshData.normalTexturePath);
                    if (meshData.normalTexturePath.empty()) tryTex(aiTextureType_HEIGHT, meshData.normalTexturePath);

                    tryTex(aiTextureType_METALNESS, meshData.metallicRoughnessTexturePath);
                    if (meshData.metallicRoughnessTexturePath.empty()) tryTex(aiTextureType_DIFFUSE_ROUGHNESS, meshData.metallicRoughnessTexturePath);

                    tryTex(aiTextureType_AMBIENT_OCCLUSION, meshData.aoTexturePath);
                    if (meshData.aoTexturePath.empty()) tryTex(aiTextureType_LIGHTMAP, meshData.aoTexturePath);

                    tryTex(aiTextureType_EMISSIVE, meshData.emissiveTexturePath);

                    // If an explicit opacity texture exists, treat as BLEND.
                    aiString opacityTex;
                    if (mat->GetTexture(aiTextureType_OPACITY, 0, &opacityTex) == AI_SUCCESS) {
                        meshData.alphaMode = 2;
                    }
                }

                for (unsigned int j = 0; j < mesh->mNumVertices; j++) {
                    Vertex vtx;
                    vtx.pos.x = mesh->mVertices[j].x;
                    vtx.pos.y = mesh->mVertices[j].y;
                    vtx.pos.z = mesh->mVertices[j].z;
                    vtx.color = {1.0f, 1.0f, 1.0f};

                    if (mesh->mTextureCoords[0]) {
                        vtx.texCoord.x = mesh->mTextureCoords[0][j].x;
                        vtx.texCoord.y = mesh->mTextureCoords[0][j].y;
                    } else {
                        vtx.texCoord = {0.0f, 0.0f};
                    }

                    if (mesh->mNormals) {
                        vtx.normal.x = mesh->mNormals[j].x;
                        vtx.normal.y = mesh->mNormals[j].y;
                        vtx.normal.z = mesh->mNormals[j].z;
                    } else {
                        vtx.normal = {0.0f, 1.0f, 0.0f};
                    }

                    meshData.vertices.push_back(vtx);
                }

                for (unsigned int j = 0; j < mesh->mNumFaces; j++) {
                    aiFace face = mesh->mFaces[j];
                    for (unsigned int k = 0; k < face.mNumIndices; k++) {
                        meshData.indices.push_back(face.mIndices[k]);
                    }
                }

                meshData.vertexCount = static_cast<uint32_t>(meshData.vertices.size());
                meshData.indexCount = static_cast<uint32_t>(meshData.indices.size());

                recenterVertices(meshData.vertices, meshData.pivotPosition);

                if (device != VK_NULL_HANDLE && physicalDevice != VK_NULL_HANDLE && findMemoryType != nullptr) {
                    createBuffers(meshData, device, physicalDevice, findMemoryType);
                    meshData.freeCPUMemory();
                }

                modelData.totalVertices += meshData.vertexCount;
                modelData.totalIndices += meshData.indexCount;
                modelData.meshes.push_back(std::move(meshData));
            }

            std::cout << "  [MESH] Loaded " << modelData.meshes.size() << " meshes" << std::endl;
        }

        auto endTotal = std::chrono::high_resolution_clock::now();
        std::cout << "  [TIMING] Model loading: " << std::chrono::duration<double, std::milli>(endTotal - startTotal).count() << "ms" << std::endl;
        
        *outData = std::move(modelData);
    }

    static void createBuffers(MeshData& meshData, VkDevice device, VkPhysicalDevice physicalDevice, uint32_t (*findMemoryType)(uint32_t, VkMemoryPropertyFlags, VkPhysicalDeviceMemoryProperties*)) {
        if (device == VK_NULL_HANDLE || physicalDevice == VK_NULL_HANDLE || findMemoryType == nullptr) {
            return;
        }

        if (meshData.vertices.empty() || meshData.indices.empty()) {
            std::cout << "  [WARN] createBuffers: empty mesh, skipping" << std::endl;
            return;
        }

        VkPhysicalDeviceMemoryProperties memProperties;
        vkGetPhysicalDeviceMemoryProperties(physicalDevice, &memProperties);

        VkDeviceSize vertexDataSize = static_cast<VkDeviceSize>(sizeof(Vertex) * meshData.vertices.size());
        VkDeviceSize indexDataSize = static_cast<VkDeviceSize>(sizeof(uint32_t) * meshData.indices.size());

        VkBufferCreateInfo bufferInfo{};
        bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
        bufferInfo.size = vertexDataSize;
        bufferInfo.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
        bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

        VkBuffer stagingVertexBuffer = VK_NULL_HANDLE;
        VkDeviceMemory stagingVertexMemory = VK_NULL_HANDLE;
        VkBuffer stagingIndexBuffer = VK_NULL_HANDLE;
        VkDeviceMemory stagingIndexMemory = VK_NULL_HANDLE;

        if (vkCreateBuffer(device, &bufferInfo, nullptr, &stagingVertexBuffer) != VK_SUCCESS) {
            throw std::runtime_error("failed to create staging vertex buffer!");
        }

        VkMemoryRequirements memRequirements;
        vkGetBufferMemoryRequirements(device, stagingVertexBuffer, &memRequirements);

        VkMemoryAllocateInfo allocInfo{};
        allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
        allocInfo.allocationSize = memRequirements.size;
        allocInfo.memoryTypeIndex = findMemoryType(
            memRequirements.memoryTypeBits,
            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
            &memProperties
        );

        if (vkAllocateMemory(device, &allocInfo, nullptr, &stagingVertexMemory) != VK_SUCCESS) {
            vkDestroyBuffer(device, stagingVertexBuffer, nullptr);
            throw std::runtime_error("failed to allocate staging vertex memory!");
        }

        VkResult bindResult = vkBindBufferMemory(device, stagingVertexBuffer, stagingVertexMemory, 0);
        if (bindResult != VK_SUCCESS) {
            vkFreeMemory(device, stagingVertexMemory, nullptr);
            vkDestroyBuffer(device, stagingVertexBuffer, nullptr);
            throw std::runtime_error("failed to bind staging vertex memory!");
        }

        void* data;
        VkResult mapResult = vkMapMemory(device, stagingVertexMemory, 0, vertexDataSize, 0, &data);
        if (mapResult != VK_SUCCESS) {
            vkFreeMemory(device, stagingVertexMemory, nullptr);
            vkDestroyBuffer(device, stagingVertexBuffer, nullptr);
            throw std::runtime_error("failed to map staging vertex memory!");
        }
        memcpy(data, meshData.vertices.data(), vertexDataSize);
        vkUnmapMemory(device, stagingVertexMemory);

        bufferInfo.size = indexDataSize;
        bufferInfo.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;

        if (vkCreateBuffer(device, &bufferInfo, nullptr, &stagingIndexBuffer) != VK_SUCCESS) {
            vkFreeMemory(device, stagingVertexMemory, nullptr);
            vkDestroyBuffer(device, stagingVertexBuffer, nullptr);
            throw std::runtime_error("failed to create staging index buffer!");
        }

        vkGetBufferMemoryRequirements(device, stagingIndexBuffer, &memRequirements);
        allocInfo.allocationSize = memRequirements.size;
        allocInfo.memoryTypeIndex = findMemoryType(
            memRequirements.memoryTypeBits,
            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
            &memProperties
        );

        if (vkAllocateMemory(device, &allocInfo, nullptr, &stagingIndexMemory) != VK_SUCCESS) {
            vkFreeMemory(device, stagingVertexMemory, nullptr);
            vkDestroyBuffer(device, stagingVertexBuffer, nullptr);
            vkDestroyBuffer(device, stagingIndexBuffer, nullptr);
            throw std::runtime_error("failed to allocate staging index memory!");
        }

        bindResult = vkBindBufferMemory(device, stagingIndexBuffer, stagingIndexMemory, 0);
        if (bindResult != VK_SUCCESS) {
            vkFreeMemory(device, stagingIndexMemory, nullptr);
            vkFreeMemory(device, stagingVertexMemory, nullptr);
            vkDestroyBuffer(device, stagingIndexBuffer, nullptr);
            vkDestroyBuffer(device, stagingVertexBuffer, nullptr);
            throw std::runtime_error("failed to bind staging index memory!");
        }

        mapResult = vkMapMemory(device, stagingIndexMemory, 0, indexDataSize, 0, &data);
        if (mapResult != VK_SUCCESS) {
            vkFreeMemory(device, stagingIndexMemory, nullptr);
            vkFreeMemory(device, stagingVertexMemory, nullptr);
            vkDestroyBuffer(device, stagingIndexBuffer, nullptr);
            vkDestroyBuffer(device, stagingVertexBuffer, nullptr);
            throw std::runtime_error("failed to map staging index memory!");
        }
        memcpy(data, meshData.indices.data(), indexDataSize);
        vkUnmapMemory(device, stagingIndexMemory);

        bufferInfo.usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_VERTEX_BUFFER_BIT;
        bufferInfo.size = vertexDataSize;

        if (vkCreateBuffer(device, &bufferInfo, nullptr, &meshData.vertexBuffer) != VK_SUCCESS) {
            vkFreeMemory(device, stagingIndexMemory, nullptr);
            vkFreeMemory(device, stagingVertexMemory, nullptr);
            vkDestroyBuffer(device, stagingIndexBuffer, nullptr);
            vkDestroyBuffer(device, stagingVertexBuffer, nullptr);
            throw std::runtime_error("failed to create vertex buffer!");
        }

        vkGetBufferMemoryRequirements(device, meshData.vertexBuffer, &memRequirements);
        allocInfo.allocationSize = memRequirements.size;
        allocInfo.memoryTypeIndex = findMemoryType(
            memRequirements.memoryTypeBits,
            VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
            &memProperties
        );

        if (vkAllocateMemory(device, &allocInfo, nullptr, &meshData.vertexMemory) != VK_SUCCESS) {
            vkFreeMemory(device, stagingIndexMemory, nullptr);
            vkFreeMemory(device, stagingVertexMemory, nullptr);
            vkDestroyBuffer(device, stagingIndexBuffer, nullptr);
            vkDestroyBuffer(device, stagingVertexBuffer, nullptr);
            vkDestroyBuffer(device, meshData.vertexBuffer, nullptr);
            throw std::runtime_error("failed to allocate vertex memory!");
        }

        bindResult = vkBindBufferMemory(device, meshData.vertexBuffer, meshData.vertexMemory, 0);
        if (bindResult != VK_SUCCESS) {
            vkFreeMemory(device, meshData.vertexMemory, nullptr);
            vkFreeMemory(device, stagingIndexMemory, nullptr);
            vkFreeMemory(device, stagingVertexMemory, nullptr);
            vkDestroyBuffer(device, meshData.vertexBuffer, nullptr);
            vkDestroyBuffer(device, stagingIndexBuffer, nullptr);
            vkDestroyBuffer(device, stagingVertexBuffer, nullptr);
            throw std::runtime_error("failed to bind vertex memory!");
        }

        bufferInfo.usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_INDEX_BUFFER_BIT;
        bufferInfo.size = indexDataSize;

        if (vkCreateBuffer(device, &bufferInfo, nullptr, &meshData.indexBuffer) != VK_SUCCESS) {
            vkFreeMemory(device, stagingIndexMemory, nullptr);
            vkFreeMemory(device, stagingVertexMemory, nullptr);
            vkDestroyBuffer(device, stagingIndexBuffer, nullptr);
            vkDestroyBuffer(device, stagingVertexBuffer, nullptr);
            throw std::runtime_error("failed to create index buffer!");
        }

        vkGetBufferMemoryRequirements(device, meshData.indexBuffer, &memRequirements);
        allocInfo.allocationSize = memRequirements.size;
        allocInfo.memoryTypeIndex = findMemoryType(
            memRequirements.memoryTypeBits,
            VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
            &memProperties
        );

        if (vkAllocateMemory(device, &allocInfo, nullptr, &meshData.indexMemory) != VK_SUCCESS) {
            vkFreeMemory(device, stagingIndexMemory, nullptr);
            vkFreeMemory(device, stagingVertexMemory, nullptr);
            vkDestroyBuffer(device, stagingIndexBuffer, nullptr);
            vkDestroyBuffer(device, stagingVertexBuffer, nullptr);
            vkDestroyBuffer(device, meshData.indexBuffer, nullptr);
            throw std::runtime_error("failed to allocate index memory!");
        }

        bindResult = vkBindBufferMemory(device, meshData.indexBuffer, meshData.indexMemory, 0);
        if (bindResult != VK_SUCCESS) {
            vkFreeMemory(device, meshData.indexMemory, nullptr);
            vkFreeMemory(device, stagingIndexMemory, nullptr);
            vkFreeMemory(device, stagingVertexMemory, nullptr);
            vkDestroyBuffer(device, meshData.indexBuffer, nullptr);
            vkDestroyBuffer(device, stagingIndexBuffer, nullptr);
            vkDestroyBuffer(device, stagingVertexBuffer, nullptr);
            throw std::runtime_error("failed to bind index memory!");
        }

        uint32_t graphicsQueueFamily = UINT32_MAX;
        uint32_t queueFamilyCount = 0;
        vkGetPhysicalDeviceQueueFamilyProperties(physicalDevice, &queueFamilyCount, nullptr);
        std::vector<VkQueueFamilyProperties> queueFamilies(queueFamilyCount);
        vkGetPhysicalDeviceQueueFamilyProperties(physicalDevice, &queueFamilyCount, queueFamilies.data());
        for (uint32_t i = 0; i < queueFamilyCount; ++i) {
            if ((queueFamilies[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) != 0) {
                graphicsQueueFamily = i;
                break;
            }
        }
        if (graphicsQueueFamily == UINT32_MAX) {
            throw std::runtime_error("failed to find graphics queue family for upload!");
        }

        VkCommandBuffer commandBuffer = VK_NULL_HANDLE;
        VkCommandPool commandPool = VK_NULL_HANDLE;
        
        VkCommandPoolCreateInfo poolInfo{};
        poolInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
        poolInfo.flags = VK_COMMAND_POOL_CREATE_TRANSIENT_BIT;
        poolInfo.queueFamilyIndex = graphicsQueueFamily;
        
        if (vkCreateCommandPool(device, &poolInfo, nullptr, &commandPool) != VK_SUCCESS) {
            vkFreeMemory(device, stagingIndexMemory, nullptr);
            vkFreeMemory(device, stagingVertexMemory, nullptr);
            vkDestroyBuffer(device, stagingIndexBuffer, nullptr);
            vkDestroyBuffer(device, stagingVertexBuffer, nullptr);
            vkFreeMemory(device, meshData.indexMemory, nullptr);
            vkFreeMemory(device, meshData.vertexMemory, nullptr);
            vkDestroyBuffer(device, meshData.indexBuffer, nullptr);
            vkDestroyBuffer(device, meshData.vertexBuffer, nullptr);
            throw std::runtime_error("failed to create command pool!");
        }
        
        VkCommandBufferAllocateInfo cmdBufInfo{};
        cmdBufInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
        cmdBufInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        cmdBufInfo.commandPool = commandPool;
        cmdBufInfo.commandBufferCount = 1;
        
        if (vkAllocateCommandBuffers(device, &cmdBufInfo, &commandBuffer) != VK_SUCCESS) {
            vkFreeMemory(device, stagingIndexMemory, nullptr);
            vkFreeMemory(device, stagingVertexMemory, nullptr);
            vkDestroyBuffer(device, stagingIndexBuffer, nullptr);
            vkDestroyBuffer(device, stagingVertexBuffer, nullptr);
            vkFreeMemory(device, meshData.indexMemory, nullptr);
            vkFreeMemory(device, meshData.vertexMemory, nullptr);
            vkDestroyBuffer(device, meshData.indexBuffer, nullptr);
            vkDestroyBuffer(device, meshData.vertexBuffer, nullptr);
            vkDestroyCommandPool(device, commandPool, nullptr);
            throw std::runtime_error("failed to allocate command buffer!");
        }
        
        VkCommandBufferBeginInfo beginInfo{};
        beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
        beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
        
        vkBeginCommandBuffer(commandBuffer, &beginInfo);
        
        VkBufferCopy copyRegion{};
        copyRegion.srcOffset = 0;
        copyRegion.dstOffset = 0;
        copyRegion.size = vertexDataSize;
        vkCmdCopyBuffer(commandBuffer, stagingVertexBuffer, meshData.vertexBuffer, 1, &copyRegion);
        
        copyRegion.size = indexDataSize;
        vkCmdCopyBuffer(commandBuffer, stagingIndexBuffer, meshData.indexBuffer, 1, &copyRegion);
        
        vkEndCommandBuffer(commandBuffer);
        
        VkSubmitInfo submitInfo{};
        submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
        submitInfo.commandBufferCount = 1;
        submitInfo.pCommandBuffers = &commandBuffer;
        
        VkQueue graphicsQueue = VK_NULL_HANDLE;
        vkGetDeviceQueue(device, graphicsQueueFamily, 0, &graphicsQueue);
        vkQueueSubmit(graphicsQueue, 1, &submitInfo, VK_NULL_HANDLE);
        vkQueueWaitIdle(graphicsQueue);

        meshData.ownerDevice = device;

        vkFreeCommandBuffers(device, commandPool, 1, &commandBuffer);
        vkDestroyCommandPool(device, commandPool, nullptr);

        vkFreeMemory(device, stagingVertexMemory, nullptr);
        vkFreeMemory(device, stagingIndexMemory, nullptr);
        vkDestroyBuffer(device, stagingVertexBuffer, nullptr);
        vkDestroyBuffer(device, stagingIndexBuffer, nullptr);
    }

    static void cleanupModel(ModelData& modelData, VkDevice device) {
        modelData.clear(device);
    }

    static void cleanup(MeshData& meshData, VkDevice device) {
        meshData.clearVulkanResources(device);
    }
};
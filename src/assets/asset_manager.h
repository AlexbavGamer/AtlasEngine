#pragma once

#include <string>
#include <unordered_map>
#include <memory>
#include <vector>

#include "../core/string/string_id.h"
#include "../renderer/memory/memory_manager.h"

namespace Atlas {

class Mesh;
class Texture;

class AssetManager {
public:
    AssetManager();
    ~AssetManager();

    // Mesh operations
    std::shared_ptr<Mesh> loadMesh(const StringID& id, const std::string& path);
    std::shared_ptr<Mesh> getMesh(const StringID& id);
    bool hasMesh(const StringID& id) const;
    void unloadMesh(const StringID& id);
    void unloadAllMeshes();

    // Texture operations
    std::shared_ptr<Texture> loadTexture(const StringID& id, const std::string& path);
    std::shared_ptr<Texture> getTexture(const StringID& id);
    bool hasTexture(const StringID& id) const;
    void unloadTexture(const StringID& id);
    void unloadAllTextures();

    // General
    void unloadAll();

    // Memory stats
    size_t getTotalMeshMemory() const { return m_TotalMeshMemory; }
    size_t getTotalTextureMemory() const { return m_TotalTextureMemory; }

private:
    std::unordered_map<StringID, std::shared_ptr<Mesh>> m_Meshes;
    std::unordered_map<StringID, std::shared_ptr<Texture>> m_Textures;
    
    size_t m_TotalMeshMemory = 0;
    size_t m_TotalTextureMemory = 0;
};

// Forward declaration
class Mesh {
public:
    struct Submesh {
        uint32_t vertexStart = 0;
        uint32_t vertexCount = 0;
        uint32_t indexStart = 0;
        uint32_t indexCount = 0;
        int32_t materialID = -1;
    };

    Mesh() = default;
    ~Mesh();

    bool isValid() const { return m_VertexBuffer != VK_NULL_HANDLE; }

    void setVertexData(void* data, size_t size);
    void setIndexData(void* data, size_t count);

    VkBuffer getVertexBuffer() const { return m_VertexBuffer; }
    VkBuffer getIndexBuffer() const { return m_IndexBuffer; }
    uint32_t getVertexCount() const { return m_VertexCount; }
    uint32_t getIndexCount() const { return m_IndexCount; }

    const std::vector<Submesh>& getSubmeshes() const { return m_Submeshes; }
    void addSubmesh(const Submesh& submesh) { m_Submeshes.push_back(submesh); }

private:
    VkBuffer m_VertexBuffer = VK_NULL_HANDLE;
    VkDeviceMemory m_VertexMemory = VK_NULL_HANDLE;
    VkBuffer m_IndexBuffer = VK_NULL_HANDLE;
    VkDeviceMemory m_IndexMemory = VK_NULL_HANDLE;

    uint32_t m_VertexCount = 0;
    uint32_t m_IndexCount = 0;

    std::vector<Submesh> m_Submeshes;
    std::string m_Path;
    size_t m_MemorySize = 0;

    friend class AssetManager;
};

class Texture {
public:
    Texture() = default;
    ~Texture();

    bool isValid() const { return m_Image != VK_NULL_HANDLE; }

    VkImage getImage() const { return m_Image; }
    VkImageView getImageView() const { return m_ImageView; }
    VkSampler getSampler() const { return m_Sampler; }

    uint32_t getWidth() const { return m_Width; }
    uint32_t getHeight() const { return m_Height; }
    VkFormat getFormat() const { return m_Format; }

private:
    VkImage m_Image = VK_NULL_HANDLE;
    VkDeviceMemory m_ImageMemory = VK_NULL_HANDLE;
    VkImageView m_ImageView = VK_NULL_HANDLE;
    VkSampler m_Sampler = VK_NULL_HANDLE;

    uint32_t m_Width = 0;
    uint32_t m_Height = 0;
    VkFormat m_Format = VK_FORMAT_R8G8B8A8_SRGB;

    std::string m_Path;
    size_t m_MemorySize = 0;

    friend class AssetManager;
};

}

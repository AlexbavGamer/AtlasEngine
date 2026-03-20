#define STB_IMAGE_IMPLEMENTATION
#include <stb_image.h>

#include "asset_manager.h"
#include "../renderer/renderer.h"
#include <stdexcept>
#include <iostream>

namespace Atlas {

static Renderer* g_Renderer = nullptr;

void AssetManager::setRenderer(Renderer* renderer) {
    m_Renderer = renderer;
    m_MemoryManager = renderer ? renderer->getMemoryManager() : nullptr;
}

AssetManager::AssetManager() = default;

AssetManager::~AssetManager() {
    unloadAll();
}

void AssetManager::shutdown() {
    unloadAll();
    m_Renderer = nullptr;
    m_MemoryManager = nullptr;
}

std::shared_ptr<Mesh> AssetManager::loadMesh(const StringID& id, const std::string& path) {
    if (auto it = m_Meshes.find(id); it != m_Meshes.end()) {
        return it->second;
    }
    return nullptr;
}

std::shared_ptr<Mesh> AssetManager::getMesh(const StringID& id) {
    if (auto it = m_Meshes.find(id); it != m_Meshes.end()) {
        return it->second;
    }
    return nullptr;
}

bool AssetManager::hasMesh(const StringID& id) const {
    return m_Meshes.find(id) != m_Meshes.end();
}

void AssetManager::unloadMesh(const StringID& id) {
    if (auto it = m_Meshes.find(id); it != m_Meshes.end()) {
        m_Meshes.erase(it);
    }
}

void AssetManager::unloadAllMeshes() {
    m_Meshes.clear();
}

std::shared_ptr<Texture> AssetManager::loadTexture(const StringID& id, const std::string& path) {
    std::cout << "[AssetManager] loadTexture path=" << path << " id=" << id.getID() << std::endl;

    if (auto it = m_Textures.find(id); it != m_Textures.end()) {
        std::cout << "[AssetManager] loadTexture cache hit path=" << path << std::endl;
        return it->second;
    }

    if (!m_MemoryManager || !m_Renderer) {
        return nullptr;
    }

    int texWidth, texHeight, texChannels;
    stbi_uc* pixels = stbi_load(path.c_str(), &texWidth, &texHeight, &texChannels, STBI_rgb_alpha);
    
    if (!pixels) {
        return nullptr;
    }

    auto texture = std::make_shared<Texture>();
    texture->setMemoryManager(m_MemoryManager);
    texture->m_Path = path;
    texture->m_Width = static_cast<uint32_t>(texWidth);
    texture->m_Height = static_cast<uint32_t>(texHeight);
    texture->m_Format = VK_FORMAT_R8G8B8A8_SRGB;

    VkDeviceSize imageSize = static_cast<VkDeviceSize>(texWidth * texHeight * 4);

    Buffer stagingBuffer(*m_MemoryManager, imageSize, 
        VK_BUFFER_USAGE_TRANSFER_SRC_BIT, VMA_MEMORY_USAGE_CPU_ONLY);

    void* data = stagingBuffer.map();
    if (data) {
        memcpy(data, pixels, static_cast<size_t>(imageSize));
        stagingBuffer.unmap();
    }
    stbi_image_free(pixels);

    VkImageCreateInfo imageInfo{};
    imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    imageInfo.imageType = VK_IMAGE_TYPE_2D;
    imageInfo.extent.width = static_cast<uint32_t>(texWidth);
    imageInfo.extent.height = static_cast<uint32_t>(texHeight);
    imageInfo.extent.depth = 1;
    imageInfo.mipLevels = 1;
    imageInfo.arrayLayers = 1;
    imageInfo.format = VK_FORMAT_R8G8B8A8_SRGB;
    imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
    imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    imageInfo.usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
    imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
    imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    texture->m_Image = Image(*m_MemoryManager, imageInfo, VMA_MEMORY_USAGE_GPU_ONLY);
    
    if (!texture->m_Image.isValid()) {
        return nullptr;
    }

    m_Renderer->immediateSubmit([&](VkCommandBuffer commandBuffer) {
        VkImageMemoryBarrier barrier{};
        barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
        barrier.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        barrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
        barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.image = texture->m_Image.getImage();
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
        region.imageExtent = {static_cast<uint32_t>(texWidth), static_cast<uint32_t>(texHeight), 1};

        vkCmdCopyBufferToImage(commandBuffer, stagingBuffer.getBuffer(),
            texture->m_Image.getImage(), VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);

        barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
        barrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;

        vkCmdPipelineBarrier(commandBuffer, VK_PIPELINE_STAGE_TRANSFER_BIT,
            VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0, 0, nullptr, 0, nullptr, 1, &barrier);
    });

    if (!texture->createSampler(m_Renderer)) {
        return nullptr;
    }

    m_Textures[id] = texture;
    return texture;
}

std::shared_ptr<Texture> AssetManager::getTexture(const StringID& id) {
    if (auto it = m_Textures.find(id); it != m_Textures.end()) {
        return it->second;
    }
    return nullptr;
}

bool AssetManager::hasTexture(const StringID& id) const {
    return m_Textures.find(id) != m_Textures.end();
}

void AssetManager::unloadTexture(const StringID& id) {
    if (auto it = m_Textures.find(id); it != m_Textures.end()) {
        m_Textures.erase(it);
    }
}

void AssetManager::unloadAllTextures() {
    m_Textures.clear();
}

void AssetManager::unloadAll() {
    unloadAllMeshes();
    unloadAllTextures();
}

bool Texture::createSampler(Renderer* renderer) {
    VkSamplerCreateInfo samplerInfo{};
    samplerInfo.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
    samplerInfo.magFilter = VK_FILTER_LINEAR;
    samplerInfo.minFilter = VK_FILTER_LINEAR;
    samplerInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
    samplerInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_REPEAT;
    samplerInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_REPEAT;
    samplerInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_REPEAT;
    samplerInfo.anisotropyEnable = VK_TRUE;
    
    VkPhysicalDeviceProperties properties{};
    vkGetPhysicalDeviceProperties(renderer->getPhysicalDevice(), &properties);
    samplerInfo.maxAnisotropy = properties.limits.maxSamplerAnisotropy;
    
    samplerInfo.borderColor = VK_BORDER_COLOR_INT_OPAQUE_BLACK;
    samplerInfo.unnormalizedCoordinates = VK_FALSE;
    samplerInfo.compareEnable = VK_FALSE;
    samplerInfo.compareOp = VK_COMPARE_OP_ALWAYS;
    samplerInfo.mipLodBias = 0.0f;
    samplerInfo.minLod = 0.0f;
    samplerInfo.maxLod = 0.0f;

    if (vkCreateSampler(renderer->getDevice(), &samplerInfo, nullptr, &m_Sampler) != VK_SUCCESS) {
        return false;
    }
    return true;
}

Texture::~Texture() {
    if (m_Sampler && m_MemoryManager) {
        vkDestroySampler(m_MemoryManager->getDevice(), m_Sampler, nullptr);
    }
}

Texture::Texture(Texture&& other) noexcept
    : m_Image(std::move(other.m_Image)),
      m_MemoryManager(other.m_MemoryManager),
      m_Sampler(other.m_Sampler),
      m_Width(other.m_Width),
      m_Height(other.m_Height),
      m_Format(other.m_Format),
      m_Path(std::move(other.m_Path)),
      m_MemorySize(other.m_MemorySize) {
    other.m_Sampler = VK_NULL_HANDLE;
    other.m_MemoryManager = nullptr;
}

Texture& Texture::operator=(Texture&& other) noexcept {
    if (this != &other) {
        if (m_Sampler && m_MemoryManager) {
            vkDestroySampler(m_MemoryManager->getDevice(), m_Sampler, nullptr);
        }
        m_Image = std::move(other.m_Image);
        m_MemoryManager = other.m_MemoryManager;
        m_Sampler = other.m_Sampler;
        m_Width = other.m_Width;
        m_Height = other.m_Height;
        m_Format = other.m_Format;
        m_Path = std::move(other.m_Path);
        m_MemorySize = other.m_MemorySize;
        other.m_Sampler = VK_NULL_HANDLE;
        other.m_MemoryManager = nullptr;
    }
    return *this;
}

Mesh::~Mesh() = default;

Mesh::Mesh(Mesh&& other) noexcept
    : m_VertexBuffer(std::move(other.m_VertexBuffer)),
      m_IndexBuffer(std::move(other.m_IndexBuffer)),
      m_MemoryManager(other.m_MemoryManager),
      m_VertexCount(other.m_VertexCount),
      m_IndexCount(other.m_IndexCount),
      m_Submeshes(std::move(other.m_Submeshes)),
      m_Path(std::move(other.m_Path)),
      m_MemorySize(other.m_MemorySize) {
    other.m_MemoryManager = nullptr;
    other.m_VertexCount = 0;
    other.m_IndexCount = 0;
    other.m_MemorySize = 0;
}

Mesh& Mesh::operator=(Mesh&& other) noexcept {
    if (this != &other) {
        m_VertexBuffer = std::move(other.m_VertexBuffer);
        m_IndexBuffer = std::move(other.m_IndexBuffer);
        m_MemoryManager = other.m_MemoryManager;
        m_VertexCount = other.m_VertexCount;
        m_IndexCount = other.m_IndexCount;
        m_Submeshes = std::move(other.m_Submeshes);
        m_Path = std::move(other.m_Path);
        m_MemorySize = other.m_MemorySize;
        other.m_MemoryManager = nullptr;
        other.m_VertexCount = 0;
        other.m_IndexCount = 0;
        other.m_MemorySize = 0;
    }
    return *this;
}

void Mesh::setVertexData(void* data, size_t size) {
    if (!m_MemoryManager) return;

    m_VertexBuffer.destroy();
    m_VertexBuffer = Buffer(*m_MemoryManager, size, 
        VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_VERTEX_BUFFER_BIT, 
        VMA_MEMORY_USAGE_GPU_ONLY);

    if (m_VertexBuffer.isValid()) {
        void* mapped = m_VertexBuffer.map();
        if (mapped) {
            memcpy(mapped, data, size);
            m_VertexBuffer.unmap();
            m_MemorySize = size;
        }
    }
}

void Mesh::setIndexData(void* data, size_t count) {
    if (!m_MemoryManager) return;

    m_IndexCount = static_cast<uint32_t>(count);
    VkDeviceSize bufferSize = count * sizeof(uint32_t);
    
    m_IndexBuffer.destroy();
    m_IndexBuffer = Buffer(*m_MemoryManager, bufferSize, 
        VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_INDEX_BUFFER_BIT, 
        VMA_MEMORY_USAGE_GPU_ONLY);

    if (m_IndexBuffer.isValid()) {
        void* mapped = m_IndexBuffer.map();
        if (mapped) {
            memcpy(mapped, data, static_cast<size_t>(bufferSize));
            m_IndexBuffer.unmap();
        }
    }
}

}
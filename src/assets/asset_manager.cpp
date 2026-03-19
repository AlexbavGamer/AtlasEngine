#define STB_IMAGE_IMPLEMENTATION
#include <stb_image.h>

#include "asset_manager.h"
#include "../renderer/renderer.h"
#include <stdexcept>

namespace Atlas {

static Renderer* g_Renderer = nullptr;

void AssetManager::setRenderer(Renderer* renderer) {
    g_Renderer = renderer;
}

AssetManager::AssetManager() = default;

AssetManager::~AssetManager() {
    unloadAll();
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
    if (auto it = m_Textures.find(id); it != m_Textures.end()) {
        return it->second;
    }

    if (!g_Renderer) {
        return nullptr;
    }

    int texWidth, texHeight, texChannels;
    stbi_uc* pixels = stbi_load(path.c_str(), &texWidth, &texHeight, &texChannels, STBI_rgb_alpha);
    
    if (!pixels) {
        return nullptr;
    }

    auto texture = std::make_shared<Texture>();
    texture->m_Path = path;
    texture->m_Width = static_cast<uint32_t>(texWidth);
    texture->m_Height = static_cast<uint32_t>(texHeight);
    texture->m_Format = VK_FORMAT_R8G8B8A8_SRGB;

    VkDeviceSize imageSize = static_cast<VkDeviceSize>(texWidth * texHeight * 4);

    VkBuffer stagingBuffer;
    VkDeviceMemory stagingMemory;
    
    VkBufferCreateInfo bufferInfo{};
    bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    bufferInfo.size = imageSize;
    bufferInfo.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
    bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    if (vkCreateBuffer(g_Renderer->getDevice(), &bufferInfo, nullptr, &stagingBuffer) != VK_SUCCESS) {
        stbi_image_free(pixels);
        return nullptr;
    }

    VkMemoryRequirements memRequirements;
    vkGetBufferMemoryRequirements(g_Renderer->getDevice(), stagingBuffer, &memRequirements);

    VkMemoryAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    allocInfo.allocationSize = memRequirements.size;
    allocInfo.memoryTypeIndex = g_Renderer->findMemoryType(
        memRequirements.memoryTypeBits, 
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT
    );

    if (vkAllocateMemory(g_Renderer->getDevice(), &allocInfo, nullptr, &stagingMemory) != VK_SUCCESS) {
        vkDestroyBuffer(g_Renderer->getDevice(), stagingBuffer, nullptr);
        stbi_image_free(pixels);
        return nullptr;
    }

    vkBindBufferMemory(g_Renderer->getDevice(), stagingBuffer, stagingMemory, 0);

    void* data;
    vkMapMemory(g_Renderer->getDevice(), stagingMemory, 0, imageSize, 0, &data);
    memcpy(data, pixels, static_cast<size_t>(imageSize));
    vkUnmapMemory(g_Renderer->getDevice(), stagingMemory);

    stbi_image_free(pixels);

    VkImageCreateInfo imageInfo{};
    imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    imageInfo.imageType = VK_IMAGE_TYPE_2D;
    imageInfo.extent.width = texWidth;
    imageInfo.extent.height = texHeight;
    imageInfo.extent.depth = 1;
    imageInfo.mipLevels = 1;
    imageInfo.arrayLayers = 1;
    imageInfo.format = VK_FORMAT_R8G8B8A8_SRGB;
    imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
    imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    imageInfo.usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
    imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
    imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    if (vkCreateImage(g_Renderer->getDevice(), &imageInfo, nullptr, &texture->m_Image) != VK_SUCCESS) {
        vkFreeMemory(g_Renderer->getDevice(), stagingMemory, nullptr);
        vkDestroyBuffer(g_Renderer->getDevice(), stagingBuffer, nullptr);
        return nullptr;
    }

    vkGetImageMemoryRequirements(g_Renderer->getDevice(), texture->m_Image, &memRequirements);

    allocInfo.allocationSize = memRequirements.size;
    allocInfo.memoryTypeIndex = g_Renderer->findMemoryType(
        memRequirements.memoryTypeBits,
        VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT
    );

    if (vkAllocateMemory(g_Renderer->getDevice(), &allocInfo, nullptr, &texture->m_ImageMemory) != VK_SUCCESS) {
        vkDestroyImage(g_Renderer->getDevice(), texture->m_Image, nullptr);
        vkFreeMemory(g_Renderer->getDevice(), stagingMemory, nullptr);
        vkDestroyBuffer(g_Renderer->getDevice(), stagingBuffer, nullptr);
        return nullptr;
    }

    vkBindImageMemory(g_Renderer->getDevice(), texture->m_Image, texture->m_ImageMemory, 0);

    VkCommandBuffer commandBuffer = g_Renderer->getCurrentCommandBuffer();
    
    VkImageMemoryBarrier barrier{};
    barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    barrier.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    barrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.image = texture->m_Image;
    barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    barrier.subresourceRange.baseMipLevel = 0;
    barrier.subresourceRange.levelCount = 1;
    barrier.subresourceRange.baseArrayLayer = 0;
    barrier.subresourceRange.layerCount = 1;
    barrier.srcAccessMask = 0;
    barrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;

    vkCmdPipelineBarrier(commandBuffer, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 0, nullptr, 1, &barrier);

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

    vkCmdCopyBufferToImage(commandBuffer, stagingBuffer, texture->m_Image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);

    barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    barrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;

    vkCmdPipelineBarrier(commandBuffer, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0, 0, nullptr, 0, nullptr, 1, &barrier);

    vkFreeMemory(g_Renderer->getDevice(), stagingMemory, nullptr);
    vkDestroyBuffer(g_Renderer->getDevice(), stagingBuffer, nullptr);

    VkImageViewCreateInfo viewInfo{};
    viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    viewInfo.image = texture->m_Image;
    viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
    viewInfo.format = VK_FORMAT_R8G8B8A8_SRGB;
    viewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    viewInfo.subresourceRange.baseMipLevel = 0;
    viewInfo.subresourceRange.levelCount = 1;
    viewInfo.subresourceRange.baseArrayLayer = 0;
    viewInfo.subresourceRange.layerCount = 1;

    if (vkCreateImageView(g_Renderer->getDevice(), &viewInfo, nullptr, &texture->m_ImageView) != VK_SUCCESS) {
        vkFreeMemory(g_Renderer->getDevice(), texture->m_ImageMemory, nullptr);
        vkDestroyImage(g_Renderer->getDevice(), texture->m_Image, nullptr);
        return nullptr;
    }

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
    vkGetPhysicalDeviceProperties(g_Renderer->getPhysicalDevice(), &properties);
    samplerInfo.maxAnisotropy = properties.limits.maxSamplerAnisotropy;
    
    samplerInfo.borderColor = VK_BORDER_COLOR_INT_OPAQUE_BLACK;
    samplerInfo.unnormalizedCoordinates = VK_FALSE;
    samplerInfo.compareEnable = VK_FALSE;
    samplerInfo.compareOp = VK_COMPARE_OP_ALWAYS;
    samplerInfo.mipLodBias = 0.0f;
    samplerInfo.minLod = 0.0f;
    samplerInfo.maxLod = 0.0f;

    if (vkCreateSampler(g_Renderer->getDevice(), &samplerInfo, nullptr, &texture->m_Sampler) != VK_SUCCESS) {
        vkDestroyImageView(g_Renderer->getDevice(), texture->m_ImageView, nullptr);
        vkFreeMemory(g_Renderer->getDevice(), texture->m_ImageMemory, nullptr);
        vkDestroyImage(g_Renderer->getDevice(), texture->m_Image, nullptr);
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

Texture::~Texture() {
    if (m_Sampler && g_Renderer) {
        vkDestroySampler(g_Renderer->getDevice(), m_Sampler, nullptr);
    }
    if (m_ImageView && g_Renderer) {
        vkDestroyImageView(g_Renderer->getDevice(), m_ImageView, nullptr);
    }
    if (m_Image && g_Renderer) {
        vkDestroyImage(g_Renderer->getDevice(), m_Image, nullptr);
    }
    if (m_ImageMemory && g_Renderer) {
        vkFreeMemory(g_Renderer->getDevice(), m_ImageMemory, nullptr);
    }
}

Mesh::~Mesh() {
    if (m_VertexBuffer && g_Renderer) {
        vkDestroyBuffer(g_Renderer->getDevice(), m_VertexBuffer, nullptr);
    }
    if (m_VertexMemory && g_Renderer) {
        vkFreeMemory(g_Renderer->getDevice(), m_VertexMemory, nullptr);
    }
    if (m_IndexBuffer && g_Renderer) {
        vkDestroyBuffer(g_Renderer->getDevice(), m_IndexBuffer, nullptr);
    }
    if (m_IndexMemory && g_Renderer) {
        vkFreeMemory(g_Renderer->getDevice(), m_IndexMemory, nullptr);
    }
}

void Mesh::setVertexData(void* data, size_t size) {
    if (m_VertexBuffer && g_Renderer) {
        vkDestroyBuffer(g_Renderer->getDevice(), m_VertexBuffer, nullptr);
    }
    if (m_VertexMemory && g_Renderer) {
        vkFreeMemory(g_Renderer->getDevice(), m_VertexMemory, nullptr);
    }

    VkBufferCreateInfo bufferInfo{};
    bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    bufferInfo.size = size;
    bufferInfo.usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_VERTEX_BUFFER_BIT;
    bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    vkCreateBuffer(g_Renderer->getDevice(), &bufferInfo, nullptr, &m_VertexBuffer);

    VkMemoryRequirements memRequirements;
    vkGetBufferMemoryRequirements(g_Renderer->getDevice(), m_VertexBuffer, &memRequirements);

    VkMemoryAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    allocInfo.allocationSize = memRequirements.size;
    allocInfo.memoryTypeIndex = g_Renderer->findMemoryType(
        memRequirements.memoryTypeBits,
        VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT
    );

    vkAllocateMemory(g_Renderer->getDevice(), &allocInfo, nullptr, &m_VertexMemory);
    vkBindBufferMemory(g_Renderer->getDevice(), m_VertexBuffer, m_VertexMemory, 0);

    m_MemorySize = size;
}

void Mesh::setIndexData(void* data, size_t count) {
    if (m_IndexBuffer && g_Renderer) {
        vkDestroyBuffer(g_Renderer->getDevice(), m_IndexBuffer, nullptr);
    }
    if (m_IndexMemory && g_Renderer) {
        vkFreeMemory(g_Renderer->getDevice(), m_IndexMemory, nullptr);
    }

    m_IndexCount = static_cast<uint32_t>(count);
    VkDeviceSize bufferSize = count * sizeof(uint32_t);

    VkBufferCreateInfo bufferInfo{};
    bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    bufferInfo.size = bufferSize;
    bufferInfo.usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_INDEX_BUFFER_BIT;
    bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    vkCreateBuffer(g_Renderer->getDevice(), &bufferInfo, nullptr, &m_IndexBuffer);

    VkMemoryRequirements memRequirements;
    vkGetBufferMemoryRequirements(g_Renderer->getDevice(), m_IndexBuffer, &memRequirements);

    VkMemoryAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    allocInfo.allocationSize = memRequirements.size;
    allocInfo.memoryTypeIndex = g_Renderer->findMemoryType(
        memRequirements.memoryTypeBits,
        VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT
    );

    vkAllocateMemory(g_Renderer->getDevice(), &allocInfo, nullptr, &m_IndexMemory);
    vkBindBufferMemory(g_Renderer->getDevice(), m_IndexBuffer, m_IndexMemory, 0);
}

}

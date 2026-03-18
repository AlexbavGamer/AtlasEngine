#define VMA_IMPLEMENTATION
#include "memory_manager.h"
#include <stdexcept>

namespace Atlas {

// ============================================================================
// MemoryManager
// ============================================================================

MemoryManager::MemoryManager(VkPhysicalDevice physicalDevice, VkDevice device)
    : m_PhysicalDevice(physicalDevice), m_Device(device) {

    VmaAllocatorCreateInfo createInfo{};
    createInfo.physicalDevice = physicalDevice;
    createInfo.device = device;
    createInfo.instance = VK_NULL_HANDLE; // Will be set automatically

    VkResult result = vmaCreateAllocator(&createInfo, &m_Allocator);
    if (result != VK_SUCCESS) {
        throw std::runtime_error("Failed to create VMA allocator: " + std::to_string(result));
    }
}

MemoryManager::~MemoryManager() {
    if (m_Allocator != VK_NULL_HANDLE) {
        vmaDestroyAllocator(m_Allocator);
    }
}

MemoryManager::Allocation MemoryManager::allocateBuffer(VkDeviceSize size, VkBufferUsageFlags usage, VmaMemoryUsage memoryUsage) {
    Allocation allocation{};
    allocation.size = size;

    VkBufferCreateInfo bufferInfo{};
    bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    bufferInfo.size = size;
    bufferInfo.usage = usage;
    bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    VmaAllocationCreateInfo allocInfo{};
    allocInfo.usage = memoryUsage;

    VkResult result = vmaCreateBuffer(m_Allocator, &bufferInfo, &allocInfo, 
        &allocation.buffer, &allocation.vmaAllocation, &allocation.allocationInfo);
    
    if (result != VK_SUCCESS) {
        throw std::runtime_error("Failed to allocate buffer: " + std::to_string(result));
    }

    m_TotalAllocatedMemory += size;
    return allocation;
}

MemoryManager::Allocation MemoryManager::allocateImage(const VkImageCreateInfo& imageInfo, VmaMemoryUsage memoryUsage) {
    Allocation allocation{};
    allocation.size = imageInfo.extent.width * imageInfo.extent.height * 4; // Approximate

    VmaAllocationCreateInfo allocInfo{};
    allocInfo.usage = memoryUsage;

    VkResult result = vmaCreateImage(m_Allocator, &imageInfo, &allocInfo,
        &allocation.image, &allocation.vmaAllocation, &allocation.allocationInfo);

    if (result != VK_SUCCESS) {
        throw std::runtime_error("Failed to allocate image: " + std::to_string(result));
    }

    m_TotalAllocatedMemory += allocation.size;
    return allocation;
}

void MemoryManager::free(Allocation& allocation) {
    if (allocation.buffer != VK_NULL_HANDLE) {
        vmaDestroyBuffer(m_Allocator, allocation.buffer, allocation.vmaAllocation);
        allocation.buffer = VK_NULL_HANDLE;
    }
    if (allocation.image != VK_NULL_HANDLE) {
        vmaDestroyImage(m_Allocator, allocation.image, allocation.vmaAllocation);
        allocation.image = VK_NULL_HANDLE;
    }
    allocation.vmaAllocation = VK_NULL_HANDLE;
    m_TotalAllocatedMemory -= allocation.size;
}

void* MemoryManager::map(Allocation& allocation) {
    if (allocation.mapped) {
        return allocation.mappedData;
    }

    void* data = nullptr;
    vmaMapMemory(m_Allocator, allocation.vmaAllocation, &data);
    allocation.mappedData = data;
    allocation.mapped = true;
    return data;
}

void MemoryManager::unmap(Allocation& allocation) {
    if (allocation.mapped) {
        vmaUnmapMemory(m_Allocator, allocation.vmaAllocation);
        allocation.mappedData = nullptr;
        allocation.mapped = false;
    }
}

void MemoryManager::flush(Allocation& allocation) {
    vmaFlushAllocation(m_Allocator, allocation.vmaAllocation, 0, VK_WHOLE_SIZE);
}

// ============================================================================
// Buffer
// ============================================================================

Buffer::Buffer(MemoryManager& memoryManager, VkDeviceSize size, VkBufferUsageFlags usage, VmaMemoryUsage memoryUsage)
    : m_MemoryManager(&memoryManager) {
    m_Allocation = memoryManager.allocateBuffer(size, usage, memoryUsage);
}

Buffer::~Buffer() {
    destroy();
}

Buffer::Buffer(Buffer&& other) noexcept
    : m_MemoryManager(other.m_MemoryManager),
      m_Allocation(other.m_Allocation) {
    other.m_MemoryManager = nullptr;
    other.m_Allocation = {};
}

Buffer& Buffer::operator=(Buffer&& other) noexcept {
    if (this != &other) {
        destroy();
        m_MemoryManager = other.m_MemoryManager;
        m_Allocation = other.m_Allocation;
        other.m_MemoryManager = nullptr;
        other.m_Allocation = {};
    }
    return *this;
}

void Buffer::destroy() {
    if (m_MemoryManager && m_Allocation.buffer != VK_NULL_HANDLE) {
        m_MemoryManager->free(m_Allocation);
        m_Allocation = {};
    }
}

void* Buffer::map() {
    if (m_MemoryManager) {
        return m_MemoryManager->map(m_Allocation);
    }
    return nullptr;
}

void Buffer::unmap() {
    if (m_MemoryManager) {
        m_MemoryManager->unmap(m_Allocation);
    }
}

void Buffer::flush() {
    if (m_MemoryManager) {
        m_MemoryManager->flush(m_Allocation);
    }
}

// ============================================================================
// Image
// ============================================================================

Image::Image(MemoryManager& memoryManager, const VkImageCreateInfo& imageInfo, VmaMemoryUsage memoryUsage)
    : m_MemoryManager(&memoryManager) {
    m_Allocation = memoryManager.allocateImage(imageInfo, memoryUsage);
    m_Width = imageInfo.extent.width;
    m_Height = imageInfo.extent.height;
    m_Format = imageInfo.format;
    createImageView(imageInfo.format);
}

Image::~Image() {
    destroy();
}

Image::Image(Image&& other) noexcept
    : m_MemoryManager(other.m_MemoryManager),
      m_Allocation(other.m_Allocation),
      m_ImageView(other.m_ImageView),
      m_Format(other.m_Format),
      m_Width(other.m_Width),
      m_Height(other.m_Height) {
    other.m_MemoryManager = nullptr;
    other.m_Allocation = {};
    other.m_ImageView = VK_NULL_HANDLE;
}

Image& Image::operator=(Image&& other) noexcept {
    if (this != &other) {
        destroy();
        m_MemoryManager = other.m_MemoryManager;
        m_Allocation = other.m_Allocation;
        m_ImageView = other.m_ImageView;
        m_Format = other.m_Format;
        m_Width = other.m_Width;
        m_Height = other.m_Height;
        other.m_MemoryManager = nullptr;
        other.m_Allocation = {};
        other.m_ImageView = VK_NULL_HANDLE;
    }
    return *this;
}

void Image::destroy() {
    if (m_MemoryManager && m_Allocation.image != VK_NULL_HANDLE) {
        m_MemoryManager->free(m_Allocation);
        m_Allocation = {};
    }
    m_ImageView = VK_NULL_HANDLE;
}

void Image::createImageView(VkFormat format) {
    VkImageViewCreateInfo viewInfo{};
    viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    viewInfo.image = m_Allocation.image;
    viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
    viewInfo.format = format;
    viewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    viewInfo.subresourceRange.baseMipLevel = 0;
    viewInfo.subresourceRange.levelCount = 1;
    viewInfo.subresourceRange.baseArrayLayer = 0;
    viewInfo.subresourceRange.layerCount = 1;

    // TODO: Get device from memory manager
    // vkCreateImageView(device, &viewInfo, nullptr, &m_ImageView);
}

}

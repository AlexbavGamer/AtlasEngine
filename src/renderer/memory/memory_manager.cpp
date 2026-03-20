#define VMA_IMPLEMENTATION
#include "memory_manager.h"
#include <stdexcept>

namespace Atlas {

// ============================================================================
// MemoryManager
// ============================================================================

MemoryManager::MemoryManager(VkInstance instance, VkPhysicalDevice physicalDevice, VkDevice device, uint32_t vulkanApiVersion)
    : m_Instance(instance)
    , m_PhysicalDevice(physicalDevice)
    , m_Device(device) {

    VmaAllocatorCreateInfo createInfo{};
    createInfo.instance = instance;
    createInfo.physicalDevice = physicalDevice;
    createInfo.device = device;
    createInfo.vulkanApiVersion = vulkanApiVersion;

    VkResult result = vmaCreateAllocator(&createInfo, &m_Allocator);
    if (result != VK_SUCCESS) {
        throw std::runtime_error("Failed to create VMA allocator: " + std::to_string(result));
    }
}

MemoryManager::~MemoryManager() {
    if (m_Allocator != VK_NULL_HANDLE) {
        vmaDestroyAllocator(m_Allocator);
        m_Allocator = VK_NULL_HANDLE;
    }
}

MemoryManager::Allocation MemoryManager::allocateBuffer(VkDeviceSize size, VkBufferUsageFlags usage, VmaMemoryUsage memoryUsage) {
    Allocation allocation{};
    allocation.type = Allocation::Type::Buffer;
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

    m_TotalAllocatedMemory += allocation.allocationInfo.size;
    return allocation;
}

MemoryManager::Allocation MemoryManager::allocateImage(const VkImageCreateInfo& imageInfo, VmaMemoryUsage memoryUsage) {
    Allocation allocation{};
    allocation.type = Allocation::Type::Image;

    VmaAllocationCreateInfo allocInfo{};
    allocInfo.usage = memoryUsage;

    VkResult result = vmaCreateImage(m_Allocator, &imageInfo, &allocInfo,
        &allocation.image, &allocation.vmaAllocation, &allocation.allocationInfo);

    if (result != VK_SUCCESS) {
        throw std::runtime_error("Failed to allocate image: " + std::to_string(result));
    }

    allocation.size = allocation.allocationInfo.size;
    m_TotalAllocatedMemory += allocation.size;
    return allocation;
}

void MemoryManager::free(Allocation& allocation) {
    if (allocation.type == Allocation::Type::Buffer && allocation.buffer != VK_NULL_HANDLE) {
        vmaDestroyBuffer(m_Allocator, allocation.buffer, allocation.vmaAllocation);
        allocation.buffer = VK_NULL_HANDLE;
    }
    else if (allocation.type == Allocation::Type::Image && allocation.image != VK_NULL_HANDLE) {
        vmaDestroyImage(m_Allocator, allocation.image, allocation.vmaAllocation);
        allocation.image = VK_NULL_HANDLE;
    }
    allocation.vmaAllocation = VK_NULL_HANDLE;
    m_TotalAllocatedMemory -= allocation.size;
    allocation.size = 0;
}

VkImageView MemoryManager::createImageView(VkImage image, VkFormat format, VkImageAspectFlags aspectMask) {
    VkImageViewCreateInfo viewInfo{};
    viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    viewInfo.image = image;
    viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
    viewInfo.format = format;
    viewInfo.subresourceRange.aspectMask = aspectMask;
    viewInfo.subresourceRange.baseMipLevel = 0;
    viewInfo.subresourceRange.levelCount = VK_REMAINING_MIP_LEVELS;
    viewInfo.subresourceRange.baseArrayLayer = 0;
    viewInfo.subresourceRange.layerCount = VK_REMAINING_ARRAY_LAYERS;

    VkImageView imageView = VK_NULL_HANDLE;
    VkResult result = vkCreateImageView(m_Device, &viewInfo, nullptr, &imageView);
    if (result != VK_SUCCESS) {
        throw std::runtime_error("Failed to create image view: " + std::to_string(result));
    }
    return imageView;
}

void MemoryManager::destroyImageView(VkImageView imageView) {
    if (imageView != VK_NULL_HANDLE) {
        vkDestroyImageView(m_Device, imageView, nullptr);
    }
}

void* MemoryManager::map(Allocation& allocation) {
    if (allocation.mapped) {
        return allocation.mappedData;
    }

    if (allocation.vmaAllocation == VK_NULL_HANDLE) {
        return nullptr;
    }

    void* data = nullptr;
    VkResult result = vmaMapMemory(m_Allocator, allocation.vmaAllocation, &data);
    if (result != VK_SUCCESS) {
        return nullptr;
    }

    allocation.mappedData = data;
    allocation.mapped = true;
    return data;
}

void MemoryManager::unmap(Allocation& allocation) {
    if (allocation.mapped && allocation.vmaAllocation != VK_NULL_HANDLE) {
        vmaUnmapMemory(m_Allocator, allocation.vmaAllocation);
        allocation.mappedData = nullptr;
        allocation.mapped = false;
    }
}

bool MemoryManager::flush(Allocation& allocation, VkDeviceSize offset, VkDeviceSize size) {
    if (allocation.vmaAllocation == VK_NULL_HANDLE) {
        return false;
    }

    VkResult result = vmaFlushAllocation(m_Allocator, allocation.vmaAllocation, offset, size);
    return result == VK_SUCCESS;
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

bool Buffer::flush(VkDeviceSize offset, VkDeviceSize size) {
    if (m_MemoryManager) {
        return m_MemoryManager->flush(m_Allocation, offset, size);
    }
    return false;
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

    m_ImageView = memoryManager.createImageView(m_Allocation.image, m_Format);
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
    if (m_MemoryManager) {
        if (m_ImageView != VK_NULL_HANDLE) {
            m_MemoryManager->destroyImageView(m_ImageView);
            m_ImageView = VK_NULL_HANDLE;
        }
        if (m_Allocation.image != VK_NULL_HANDLE) {
            m_MemoryManager->free(m_Allocation);
            m_Allocation = {};
        }
    }
}

}
#pragma once

#include <vulkan/vulkan.h>
#include <vk_mem_alloc.h>
#include <memory>

namespace Atlas {

class MemoryManager {
public:
    MemoryManager(VkPhysicalDevice physicalDevice, VkDevice device);
    ~MemoryManager();

    struct Allocation {
        VkBuffer buffer = VK_NULL_HANDLE;
        VkImage image = VK_NULL_HANDLE;
        VmaAllocation vmaAllocation = VK_NULL_HANDLE;
        VmaAllocationInfo allocationInfo{};
        VkDeviceSize size = 0;
        bool mapped = false;
        void* mappedData = nullptr;
    };

    Allocation allocateBuffer(VkDeviceSize size, VkBufferUsageFlags usage, VmaMemoryUsage memoryUsage);
    Allocation allocateImage(const VkImageCreateInfo& imageInfo, VmaMemoryUsage memoryUsage);
    void free(Allocation& allocation);

    void* map(Allocation& allocation);
    void unmap(Allocation& allocation);

    void flush(Allocation& allocation);

    VkDeviceSize getTotalAllocatedMemory() const { return m_TotalAllocatedMemory; }

private:
    VmaAllocator m_Allocator = VK_NULL_HANDLE;
    VkDevice m_Device = VK_NULL_HANDLE;
    VkPhysicalDevice m_PhysicalDevice = VK_NULL_HANDLE;
    VkDeviceSize m_TotalAllocatedMemory = 0;
};

class Buffer {
public:
    Buffer() = default;
    Buffer(MemoryManager& memoryManager, VkDeviceSize size, VkBufferUsageFlags usage, VmaMemoryUsage memoryUsage);
    ~Buffer();

    Buffer(const Buffer&) = delete;
    Buffer& operator=(const Buffer&) = delete;

    Buffer(Buffer&& other) noexcept;
    Buffer& operator=(Buffer&& other) noexcept;

    void destroy();

    VkBuffer getBuffer() const { return m_Allocation.buffer; }
    VkDeviceMemory getMemory() const { return m_Allocation.vmaAllocation ? VK_NULL_HANDLE : VK_NULL_HANDLE; }
    VkDeviceSize getSize() const { return m_Allocation.size; }
    bool isValid() const { return m_Allocation.buffer != VK_NULL_HANDLE; }

    void* map();
    void unmap();
    void flush();

    template<typename T>
    T* mapAs() { return static_cast<T*>(map()); }

private:
    MemoryManager* m_MemoryManager = nullptr;
    MemoryManager::Allocation m_Allocation{};
};

class Image {
public:
    Image() = default;
    Image(MemoryManager& memoryManager, const VkImageCreateInfo& imageInfo, VmaMemoryUsage memoryUsage);
    ~Image();

    Image(const Image&) = delete;
    Image& operator=(const Image&) = delete;

    Image(Image&& other) noexcept;
    Image& operator=(Image&& other) noexcept;

    void destroy();

    VkImage getImage() const { return m_Allocation.image; }
    VkImageView getImageView() const { return m_ImageView; }
    VkFormat getFormat() const { return m_Format; }
    uint32_t getWidth() const { return m_Width; }
    uint32_t getHeight() const { return m_Height; }
    bool isValid() const { return m_Allocation.image != VK_NULL_HANDLE; }

private:
    void createImageView(VkFormat format);

    MemoryManager* m_MemoryManager = nullptr;
    MemoryManager::Allocation m_Allocation{};
    VkImageView m_ImageView = VK_NULL_HANDLE;
    VkFormat m_Format = VK_FORMAT_UNDEFINED;
    uint32_t m_Width = 0;
    uint32_t m_Height = 0;
};

}

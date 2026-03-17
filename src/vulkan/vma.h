#pragma once

#include <vulkan/vulkan.h>
#include <vk_mem_alloc.h>

#include <memory>

class VmaAllocator {
public:
    VmaAllocator() = default;
    ~VmaAllocator() { destroy(); }

    void init(VkPhysicalDevice physicalDevice, VkDevice device, VkInstance instance) {
        VmaAllocatorCreateInfo allocatorInfo = {};
        allocatorInfo.physicalDevice = physicalDevice;
        allocatorInfo.device = device;
        allocatorInfo.instance = instance;
        allocatorInfo.vulkanApiVersion = VK_API_VERSION_1_0;

        vmaCreateAllocator(&allocatorInfo, &allocator);
    }

    void destroy() {
        if (allocator != VK_NULL_HANDLE) {
            vmaDestroyAllocator(allocator);
            allocator = VK_NULL_HANDLE;
        }
    }

    VkResult createBuffer(VkBufferCreateInfo* bufferInfo, VmaAllocationCreateInfo* allocInfo, VkBuffer* buffer, VmaAllocation* allocation, VmaAllocationInfo* allocInfoOut) {
        return vmaCreateBuffer(allocator, bufferInfo, allocInfo, buffer, allocation, allocInfoOut);
    }

    VkResult createImage(VkImageCreateInfo* imageInfo, VmaAllocationCreateInfo* allocInfo, VkImage* image, VmaAllocation* allocation, VmaAllocationInfo* allocInfoOut) {
        return vmaCreateImage(allocator, imageInfo, allocInfo, image, allocation, allocInfoOut);
    }

    void destroyBuffer(VkBuffer buffer, VmaAllocation allocation) {
        vmaDestroyBuffer(allocator, buffer, allocation);
    }

    void destroyImage(VkImage image, VmaAllocation allocation) {
        vmaDestroyImage(allocator, image, allocation);
    }

    void* mapMemory(VmaAllocation allocation) {
        void* data;
        vmaMapMemory(allocator, allocation, &data);
        return data;
    }

    void unmapMemory(VmaAllocation allocation) {
        vmaUnmapMemory(allocator, allocation);
    }

    VmaAllocator getAllocator() const { return allocator; }

private:
    VmaAllocator allocator = VK_NULL_HANDLE;
};

class VmaBuffer {
public:
    VmaBuffer() = default;
    ~VmaBuffer() { destroy(); }

    VkBuffer buffer = VK_NULL_HANDLE;
    VmaAllocation allocation = VK_NULL_HANDLE;
    VmaAllocationInfo allocInfo;
    VkDeviceSize size = 0;

    bool isValid() const { return buffer != VK_NULL_HANDLE; }

    void destroy() {
        if (allocation != VK_NULL_HANDLE && buffer != VK_NULL_HANDLE) {
            vmaDestroyBuffer(allocator, buffer, allocation);
            buffer = VK_NULL_HANDLE;
            allocation = VK_NULL_HANDLE;
            size = 0;
        }
    }

    VmaAllocator* allocator = nullptr;
};

class VmaImage {
public:
    VmaImage() = default;
    ~VmaImage() { destroy(); }

    VkImage image = VK_NULL_HANDLE;
    VmaAllocation allocation = VK_NULL_HANDLE;
    VmaAllocationInfo allocInfo;
    VkImageView imageView = VK_NULL_HANDLE;
    VkSampler sampler = VK_NULL_HANDLE;

    bool isValid() const { return image != VK_NULL_HANDLE; }

    void destroy() {
        if (allocation != VK_NULL_HANDLE && image != VK_NULL_HANDLE) {
            vmaDestroyImage(allocator, image, allocation);
            image = VK_NULL_HANDLE;
            allocation = VK_NULL_HANDLE;
        }
    }

    VmaAllocator* allocator = nullptr;
};

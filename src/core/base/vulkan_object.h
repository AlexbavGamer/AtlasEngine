#pragma once

#include <vulkan/vulkan.h>
#include <functional>

namespace Atlas {

template<typename T, typename Dispatch>
class VulkanObject {
public:
    VulkanObject() : m_Handle(VK_NULL_HANDLE), m_Device(VK_NULL_HANDLE) {}

    VulkanObject(T handle, VkDevice device, std::function<void(VkDevice, T, const Dispatch*)> destroyFunc)
        : m_Handle(handle), m_Device(device), m_DestroyFunc(destroyFunc) {}

    ~VulkanObject() {
        destroy();
    }

    VulkanObject(VulkanObject&& other) noexcept
        : m_Handle(other.m_Handle), m_Device(other.m_Device), m_DestroyFunc(std::move(other.m_DestroyFunc)) {
        other.m_Handle = VK_NULL_HANDLE;
        other.m_Device = VK_NULL_HANDLE;
    }

    VulkanObject& operator=(VulkanObject&& other) noexcept {
        if (this != &other) {
            destroy();
            m_Handle = other.m_Handle;
            m_Device = other.m_Device;
            m_DestroyFunc = std::move(other.m_DestroyFunc);
            other.m_Handle = VK_NULL_HANDLE;
            other.m_Device = VK_NULL_HANDLE;
        }
        return *this;
    }

    VulkanObject(const VulkanObject&) = delete;
    VulkanObject& operator=(const VulkanObject&) = delete;

    void destroy() {
        if (m_Handle != VK_NULL_HANDLE && m_Device != VK_NULL_HANDLE && m_DestroyFunc) {
            m_DestroyFunc(m_Device, m_Handle, nullptr);
            m_Handle = VK_NULL_HANDLE;
        }
    }

    T get() const { return m_Handle; }
    operator T() const { return m_Handle; }
    bool isValid() const { return m_Handle != VK_NULL_HANDLE; }

private:
    T m_Handle;
    VkDevice m_Device;
    std::function<void(VkDevice, T, const Dispatch*)> m_DestroyFunc;
};

template<typename Dispatch = VkSystemAllocationScope>
class DeletionQueue {
public:
    template<typename T>
    void push(T handle, std::function<void(VkDevice, T, const Dispatch*)> destroyFunc, VkDevice device) {
        m_Queue.emplace_back([=]() {
            if (handle != VK_NULL_HANDLE) {
                destroyFunc(device, handle, nullptr);
            }
        });
    }

    void flush() {
        for (auto& func : m_Queue) {
            func();
        }
        m_Queue.clear();
    }

    ~DeletionQueue() {
        flush();
    }

private:
    std::vector<std::function<void()>> m_Queue;
};

#define VK_DESTROY_FUNC(name, func) \
    [](VkDevice device, name##_t handle, const VkAllocationCallbacks* pAllocator) { \
        func(device, handle, pAllocator); \
    }

using VDeleter = std::function<void(VkDevice, VkFramebuffer)>;

template<typename T>
struct VulkanDeleter {
    VulkanDeleter() : device(VK_NULL_HANDLE), deleter(nullptr) {}
    VulkanDeleter(VkDevice device, std::function<void(VkDevice, T, const VkAllocationCallbacks*)> deleter)
        : device(device), deleter(deleter) {}

    VkDevice device;
    std::function<void(VkDevice, T, const VkAllocationCallbacks*)> deleter;

    T object = VK_NULL_HANDLE;

    void destroy() {
        if (object != VK_NULL_HANDLE && deleter) {
            deleter(device, object, nullptr);
            object = VK_NULL_HANDLE;
        }
    }

    operator T() const { return object; }
    T operator*() const { return object; }
    T* put() { return &object; }
    T get() const { return object; }
    bool isValid() const { return object != VK_NULL_HANDLE; }

    void operator=(T other) { object = other; }
};

}

#pragma once

#ifdef TRACY_ENABLE
#include <tracy/Tracy.hpp>
#include <tracy/TracyVulkan.hpp>

#define TracyVkContext Tracy::VulkanContext
#define TracyVkContextName Tracy::VulkanContextName
#define TracyVkDestroy(x) Tracy::VulkanDestroy(x)

class Profiler {
public:
    static void init(VkInstance instance, VkPhysicalDevice physicalDevice, VkDevice device, uint32_t queueFamily, VkQueue queue) {
        s_context = Tracy::VulkanContext(instance, physicalDevice, device, queueFamily, queue);
    }

    static void destroy() {
        if (s_context) {
            Tracy::VulkanDestroy(s_context);
            s_context = nullptr;
        }
    }

    static TracyVkContext get() { return s_context; }

private:
    static TracyVkContext s_context;
};

#else
#define TracyVkContext void*
#define TracyVkContextName(...)
#define TracyVkDestroy(x)
#define ZoneScoped
#define ZoneScopedN(x)
#define ZoneScopedC(x, y)
#define FrameMark
#define FrameMarkStart(x)
#define FrameMarkEnd(x)

class Profiler {
public:
    static void init(VkInstance, VkPhysicalDevice, VkDevice, uint32_t, VkQueue) {}
    static void destroy() {}
    static void* get() { return nullptr; }
};
#endif

#define PROFILE_SCOPE(x) ZoneScopedN(x)
#define PROFILE_FUNCTION() ZoneScoped
#define PROFILE_FRAME() FrameMark

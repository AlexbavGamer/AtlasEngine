#pragma once

#include <vector>
#include <vulkan/vulkan.h>
#include "../ecs/ecs.h"

class Renderable {
public:
    virtual void render(VkCommandBuffer commandBuffer) = 0;
};

class Scene {
public:
    Scene(std::shared_ptr<World> world) : ecsWorld(world) {}

    void addRenderable(Renderable* renderable); // Keep for backward compatibility
    void render(VkCommandBuffer commandBuffer);

    std::shared_ptr<World> getWorld() { return ecsWorld; }

private:
    std::vector<Renderable*> renderables; // Legacy renderables
    std::shared_ptr<World> ecsWorld;
};
#pragma once
#include "ecs.h"
#include "components.h"

// Forward declaration
class VulkanEngine;

// RenderSystem - handles rendering of entities with Renderable and Transform components
class RenderSystem : public System {
public:
    VulkanEngine* engine;

    RenderSystem(VulkanEngine* eng) : engine(eng) {}

    void update(float deltaTime) override;
};
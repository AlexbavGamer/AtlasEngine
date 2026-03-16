#include "scene.h"
#include "../ecs/components.h"

void Scene::addRenderable(Renderable* renderable) {
    renderables.push_back(renderable);
}

void Scene::render(VkCommandBuffer commandBuffer) {
    // Render legacy renderables
    for (auto* renderable : renderables) {
        renderable->render(commandBuffer);
    }

    // Render ECS entities with Renderable component
    if (ecsWorld) {
        for (const auto& pair : ecsWorld->getEntities()) {
            auto entity = pair.second;
            auto renderableComp = entity->getComponent<RenderableComponent>();
            if (renderableComp && renderableComp->visible) {
                // For now, just render legacy style - will be improved with proper ECS renderables
                // TODO: Implement proper ECS-based rendering
            }
        }
    }
}
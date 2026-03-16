#pragma once

#include <functional>
#include <imgui.h>
#include <imgui_impl_vulkan.h>
#include <memory>
#include "../ecs/ecs.h"

class UIManager {
public:
    UIManager(std::shared_ptr<World> world);

    void render(ImTextureID viewportTexture);
    void setSelectedEntity(EntityID entity);
    EntityID getSelectedEntity() const;

private:
    std::shared_ptr<World> ecsWorld;
    EntityID selectedEntity = 0;

    void renderViewport(ImTextureID viewportTexture);
    void renderHierarchy();
    void renderProperties();
    void renderContentExplorer();
    void renderTransformPanel();
};

#include "scene.h"
#include "../ecs/components/components.h"
#include <algorithm>

namespace Atlas {

using namespace ECS;

entt::entity Scene::createEntity(const std::string& name) {
    entt::entity entity = m_Registry.create();
    m_Registry.emplace<Transform>(entity);
    m_Registry.emplace<Renderable>(entity);
    m_Registry.emplace<TagComponent>(entity, name);
    m_Dirty = true;
    return entity;
}

void Scene::destroyEntity(entt::entity entity) {
    m_Registry.destroy(entity);
    m_Dirty = true;
}

CameraComponent* Scene::getActiveCamera() {
    if (m_ActiveCamera != entt::null && m_Registry.valid(m_ActiveCamera)) {
        return &m_Registry.get<CameraComponent>(m_ActiveCamera);
    }
    return nullptr;
}

void Scene::setActiveCamera(entt::entity entity) {
    if (m_Registry.all_of<CameraComponent>(entity)) {
        m_ActiveCamera = entity;
    }
}

std::vector<entt::entity> Scene::getAllEntities() {
    std::vector<entt::entity> entities;
    for (auto entity : m_Registry.view<entt::entity>()) {
        entities.push_back(entity);
    }
    return entities;
}

std::vector<entt::entity> Scene::getEntitiesWithMesh() {
    return getAllEntities();
}

std::vector<entt::entity> Scene::getEntitiesWithCamera() {
    return getAllEntities();
}

std::vector<entt::entity> Scene::getEntitiesWithLight() {
    return getAllEntities();
}

}

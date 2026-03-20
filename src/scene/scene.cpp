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
    if (!m_Registry.valid(entity)) return;

    // Detach from parent
    if (m_Registry.all_of<ParentComponent>(entity)) {
        auto parent = m_Registry.get<ParentComponent>(entity).parent;
        if (m_Registry.valid(parent) && m_Registry.all_of<ChildrenComponent>(parent)) {
            auto &siblings = m_Registry.get<ChildrenComponent>(parent).children;
            siblings.erase(std::remove(siblings.begin(), siblings.end(), entity), siblings.end());
        }
        m_Registry.remove<ParentComponent>(entity);
    }

    // Recursively destroy child entities
    if (m_Registry.all_of<ChildrenComponent>(entity)) {
        auto children = m_Registry.get<ChildrenComponent>(entity).children;
        for (auto child : children) {
            destroyEntity(child);
        }
        m_Registry.remove<ChildrenComponent>(entity);
    }

    // Destroy the entity itself
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
    m_Registry.each([&](auto entity) {
        entities.push_back(entity);
    });
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

std::vector<entt::entity> Scene::getRootEntities() {
    std::vector<entt::entity> roots;
    m_Registry.each([&](auto entity) {
        if (!m_Registry.all_of<ParentComponent>(entity)) {
            roots.push_back(entity);
        }
    });
    return roots;
}

std::vector<entt::entity> Scene::getChildren(entt::entity parent) {
    if (!m_Registry.valid(parent)) return {};
    if (!m_Registry.all_of<ChildrenComponent>(parent)) return {};
    return m_Registry.get<ChildrenComponent>(parent).children;
}

void Scene::setParent(entt::entity child, entt::entity parent) {
    if (!m_Registry.valid(child) || (parent != entt::null && !m_Registry.valid(parent))) return;

    // Ensure both child and parent have transforms (needed for world-space hierarchy).
    if (!m_Registry.all_of<Transform>(child)) {
        m_Registry.emplace<Transform>(child);
    }
    if (parent != entt::null && !m_Registry.all_of<Transform>(parent)) {
        m_Registry.emplace<Transform>(parent);
    }

    // Remove existing from previous parent
    if (m_Registry.all_of<ParentComponent>(child)) {
        auto oldParent = m_Registry.get<ParentComponent>(child).parent;
        if (oldParent != entt::null && m_Registry.all_of<ChildrenComponent>(oldParent)) {
            auto &children = m_Registry.get<ChildrenComponent>(oldParent).children;
            children.erase(std::remove(children.begin(), children.end(), child), children.end());
        }
    }

    if (parent == entt::null) {
        m_Registry.remove<ParentComponent>(child);
        return;
    }

    if (!m_Registry.all_of<ParentComponent>(child)) {
        m_Registry.emplace<ParentComponent>(child, ParentComponent{parent});
    } else {
        m_Registry.get<ParentComponent>(child).parent = parent;
    }

    if (!m_Registry.all_of<ChildrenComponent>(parent)) {
        m_Registry.emplace<ChildrenComponent>(parent, ChildrenComponent{});
    }
    m_Registry.get<ChildrenComponent>(parent).children.push_back(child);
}

glm::mat4 Scene::getWorldTransform(entt::entity entity) const {
    if (!m_Registry.valid(entity)) return glm::mat4(1.0f);
    glm::mat4 local(1.0f);
    if (m_Registry.all_of<Transform>(entity)) {
        auto &t = m_Registry.get<Transform>(entity);
        local = t.getModelMatrix();
    }

    if (m_Registry.all_of<ParentComponent>(entity)) {
        auto parent = m_Registry.get<ParentComponent>(entity).parent;
        if (parent != entt::null) {
            return getWorldTransform(parent) * local;
        }
    }

    return local;
}

}

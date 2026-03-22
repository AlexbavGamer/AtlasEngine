#include "scene.h"
#include "../ecs/components/components.h"

#include <algorithm>
#include <cmath>
#include <unordered_set>
#include <vector>

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
    for (auto [entity] : m_Registry.storage<entt::entity>().each()) {
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

std::vector<entt::entity> Scene::getRootEntities() {
    std::vector<entt::entity> roots;
    for (auto [entity] : m_Registry.storage<entt::entity>().each()) {
        if (!m_Registry.all_of<ParentComponent>(entity)) {
            roots.push_back(entity);
        }
    }
    return roots;
}

const std::vector<entt::entity>& Scene::getChildren(entt::entity parent) const {
    static const std::vector<entt::entity> empty;

    if (!m_Registry.valid(parent)) return empty;
    if (!m_Registry.all_of<ChildrenComponent>(parent)) return empty;
    return m_Registry.get<ChildrenComponent>(parent).children;
}

void Scene::setParent(entt::entity child, entt::entity parent) {
    if (!m_Registry.valid(child)) return;
    if (parent != entt::null && !m_Registry.valid(parent)) return;
    if (parent == child) return;

    // Prevent cycles: parent cannot be a descendant of child.
    if (parent != entt::null) {
        entt::entity p = parent;
        while (p != entt::null && m_Registry.valid(p) && m_Registry.all_of<ParentComponent>(p)) {
            entt::entity pp = m_Registry.get<ParentComponent>(p).parent;
            if (pp == child) {
                return;
            }
            p = pp;
        }
    }

    // Ensure both child and parent have transforms (needed for world-space hierarchy).
    if (!m_Registry.all_of<Transform>(child)) {
        m_Registry.emplace<Transform>(child);
    }
    if (parent != entt::null && !m_Registry.all_of<Transform>(parent)) {
        m_Registry.emplace<Transform>(parent);
    }

    entt::entity oldParent = entt::null;
    if (m_Registry.all_of<ParentComponent>(child)) {
        oldParent = m_Registry.get<ParentComponent>(child).parent;
    }

    // No-op.
    if (oldParent == parent) {
        return;
    }

    // Remove from previous parent's children list.
    if (oldParent != entt::null && m_Registry.valid(oldParent) && m_Registry.all_of<ChildrenComponent>(oldParent)) {
        auto& children = m_Registry.get<ChildrenComponent>(oldParent).children;
        children.erase(std::remove(children.begin(), children.end(), child), children.end());
    }

    if (parent == entt::null) {
        if (m_Registry.all_of<ParentComponent>(child)) {
            m_Registry.remove<ParentComponent>(child);
        }
        m_Dirty = true;
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

    auto& list = m_Registry.get<ChildrenComponent>(parent).children;
    if (std::find(list.begin(), list.end(), child) == list.end()) {
        list.push_back(child);
    }

    m_Dirty = true;
}

bool Scene::updateWorldTransforms() {
    bool changedAny = false;
    // Ensure WorldTransform exists for every entity that has a local Transform.
    auto view = m_Registry.view<Transform>();
    for (auto e : view) {
        if (!m_Registry.all_of<WorldTransform>(e)) {
            m_Registry.emplace<WorldTransform>(e);
        }
    }

    std::unordered_set<uint32_t> visited;
    const auto approxCount = static_cast<size_t>(view.size());
    visited.reserve(approxCount);

    std::vector<entt::entity> stack;
    stack.reserve(approxCount);

    // Roots: entities with Transform but no valid parent.
    for (auto e : view) {
        entt::entity p = entt::null;
        if (m_Registry.all_of<ParentComponent>(e)) {
            p = m_Registry.get<ParentComponent>(e).parent;
        }
        if (p == entt::null || !m_Registry.valid(p)) {
            stack.push_back(e);
        }
    }

    while (!stack.empty()) {
        entt::entity e = stack.back();
        stack.pop_back();

        uint32_t id = static_cast<uint32_t>(e);
        if (visited.find(id) != visited.end()) {
            continue;
        }
        visited.insert(id);

        glm::mat4 local = m_Registry.get<Transform>(e).getModelMatrix();
        glm::mat4 world = local;

        if (m_Registry.all_of<ParentComponent>(e)) {
            entt::entity p = m_Registry.get<ParentComponent>(e).parent;
            if (p != entt::null && m_Registry.valid(p) && m_Registry.all_of<WorldTransform>(p)) {
                world = m_Registry.get<WorldTransform>(p).matrix * local;
            }
        }

        {
            auto& wt = m_Registry.get<WorldTransform>(e);
            const glm::mat4 before = wt.matrix;
            wt.matrix = world;

            const float eps = 1e-6f;
            for (int r = 0; r < 4 && !changedAny; ++r) {
                for (int c = 0; c < 4; ++c) {
                    if (std::fabs(before[r][c] - world[r][c]) > eps) {
                        changedAny = true;
                        break;
                    }
                }
            }
        }

        if (m_Registry.all_of<ChildrenComponent>(e)) {
            const auto& children = m_Registry.get<ChildrenComponent>(e).children;
            for (auto c : children) {
                if (c != entt::null && m_Registry.valid(c) && m_Registry.all_of<Transform>(c)) {
                    stack.push_back(c);
                }
            }
        }
    }

    // Fallback: handle any remaining entities (corrupted/missing children lists).
    for (auto e : view) {
        uint32_t id = static_cast<uint32_t>(e);
        if (visited.find(id) != visited.end()) {
            continue;
        }
        {
            auto& wt = m_Registry.get<WorldTransform>(e);
            const glm::mat4 world = getWorldTransform(e);
            const glm::mat4 before = wt.matrix;
            wt.matrix = world;

            const float eps = 1e-6f;
            for (int r = 0; r < 4 && !changedAny; ++r) {
                for (int c = 0; c < 4; ++c) {
                    if (std::fabs(before[r][c] - world[r][c]) > eps) {
                        changedAny = true;
                        break;
                    }
                }
            }
        }
    }

    return changedAny;
}

glm::mat4 Scene::getCachedWorldTransform(entt::entity entity) const {
    if (!m_Registry.valid(entity)) return glm::mat4(1.0f);
    if (m_Registry.all_of<WorldTransform>(entity)) {
        return m_Registry.get<WorldTransform>(entity).matrix;
    }
    return getWorldTransform(entity);
}

glm::mat4 Scene::getWorldTransform(entt::entity entity) const {
    if (!m_Registry.valid(entity)) return glm::mat4(1.0f);

    // Build local transforms up the parent chain, then multiply root->leaf.
    std::vector<glm::mat4> chain;
    chain.reserve(16);

    entt::entity e = entity;
    int depth = 0;

    while (e != entt::null && m_Registry.valid(e)) {
        glm::mat4 local(1.0f);
        if (m_Registry.all_of<Transform>(e)) {
            auto& t = m_Registry.get<Transform>(e);
            local = t.getModelMatrix();
        }
        chain.push_back(local);

        if (!m_Registry.all_of<ParentComponent>(e)) {
            break;
        }

        entt::entity p = m_Registry.get<ParentComponent>(e).parent;
        if (p == entt::null || !m_Registry.valid(p)) {
            break;
        }

        e = p;

        // Defensive guard against corrupted hierarchies.
        if (++depth > 1024) {
            break;
        }
    }

    glm::mat4 world(1.0f);
    for (auto it = chain.rbegin(); it != chain.rend(); ++it) {
        world = world * (*it);
    }
    return world;
}

}

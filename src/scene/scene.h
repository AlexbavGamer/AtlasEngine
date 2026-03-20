#pragma once

#include <entt/entt.hpp>
#include <string>
#include <memory>
#include <vector>
#include <glm/glm.hpp>

#include "../ecs/ecs.h"
#include "../ecs/components/components.h"

namespace Atlas {
using namespace ECS;
class Scene {
public:
    Scene() = default;
    ~Scene() = default;

    entt::registry& getRegistry() { return m_Registry; }
    const entt::registry& getRegistry() const { return m_Registry; }

    // Entity creation
    entt::entity createEntity(const std::string& name = "Entity");
    void destroyEntity(entt::entity entity); // recursive with child cleanup

    // Transform component
    Transform& getTransform(entt::entity entity) {
        return m_Registry.get<Transform>(entity);
    }

    bool hasTransform(entt::entity entity) const {
        return m_Registry.all_of<Transform>(entity);
    }

    // Camera
    CameraComponent* getActiveCamera();
    void setActiveCamera(entt::entity entity);

    // Hierarchy
    std::vector<entt::entity> getAllEntities();
    std::vector<entt::entity> getRootEntities();
    std::vector<entt::entity> getChildren(entt::entity parent);
    void setParent(entt::entity child, entt::entity parent);

    std::vector<entt::entity> getEntitiesWithMesh();
    std::vector<entt::entity> getEntitiesWithCamera();
    std::vector<entt::entity> getEntitiesWithLight();

    glm::mat4 getWorldTransform(entt::entity entity) const;

    // Scene name
    const std::string& getName() const { return m_Name; }
    void setName(const std::string& name) { m_Name = name; }

    // Dirty flag
    bool isDirty() const { return m_Dirty; }
    void setDirty(bool dirty) { m_Dirty = dirty; }

private:
    entt::registry m_Registry;
    std::string m_Name = "Untitled";
    bool m_Dirty = false;
    entt::entity m_ActiveCamera = entt::null;
};

}

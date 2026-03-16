#pragma once

#include <cstdint>
#include <unordered_map>
#include <memory>
#include <typeindex>
#include <vector>
#include <algorithm>

class Component {
public:
    virtual ~Component() = default;
};

using EntityID = uint32_t;

class Entity {
public:
    EntityID id;
    std::unordered_map<std::type_index, std::unique_ptr<Component>> components;

    template<typename T>
    void addComponent(std::unique_ptr<T> comp) {
        components[typeid(T)] = std::move(comp);
    }

    template<typename T>
    T* getComponent() {
        auto it = components.find(typeid(T));
        return it != components.end() ? static_cast<T*>(it->second.get()) : nullptr;
    }

    template<typename T>
    void removeComponent() {
        this->components.erase(typeid(T));
    }

    template<typename T>
    bool hasComponent() const {
        return this->components.find(typeid(T)) != this->components.end();
    }
};

class System {
public:
    virtual ~System() = default;
    virtual void update(float dt) = 0;
};

class World {
public:
    EntityID createEntity() {
        EntityID id = nextID++;
        entities[id] = std::make_shared<Entity>();
        entities[id]->id = id;
        return id;
    }

    std::shared_ptr<Entity> getEntity(EntityID id) {
        auto it = entities.find(id);
        return it != entities.end() ? it->second : nullptr;
    }

    void destroyEntity(EntityID id) {
        entities.erase(id);
    }

    void addSystem(std::shared_ptr<System> system) {
        systems.push_back(system);
    }

    void removeSystem(std::shared_ptr<System> system) {
        systems.erase(std::remove(systems.begin(), systems.end(), system), systems.end());
    }

    void update(float dt) {
        for (auto& sys : systems) {
            sys->update(dt);
        }
    }

    const std::unordered_map<EntityID, std::shared_ptr<Entity>>& getEntities() const {
        return entities;
    }

private:
    std::unordered_map<EntityID, std::shared_ptr<Entity>> entities;
    std::vector<std::shared_ptr<System>> systems;
    EntityID nextID = 0;
};
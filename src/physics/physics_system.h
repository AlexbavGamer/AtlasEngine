#pragma once

#include <cstdint>
#include <memory>

#include <entt/entt.hpp>

namespace Atlas {
class Scene;

namespace Physics {

class PhysicsSystem {
public:
    PhysicsSystem();
    ~PhysicsSystem();

    bool initialize();
    void shutdown();

    void rebuild(Scene* scene);
    void clear();
    void step(Scene* scene, float deltaTime);

    bool isInitialized() const;

private:
    struct Impl;
    std::unique_ptr<Impl> m_Impl;
};

} // namespace Physics
} // namespace Atlas

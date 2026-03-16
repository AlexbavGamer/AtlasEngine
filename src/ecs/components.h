#pragma once
#include "ecs.h"
#include <glm/glm.hpp>
#include <string>

// Transform component
class Transform : public Component {
public:
    glm::vec3 position;
    glm::vec3 rotation; // Euler angles in degrees
    glm::vec3 scale;

    Transform() : position(0.0f), rotation(0.0f), scale(1.0f) {}
    Transform(const glm::vec3& pos, const glm::vec3& rot, const glm::vec3& scl)
        : position(pos), rotation(rot), scale(scl) {}
};

// Mesh component (placeholder for now)
class Mesh : public Component {
public:
    std::string meshPath; // Path to mesh file

    Mesh() {}
    Mesh(const std::string& path) : meshPath(path) {}
};

// Renderable component (for entities that should be rendered)
class RenderableComponent : public Component {
public:
    bool visible;

    RenderableComponent() : visible(true) {}
    RenderableComponent(bool vis) : visible(vis) {}
};
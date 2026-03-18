#pragma once

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

namespace Atlas { namespace ECS {

struct Transform {
    glm::vec3 position = glm::vec3(0.0f);
    glm::vec3 rotation = glm::vec3(0.0f);  // Euler angles in radians
    glm::vec3 scale = glm::vec3(1.0f);

    glm::mat4 getModelMatrix() const {
        glm::mat4 model = glm::mat4(1.0f);
        model = glm::translate(model, position);
        model = glm::rotate(model, rotation.z, glm::vec3(0.0f, 0.0f, 1.0f));
        model = glm::rotate(model, rotation.y, glm::vec3(0.0f, 1.0f, 0.0f));
        model = glm::rotate(model, rotation.x, glm::vec3(1.0f, 0.0f, 0.0f));
        model = glm::scale(model, scale);
        return model;
    }

    glm::vec3 getForward() const {
        glm::mat4 rot = glm::rotate(glm::mat4(1.0f), rotation.z, glm::vec3(0.0f, 0.0f, 1.0f));
        rot = glm::rotate(rot, rotation.y, glm::vec3(0.0f, 1.0f, 0.0f));
        rot = glm::rotate(rot, rotation.x, glm::vec3(1.0f, 0.0f, 0.0f));
        return glm::normalize(rot * glm::vec4(0.0f, 0.0f, -1.0f, 0.0f));
    }

    glm::vec3 getRight() const {
        return glm::cross(getForward(), glm::vec3(0.0f, 1.0f, 0.0f));
    }

    glm::vec3 getUp() const {
        return glm::cross(getRight(), getForward());
    }
};

}}

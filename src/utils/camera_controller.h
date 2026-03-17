#pragma once

#include <GLFW/glfw3.h>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

class CameraController {
public:
    CameraController(GLFWwindow* window, glm::vec3& cameraPosition, glm::vec3& cameraTarget, glm::vec3& cameraUp);

    void update(float deltaTime);
    void setSpeed(float speed) { moveSpeed = speed; }
    void setSensitivity(float sens) { sensitivity = sens; }

private:
    GLFWwindow* window;
    glm::vec3& position;
    glm::vec3& target;
    glm::vec3& up;

    float moveSpeed = 5.0f;
    float sensitivity = 0.1f;
    float zoomSpeed = 1.0f;

    bool isRightMouseDown = false;
    float lastMouseX = 0.0f;
    float lastMouseY = 0.0f;

    glm::vec3 getCameraForward() const;
    glm::vec3 getCameraRight() const;
};

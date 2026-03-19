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
    void setEnabled(bool enabled) { m_Enabled = enabled; }
    bool isEnabled() const { return m_Enabled; }
    
    glm::mat4 getViewMatrix() const;
    glm::mat4 getProjMatrix() const;
    
    void setAspectRatio(float aspect) { m_AspectRatio = aspect; }

private:
    GLFWwindow* window;
    glm::vec3& position;
    glm::vec3& target;
    glm::vec3& up;

    float moveSpeed = 5.0f;
    float sensitivity = 0.1f;
    float zoomSpeed = 1.0f;
    float m_AspectRatio = 16.0f / 9.0f;
    bool m_Enabled = true;

    bool isRightMouseDown = false;
    float lastMouseX = 0.0f;
    float lastMouseY = 0.0f;

    glm::vec3 getCameraForward() const;
    glm::vec3 getCameraRight() const;
};

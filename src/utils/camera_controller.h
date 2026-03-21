#pragma once

#include <GLFW/glfw3.h>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

class CameraController {
public:
    CameraController(GLFWwindow* window, glm::vec3& cameraPosition, glm::vec3& cameraTarget, glm::vec3& cameraUp);

    void update(float deltaTime);

    // Movement
    void setSpeed(float speed) { moveSpeed = speed; }
    float getSpeed() const { return moveSpeed; }

    void setBoostMultiplier(float mul) { boostMultiplier = mul; }
    float getBoostMultiplier() const { return boostMultiplier; }

    void setSlowMultiplier(float mul) { slowMultiplier = mul; }
    float getSlowMultiplier() const { return slowMultiplier; }

    void setRequireRmbForMove(bool v) { requireRmbForMove = v; }
    bool getRequireRmbForMove() const { return requireRmbForMove; }

    // Look
    void setSensitivity(float sens) { sensitivity = sens; }
    float getSensitivity() const { return sensitivity; }

    void setLockCursorOnLook(bool v) { lockCursorOnLook = v; }
    bool getLockCursorOnLook() const { return lockCursorOnLook; }

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
    float boostMultiplier = 4.0f;
    float slowMultiplier = 0.25f;

    float sensitivity = 0.1f;
    float zoomSpeed = 1.0f;

    bool requireRmbForMove = true;
    bool lockCursorOnLook = true;
    float m_AspectRatio = 16.0f / 9.0f;
    bool m_Enabled = true;

    bool isRightMouseDown = false;
    float lastMouseX = 0.0f;
    float lastMouseY = 0.0f;

    glm::vec3 getCameraForward() const;
    glm::vec3 getCameraRight() const;
};

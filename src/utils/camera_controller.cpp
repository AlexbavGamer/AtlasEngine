#include "camera_controller.h"

CameraController::CameraController(GLFWwindow* window, glm::vec3& cameraPosition, glm::vec3& cameraTarget, glm::vec3& cameraUp)
    : window(window), position(cameraPosition), target(cameraTarget), up(cameraUp) {}

glm::vec3 CameraController::getCameraForward() const {
    return glm::normalize(target - position);
}

glm::vec3 CameraController::getCameraRight() const {
    return glm::normalize(glm::cross(getCameraForward(), up));
}

void CameraController::update(float deltaTime) {
    if (!window) return;

    double mouseX, mouseY;
    glfwGetCursorPos(window, &mouseX, &mouseY);

    bool rightMouseDown = glfwGetMouseButton(window, GLFW_MOUSE_BUTTON_RIGHT) == GLFW_PRESS;

    if (rightMouseDown && !isRightMouseDown) {
        isRightMouseDown = true;
        lastMouseX = static_cast<float>(mouseX);
        lastMouseY = static_cast<float>(mouseY);
    } else if (!rightMouseDown && isRightMouseDown) {
        isRightMouseDown = false;
    }

    if (isRightMouseDown) {
        float deltaX = static_cast<float>(mouseX) - lastMouseX;
        float deltaY = static_cast<float>(mouseY) - lastMouseY;

        float yaw = glm::radians(deltaX * sensitivity);
        float pitch = glm::radians(-deltaY * sensitivity);

        glm::vec3 forward = getCameraForward();
        glm::vec3 right = getCameraRight();

        glm::mat4 rotationY = glm::rotate(glm::mat4(1.0f), yaw, up);
        glm::mat4 rotationX = glm::rotate(glm::mat4(1.0f), pitch, right);

        glm::vec4 newTarget = rotationX * rotationY * glm::vec4(target - position, 1.0f);
        target = position + glm::vec3(newTarget);

        lastMouseX = static_cast<float>(mouseX);
        lastMouseY = static_cast<float>(mouseY);
    }

    float speed = moveSpeed * deltaTime;
    glm::vec3 forward = getCameraForward();
    glm::vec3 right = getCameraRight();

    if (glfwGetKey(window, GLFW_KEY_W) == GLFW_PRESS) {
        position += forward * speed;
        target += forward * speed;
    }
    if (glfwGetKey(window, GLFW_KEY_S) == GLFW_PRESS) {
        position -= forward * speed;
        target -= forward * speed;
    }
    if (glfwGetKey(window, GLFW_KEY_A) == GLFW_PRESS) {
        position -= right * speed;
        target -= right * speed;
    }
    if (glfwGetKey(window, GLFW_KEY_D) == GLFW_PRESS) {
        position += right * speed;
        target += right * speed;
    }
    if (glfwGetKey(window, GLFW_KEY_Q) == GLFW_PRESS) {
        position -= up * speed;
        target -= up * speed;
    }
    if (glfwGetKey(window, GLFW_KEY_E) == GLFW_PRESS) {
        position += up * speed;
        target += up * speed;
    }
}

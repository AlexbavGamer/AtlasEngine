#pragma once

#include <GLFW/glfw3.h>

#include <glm/glm.hpp>

namespace Atlas {

class GameInput {
public:
    void setWindow(GLFWwindow* window) { m_Window = window; }

    void setEnabled(bool enabled) {
        if (m_Enabled == enabled) {
            return;
        }

        m_Enabled = enabled;
        if (!m_Enabled) {
            setMouseCaptured(false);
            m_MouseDelta = glm::vec2(0.0f);
            m_HasMousePosition = false;
        }
    }

    bool isEnabled() const { return m_Enabled; }

    void update() {
        if (!m_Enabled || !m_Window) {
            m_MouseDelta = glm::vec2(0.0f);
            m_HasMousePosition = false;
            return;
        }

        double mouseX = 0.0;
        double mouseY = 0.0;
        glfwGetCursorPos(m_Window, &mouseX, &mouseY);
        glm::vec2 current(static_cast<float>(mouseX), static_cast<float>(mouseY));

        if (!m_HasMousePosition) {
            m_MousePosition = current;
            m_MouseDelta = glm::vec2(0.0f);
            m_HasMousePosition = true;
            return;
        }

        m_MouseDelta = current - m_MousePosition;
        m_MousePosition = current;
    }

    bool isKeyDown(int key) const {
        return m_Enabled && m_Window && glfwGetKey(m_Window, key) == GLFW_PRESS;
    }

    bool isMouseButtonDown(int button) const {
        return m_Enabled && m_Window && glfwGetMouseButton(m_Window, button) == GLFW_PRESS;
    }

    glm::vec2 getMousePosition() const {
        return m_Enabled ? m_MousePosition : glm::vec2(0.0f);
    }

    glm::vec2 getMouseDelta() const {
        return m_Enabled ? m_MouseDelta : glm::vec2(0.0f);
    }

    void setMouseCaptured(bool captured) {
        if (!m_Window) {
            m_MouseCaptured = false;
            return;
        }

        const bool finalCaptured = m_Enabled && captured;
        m_MouseCaptured = finalCaptured;
        glfwSetInputMode(m_Window, GLFW_CURSOR, finalCaptured ? GLFW_CURSOR_DISABLED : GLFW_CURSOR_NORMAL);

        double mouseX = 0.0;
        double mouseY = 0.0;
        glfwGetCursorPos(m_Window, &mouseX, &mouseY);
        m_MousePosition = glm::vec2(static_cast<float>(mouseX), static_cast<float>(mouseY));
        m_MouseDelta = glm::vec2(0.0f);
        m_HasMousePosition = true;
    }

    bool isMouseCaptured() const {
        return m_Enabled && m_MouseCaptured;
    }

private:
    GLFWwindow* m_Window = nullptr;
    glm::vec2 m_MousePosition{0.0f};
    glm::vec2 m_MouseDelta{0.0f};
    bool m_HasMousePosition = false;
    bool m_MouseCaptured = false;
    bool m_Enabled = false;
};

} // namespace Atlas

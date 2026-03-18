#pragma once

#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h>
#include <string>
#include <functional>

namespace Atlas {

class Window {
public:
    using ResizeCallback = std::function<void(int width, int height)>;
    using KeyCallback = std::function<void(int key, int scancode, int action, int mods)>;
    using MouseButtonCallback = std::function<void(int button, int action, int mods)>;
    using MouseMoveCallback = std::function<void(double x, double y)>;
    using ScrollCallback = std::function<void(double xoffset, double yoffset)>;

    Window(int width, int height, const std::string& title);
    ~Window();

    void update();
    bool shouldClose() const;
    void close();

    GLFWwindow* getGLFWWindow() const { return m_Window; }
    void* getNativeWindow() const { return reinterpret_cast<void*>(m_Window); }

    int getWidth() const { return m_Width; }
    int getHeight() const { return m_Height; }
    float getAspectRatio() const { return static_cast<float>(m_Width) / static_cast<float>(m_Height); }

    bool isMinimized() const;
    bool isMaximized() const;
    void maximize();
    void restore();

    void setResizeCallback(ResizeCallback callback) { m_ResizeCallback = std::move(callback); }
    void setKeyCallback(KeyCallback callback) { m_KeyCallback = std::move(callback); }
    void setMouseButtonCallback(MouseButtonCallback callback) { m_MouseButtonCallback = std::move(callback); }
    void setMouseMoveCallback(MouseMoveCallback callback) { m_MouseMoveCallback = std::move(callback); }
    void setScrollCallback(ScrollCallback callback) { m_ScrollCallback = std::move(callback); }

    void setTitle(const std::string& title);
    void setCursorPos(double x, double y);
    void hideCursor();
    void showCursor();

    bool isKeyPressed(int key) const;
    bool isMouseButtonPressed(int button) const;
    void getCursorPos(double& x, double& y) const;

    VkSurfaceKHR createSurface(VkInstance instance);

private:
    GLFWwindow* m_Window = nullptr;
    int m_Width = 0;
    int m_Height = 0;
    std::string m_Title;

    ResizeCallback m_ResizeCallback;
    KeyCallback m_KeyCallback;
    MouseButtonCallback m_MouseButtonCallback;
    MouseMoveCallback m_MouseMoveCallback;
    ScrollCallback m_ScrollCallback;

    static void glfwResizeCallback(GLFWwindow* window, int width, int height);
    static void glfwKeyCallback(GLFWwindow* window, int key, int scancode, int action, int mods);
    static void glfwMouseButtonCallback(GLFWwindow* window, int button, int action, int mods);
    static void glfwMouseMoveCallback(GLFWwindow* window, double xpos, double ypos);
    static void glfwScrollCallback(GLFWwindow* window, double xoffset, double yoffset);
};

}

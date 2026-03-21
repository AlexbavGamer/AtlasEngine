#include "window.h"
#include <stdexcept>

namespace Atlas {

Window::Window(int width, int height, const std::string& title)
    : m_Width(width), m_Height(height), m_Title(title) {
    
    if (!glfwInit()) {
        throw std::runtime_error("Failed to initialize GLFW");
    }

    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
    glfwWindowHint(GLFW_RESIZABLE, GLFW_TRUE);
    glfwWindowHint(GLFW_MAXIMIZED, GLFW_TRUE);

    m_Window = glfwCreateWindow(width, height, title.c_str(), nullptr, nullptr);
    if (!m_Window) {
        glfwTerminate();
        throw std::runtime_error("Failed to create window");
    }

    glfwSetWindowUserPointer(m_Window, this);

    glfwSetFramebufferSizeCallback(m_Window, glfwResizeCallback);
    glfwSetKeyCallback(m_Window, glfwKeyCallback);
    glfwSetMouseButtonCallback(m_Window, glfwMouseButtonCallback);
    glfwSetCursorPosCallback(m_Window, glfwMouseMoveCallback);
    glfwSetScrollCallback(m_Window, glfwScrollCallback);
    glfwSetDropCallback(m_Window, glfwDropCallback);

    // Ensure we start maximized (and capture the real framebuffer size immediately).
    glfwMaximizeWindow(m_Window);
    int fbW = 0, fbH = 0;
    glfwGetFramebufferSize(m_Window, &fbW, &fbH);
    if (fbW > 0 && fbH > 0) {
        m_Width = fbW;
        m_Height = fbH;
    }
}

Window::~Window() {
    if (m_Window) {
        glfwDestroyWindow(m_Window);
    }
    glfwTerminate();
}

void Window::update() {
    glfwPollEvents();
}

bool Window::shouldClose() const {
    return glfwWindowShouldClose(m_Window) != 0;
}

void Window::close() {
    glfwSetWindowShouldClose(m_Window, GLFW_TRUE);
}

bool Window::isMinimized() const {
    return glfwGetWindowAttrib(m_Window, GLFW_ICONIFIED) != 0;
}

bool Window::isMaximized() const {
    return glfwGetWindowAttrib(m_Window, GLFW_MAXIMIZED) != 0;
}

void Window::maximize() {
    glfwMaximizeWindow(m_Window);
}

void Window::restore() {
    glfwRestoreWindow(m_Window);
}

void Window::setTitle(const std::string& title) {
    m_Title = title;
    glfwSetWindowTitle(m_Window, title.c_str());
}

void Window::setCursorPos(double x, double y) {
    glfwSetCursorPos(m_Window, x, y);
}

void Window::hideCursor() {
    glfwSetInputMode(m_Window, GLFW_CURSOR, GLFW_CURSOR_HIDDEN);
}

void Window::showCursor() {
    glfwSetInputMode(m_Window, GLFW_CURSOR, GLFW_CURSOR_NORMAL);
}

bool Window::isKeyPressed(int key) const {
    return glfwGetKey(m_Window, key) == GLFW_PRESS;
}

bool Window::isMouseButtonPressed(int button) const {
    return glfwGetMouseButton(m_Window, button) == GLFW_PRESS;
}

void Window::getCursorPos(double& x, double& y) const {
    glfwGetCursorPos(m_Window, &x, &y);
}

VkSurfaceKHR Window::createSurface(VkInstance instance) {
    VkSurfaceKHR surface;
    if (glfwCreateWindowSurface(instance, m_Window, nullptr, &surface) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create window surface");
    }
    return surface;
}

void Window::glfwResizeCallback(GLFWwindow* window, int width, int height) {
    Window* win = reinterpret_cast<Window*>(glfwGetWindowUserPointer(window));
    win->m_Width = width;
    win->m_Height = height;
    if (win->m_ResizeCallback) {
        win->m_ResizeCallback(width, height);
    }
}

void Window::glfwKeyCallback(GLFWwindow* window, int key, int scancode, int action, int mods) {
    Window* win = reinterpret_cast<Window*>(glfwGetWindowUserPointer(window));
    if (win->m_KeyCallback) {
        win->m_KeyCallback(key, scancode, action, mods);
    }
}

void Window::glfwMouseButtonCallback(GLFWwindow* window, int button, int action, int mods) {
    Window* win = reinterpret_cast<Window*>(glfwGetWindowUserPointer(window));
    if (win->m_MouseButtonCallback) {
        win->m_MouseButtonCallback(button, action, mods);
    }
}

void Window::glfwMouseMoveCallback(GLFWwindow* window, double xpos, double ypos) {
    Window* win = reinterpret_cast<Window*>(glfwGetWindowUserPointer(window));
    if (win->m_MouseMoveCallback) {
        win->m_MouseMoveCallback(xpos, ypos);
    }
}

void Window::glfwScrollCallback(GLFWwindow* window, double xoffset, double yoffset) {
    Window* win = reinterpret_cast<Window*>(glfwGetWindowUserPointer(window));
    if (win->m_ScrollCallback) {
        win->m_ScrollCallback(xoffset, yoffset);
    }
}

void Window::glfwDropCallback(GLFWwindow* window, int count, const char** paths) {
    Window* win = reinterpret_cast<Window*>(glfwGetWindowUserPointer(window));
    if (!win || !win->m_FileDropCallback) return;

    std::vector<std::string> out;
    out.reserve(static_cast<size_t>(count));
    for (int i = 0; i < count; ++i) {
        if (paths[i]) {
            out.emplace_back(paths[i]);
        }
    }

    if (!out.empty()) {
        win->m_FileDropCallback(out);
    }
}

}

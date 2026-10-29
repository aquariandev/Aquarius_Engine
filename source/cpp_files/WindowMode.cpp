#include "WindowMode.hpp"

#include <GLFW/glfw3.h>

void ToggleMaximized(GLFWwindow* window) {
    if (!window) {
        return;
    }
    if (glfwGetWindowAttrib(window, GLFW_MAXIMIZED) == GLFW_TRUE) {
        glfwRestoreWindow(window);
    } else {
        glfwMaximizeWindow(window);
    }
}

void SetMaximized(GLFWwindow* window, bool maximized) {
    if (!window) {
        return;
    }
    if (maximized) {
        glfwMaximizeWindow(window);
    } else {
        glfwRestoreWindow(window);
    }
}

bool IsWindowMaximized(GLFWwindow* window) {
    return window &&
           glfwGetWindowAttrib(window, GLFW_MAXIMIZED) == GLFW_TRUE;
}

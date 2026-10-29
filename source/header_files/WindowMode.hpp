#pragma once

struct GLFWwindow;

void ToggleMaximized(GLFWwindow* window);
void SetMaximized(GLFWwindow* window, bool maximized);
bool IsWindowMaximized(GLFWwindow* window);

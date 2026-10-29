#include "SplashScreen.hpp"

#include <glad/glad.h>
#include <GLFW/glfw3.h>
#include <imgui.h>
#include <backends/imgui_impl_glfw.h>
#include <backends/imgui_impl_opengl3.h>
#include <stb_image.h>

#include <algorithm>
#include <cstdint>

bool ShowSplashScreen(std::string& error) {
    error.clear();
#ifndef AQUARIUS_SPLASH_IMAGE_PATH
    error = "Splash screen image path is not configured.";
    return false;
#else
    int imageWidth = 0;
    int imageHeight = 0;
    int imageChannels = 0;
    stbi_uc* image = stbi_load(AQUARIUS_SPLASH_IMAGE_PATH, &imageWidth,
                               &imageHeight, &imageChannels, STBI_rgb_alpha);
    if (!image) {
        const char* reason = stbi_failure_reason();
        error = std::string("Could not load splash screen image '") +
                AQUARIUS_SPLASH_IMAGE_PATH + "': " +
                (reason ? reason : "unknown image error");
        return false;
    }

    if (!glfwInit()) {
        stbi_image_free(image);
        error = "GLFW initialization failed for the splash screen.";
        return false;
    }

    GLFWmonitor* monitor = glfwGetPrimaryMonitor();
    if (!monitor) {
        stbi_image_free(image);
        glfwTerminate();
        error = "Could not find a display for the splash screen.";
        return false;
    }
    int monitorX = 0;
    int monitorY = 0;
    int monitorWidth = 0;
    int monitorHeight = 0;
    glfwGetMonitorWorkarea(monitor, &monitorX, &monitorY, &monitorWidth,
                           &monitorHeight);
    if (monitorWidth <= 0 || monitorHeight <= 0) {
        const GLFWvidmode* videoMode = glfwGetVideoMode(monitor);
        if (!videoMode) {
            stbi_image_free(image);
            glfwTerminate();
            error = "Could not determine display dimensions for the splash "
                    "screen.";
            return false;
        }
        monitorWidth = videoMode->width;
        monitorHeight = videoMode->height;
    }

    constexpr float requestedImageScale = 0.5f;
    const float imageScale =
        std::min({requestedImageScale,
                  static_cast<float>(monitorWidth) /
                      static_cast<float>(imageWidth),
                  static_cast<float>(monitorHeight) /
                      static_cast<float>(imageHeight)});
    const int windowWidth = std::max(
        1, static_cast<int>(static_cast<float>(imageWidth) * imageScale));
    const int windowHeight = std::max(
        1, static_cast<int>(static_cast<float>(imageHeight) * imageScale));
    const int windowX = monitorX + (monitorWidth - windowWidth) / 2;
    const int windowY = monitorY + (monitorHeight - windowHeight) / 2;
    GLFWwindow* window = nullptr;
    bool imguiGlfwInitialized = false;
    bool imguiOpenGLInitialized = false;
    bool imguiContextCreated = false;
    GLuint texture = 0;
    const auto cleanup = [&]() {
        if (imguiOpenGLInitialized) {
            ImGui_ImplOpenGL3_Shutdown();
        }
        if (imguiGlfwInitialized) {
            ImGui_ImplGlfw_Shutdown();
        }
        if (imguiContextCreated) {
            ImGui::DestroyContext();
        }
        if (texture != 0 && window) {
            glfwMakeContextCurrent(window);
            glDeleteTextures(1, &texture);
        }
        if (window) {
            glfwDestroyWindow(window);
        }
        glfwTerminate();
        stbi_image_free(image);
    };

    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 4);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 6);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
    glfwWindowHint(GLFW_RESIZABLE, GLFW_FALSE);
    glfwWindowHint(GLFW_DECORATED, GLFW_FALSE);
    glfwWindowHint(GLFW_VISIBLE, GLFW_FALSE);
    window = glfwCreateWindow(windowWidth, windowHeight, "Aquarius Engine",
                              nullptr, nullptr);
    if (!window) {
        cleanup();
        error = "Could not create the splash screen window.";
        return false;
    }
    glfwSetWindowPos(window, windowX, windowY);
    glfwMakeContextCurrent(window);
    glfwSwapInterval(1);
    if (!gladLoadGLLoader(reinterpret_cast<GLADloadproc>(glfwGetProcAddress))) {
        cleanup();
        error = "Could not initialize OpenGL for the splash screen.";
        return false;
    }

    glGenTextures(1, &texture);
    glBindTexture(GL_TEXTURE_2D, texture);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, imageWidth, imageHeight, 0,
                 GL_RGBA, GL_UNSIGNED_BYTE, image);
    stbi_image_free(image);
    image = nullptr;

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    imguiContextCreated = true;
    ImGui::GetIO().IniFilename = nullptr;
    ImGui::StyleColorsDark();
    if (!ImGui_ImplGlfw_InitForOpenGL(window, true)) {
        cleanup();
        error = "Dear ImGui could not initialize its splash-screen GLFW backend.";
        return false;
    }
    imguiGlfwInitialized = true;
    if (!ImGui_ImplOpenGL3_Init("#version 460 core")) {
        cleanup();
        error = "Dear ImGui could not initialize its splash-screen OpenGL backend.";
        return false;
    }
    imguiOpenGLInitialized = true;

    glfwShowWindow(window);
    glfwPollEvents();
    int actualWindowWidth = 0;
    int actualWindowHeight = 0;
    glfwGetWindowSize(window, &actualWindowWidth, &actualWindowHeight);
    glfwSetWindowPos(window,
                     monitorX + (monitorWidth - actualWindowWidth) / 2,
                     monitorY + (monitorHeight - actualWindowHeight) / 2);
    glfwPollEvents();

    constexpr double fadeDuration = 1.0;
    constexpr double holdDuration = 5.0;
    constexpr double totalDuration = fadeDuration * 2.0 + holdDuration;
    const float displayImageWidth =
        static_cast<float>(imageWidth) * imageScale;
    const float displayImageHeight =
        static_cast<float>(imageHeight) * imageScale;
    const double startTime = glfwGetTime();

    while (!glfwWindowShouldClose(window)) {
        glfwPollEvents();
        const double elapsed = glfwGetTime() - startTime;
        if (elapsed >= totalDuration) {
            break;
        }
        const float alpha =
            elapsed < fadeDuration
                ? static_cast<float>(elapsed / fadeDuration)
                : elapsed < fadeDuration + holdDuration
                      ? 1.0f
                      : static_cast<float>(
                            (totalDuration - elapsed) / fadeDuration);

        ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplGlfw_NewFrame();
        ImGui::NewFrame();
        const ImGuiIO& io = ImGui::GetIO();
        const ImVec2 imageMin(
            (io.DisplaySize.x - displayImageWidth) * 0.5f,
            (io.DisplaySize.y - displayImageHeight) * 0.5f);
        const ImVec2 imageMax(imageMin.x + displayImageWidth,
                             imageMin.y + displayImageHeight);
        ImGui::GetBackgroundDrawList()->AddImage(
            reinterpret_cast<ImTextureID>(static_cast<intptr_t>(texture)),
            imageMin, imageMax, ImVec2(0.0f, 0.0f), ImVec2(1.0f, 1.0f),
            IM_COL32(255, 255, 255,
                     static_cast<int>(alpha * 255.0f)));
        ImGui::Render();

        int framebufferWidth = 0;
        int framebufferHeight = 0;
        glfwGetFramebufferSize(window, &framebufferWidth, &framebufferHeight);
        glViewport(0, 0, framebufferWidth, framebufferHeight);
        glClearColor(0.035f, 0.045f, 0.07f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT);
        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
        glfwSwapBuffers(window);
    }

    cleanup();
    return true;
#endif
}

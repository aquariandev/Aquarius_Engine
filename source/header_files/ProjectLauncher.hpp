#pragma once

#include "WindowMode.hpp"

#include <filesystem>
#include <optional>
#include <string>
#include <vector>

struct GLFWwindow;
struct ImFont;

class ProjectLauncher {
public:
    ProjectLauncher() = default;
    ~ProjectLauncher();

    ProjectLauncher(const ProjectLauncher&) = delete;
    ProjectLauncher& operator=(const ProjectLauncher&) = delete;

    bool Initialize(std::string& error);
    std::optional<std::filesystem::path> Run();
    bool IsMaximized() const;

private:
    enum class Screen { MainMenu, CreateProject };

    void RenderMainMenu();
    void RenderCreateProject();
    void RenderProjectFilePicker();
    bool SelectProjectFile(const std::filesystem::path& file);
    void LoadRecentProjects();
    void AddRecentProject(const std::filesystem::path& file);
    bool SaveRecentProjects(std::string& error) const;
    void OpenRecentProject(const std::filesystem::path& file);
    void CreateProject();

    GLFWwindow* m_Window = nullptr;
    bool m_Initialized = false;
    bool m_ImGuiInitialized = false;
    bool m_F11WasPressed = false;
    Screen m_Screen = Screen::MainMenu;
    ImFont* m_LargeMenuFont = nullptr;
    ImFont* m_TitleFont = nullptr;
    std::filesystem::path m_ProjectLocation;
    std::vector<std::filesystem::path> m_RecentProjects;
    std::optional<std::filesystem::path> m_MissingRecentProject;
    bool m_OpenMissingRecentPrompt = false;
    bool m_ProjectPickerOpen = false;
    bool m_OpenProjectPickerPopup = false;
    std::filesystem::path m_ProjectPickerDirectory;
    std::filesystem::path m_SelectedProjectFile;
    std::string m_ProjectPickerError;
    char m_ProjectPickerSearch[256]{};
    std::string m_Status;
    std::string m_Error;
    std::optional<std::filesystem::path> m_CreatedProject;
    char m_ProjectName[128]{};
};

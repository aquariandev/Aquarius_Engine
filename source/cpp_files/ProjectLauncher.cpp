#include "ProjectLauncher.hpp"
#include "ImGuiFont.hpp"
#include "ModelFileDialog.hpp"
#include "Project.hpp"
#include "EditorScene.hpp"

#include <glad/glad.h>
#include <GLFW/glfw3.h>
#include <imgui.h>
#include <backends/imgui_impl_glfw.h>
#include <backends/imgui_impl_opengl3.h>

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <exception>
#include <fstream>
#include <iomanip>
#include <system_error>
#include <utility>

namespace {

enum class LauncherIcon {
    NewProject,
    LoadProject,
    Quit
};

std::filesystem::path RecentProjectsFile(std::string& error) {
#ifdef _WIN32
    const char* baseDirectory = std::getenv("APPDATA");
    if (!baseDirectory || *baseDirectory == '\0') {
        error = "Could not locate the user's application data directory.";
        return {};
    }
    return std::filesystem::path(baseDirectory) / "AquariusEngine" /
           "recent-projects.txt";
#else
    const char* configDirectory = std::getenv("XDG_CONFIG_HOME");
    if (configDirectory && *configDirectory != '\0') {
        return std::filesystem::path(configDirectory) / "AquariusEngine" /
               "recent-projects.txt";
    }
    const char* homeDirectory = std::getenv("HOME");
    if (!homeDirectory || *homeDirectory == '\0') {
        error = "Could not locate the user's configuration directory.";
        return {};
    }
    return std::filesystem::path(homeDirectory) / ".config" /
           "AquariusEngine" / "recent-projects.txt";
#endif
}

bool IconButton(const char* id, const char* tooltip, LauncherIcon icon,
                float buttonSize) {
    const float scale = buttonSize / 96.0f;
    const float iconSize = 56.0f * scale;
    ImGui::PushID(id);
    const bool clicked = ImGui::Button("##IconButton",
                                       ImVec2(buttonSize, buttonSize));
    const ImVec2 buttonMin = ImGui::GetItemRectMin();
    const ImVec2 buttonMax = ImGui::GetItemRectMax();
    const ImVec2 center((buttonMin.x + buttonMax.x) * 0.5f,
                        (buttonMin.y + buttonMax.y) * 0.5f);
    const float half = iconSize * 0.5f;
    const ImVec2 topLeft(center.x - half, center.y - half);
    const ImVec2 bottomRight(center.x + half, center.y + half);
    ImDrawList* drawList = ImGui::GetWindowDrawList();
    const ImU32 iconColor = ImGui::GetColorU32(ImGuiCol_Text);
    const float thickness = 3.0f * scale;

    if (icon == LauncherIcon::NewProject) {
        const ImVec2 folder[] = {
            ImVec2(topLeft.x + 5.0f * scale, center.y - 14.0f * scale),
            ImVec2(topLeft.x + 19.0f * scale, center.y - 14.0f * scale),
            ImVec2(topLeft.x + 25.0f * scale, center.y - 7.0f * scale),
            ImVec2(bottomRight.x - 5.0f * scale, center.y - 7.0f * scale),
            ImVec2(bottomRight.x - 5.0f * scale, center.y + 17.0f * scale),
            ImVec2(topLeft.x + 5.0f * scale, center.y + 17.0f * scale)};
        drawList->AddPolyline(folder, 6, iconColor, ImDrawFlags_Closed,
                              thickness);
        drawList->AddLine(ImVec2(center.x, center.y - 1.0f * scale),
                          ImVec2(center.x, center.y + 11.0f * scale),
                          iconColor, thickness);
        drawList->AddLine(ImVec2(center.x - 6.0f * scale,
                                 center.y + 5.0f * scale),
                          ImVec2(center.x + 6.0f * scale,
                                 center.y + 5.0f * scale),
                          iconColor, thickness);
    } else if (icon == LauncherIcon::LoadProject) {
        const ImVec2 folder[] = {
            ImVec2(topLeft.x + 5.0f * scale, center.y - 14.0f * scale),
            ImVec2(topLeft.x + 19.0f * scale, center.y - 14.0f * scale),
            ImVec2(topLeft.x + 25.0f * scale, center.y - 7.0f * scale),
            ImVec2(bottomRight.x - 5.0f * scale, center.y - 7.0f * scale),
            ImVec2(bottomRight.x - 5.0f * scale, center.y + 17.0f * scale),
            ImVec2(topLeft.x + 5.0f * scale, center.y + 17.0f * scale)};
        drawList->AddPolyline(folder, 6, iconColor, ImDrawFlags_Closed,
                              thickness);
        const ImVec2 cursor[] = {
            ImVec2(center.x - 8.0f * scale, center.y - 4.0f * scale),
            ImVec2(center.x + 12.0f * scale, center.y + 7.0f * scale),
            ImVec2(center.x + 3.0f * scale, center.y + 8.0f * scale),
            ImVec2(center.x + 7.0f * scale, center.y + 17.0f * scale),
            ImVec2(center.x + 2.0f * scale, center.y + 20.0f * scale),
            ImVec2(center.x - 2.0f * scale, center.y + 11.0f * scale),
            ImVec2(center.x - 8.0f * scale, center.y + 17.0f * scale)};
        drawList->AddConvexPolyFilled(cursor, 7, iconColor);
    } else {
        const float left = center.x - 16.0f * scale;
        const float right = center.x + 9.0f * scale;
        const float top = center.y - 20.0f * scale;
        const float bottom = center.y + 20.0f * scale;
        drawList->AddLine(ImVec2(left, top), ImVec2(right, top), iconColor,
                          thickness);
        drawList->AddLine(ImVec2(left, top), ImVec2(left, bottom), iconColor,
                          thickness);
        drawList->AddLine(ImVec2(left, bottom), ImVec2(right, bottom), iconColor,
                          thickness);
        drawList->AddLine(ImVec2(center.x - 9.0f * scale, center.y),
                          ImVec2(center.x + 22.0f * scale, center.y),
                          iconColor, thickness);
        drawList->AddLine(ImVec2(center.x + 22.0f * scale, center.y),
                          ImVec2(center.x + 11.0f * scale,
                                 center.y - 11.0f * scale),
                          iconColor, thickness);
        drawList->AddLine(ImVec2(center.x + 22.0f * scale, center.y),
                          ImVec2(center.x + 11.0f * scale,
                                 center.y + 11.0f * scale),
                          iconColor, thickness);
    }

    if (ImGui::IsItemHovered()) {
        const ImGuiIO& io = ImGui::GetIO();
        ImGui::SetNextWindowPos(
            ImVec2(io.MousePos.x + 18.0f, io.MousePos.y + 18.0f),
            ImGuiCond_Always);
        ImGui::BeginTooltip();
        ImGui::TextUnformatted(tooltip);
        ImGui::EndTooltip();
    }
    ImGui::PopID();
    return clicked;
}

} // namespace

ProjectLauncher::~ProjectLauncher() {
    if (m_ImGuiInitialized) {
        ImGui_ImplOpenGL3_Shutdown();
        ImGui_ImplGlfw_Shutdown();
        ImGui::DestroyContext();
    }
    if (m_Window) {
        glfwDestroyWindow(m_Window);
    }
    if (m_Initialized) {
        glfwTerminate();
    }
}

bool ProjectLauncher::Initialize(std::string& error) {
    if (!glfwInit()) {
        error = "GLFW initialization failed.";
        return false;
    }
    m_Initialized = true;

    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 4);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 6);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
    glfwWindowHint(GLFW_RESIZABLE, GLFW_TRUE);
    m_Window =
        glfwCreateWindow(1200, 800, "Aquarius Engine - Project Launcher", nullptr,
                         nullptr);
    if (!m_Window) {
        error = "Could not create the project launcher window.";
        return false;
    }

    glfwMakeContextCurrent(m_Window);
    glfwSwapInterval(1);
    if (!gladLoadGLLoader(
            reinterpret_cast<GLADloadproc>(glfwGetProcAddress))) {
        error = "Could not initialize OpenGL for the project launcher.";
        return false;
    }

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    LoadBundledFont();
    m_LargeMenuFont = LoadBundledFont(22.4f);
#ifdef AQUARIUS_TITLE_FONT_PATH
    static const ImWchar titleGlyphRanges[] = {
        0x0020, 0x00FF,
        0x0100, 0x017F,
        0,
    };
    m_TitleFont = ImGui::GetIO().Fonts->AddFontFromFileTTF(
        AQUARIUS_TITLE_FONT_PATH, 52.0f, nullptr, titleGlyphRanges);
#endif
    if (!m_TitleFont) {
        m_TitleFont = m_LargeMenuFont;
    }
    ImGui::StyleColorsDark();
    ImGui::GetStyle().WindowRounding = 4.0f;
    if (!ImGui_ImplGlfw_InitForOpenGL(m_Window, true)) {
        error = "Dear ImGui could not initialize its GLFW backend.";
        ImGui::DestroyContext();
        return false;
    }
    if (!ImGui_ImplOpenGL3_Init("#version 460 core")) {
        error = "Dear ImGui could not initialize its OpenGL backend.";
        ImGui_ImplGlfw_Shutdown();
        ImGui::DestroyContext();
        return false;
    }
    m_ImGuiInitialized = true;

    std::error_code pathError;
    m_ProjectLocation = std::filesystem::current_path(pathError);
    if (pathError) {
        m_ProjectLocation = std::filesystem::path(".");
    }
    LoadRecentProjects();
    return true;
}

std::optional<std::filesystem::path> ProjectLauncher::Run() {
    while (m_Window && !glfwWindowShouldClose(m_Window) &&
           !m_CreatedProject.has_value()) {
        glfwPollEvents();
        const bool f11Pressed =
            glfwGetKey(m_Window, GLFW_KEY_F11) == GLFW_PRESS;
        if (f11Pressed && !m_F11WasPressed) {
            ToggleMaximized(m_Window);
        }
        m_F11WasPressed = f11Pressed;
        ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplGlfw_NewFrame();
        ImGui::NewFrame();

        const ImGuiIO& io = ImGui::GetIO();
        constexpr ImVec2 launcherSize(1000.0f, 760.0f);
        const ImVec2 panelSize(
            std::min(launcherSize.x, std::max(1.0f, io.DisplaySize.x - 32.0f)),
            std::min(launcherSize.y, std::max(1.0f, io.DisplaySize.y - 32.0f)));
        ImGui::SetNextWindowPos(
            ImVec2((io.DisplaySize.x - panelSize.x) * 0.5f,
                   (io.DisplaySize.y - panelSize.y) * 0.5f),
            ImGuiCond_Always);
        ImGui::SetNextWindowSize(panelSize, ImGuiCond_Always);
        constexpr ImGuiWindowFlags flags =
            ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
            ImGuiWindowFlags_NoMove |
            ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoSavedSettings;
        ImGui::Begin("Aquarius Engine", nullptr, flags);
        const ImVec2 contentSize = ImGui::GetContentRegionAvail();
        const float centeredContentHeight =
            m_Screen == Screen::MainMenu ? 680.0f : 360.0f;
        ImGui::SetCursorPosY(
            std::max(0.0f, (contentSize.y - centeredContentHeight) * 0.5f));
        if (m_Screen == Screen::MainMenu) {
            ImGui::PushFont(m_LargeMenuFont);
            RenderMainMenu();
            ImGui::PopFont();
        } else if (m_Screen == Screen::CreateProject) {
            RenderCreateProject();
        }
        ImGui::End();

        ImGui::Render();
        int framebufferWidth = 0;
        int framebufferHeight = 0;
        glfwGetFramebufferSize(m_Window, &framebufferWidth, &framebufferHeight);
        glViewport(0, 0, framebufferWidth, framebufferHeight);
        glClearColor(0.08f, 0.09f, 0.12f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT);
        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
        glfwSwapBuffers(m_Window);
    }

    return m_CreatedProject;
}

void ProjectLauncher::RenderMainMenu() {
    constexpr float buttonSize = 134.4f;
    constexpr float buttonSpacing = 8.4f;
    constexpr float actionsWidth = 216.0f;
    constexpr float layoutWidth = 880.0f;
    const char* title = "Aquarius Engine";
    ImGui::PushFont(m_TitleFont);
    const ImVec2 titleSize = ImGui::CalcTextSize(title);
    ImGui::SetCursorPosX(
        std::max(0.0f, (ImGui::GetContentRegionAvail().x - titleSize.x) * 0.5f));
    ImGui::TextUnformatted(title);
    ImGui::PopFont();
    ImGui::Dummy(ImVec2(0.0f, 16.0f));

    ImGui::SetCursorPosX(
        std::max(0.0f, (ImGui::GetContentRegionAvail().x - layoutWidth) * 0.5f));
    ImGui::BeginGroup();
    ImGui::BeginChild("LauncherHome", ImVec2(layoutWidth, 0.0f), false);
    ImGui::BeginChild("LauncherActions", ImVec2(actionsWidth, 0.0f), false);
    ImGui::SetCursorPosX((actionsWidth - buttonSize) * 0.5f);

    if (IconButton("CreateProject", "Create project",
                   LauncherIcon::NewProject, buttonSize)) {
        std::filesystem::path directory;
        bool cancelled = false;
        std::string error;
        if (OpenDirectoryDialog(directory, m_ProjectLocation, cancelled,
                               error)) {
            m_ProjectLocation = std::move(directory);
            m_ProjectName[0] = '\0';
            m_Error.clear();
            m_Status.clear();
            m_Screen = Screen::CreateProject;
        } else if (!cancelled) {
            m_Error = std::move(error);
        }
    }
    ImGui::Dummy(ImVec2(0.0f, buttonSpacing));
    ImGui::SetCursorPosX((actionsWidth - buttonSize) * 0.5f);
    if (IconButton("LoadProject", "Load project", LauncherIcon::LoadProject,
                   buttonSize)) {
        std::error_code filesystemError;
        m_ProjectPickerDirectory =
            m_ProjectLocation.empty()
                ? std::filesystem::current_path(filesystemError)
                : m_ProjectLocation;
        if (filesystemError) {
            m_ProjectPickerError =
                "Could not determine the starting folder: " +
                filesystemError.message();
        } else {
            m_ProjectPickerDirectory = std::filesystem::weakly_canonical(
                m_ProjectPickerDirectory, filesystemError);
            if (filesystemError) {
                m_ProjectPickerError =
                    "Could not open the starting folder: " +
                    filesystemError.message();
            } else {
                m_ProjectPickerError.clear();
            }
        }
        m_SelectedProjectFile.clear();
        m_ProjectPickerSearch[0] = '\0';
        m_ProjectPickerOpen = true;
        m_OpenProjectPickerPopup = true;
    }
    ImGui::Dummy(ImVec2(0.0f, buttonSpacing));
    ImGui::SetCursorPosX((actionsWidth - buttonSize) * 0.5f);
    if (IconButton("Quit", "Quit", LauncherIcon::Quit, buttonSize)) {
        glfwSetWindowShouldClose(m_Window, GLFW_TRUE);
    }
    ImGui::EndChild();
    ImGui::SameLine(0.0f, 20.0f);
    ImGui::BeginChild("RecentProjectsPanel", ImVec2(0.0f, 0.0f), true);
    ImGui::TextUnformatted("Recent projects");
    ImGui::Separator();
    if (m_RecentProjects.empty()) {
        ImGui::TextWrapped("Projects you open or create will appear here.");
    } else {
        for (std::size_t index = 0; index < m_RecentProjects.size(); ++index) {
            const std::filesystem::path& projectFile = m_RecentProjects[index];
            const std::string label = projectFile.stem().string();
            const std::string path = projectFile.string();
            ImGui::PushID(static_cast<int>(index));
            if (ImGui::Button("##RecentProject", ImVec2(-1.0f, 70.0f))) {
                OpenRecentProject(projectFile);
            }
            const ImVec2 buttonMin = ImGui::GetItemRectMin();
            const ImVec2 buttonMax = ImGui::GetItemRectMax();
            const ImVec2 textPosition(buttonMin.x + 10.0f,
                                      buttonMin.y + 5.0f);
            ImDrawList* drawList = ImGui::GetWindowDrawList();
            const ImU32 titleColor = ImGui::GetColorU32(ImGuiCol_Text);
            const ImU32 pathColor = ImGui::GetColorU32(ImGuiCol_TextDisabled);
            const ImVec4 clipRect(buttonMin.x + 8.0f, buttonMin.y + 2.0f,
                                  buttonMax.x - 8.0f, buttonMax.y - 2.0f);
            drawList->AddText(m_LargeMenuFont, 22.4f, textPosition, titleColor,
                              label.c_str(), nullptr, 0.0f, &clipRect);
            drawList->AddText(
                m_LargeMenuFont, 22.4f,
                ImVec2(textPosition.x, textPosition.y + 32.0f), pathColor,
                path.c_str(), nullptr, 0.0f, &clipRect);
            ImGui::Spacing();
            ImGui::PopID();
        }
    }
    ImGui::EndChild();
    ImGui::EndChild();

    if (m_OpenMissingRecentPrompt && m_MissingRecentProject) {
        ImGui::OpenPopup("Missing recent project");
        m_OpenMissingRecentPrompt = false;
    }
    if (ImGui::BeginPopupModal("Missing recent project", nullptr,
                               ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::TextWrapped("The recent project could not be found:\n%s",
                           m_MissingRecentProject
                               ? m_MissingRecentProject->string().c_str()
                               : "");
        ImGui::Spacing();
        ImGui::TextUnformatted(
            "Do you want to remove this reference from Recent projects?");
        ImGui::Spacing();
        if (ImGui::Button("Remove reference", ImVec2(190.0f, 42.0f))) {
            if (m_MissingRecentProject) {
                std::erase(m_RecentProjects, *m_MissingRecentProject);
                std::string saveError;
                if (!SaveRecentProjects(saveError)) {
                    m_Error = "Could not update recent projects: " + saveError;
                }
            }
            m_MissingRecentProject.reset();
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button("Keep reference", ImVec2(190.0f, 42.0f))) {
            m_MissingRecentProject.reset();
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }
    RenderProjectFilePicker();
    if (!m_Status.empty()) {
        ImGui::Dummy(ImVec2(0.0f, 12.0f));
        ImGui::TextWrapped("%s", m_Status.c_str());
    }
    if (!m_Error.empty()) {
        ImGui::Dummy(ImVec2(0.0f, 6.0f));
        ImGui::TextColored(ImVec4(1.0f, 0.4f, 0.35f, 1.0f), "%s",
                           m_Error.c_str());
    }
    ImGui::EndGroup();
}

void ProjectLauncher::RenderProjectFilePicker() {
    if (m_OpenProjectPickerPopup) {
        ImGui::OpenPopup("Open Aquarius project");
        m_OpenProjectPickerPopup = false;
    }
    if (!m_ProjectPickerOpen) {
        return;
    }

    ImGui::SetNextWindowSize(ImVec2(680.0f, 560.0f),
                             ImGuiCond_Appearing);
    if (!ImGui::BeginPopupModal("Open Aquarius project",
                                &m_ProjectPickerOpen)) {
        return;
    }

    ImGui::TextWrapped("Choose a project file (.aqenproj only)");
    ImGui::TextWrapped("%s", m_ProjectPickerDirectory.string().c_str());
    ImGui::SetNextItemWidth(-1.0f);
    ImGui::InputTextWithHint("##ProjectPickerSearch", "Search project files...",
                             m_ProjectPickerSearch,
                             sizeof(m_ProjectPickerSearch));

    std::vector<std::filesystem::path> directories;
    std::vector<std::filesystem::path> projectFiles;
    std::string listingError;
    std::error_code iteratorError;
    std::filesystem::directory_iterator iterator(m_ProjectPickerDirectory,
                                                 iteratorError);
    if (!iteratorError) {
        const std::filesystem::directory_iterator end;
        while (iterator != end) {
            const std::filesystem::directory_entry entry = *iterator;
            std::error_code statusError;
            const std::filesystem::file_status status = entry.status(statusError);
            if (!statusError) {
                if (std::filesystem::is_directory(status)) {
                    directories.push_back(entry.path());
                } else if (std::filesystem::is_regular_file(status) &&
                           entry.path().extension() == ".aqenproj") {
                    projectFiles.push_back(entry.path());
                }
            }

            // A broken or concurrently removed child entry must not hide the
            // other folders and project files in the current directory.
            iterator.increment(iteratorError);
            if (iteratorError) {
                break;
            }
        }
    }
    const auto sortByName = [](const std::filesystem::path& left,
                               const std::filesystem::path& right) {
        return left.filename().string() < right.filename().string();
    };
    std::sort(directories.begin(), directories.end(), sortByName);
    std::sort(projectFiles.begin(), projectFiles.end(), sortByName);

    if (iteratorError) {
        listingError = "Could not read this folder: " +
                       iteratorError.message();
    }

    if (ImGui::Button("..", ImVec2(48.0f, 0.0f))) {
        const std::filesystem::path parent =
            m_ProjectPickerDirectory.parent_path();
        if (!parent.empty() && parent != m_ProjectPickerDirectory) {
            m_ProjectPickerDirectory = parent;
            m_SelectedProjectFile.clear();
        }
    }
    ImGui::BeginChild("ProjectPickerEntries", ImVec2(0.0f, -72.0f), true);
    if (!iteratorError) {
        std::string query(m_ProjectPickerSearch);
        std::transform(query.begin(), query.end(), query.begin(),
                       [](unsigned char character) {
                           return static_cast<char>(std::tolower(character));
                       });
        for (const std::filesystem::path& directory : directories) {
            const std::string name = directory.filename().string();
            if (!query.empty()) {
                std::string foldedName = name;
                std::transform(foldedName.begin(), foldedName.end(),
                               foldedName.begin(),
                               [](unsigned char character) {
                                   return static_cast<char>(
                                       std::tolower(character));
                               });
                if (foldedName.find(query) == std::string::npos) {
                    continue;
                }
            }
            const std::string label = "[Folder] " + name;
            if (ImGui::Selectable(label.c_str(), false)) {
                std::error_code canonicalError;
                const std::filesystem::path resolved =
                    std::filesystem::weakly_canonical(directory,
                                                      canonicalError);
                if (canonicalError) {
                    m_ProjectPickerError =
                        "Could not open folder: " + canonicalError.message();
                } else {
                    m_ProjectPickerDirectory = resolved;
                    m_SelectedProjectFile.clear();
                    m_ProjectPickerError.clear();
                }
                break;
            }
        }
        for (const std::filesystem::path& file : projectFiles) {
            const std::string name = file.filename().string();
            if (!query.empty()) {
                std::string foldedName = name;
                std::transform(foldedName.begin(), foldedName.end(),
                               foldedName.begin(),
                               [](unsigned char character) {
                                   return static_cast<char>(
                                       std::tolower(character));
                               });
                if (foldedName.find(query) == std::string::npos) {
                    continue;
                }
            }
            const bool selected = file == m_SelectedProjectFile;
            if (ImGui::Selectable(name.c_str(), selected,
                                  ImGuiSelectableFlags_AllowDoubleClick)) {
                m_SelectedProjectFile = file;
                if (ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) {
                    if (SelectProjectFile(file)) {
                        m_ProjectPickerOpen = false;
                        ImGui::CloseCurrentPopup();
                    } else {
                        m_ProjectPickerError = m_Error;
                    }
                }
            }
        }
        if (directories.empty() && projectFiles.empty()) {
            ImGui::TextDisabled("No project files or folders here.");
        } else if (projectFiles.empty()) {
            ImGui::TextDisabled("No .aqenproj files in this folder.");
        }
    }
    ImGui::EndChild();

    if (!m_ProjectPickerError.empty()) {
        ImGui::TextColored(ImVec4(1.0f, 0.4f, 0.35f, 1.0f), "%s",
                           m_ProjectPickerError.c_str());
    }
    if (!listingError.empty()) {
        ImGui::TextColored(ImVec4(1.0f, 0.4f, 0.35f, 1.0f), "%s",
                           listingError.c_str());
    }
    if (ImGui::Button("Open project", ImVec2(160.0f, 36.0f))) {
        if (m_SelectedProjectFile.empty()) {
            m_ProjectPickerError = "Select an .aqenproj file to open.";
        } else if (SelectProjectFile(m_SelectedProjectFile)) {
            m_ProjectPickerOpen = false;
            ImGui::CloseCurrentPopup();
        } else {
            m_ProjectPickerError = m_Error;
        }
    }
    ImGui::SameLine();
    if (ImGui::Button("Cancel", ImVec2(120.0f, 36.0f))) {
        m_ProjectPickerOpen = false;
        ImGui::CloseCurrentPopup();
    }
    ImGui::EndPopup();
}

void ProjectLauncher::RenderCreateProject() {
    const float contentWidth =
        std::min(640.0f, ImGui::GetContentRegionAvail().x);
    ImGui::SetCursorPosX(
        std::max(0.0f, (ImGui::GetContentRegionAvail().x - contentWidth) *
                           0.5f));
    ImGui::BeginGroup();
    ImGui::Text("Create a new project");
    ImGui::Separator();
    ImGui::Spacing();
    ImGui::Text("Project name");
    ImGui::SetNextItemWidth(contentWidth);
    ImGui::InputTextWithHint("##ProjectName", "MyProject", m_ProjectName,
                             sizeof(m_ProjectName));

    ImGui::Spacing();
    ImGui::Text("Create in");
    if (ImGui::Button("Choose folder...", ImVec2(140.0f, 0.0f))) {
        std::filesystem::path directory;
        bool cancelled = false;
        std::string error;
        if (OpenDirectoryDialog(directory, m_ProjectLocation, cancelled,
                                error)) {
            m_ProjectLocation = std::move(directory);
            m_Error.clear();
        } else if (!cancelled) {
            m_Error = std::move(error);
        }
    }
    ImGui::SameLine();
    ImGui::TextWrapped("%s", m_ProjectLocation.string().c_str());

    ImGui::Spacing();
    ImGui::Separator();
    if (!m_Error.empty()) {
        ImGui::TextColored(ImVec4(1.0f, 0.4f, 0.35f, 1.0f), "%s",
                           m_Error.c_str());
    }
    if (ImGui::Button("Create", ImVec2(150.0f, 38.0f))) {
        CreateProject();
    }
    ImGui::SameLine();
    if (ImGui::Button("Back", ImVec2(150.0f, 38.0f))) {
        m_Screen = Screen::MainMenu;
        m_Error.clear();
    }
    ImGui::EndGroup();
}

bool ProjectLauncher::SelectProjectFile(
    const std::filesystem::path& file) {
    ProjectDescriptor project;
    if (!LoadProjectDescriptor(file, project, m_Error)) {
        return false;
    }
    m_CreatedProject = file;
    m_ProjectLocation = project.root;
    AddRecentProject(file);
    return true;
}

void ProjectLauncher::LoadRecentProjects() {
    std::string pathError;
    const std::filesystem::path recentFile = RecentProjectsFile(pathError);
    if (!pathError.empty()) {
        m_Error = std::move(pathError);
        return;
    }

    std::error_code filesystemError;
    if (!std::filesystem::exists(recentFile, filesystemError)) {
        if (filesystemError) {
            m_Error = "Could not inspect recent projects: " +
                      filesystemError.message();
        }
        return;
    }

    std::ifstream input(recentFile);
    if (!input) {
        m_Error = "Could not read recent projects file '" +
                  recentFile.string() + "'.";
        return;
    }

    constexpr std::size_t maxRecentProjects = 20;
    std::string entry;
    while (m_RecentProjects.size() < maxRecentProjects &&
           input >> std::quoted(entry)) {
        const std::filesystem::path projectFile(entry);
        if (projectFile.empty() ||
            std::find(m_RecentProjects.begin(), m_RecentProjects.end(),
                      projectFile) != m_RecentProjects.end()) {
            continue;
        }
        m_RecentProjects.push_back(projectFile);
    }
    if (!input.eof()) {
        m_Error = "Recent projects file contains invalid data.";
    } else if (input.bad()) {
        m_Error = "Could not finish reading recent projects file.";
    }
}

void ProjectLauncher::AddRecentProject(const std::filesystem::path& file) {
    std::error_code filesystemError;
    std::filesystem::path normalized =
        std::filesystem::absolute(file, filesystemError).lexically_normal();
    if (filesystemError) {
        m_Error = "Project opened, but its path could not be added to recent "
                  "projects: " +
                  filesystemError.message();
        return;
    }
    std::erase(m_RecentProjects, normalized);
    m_RecentProjects.insert(m_RecentProjects.begin(), std::move(normalized));
    constexpr std::size_t maxRecentProjects = 20;
    if (m_RecentProjects.size() > maxRecentProjects) {
        m_RecentProjects.resize(maxRecentProjects);
    }

    std::string saveError;
    if (!SaveRecentProjects(saveError)) {
        m_Error = "Project opened, but recent projects could not be saved: " +
                  saveError;
    }
}

bool ProjectLauncher::SaveRecentProjects(std::string& error) const {
    error.clear();
    std::string pathError;
    const std::filesystem::path recentFile = RecentProjectsFile(pathError);
    if (!pathError.empty()) {
        error = std::move(pathError);
        return false;
    }

    std::error_code filesystemError;
    std::filesystem::create_directories(recentFile.parent_path(),
                                        filesystemError);
    if (filesystemError) {
        error = "Could not create configuration directory: " +
                filesystemError.message();
        return false;
    }
    std::ofstream output(recentFile, std::ios::trunc);
    if (!output) {
        error = "Could not write recent projects file '" + recentFile.string() +
                "'.";
        return false;
    }
    for (const std::filesystem::path& projectFile : m_RecentProjects) {
        output << std::quoted(projectFile.string()) << '\n';
    }
    if (!output) {
        error = "Could not finish writing recent projects file.";
        return false;
    }
    return true;
}

void ProjectLauncher::OpenRecentProject(
    const std::filesystem::path& file) {
    std::error_code filesystemError;
    const bool exists = std::filesystem::exists(file, filesystemError);
    if (filesystemError) {
        m_Error = "Could not inspect recent project '" + file.string() +
                  "': " + filesystemError.message();
        return;
    }
    if (!exists) {
        m_MissingRecentProject = file;
        m_OpenMissingRecentPrompt = true;
        return;
    }
    SelectProjectFile(file);
}

bool ProjectLauncher::IsMaximized() const {
    return IsWindowMaximized(m_Window);
}

void ProjectLauncher::CreateProject() {
    m_Error.clear();
    std::string projectName = m_ProjectName;
    const auto isSpace = [](unsigned char character) {
        return std::isspace(character) != 0;
    };
    projectName.erase(projectName.begin(),
                      std::find_if_not(projectName.begin(), projectName.end(),
                                       isSpace));
    projectName.erase(std::find_if_not(projectName.rbegin(),
                                       projectName.rend(), isSpace)
                          .base(),
                      projectName.end());

    if (projectName.empty()) {
        m_Error = "Enter a project name.";
        return;
    }
    if (projectName == "." || projectName == ".." ||
        projectName.find_first_of("<>:\"/\\|?*") != std::string::npos ||
        projectName.back() == '.') {
        m_Error = "The project name contains characters that cannot be used "
                  "in a folder name.";
        return;
    }

    std::error_code error;
    if (!std::filesystem::is_directory(m_ProjectLocation, error) || error) {
        m_Error = "Choose an existing parent folder.";
        return;
    }

    const std::filesystem::path projectRoot =
        m_ProjectLocation / (projectName + "_AquEng");
    if (!std::filesystem::create_directory(projectRoot, error)) {
        m_Error = error ? "Could not create the project folder: " +
                              error.message()
                        : "A folder with this project name already exists.";
        return;
    }

    try {
        const std::filesystem::path projectAssets = projectRoot / "assets";
        std::filesystem::create_directories(projectAssets, error);
        if (error) {
            throw std::filesystem::filesystem_error(
                "Could not create the project assets folder", projectAssets,
                error);
        }

        std::ofstream projectFile(projectRoot / (projectName + ".aqenproj"),
                                  std::ios::binary);
        if (!projectFile) {
            throw std::runtime_error("Could not create the .aqenproj file.");
        }
        projectFile << "name=" << projectName << '\n'
                    << "version=1\n"
                    << "assets=assets\n";
        if (!projectFile) {
            throw std::runtime_error("Could not write the .aqenproj file.");
        }
        std::string sceneError;
        if (!CreateDefaultEditorScene(projectRoot, sceneError)) {
            throw std::runtime_error(
                "Could not create the default editor scene: " + sceneError);
        }
    } catch (const std::exception& exception) {
        std::error_code cleanupError;
        std::filesystem::remove_all(projectRoot, cleanupError);
        m_Error = exception.what();
        if (cleanupError) {
            m_Error += " Cleanup failed: " + cleanupError.message();
        }
        return;
    }

    m_CreatedProject = projectRoot / (projectName + ".aqenproj");
    AddRecentProject(*m_CreatedProject);
}

#include "AssetBrowser.hpp"
#include "ImGuiFont.hpp"
#include "ModelFileDialog.hpp"

#include <glad/glad.h>
#include <GLFW/glfw3.h>
#include <imgui.h>
#include <backends/imgui_impl_glfw.h>
#include <backends/imgui_impl_opengl3.h>
#include <stb_image.h>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>

#include <assimp/Importer.hpp>
#include <assimp/postprocess.h>
#include <assimp/scene.h>

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <ctime>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <limits>
#include <sstream>
#include <system_error>
#include <utility>

namespace {

std::string ToLower(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(),
                   [](unsigned char character) {
                       return static_cast<char>(std::tolower(character));
                   });
    return value;
}

std::string RelativeAssetPath(const std::filesystem::path& path,
                              const std::filesystem::path& root) {
    return path.lexically_relative(root).generic_string();
}

bool IsWithinDirectory(const std::filesystem::path& path,
                       const std::filesystem::path& directory) {
    const std::filesystem::path relative = path.lexically_relative(directory);
    return !relative.empty() && *relative.begin() != "..";
}

bool IsStrictlyWithinDirectory(const std::filesystem::path& path,
                               const std::filesystem::path& directory) {
    return path != directory && IsWithinDirectory(path, directory);
}

bool ResolveProjectDirectories(
    const std::filesystem::path& projectRoot,
    const std::filesystem::path& currentDirectory,
    std::filesystem::path& resolvedRoot,
    std::filesystem::path& resolvedCurrent, std::string& error) {
    std::error_code filesystemError;
    resolvedRoot = std::filesystem::canonical(projectRoot, filesystemError);
    if (filesystemError) {
        error = "Could not resolve the project folder: " +
                filesystemError.message();
        return false;
    }
    resolvedCurrent =
        std::filesystem::canonical(currentDirectory, filesystemError);
    if (filesystemError ||
        !IsWithinDirectory(resolvedCurrent, resolvedRoot)) {
        error = "Could not safely resolve the current project folder." +
                (filesystemError ? " " + filesystemError.message()
                                 : std::string{});
        return false;
    }
    return true;
}

bool ResolveProjectChildFolder(
    const std::filesystem::path& projectRoot,
    const std::filesystem::path& currentDirectory,
    const std::filesystem::path& folder,
    std::filesystem::path& resolvedRoot,
    std::filesystem::path& resolvedCurrent,
    std::filesystem::path& resolvedFolder, std::string& error) {
    if (!ResolveProjectDirectories(projectRoot, currentDirectory, resolvedRoot,
                                   resolvedCurrent, error)) {
        return false;
    }
    std::error_code filesystemError;
    const auto status = std::filesystem::symlink_status(folder, filesystemError);
    if (filesystemError || !std::filesystem::is_directory(status) ||
        std::filesystem::is_symlink(status)) {
        error = "The selected folder is unavailable or is a symbolic link." +
                (filesystemError ? " " + filesystemError.message()
                                 : std::string{});
        return false;
    }
    resolvedFolder = std::filesystem::canonical(folder, filesystemError);
    if (filesystemError) {
        error = "Could not resolve the selected folder: " +
                filesystemError.message();
        return false;
    }
    if (resolvedFolder.parent_path() != resolvedCurrent ||
        !IsWithinDirectory(resolvedFolder, resolvedRoot)) {
        error =
            "The selected folder is outside the current project location.";
        return false;
    }
    return true;
}

bool IsPreviewableImage(const std::filesystem::path& file) {
    const std::string extension = ToLower(file.extension().string());
    return extension == ".png" || extension == ".jpg" ||
           extension == ".jpeg" || extension == ".bmp" ||
           extension == ".tga" || extension == ".gif" ||
           extension == ".psd" || extension == ".pic" ||
           extension == ".hdr" || extension == ".ppm" ||
           extension == ".pgm";
}

bool IsMeshAsset(const std::filesystem::path& file) {
    const std::string extension = ToLower(file.extension().string());
    return extension == ".glb" || extension == ".gltf" ||
           extension == ".fbx" || extension == ".obj";
}

bool LoadPreviewMeshData(const std::filesystem::path& file,
                         ModelPreviewMesh& mesh, std::string& error) {
    error.clear();
    Assimp::Importer importer;
    const aiScene* scene = importer.ReadFile(
        file.string(), aiProcess_Triangulate | aiProcess_GenSmoothNormals |
                           aiProcess_JoinIdenticalVertices |
                           aiProcess_ValidateDataStructure);
    if (!scene || !scene->mRootNode ||
        (scene->mFlags & AI_SCENE_FLAGS_INCOMPLETE) != 0) {
        error = "Could not load model preview '" + file.string() +
                "': " + importer.GetErrorString();
        return false;
    }

    constexpr std::size_t maxPreviewVertices = 100'000;
    constexpr std::size_t maxPreviewIndices = 300'000;
    ModelPreviewMesh preview;
    bool exceededPreviewLimit = false;
    const auto appendNode = [&](const auto& self, const aiNode* node,
                                const aiMatrix4x4& parentTransform) -> bool {
        const aiMatrix4x4 transform = parentTransform * node->mTransformation;
        aiMatrix3x3 normalTransform(transform);
        normalTransform.Inverse().Transpose();
        for (unsigned int i = 0; i < node->mNumMeshes; ++i) {
            const aiMesh* sourceMesh = scene->mMeshes[node->mMeshes[i]];
            if (!sourceMesh->HasPositions() || !sourceMesh->HasNormals()) {
                continue;
            }
            const std::size_t vertexOffset = preview.vertices.size();
            if (sourceMesh->mNumVertices == 0 ||
                vertexOffset + sourceMesh->mNumVertices >
                    maxPreviewVertices ||
                preview.indices.size() +
                        static_cast<std::size_t>(sourceMesh->mNumFaces) * 3 >
                    maxPreviewIndices) {
                exceededPreviewLimit = true;
                return false;
            }
            for (unsigned int vertex = 0;
                 vertex < sourceMesh->mNumVertices; ++vertex) {
                const aiVector3D position =
                    transform * sourceMesh->mVertices[vertex];
                aiVector3D normal =
                    normalTransform * sourceMesh->mNormals[vertex];
                normal.Normalize();
                const aiVector3D uv =
                    sourceMesh->HasTextureCoords(0)
                        ? sourceMesh->mTextureCoords[0][vertex]
                        : aiVector3D(0.0f, 0.0f, 0.0f);
                preview.vertices.push_back(
                    {position.x, position.y, position.z, normal.x, normal.y,
                     normal.z, uv.x, uv.y});
            }
            for (unsigned int face = 0; face < sourceMesh->mNumFaces; ++face) {
                const aiFace& sourceFace = sourceMesh->mFaces[face];
                if (sourceFace.mNumIndices != 3) {
                    continue;
                }
                for (unsigned int index = 0; index < 3; ++index) {
                    preview.indices.push_back(
                        static_cast<std::uint32_t>(vertexOffset) +
                        sourceFace.mIndices[index]);
                }
            }
        }
        for (unsigned int child = 0; child < node->mNumChildren; ++child) {
            if (!self(self, node->mChildren[child], transform)) {
                return false;
            }
        }
        return true;
    };

    if (!appendNode(appendNode, scene->mRootNode, aiMatrix4x4{})) {
        error = exceededPreviewLimit
                    ? "Model is too large for the content-browser preview."
                    : "Could not build the content-browser model preview.";
        return false;
    }
    if (preview.vertices.empty() || preview.indices.empty()) {
        error = "The model contains no previewable triangle geometry.";
        return false;
    }
    mesh = std::move(preview);
    return true;
}

bool IsMaterialAsset(const std::filesystem::path& file) {
    return ToLower(file.extension().string()) == ".aqenmat";
}

GLuint CompilePreviewShader(GLenum type, const char* source,
                            std::string& error) {
    const GLuint shader = glCreateShader(type);
    glShaderSource(shader, 1, &source, nullptr);
    glCompileShader(shader);
    GLint compiled = GL_FALSE;
    glGetShaderiv(shader, GL_COMPILE_STATUS, &compiled);
    if (compiled == GL_FALSE) {
        GLint logLength = 0;
        glGetShaderiv(shader, GL_INFO_LOG_LENGTH, &logLength);
        std::string log(static_cast<std::size_t>(std::max(logLength, 1)), '\0');
        glGetShaderInfoLog(shader, logLength, nullptr, log.data());
        glDeleteShader(shader);
        error = "Material preview shader compilation failed: " + log;
        return 0;
    }
    return shader;
}

int RotationDegrees(float radians) {
    if (!std::isfinite(radians)) {
        return 0;
    }
    constexpr float radiansToDegrees = 180.0f / 3.14159265358979323846f;
    float degrees = std::fmod(radians * radiansToDegrees, 360.0f);
    if (degrees < 0.0f) {
        degrees += 360.0f;
    }
    return static_cast<int>(std::lround(degrees)) % 360;
}

std::string CurrentTimestamp() {
    const std::time_t currentTime = std::time(nullptr);
    const std::tm* localTime = std::localtime(&currentTime);
    if (!localTime) {
        return "Unknown time";
    }
    std::ostringstream timestamp;
    timestamp << std::put_time(localTime, "%Y-%m-%d %H:%M:%S");
    return timestamp.str();
}

} // namespace

bool AssetBrowser::Initialize(GLFWwindow* window,
                              std::filesystem::path projectRoot,
                              std::filesystem::path assetRoot,
                              std::string& error) {
    if (!window) {
        error = "Cannot initialize the asset browser without a GLFW window.";
        return false;
    }

    std::error_code pathError;
    m_ProjectRoot = std::filesystem::canonical(projectRoot, pathError);
    if (pathError) {
        error = "Could not resolve the project folder: " +
                pathError.message();
        return false;
    }
    if (!std::filesystem::is_directory(m_ProjectRoot, pathError) ||
        pathError) {
        error = "The project folder is unavailable." +
                (pathError ? " " + pathError.message() : std::string{});
        return false;
    }

    const std::filesystem::file_status assetStatus =
        std::filesystem::symlink_status(assetRoot, pathError);
    if (pathError || std::filesystem::is_symlink(assetStatus)) {
        error = "The project assets directory is unavailable or is a "
                "symbolic link.";
        if (pathError) {
            error += " " + pathError.message();
        }
        return false;
    }
    if (!std::filesystem::is_directory(assetStatus)) {
        error = "The project assets directory is missing or unavailable: " +
                assetRoot.string();
        return false;
    }
    m_AssetRoot = std::filesystem::canonical(assetRoot, pathError);
    if (pathError) {
        error = "Could not resolve the project assets folder: " +
                pathError.message();
        return false;
    }
    if (!IsStrictlyWithinDirectory(m_AssetRoot, m_ProjectRoot)) {
        error = "The project assets folder must resolve inside the project.";
        return false;
    }

    m_MainWindow = window;
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    m_MainImGuiContext = ImGui::GetCurrentContext();
    LoadBundledFont();
    m_LargeDialogFont = LoadBundledFont(32.0f);
    ImGui::StyleColorsDark();
    ImGui::GetStyle().WindowRounding = 0.0f;

    if (!ImGui_ImplGlfw_InitForOpenGL(window, true)) {
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

    m_WorkLogPath = m_ProjectRoot / "editor-worklog.log";
    std::ifstream workLog(m_WorkLogPath);
    if (workLog) {
        std::string entry;
        while (std::getline(workLog, entry)) {
            m_WorkLog.push_back(std::move(entry));
        }
        if (workLog.bad()) {
            std::cerr << "Could not read the complete work log: "
                      << m_WorkLogPath << '\n';
            SetStatusMessage("Could not read the complete work log.");
        }
    } else {
        std::error_code logError;
        const bool logExists = std::filesystem::exists(m_WorkLogPath, logError);
        if (logError || logExists) {
            const std::string message =
                "Could not open work log '" + m_WorkLogPath.string() + "'" +
                (logError ? ": " + logError.message() : ".");
            std::cerr << message << '\n';
            SetStatusMessage(message);
        }
    }
    m_CurrentDirectory = m_AssetRoot;
    m_Initialized = true;
    RefreshEntries();
    return true;
}

void AssetBrowser::Shutdown() noexcept {
    if (!m_Initialized) {
        return;
    }
    DestroyMaterialEditorWindow();
    DestroyLandscapeWindow();
    glfwMakeContextCurrent(m_MainWindow);
    ImGui::SetCurrentContext(m_MainImGuiContext);
    if (m_PreviewTexture != 0) {
        const GLuint texture = m_PreviewTexture;
        glDeleteTextures(1, &texture);
        m_PreviewTexture = 0;
    }
    for (const auto& [path, thumbnail] : m_Thumbnails) {
        if (thumbnail.texture != 0) {
            const GLuint texture = thumbnail.texture;
            glDeleteTextures(1, &texture);
        }
    }
    m_Thumbnails.clear();
    if (m_MaterialPreviewProgram != 0) {
        glDeleteProgram(m_MaterialPreviewProgram);
    }
    for (PreviewTarget& target : m_PreviewTargets) {
        DestroyPreviewTarget(target);
    }
    if (m_MaterialPreviewVertexBuffer != 0) {
        glDeleteBuffers(1, &m_MaterialPreviewVertexBuffer);
        m_MaterialPreviewVertexBuffer = 0;
    }
    if (m_MaterialPreviewIndexBuffer != 0) {
        glDeleteBuffers(1, &m_MaterialPreviewIndexBuffer);
        m_MaterialPreviewIndexBuffer = 0;
    }
    glDeleteTextures(5, m_MaterialPreviewFallbackTextures);
    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImGui::DestroyContext();
    m_MainImGuiContext = nullptr;
    m_MainWindow = nullptr;
    m_Initialized = false;
}

void AssetBrowser::DestroyMaterialEditorWindow() noexcept {
    if (!m_MaterialEditorWindow) {
        return;
    }
    glfwMakeContextCurrent(m_MaterialEditorWindow);
    if (m_MaterialEditorContext) {
        ImGui::SetCurrentContext(m_MaterialEditorContext);
        ImGui_ImplOpenGL3_Shutdown();
        ImGui_ImplGlfw_Shutdown();
        ImGui::DestroyContext(m_MaterialEditorContext);
        m_MaterialEditorContext = nullptr;
    }
    glfwDestroyWindow(m_MaterialEditorWindow);
    m_MaterialEditorWindow = nullptr;
    if (m_MainWindow) {
        glfwMakeContextCurrent(m_MainWindow);
    }
    if (m_MainImGuiContext) {
        ImGui::SetCurrentContext(m_MainImGuiContext);
    }
}

void AssetBrowser::DestroyLandscapeWindow() noexcept {
    if (!m_LandscapeWindow) {
        return;
    }
    glfwMakeContextCurrent(m_LandscapeWindow);
    if (m_LandscapeImGuiContext) {
        ImGui::SetCurrentContext(m_LandscapeImGuiContext);
        ImGui_ImplOpenGL3_Shutdown();
        ImGui_ImplGlfw_Shutdown();
        ImGui::DestroyContext(m_LandscapeImGuiContext);
        m_LandscapeImGuiContext = nullptr;
    }
    glfwDestroyWindow(m_LandscapeWindow);
    m_LandscapeWindow = nullptr;
    if (m_MainWindow) {
        glfwMakeContextCurrent(m_MainWindow);
    }
    if (m_MainImGuiContext) {
        ImGui::SetCurrentContext(m_MainImGuiContext);
    }
}

bool AssetBrowser::InitializeMaterialEditorWindow(std::string& error) {
    error.clear();
    if (m_MaterialEditorWindow) {
        return true;
    }

    glfwMakeContextCurrent(m_MainWindow);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 4);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 6);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
    glfwWindowHint(GLFW_VISIBLE, GLFW_TRUE);
    const std::string title =
        (m_EditingMaterialPath.empty() ? "Create Material"
                                       : "Edit Material") +
        std::string(" - Aquarius Engine");
    m_MaterialEditorWindow =
        glfwCreateWindow(760, 960, title.c_str(), nullptr, m_MainWindow);
    if (!m_MaterialEditorWindow) {
        error = "Could not create the material editor window.";
        glfwMakeContextCurrent(m_MainWindow);
        ImGui::SetCurrentContext(m_MainImGuiContext);
        return false;
    }

    glfwMakeContextCurrent(m_MaterialEditorWindow);
    ImGui::SetCurrentContext(m_MainImGuiContext);
    const ImGuiStyle mainStyle = ImGui::GetStyle();
    m_MaterialEditorContext = ImGui::CreateContext();
    ImGui::SetCurrentContext(m_MaterialEditorContext);
    ImGui::GetIO().IniFilename = nullptr;
    ImGui::GetStyle() = mainStyle;
    LoadBundledFont();
    if (!ImGui_ImplGlfw_InitForOpenGL(m_MaterialEditorWindow, false)) {
        error = "Dear ImGui could not initialize the material window input.";
        ImGui::DestroyContext(m_MaterialEditorContext);
        m_MaterialEditorContext = nullptr;
        glfwDestroyWindow(m_MaterialEditorWindow);
        m_MaterialEditorWindow = nullptr;
        glfwMakeContextCurrent(m_MainWindow);
        ImGui::SetCurrentContext(m_MainImGuiContext);
        return false;
    }
    if (!ImGui_ImplOpenGL3_Init("#version 460 core")) {
        error = "Dear ImGui could not initialize the material window renderer.";
        ImGui_ImplGlfw_Shutdown();
        ImGui::DestroyContext(m_MaterialEditorContext);
        m_MaterialEditorContext = nullptr;
        glfwDestroyWindow(m_MaterialEditorWindow);
        m_MaterialEditorWindow = nullptr;
        glfwMakeContextCurrent(m_MainWindow);
        ImGui::SetCurrentContext(m_MainImGuiContext);
        return false;
    }

    glfwSetWindowUserPointer(m_MaterialEditorWindow, this);
    glfwSetWindowFocusCallback(m_MaterialEditorWindow,
                               MaterialEditorFocusCallback);
    glfwSetCursorEnterCallback(m_MaterialEditorWindow,
                               MaterialEditorCursorEnterCallback);
    glfwSetCursorPosCallback(m_MaterialEditorWindow,
                             MaterialEditorCursorPositionCallback);
    glfwSetMouseButtonCallback(m_MaterialEditorWindow,
                               MaterialEditorMouseButtonCallback);
    glfwSetScrollCallback(m_MaterialEditorWindow,
                          MaterialEditorScrollCallback);
    glfwSetKeyCallback(m_MaterialEditorWindow, MaterialEditorKeyCallback);
    glfwSetCharCallback(m_MaterialEditorWindow,
                        MaterialEditorCharacterCallback);
    glfwMakeContextCurrent(m_MainWindow);
    ImGui::SetCurrentContext(m_MainImGuiContext);
    return true;
}

bool AssetBrowser::InitializeLandscapeWindow(std::string& error) {
    error.clear();
    if (m_LandscapeWindow) {
        return true;
    }
    glfwMakeContextCurrent(m_MainWindow);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 4);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 6);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
    glfwWindowHint(GLFW_VISIBLE, GLFW_TRUE);
    m_LandscapeWindow = glfwCreateWindow(
        520, 390, "Create Landscape Plane - Aquarius Engine", nullptr,
        m_MainWindow);
    if (!m_LandscapeWindow) {
        error = "Could not create the landscape plane window.";
        return false;
    }

    glfwMakeContextCurrent(m_LandscapeWindow);
    ImGui::SetCurrentContext(m_MainImGuiContext);
    const ImGuiStyle mainStyle = ImGui::GetStyle();
    m_LandscapeImGuiContext = ImGui::CreateContext();
    ImGui::SetCurrentContext(m_LandscapeImGuiContext);
    ImGui::GetIO().IniFilename = nullptr;
    ImGui::GetStyle() = mainStyle;
    LoadBundledFont();
    if (!ImGui_ImplGlfw_InitForOpenGL(m_LandscapeWindow, false)) {
        error = "Dear ImGui could not initialize the landscape window input.";
        ImGui::DestroyContext(m_LandscapeImGuiContext);
        m_LandscapeImGuiContext = nullptr;
        glfwDestroyWindow(m_LandscapeWindow);
        m_LandscapeWindow = nullptr;
        glfwMakeContextCurrent(m_MainWindow);
        ImGui::SetCurrentContext(m_MainImGuiContext);
        return false;
    }
    if (!ImGui_ImplOpenGL3_Init("#version 460 core")) {
        error = "Dear ImGui could not initialize the landscape window renderer.";
        ImGui_ImplGlfw_Shutdown();
        ImGui::DestroyContext(m_LandscapeImGuiContext);
        m_LandscapeImGuiContext = nullptr;
        glfwDestroyWindow(m_LandscapeWindow);
        m_LandscapeWindow = nullptr;
        glfwMakeContextCurrent(m_MainWindow);
        ImGui::SetCurrentContext(m_MainImGuiContext);
        return false;
    }
    glfwSetWindowUserPointer(m_LandscapeWindow, this);
    glfwSetWindowFocusCallback(m_LandscapeWindow, MaterialEditorFocusCallback);
    glfwSetCursorEnterCallback(m_LandscapeWindow,
                               MaterialEditorCursorEnterCallback);
    glfwSetCursorPosCallback(m_LandscapeWindow,
                             MaterialEditorCursorPositionCallback);
    glfwSetMouseButtonCallback(m_LandscapeWindow,
                               MaterialEditorMouseButtonCallback);
    glfwSetScrollCallback(m_LandscapeWindow, MaterialEditorScrollCallback);
    glfwSetKeyCallback(m_LandscapeWindow, MaterialEditorKeyCallback);
    glfwSetCharCallback(m_LandscapeWindow, MaterialEditorCharacterCallback);
    glfwMakeContextCurrent(m_MainWindow);
    ImGui::SetCurrentContext(m_MainImGuiContext);
    return true;
}

void AssetBrowser::RenderMaterialEditorWindow() {
    if (!m_MaterialCreatorOpen) {
        DestroyMaterialEditorWindow();
        return;
    }
    if (!m_MaterialEditorWindow) {
        std::string error;
        if (!InitializeMaterialEditorWindow(error)) {
            m_MaterialError = error;
            SetStatusMessage(error);
            m_MaterialCreatorOpen = false;
            return;
        }
    }
    if (glfwWindowShouldClose(m_MaterialEditorWindow)) {
        m_MaterialCreatorOpen = false;
        DestroyMaterialEditorWindow();
        return;
    }

    const std::string title =
        (m_EditingMaterialPath.empty()
             ? std::string("Create Material")
             : std::string("Edit Material: ") + m_MaterialDraft.name) +
        " - Aquarius Engine";
    glfwSetWindowTitle(m_MaterialEditorWindow, title.c_str());
    glfwMakeContextCurrent(m_MaterialEditorWindow);
    ImGui::SetCurrentContext(m_MaterialEditorContext);
    ImGui_ImplOpenGL3_NewFrame();
    ImGui_ImplGlfw_NewFrame();
    ImGui::NewFrame();
    const ImGuiIO& io = ImGui::GetIO();
    ImGui::SetNextWindowPos(ImVec2(0.0f, 0.0f), ImGuiCond_Always);
    ImGui::SetNextWindowSize(io.DisplaySize, ImGuiCond_Always);
    DrawMaterialCreator();
    ImGui::Render();

    int framebufferWidth = 0;
    int framebufferHeight = 0;
    glfwGetFramebufferSize(m_MaterialEditorWindow, &framebufferWidth,
                           &framebufferHeight);
    glViewport(0, 0, framebufferWidth, framebufferHeight);
    glClearColor(0.075f, 0.085f, 0.10f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);
    ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
    glfwSwapBuffers(m_MaterialEditorWindow);

    glfwMakeContextCurrent(m_MainWindow);
    ImGui::SetCurrentContext(m_MainImGuiContext);
    if (!m_MaterialCreatorOpen) {
        DestroyMaterialEditorWindow();
    }
}

void AssetBrowser::RenderLandscapeWindow() {
    if (!m_LandscapeCreatorOpen) {
        DestroyLandscapeWindow();
        return;
    }
    if (!m_LandscapeWindow) {
        std::string error;
        if (!InitializeLandscapeWindow(error)) {
            m_LandscapeError = error;
            SetStatusMessage(error);
            m_LandscapeCreatorOpen = false;
            return;
        }
    }
    if (glfwWindowShouldClose(m_LandscapeWindow)) {
        m_LandscapeCreatorOpen = false;
        DestroyLandscapeWindow();
        return;
    }

    glfwMakeContextCurrent(m_LandscapeWindow);
    ImGui::SetCurrentContext(m_LandscapeImGuiContext);
    ImGui_ImplOpenGL3_NewFrame();
    ImGui_ImplGlfw_NewFrame();
    ImGui::NewFrame();
    ImGui::SetNextWindowPos(ImVec2(0.0f, 0.0f), ImGuiCond_Always);
    ImGui::SetNextWindowSize(ImGui::GetIO().DisplaySize, ImGuiCond_Always);
    if (ImGui::Begin("Landscape Plane", nullptr,
                     ImGuiWindowFlags_NoDecoration |
                         ImGuiWindowFlags_NoMove |
                         ImGuiWindowFlags_NoResize |
                         ImGuiWindowFlags_NoSavedSettings)) {
        ImGui::TextUnformatted("Create a subdivided plane in the scene.");
        ImGui::TextDisabled(
            "This static mesh can be sculpted into terrain later.");
        ImGui::Separator();
        ImGui::TextUnformatted("Asset name");
        ImGui::SetNextItemWidth(-1.0f);
        ImGui::InputText("##LandscapeName", m_LandscapeName,
                         sizeof(m_LandscapeName));
        ImGui::InputFloat("Width", &m_LandscapeWidth, 1.0f, 10.0f, "%.1f");
        ImGui::InputFloat("Depth", &m_LandscapeDepth, 1.0f, 10.0f, "%.1f");
        ImGui::InputInt("Segments X", &m_LandscapeSegmentsX);
        ImGui::InputInt("Segments Z", &m_LandscapeSegmentsZ);
        ImGui::TextDisabled("More segments allow finer future terrain detail.");
        if (!m_LandscapeError.empty()) {
            ImGui::TextWrapped("%s", m_LandscapeError.c_str());
        }
        if (ImGui::Button("Add Plane to Scene", ImVec2(-1.0f, 0.0f))) {
            EditorSceneModel landscape;
            landscape.name = m_LandscapeName;
            landscape.proceduralLandscape = true;
            landscape.landscapeWidth = m_LandscapeWidth;
            landscape.landscapeDepth = m_LandscapeDepth;
            landscape.landscapeSegmentsX = static_cast<unsigned int>(
                std::max(m_LandscapeSegmentsX, 0));
            landscape.landscapeSegmentsZ = static_cast<unsigned int>(
                std::max(m_LandscapeSegmentsZ, 0));
            m_PendingLandscape = std::move(landscape);
            m_LandscapeError.clear();
            m_LandscapeCreatorOpen = false;
            SetStatusMessage("Created landscape plane '" +
                             std::string(m_LandscapeName) + "'.");
        }
        if (ImGui::Button("Close", ImVec2(-1.0f, 0.0f))) {
            m_LandscapeCreatorOpen = false;
        }
    }
    ImGui::End();
    ImGui::Render();
    int framebufferWidth = 0;
    int framebufferHeight = 0;
    glfwGetFramebufferSize(m_LandscapeWindow, &framebufferWidth,
                           &framebufferHeight);
    glViewport(0, 0, framebufferWidth, framebufferHeight);
    glClearColor(0.075f, 0.085f, 0.10f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);
    ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
    glfwSwapBuffers(m_LandscapeWindow);
    glfwMakeContextCurrent(m_MainWindow);
    ImGui::SetCurrentContext(m_MainImGuiContext);
    if (!m_LandscapeCreatorOpen) {
        DestroyLandscapeWindow();
    }
}

namespace {

// Makes an ImGui context current for the scope and restores the previous one.
class ImGuiContextScope {
public:
    explicit ImGuiContextScope(ImGuiContext* context)
        : m_Previous(ImGui::GetCurrentContext()) {
        ImGui::SetCurrentContext(context);
    }
    ImGuiContextScope(const ImGuiContextScope&) = delete;
    ImGuiContextScope& operator=(const ImGuiContextScope&) = delete;
    ~ImGuiContextScope() { ImGui::SetCurrentContext(m_Previous); }

private:
    ImGuiContext* m_Previous;
};

}  // namespace

void AssetBrowser::MaterialEditorFocusCallback(GLFWwindow* window,
                                               int focused) {
    auto* browser =
        static_cast<AssetBrowser*>(glfwGetWindowUserPointer(window));
    ImGuiContext* context =
        browser ? browser->GetAuxiliaryImGuiContext(window) : nullptr;
    if (!context) {
        return;
    }
    ImGuiContextScope scope(context);
    ImGui_ImplGlfw_WindowFocusCallback(window, focused);
}

void AssetBrowser::MaterialEditorCursorEnterCallback(GLFWwindow* window,
                                                     int entered) {
    auto* browser =
        static_cast<AssetBrowser*>(glfwGetWindowUserPointer(window));
    ImGuiContext* context =
        browser ? browser->GetAuxiliaryImGuiContext(window) : nullptr;
    if (!context) {
        return;
    }
    ImGuiContextScope scope(context);
    ImGui_ImplGlfw_CursorEnterCallback(window, entered);
}

void AssetBrowser::MaterialEditorCursorPositionCallback(GLFWwindow* window,
                                                        double x, double y) {
    auto* browser =
        static_cast<AssetBrowser*>(glfwGetWindowUserPointer(window));
    ImGuiContext* context =
        browser ? browser->GetAuxiliaryImGuiContext(window) : nullptr;
    if (!context) {
        return;
    }
    ImGuiContextScope scope(context);
    ImGui_ImplGlfw_CursorPosCallback(window, x, y);
}

void AssetBrowser::MaterialEditorMouseButtonCallback(GLFWwindow* window,
                                                     int button, int action,
                                                     int mods) {
    auto* browser =
        static_cast<AssetBrowser*>(glfwGetWindowUserPointer(window));
    ImGuiContext* context =
        browser ? browser->GetAuxiliaryImGuiContext(window) : nullptr;
    if (!context) {
        return;
    }
    ImGuiContextScope scope(context);
    ImGui_ImplGlfw_MouseButtonCallback(window, button, action, mods);
}

void AssetBrowser::MaterialEditorScrollCallback(GLFWwindow* window,
                                                double xOffset,
                                                double yOffset) {
    auto* browser =
        static_cast<AssetBrowser*>(glfwGetWindowUserPointer(window));
    ImGuiContext* context =
        browser ? browser->GetAuxiliaryImGuiContext(window) : nullptr;
    if (!context) {
        return;
    }
    ImGuiContextScope scope(context);
    ImGui_ImplGlfw_ScrollCallback(window, xOffset, yOffset);
}

void AssetBrowser::MaterialEditorKeyCallback(GLFWwindow* window, int key,
                                             int scanCode, int action,
                                             int mods) {
    auto* browser =
        static_cast<AssetBrowser*>(glfwGetWindowUserPointer(window));
    ImGuiContext* context =
        browser ? browser->GetAuxiliaryImGuiContext(window) : nullptr;
    if (!context) {
        return;
    }
    ImGuiContextScope scope(context);
    ImGui_ImplGlfw_KeyCallback(window, key, scanCode, action, mods);
}

void AssetBrowser::MaterialEditorCharacterCallback(GLFWwindow* window,
                                                   unsigned int codepoint) {
    auto* browser =
        static_cast<AssetBrowser*>(glfwGetWindowUserPointer(window));
    ImGuiContext* context =
        browser ? browser->GetAuxiliaryImGuiContext(window) : nullptr;
    if (!context) {
        return;
    }
    ImGuiContextScope scope(context);
    ImGui_ImplGlfw_CharCallback(window, codepoint);
}

ImGuiContext* AssetBrowser::GetAuxiliaryImGuiContext(
    GLFWwindow* window) const {
    if (window == m_MaterialEditorWindow) {
        return m_MaterialEditorContext;
    }
    if (window == m_LandscapeWindow) {
        return m_LandscapeImGuiContext;
    }
    return nullptr;
}

bool AssetBrowser::WantsMouse() const {
    if (!m_Initialized) {
        return false;
    }
    if (!m_MainWindow ||
        glfwGetWindowAttrib(m_MainWindow, GLFW_FOCUSED) != GLFW_TRUE) {
        return false;
    }
    if (m_MaterialEditorWindow &&
        glfwGetWindowAttrib(m_MaterialEditorWindow, GLFW_FOCUSED) == GLFW_TRUE) {
        return true;
    }
    const ImGuiIO& io = ImGui::GetIO();
    bool overUnresolvedPanel = false;
    if (m_UnresolvedPanelVisible) {
        double cursorX = 0.0;
        double cursorY = 0.0;
        glfwGetCursorPos(m_MainWindow, &cursorX, &cursorY);
        overUnresolvedPanel =
            cursorX >= m_UnresolvedPanelMin.x &&
            cursorX <= m_UnresolvedPanelMax.x &&
            cursorY >= m_UnresolvedPanelMin.y &&
            cursorY <= m_UnresolvedPanelMax.y;
    }
    bool overInspectorPanel = false;
    if (m_InspectorPanelVisible) {
        double cursorX = 0.0;
        double cursorY = 0.0;
        glfwGetCursorPos(m_MainWindow, &cursorX, &cursorY);
        overInspectorPanel =
            cursorX >= m_InspectorPanelMin.x &&
            cursorX <= m_InspectorPanelMax.x &&
            cursorY >= m_InspectorPanelMin.y &&
            cursorY <= m_InspectorPanelMax.y;
    }
    bool overViewportToolbar = false;
    if (m_ViewportToolbarVisible) {
        double cursorX = 0.0;
        double cursorY = 0.0;
        glfwGetCursorPos(m_MainWindow, &cursorX, &cursorY);
        overViewportToolbar =
            cursorX >= m_ViewportToolbarMin.x &&
            cursorX <= m_ViewportToolbarMax.x &&
            cursorY >= m_ViewportToolbarMin.y &&
            cursorY <= m_ViewportToolbarMax.y;
    }
    const bool overWorkLogButton =
        io.MousePos.x >= io.DisplaySize.x - 132.0f && io.MousePos.y <= 44.0f;
    return m_DeleteConfirmationRequested ||
           m_UnresolvedRemoveTarget.has_value() || m_WorkLogOpen ||
           overWorkLogButton || overUnresolvedPanel || overInspectorPanel ||
           overViewportToolbar ||
           m_ViewModeMenuCapturesMouse ||
           m_LightsMenuCapturesMouse ||
           (m_Visible && io.WantCaptureMouse);
}

bool AssetBrowser::WantsKeyboard() const {
    if (!m_Initialized || !m_MainWindow ||
        glfwGetWindowAttrib(m_MainWindow, GLFW_FOCUSED) != GLFW_TRUE) {
        return false;
    }
    return m_DeleteConfirmationRequested ||
           m_UnresolvedRemoveTarget.has_value() || m_WorkLogOpen ||
           m_InspectorPanelCapturesKeyboard ||
           m_ViewModeMenuCapturesKeyboard ||
           m_LightsMenuCapturesKeyboard ||
           (m_Visible && ImGui::GetIO().WantCaptureKeyboard);
}

std::optional<std::filesystem::path> AssetBrowser::TakePendingSceneDrop() {
    return std::exchange(m_PendingSceneDrop, std::nullopt);
}

std::optional<EditorSceneModel> AssetBrowser::TakePendingLandscape() {
    return std::exchange(m_PendingLandscape, std::nullopt);
}

std::optional<LightType> AssetBrowser::TakePendingLightCreation() {
    return std::exchange(m_PendingLightCreation, std::nullopt);
}

bool AssetBrowser::TakeProjectionToggleRequest() {
    return std::exchange(m_ProjectionToggleRequested, false);
}

std::optional<std::filesystem::path>
AssetBrowser::TakePendingMaterialDrop() {
    return std::exchange(m_PendingMaterialDrop, std::nullopt);
}

std::optional<std::pair<std::filesystem::path, ModelMaterial>>
AssetBrowser::TakeUpdatedMaterial() {
    return std::exchange(m_UpdatedMaterial, std::nullopt);
}

std::optional<std::filesystem::path> AssetBrowser::GetActiveSceneDrag() const {
    return m_ActiveSceneDrag;
}

bool AssetBrowser::TakeSaveRequest() {
    return std::exchange(m_SaveRequested, false);
}

std::optional<std::pair<std::filesystem::path, std::filesystem::path>>
AssetBrowser::TakePendingImport() {
    return std::exchange(m_PendingImport, std::nullopt);
}

void AssetBrowser::CompleteModelImport(const std::filesystem::path& output,
                                       std::string error) {
    if (!error.empty()) {
        SetStatusMessage("Model import failed: " + error);
        m_PreviewError = std::move(error);
        return;
    }
    m_CurrentDirectory = output.parent_path();
    RefreshEntries();
    m_SelectedAsset = output;
    LoadPreview(output);
    SetStatusMessage("Imported model " + output.filename().string() + ".");
}

void AssetBrowser::SetPreviewError(std::string error) {
    m_PreviewError = std::move(error);
}

void AssetBrowser::SetStatusMessage(std::string message) {
    if (message.empty()) {
        return;
    }
    std::replace(message.begin(), message.end(), '\n', ' ');
    std::replace(message.begin(), message.end(), '\r', ' ');
    const std::string entry = CurrentTimestamp() + " | " + message;
    m_WorkLog.push_back(entry);

    std::ofstream workLog(m_WorkLogPath, std::ios::app);
    if (!workLog || !(workLog << entry << '\n')) {
        const std::string error =
            "Could not write work log '" + m_WorkLogPath.string() + "'.";
        std::cerr << error << '\n';
        m_WorkLog.push_back(CurrentTimestamp() + " | " + error);
    }
}

void AssetBrowser::RequestDeleteConfirmation(std::string objectName) {
    if (m_DeleteConfirmationRequested) {
        return;
    }
    m_DeleteObjectName = std::move(objectName);
    m_DeleteConfirmationRequested = true;
    m_DeletePopupNeedsOpen = true;
}

std::optional<bool> AssetBrowser::TakeDeleteConfirmationResult() {
    if (!std::exchange(m_DeleteResultPending, false)) {
        return std::nullopt;
    }
    return m_DeleteConfirmed;
}

std::optional<UnresolvedActorAction>
AssetBrowser::TakeUnresolvedActorAction() {
    return std::exchange(m_UnresolvedActorAction, std::nullopt);
}

void AssetBrowser::RefreshEntries() {
    for (const auto& [path, thumbnail] : m_Thumbnails) {
        if (thumbnail.texture != 0) {
            const GLuint texture = thumbnail.texture;
            glDeleteTextures(1, &texture);
        }
    }
    m_Thumbnails.clear();
    m_Directories.clear();
    m_Files.clear();
    m_ScanError.clear();

    std::error_code error;
    if (!std::filesystem::is_directory(m_CurrentDirectory, error)) {
        m_ScanError = "Folder is unavailable: " + m_CurrentDirectory.string();
        if (error) {
            m_ScanError += " (" + error.message() + ")";
        }
        return;
    }

    std::filesystem::directory_iterator iterator(m_CurrentDirectory, error);
    const std::filesystem::directory_iterator end;
    if (error) {
        m_ScanError = "Could not scan folder: " + error.message();
        return;
    }

    while (iterator != end) {
        const std::filesystem::directory_entry& entry = *iterator;
        std::error_code statusError;
        const auto status = entry.symlink_status(statusError);
        if (statusError) {
            m_ScanError = "Could not inspect '" + entry.path().string() +
                          "': " + statusError.message();
        } else if (std::filesystem::is_directory(status)) {
            m_Directories.push_back(entry.path());
        } else if (std::filesystem::is_regular_file(status)) {
            const std::string extension =
                ToLower(entry.path().extension().string());
            if (extension != ".log" && extension != ".aqenproj") {
                m_Files.push_back(entry.path());
            }
        }

        iterator.increment(error);
        if (error) {
            m_ScanError = "Could not continue scanning folder: " +
                          error.message();
            iterator = end;
        }
    }

    const auto sortByName = [](const auto& left, const auto& right) {
        return ToLower(left.filename().string()) <
               ToLower(right.filename().string());
    };
    std::sort(m_Directories.begin(), m_Directories.end(), sortByName);
    std::sort(m_Files.begin(), m_Files.end(), sortByName);
    if (std::find(m_Files.begin(), m_Files.end(), m_SelectedAsset) ==
        m_Files.end()) {
        m_SelectedAsset.clear();
        LoadPreview({});
    }
}

void AssetBrowser::LoadPreview(const std::filesystem::path& file) {
    if (m_PreviewTexture != 0) {
        const GLuint texture = m_PreviewTexture;
        glDeleteTextures(1, &texture);
        m_PreviewTexture = 0;
    }
    m_PreviewWidth = 0;
    m_PreviewHeight = 0;
    m_PreviewError.clear();
    m_PreviewMesh = {};

    if (file.empty()) {
        return;
    }
    if (IsMeshAsset(file)) {
        LoadMeshPreview(file);
        return;
    }
    if (!IsPreviewableImage(file)) {
        return;
    }

    int sourceWidth = 0;
    int sourceHeight = 0;
    GLint maxTextureSize = 0;
    glGetIntegerv(GL_MAX_TEXTURE_SIZE, &maxTextureSize);
    if (!stbi_info(file.string().c_str(), &sourceWidth, &sourceHeight, nullptr) ||
        sourceWidth <= 0 || sourceHeight <= 0 ||
        sourceWidth > maxTextureSize || sourceHeight > maxTextureSize ||
        static_cast<std::uint64_t>(sourceWidth) * sourceHeight > 16'777'216) {
        m_PreviewError = "Image dimensions are invalid or too large to preview.";
        return;
    }

    int channels = 0;
    stbi_uc* pixels = stbi_load(file.string().c_str(), &m_PreviewWidth,
                                &m_PreviewHeight, &channels, STBI_rgb_alpha);
    if (!pixels) {
        m_PreviewWidth = 0;
        m_PreviewHeight = 0;
        m_PreviewError =
            "Could not load image: " +
            std::string(stbi_failure_reason() ? stbi_failure_reason()
                                               : "unknown image error");
        return;
    }
    if (m_PreviewWidth != sourceWidth || m_PreviewHeight != sourceHeight) {
        stbi_image_free(pixels);
        m_PreviewWidth = 0;
        m_PreviewHeight = 0;
        m_PreviewError = "Image dimensions changed while loading the preview.";
        return;
    }

    GLint previousAlignment = 4;
    glGetIntegerv(GL_UNPACK_ALIGNMENT, &previousAlignment);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    GLuint texture = 0;
    glGenTextures(1, &texture);
    glBindTexture(GL_TEXTURE_2D, texture);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, m_PreviewWidth, m_PreviewHeight,
                 0, GL_RGBA, GL_UNSIGNED_BYTE, pixels);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glPixelStorei(GL_UNPACK_ALIGNMENT, previousAlignment);
    glBindTexture(GL_TEXTURE_2D, 0);
    stbi_image_free(pixels);

    if (texture == 0 || glGetError() != GL_NO_ERROR) {
        if (texture != 0) {
            glDeleteTextures(1, &texture);
        }
        m_PreviewWidth = 0;
        m_PreviewHeight = 0;
        m_PreviewError = "Could not upload the image preview to OpenGL.";
        return;
    }
    m_PreviewTexture = texture;
}

bool AssetBrowser::LoadMeshPreview(const std::filesystem::path& file) {
    return LoadPreviewMeshData(file, m_PreviewMesh, m_PreviewError);
}

AssetBrowser::AssetThumbnail&
AssetBrowser::LoadThumbnail(const std::filesystem::path& file) {
    const std::string key = file.string();
    AssetThumbnail& thumbnail = m_Thumbnails[key];
    if (thumbnail.attempted) {
        return thumbnail;
    }
    thumbnail.attempted = true;
    if (IsMeshAsset(file)) {
        thumbnail.mesh =
            LoadPreviewMeshData(file, thumbnail.meshData, m_PreviewError);
        return thumbnail;
    }
    if (!IsPreviewableImage(file)) {
        return thumbnail;
    }

    int imageWidth = 0;
    int imageHeight = 0;
    GLint maxTextureSize = 0;
    glGetIntegerv(GL_MAX_TEXTURE_SIZE, &maxTextureSize);
    if (!stbi_info(file.string().c_str(), &imageWidth, &imageHeight, nullptr) ||
        imageWidth <= 0 || imageHeight <= 0 ||
        imageWidth > maxTextureSize || imageHeight > maxTextureSize ||
        static_cast<std::uint64_t>(imageWidth) * imageHeight > 16'777'216) {
        return thumbnail;
    }

    int channels = 0;
    stbi_uc* pixels =
        stbi_load(file.string().c_str(), &imageWidth, &imageHeight, &channels,
                  STBI_rgb_alpha);
    if (!pixels) {
        return thumbnail;
    }
    GLint previousAlignment = 4;
    glGetIntegerv(GL_UNPACK_ALIGNMENT, &previousAlignment);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    GLuint texture = 0;
    glGenTextures(1, &texture);
    glBindTexture(GL_TEXTURE_2D, texture);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, imageWidth, imageHeight, 0,
                 GL_RGBA, GL_UNSIGNED_BYTE, pixels);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glPixelStorei(GL_UNPACK_ALIGNMENT, previousAlignment);
    glBindTexture(GL_TEXTURE_2D, 0);
    stbi_image_free(pixels);
    if (texture != 0 && glGetError() == GL_NO_ERROR) {
        thumbnail.texture = texture;
        thumbnail.width = imageWidth;
        thumbnail.height = imageHeight;
    } else if (texture != 0) {
        glDeleteTextures(1, &texture);
    }
    return thumbnail;
}

void AssetBrowser::DrawMeshPreview(const ModelPreviewMesh& mesh, ImVec2 origin,
                                   ImVec2 available) {
    if (available.x <= 0.0f || available.y <= 0.0f) {
        return;
    }

    std::array<float, 3> minimum{
        std::numeric_limits<float>::max(),
        std::numeric_limits<float>::max(),
        std::numeric_limits<float>::max()};
    std::array<float, 3> maximum{
        std::numeric_limits<float>::lowest(),
        std::numeric_limits<float>::lowest(),
        std::numeric_limits<float>::lowest()};
    for (const auto& vertex : mesh.vertices) {
        for (int axis = 0; axis < 3; ++axis) {
            minimum[axis] = std::min(minimum[axis], vertex[axis]);
            maximum[axis] = std::max(maximum[axis], vertex[axis]);
        }
    }

    const std::array<float, 3> center{
        (minimum[0] + maximum[0]) * 0.5f,
        (minimum[1] + maximum[1]) * 0.5f,
        (minimum[2] + maximum[2]) * 0.5f};
    const auto project = [&](std::uint32_t vertexIndex) {
        const auto& vertex = mesh.vertices[vertexIndex];
        const float x = vertex[0] - center[0];
        const float y = vertex[1] - center[1];
        const float z = vertex[2] - center[2];
        return ImVec2((x - z) * 0.70710678f,
                      y * 0.81649658f - (x + z) * 0.40824829f);
    };

    std::vector<ImVec2> projected;
    projected.reserve(mesh.vertices.size());
    float minX = std::numeric_limits<float>::max();
    float minY = std::numeric_limits<float>::max();
    float maxX = std::numeric_limits<float>::lowest();
    float maxY = std::numeric_limits<float>::lowest();
    for (std::uint32_t i = 0; i < mesh.vertices.size(); ++i) {
        const ImVec2 point = project(i);
        projected.push_back(point);
        minX = std::min(minX, point.x);
        minY = std::min(minY, point.y);
        maxX = std::max(maxX, point.x);
        maxY = std::max(maxY, point.y);
    }

    const float width = std::max(maxX - minX, 0.0001f);
    const float height = std::max(maxY - minY, 0.0001f);
    const float scale = std::min((available.x - 16.0f) / width,
                                 (available.y - 16.0f) / height);
    if (scale <= 0.0f) {
        return;
    }

    const ImVec2 midpoint(origin.x + available.x * 0.5f,
                          origin.y + available.y * 0.5f);
    const auto screenPoint = [&](ImVec2 point) {
        return ImVec2(midpoint.x + (point.x - (minX + maxX) * 0.5f) * scale,
                      midpoint.y - (point.y - (minY + maxY) * 0.5f) * scale);
    };

    ImDrawList* drawList = ImGui::GetWindowDrawList();
    drawList->PushClipRect(origin,
                           ImVec2(origin.x + available.x,
                                  origin.y + available.y),
                           true);
    for (std::size_t i = 0; i < mesh.indices.size(); i += 3) {
        const std::uint32_t indexA = mesh.indices[i];
        const std::uint32_t indexB = mesh.indices[i + 1];
        const std::uint32_t indexC = mesh.indices[i + 2];
        const ImVec2 a = screenPoint(projected[indexA]);
        const ImVec2 b = screenPoint(projected[indexB]);
        const ImVec2 c = screenPoint(projected[indexC]);
        const auto& vertexA = mesh.vertices[indexA];
        const auto& vertexB = mesh.vertices[indexB];
        const auto& vertexC = mesh.vertices[indexC];
        const float edgeOneX = vertexB[0] - vertexA[0];
        const float edgeOneY = vertexB[1] - vertexA[1];
        const float edgeOneZ = vertexB[2] - vertexA[2];
        const float edgeTwoX = vertexC[0] - vertexA[0];
        const float edgeTwoY = vertexC[1] - vertexA[1];
        const float edgeTwoZ = vertexC[2] - vertexA[2];
        const float normalX = edgeOneY * edgeTwoZ - edgeOneZ * edgeTwoY;
        const float normalY = edgeOneZ * edgeTwoX - edgeOneX * edgeTwoZ;
        const float normalZ = edgeOneX * edgeTwoY - edgeOneY * edgeTwoX;
        const float normalLength =
            std::sqrt(normalX * normalX + normalY * normalY + normalZ * normalZ);
        const float diffuse = normalLength > 0.0f
                                  ? std::max(0.0f,
                                             (-0.4f * normalX +
                                              0.8f * normalY +
                                              0.5f * normalZ) /
                                                 (normalLength * 1.0247f))
                                  : 0.0f;
        const float shade = 0.25f + 0.75f * diffuse;
        const int channel = static_cast<int>(std::clamp(shade, 0.0f, 1.0f) *
                                             210.0f);
        const ImU32 color = IM_COL32(channel / 2, channel, channel, 255);
        drawList->AddTriangleFilled(a, b, c, color);
        drawList->AddTriangle(a, b, c, IM_COL32(28, 32, 40, 220), 1.0f);
    }
    drawList->PopClipRect();
}

void AssetBrowser::DrawDeleteConfirmation() {
    ImGui::SetNextWindowPos(ImVec2(0.0f, 0.0f), ImGuiCond_Always);
    ImGui::SetNextWindowSize(ImVec2(1.0f, 1.0f), ImGuiCond_Always);
    constexpr ImGuiWindowFlags hostFlags =
        ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoBackground |
        ImGuiWindowFlags_NoInputs | ImGuiWindowFlags_NoSavedSettings |
        ImGuiWindowFlags_NoBringToFrontOnFocus;
    ImGui::Begin("DeleteConfirmationHost", nullptr, hostFlags);
    if (m_DeletePopupNeedsOpen) {
        ImGui::OpenPopup("Confirm deleting actor");
        m_DeletePopupNeedsOpen = false;
    }
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(16.0f, 16.0f));
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(8.0f, 6.0f));
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(16.0f, 12.0f));
    if (ImGui::BeginPopupModal("Confirm deleting actor", nullptr,
                               ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::PushFont(m_LargeDialogFont);
        ImGui::Text("Delete '%s' from the scene?", m_DeleteObjectName.c_str());
        ImGui::Spacing();
        if (ImGui::Button("Delete", ImVec2(240.0f, 0.0f))) {
            m_DeleteConfirmed = true;
            m_DeleteResultPending = true;
            m_DeleteConfirmationRequested = false;
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel", ImVec2(240.0f, 0.0f))) {
            m_DeleteConfirmed = false;
            m_DeleteResultPending = true;
            m_DeleteConfirmationRequested = false;
            ImGui::CloseCurrentPopup();
        }
        ImGui::PopFont();
        ImGui::EndPopup();
    } else if (m_DeleteConfirmationRequested) {
        m_DeleteConfirmed = false;
        m_DeleteResultPending = true;
        m_DeleteConfirmationRequested = false;
    }
    ImGui::PopStyleVar(3);
    ImGui::End();
}

void AssetBrowser::DrawUnresolvedActors(
    const std::vector<UnresolvedActorView>& unresolvedActors) {
    if (unresolvedActors.empty() && !m_UnresolvedRemoveTarget) {
        m_UnresolvedPanelVisible = false;
        return;
    }

    const ImGuiIO& io = ImGui::GetIO();
    ImGui::SetNextWindowPos(ImVec2(12.0f, 54.0f), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize(ImVec2(390.0f, 300.0f),
                             ImGuiCond_FirstUseEver);
    constexpr ImGuiWindowFlags flags =
        ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoResize;
    ImGui::SetNextWindowSizeConstraints(
        ImVec2(280.0f, 160.0f),
        ImVec2(std::max(280.0f, io.DisplaySize.x - 24.0f),
               std::max(160.0f, io.DisplaySize.y - 70.0f)));
    const bool panelContentsVisible =
        ImGui::Begin("Unresolved Scene Actors", nullptr, flags);
    const ImVec2 panelPosition = ImGui::GetWindowPos();
    const ImVec2 panelSize = ImGui::GetWindowSize();
    m_UnresolvedPanelMin = glm::vec2(panelPosition.x, panelPosition.y);
    m_UnresolvedPanelMax =
        glm::vec2(panelPosition.x + panelSize.x,
                  panelPosition.y + panelSize.y);
    m_UnresolvedPanelVisible = true;
    if (panelContentsVisible) {
        ImGui::TextWrapped(
            "%zu actor(s) could not be restored. They are preserved when "
            "saving until repaired or explicitly removed.",
            unresolvedActors.size());
        ImGui::Separator();
        ImGui::BeginChild("UnresolvedActorList");
        for (const UnresolvedActorView& actor : unresolvedActors) {
            const std::string actorId = std::to_string(actor.sceneOrder);
            ImGui::PushID(actorId.c_str());
            ImGui::TextWrapped("%s", actor.name.c_str());
            if (actor.proceduralLandscape) {
                ImGui::TextDisabled("Procedural landscape");
            } else {
                ImGui::TextWrapped("%s", actor.path.string().c_str());
            }
            ImGui::TextWrapped("Error: %s", actor.error.c_str());
            if (ImGui::Button("Retry")) {
                m_UnresolvedActorAction = UnresolvedActorAction{
                    UnresolvedActorActionType::Retry, actor.sceneOrder, {}};
            }
            if (!actor.proceduralLandscape) {
                ImGui::SameLine();
                if (ImGui::Button("Relink...")) {
                    std::filesystem::path replacement;
                    bool cancelled = false;
                    std::string dialogError;
                    const std::filesystem::path initialDirectory =
                        actor.path.empty() ? m_AssetRoot
                                           : actor.path.parent_path();
                    if (OpenModelFileDialog(replacement, initialDirectory,
                                            cancelled, dialogError)) {
                        m_UnresolvedActorAction = UnresolvedActorAction{
                            UnresolvedActorActionType::Relink,
                            actor.sceneOrder, std::move(replacement)};
                    } else if (!cancelled) {
                        SetStatusMessage("Could not relink actor: " +
                                         dialogError);
                    }
                }
            }
            ImGui::SameLine();
            if (ImGui::Button("Remove...")) {
                m_UnresolvedRemoveTarget = actor.sceneOrder;
                m_UnresolvedRemovePopupNeedsOpen = true;
            }
            ImGui::Separator();
            ImGui::PopID();
        }
        ImGui::EndChild();

        if (m_UnresolvedRemovePopupNeedsOpen) {
            ImGui::OpenPopup("Remove unresolved actor");
            m_UnresolvedRemovePopupNeedsOpen = false;
        }
        const bool removePopupOpen = ImGui::BeginPopupModal(
            "Remove unresolved actor", nullptr,
            ImGuiWindowFlags_AlwaysAutoResize);
        if (removePopupOpen) {
            const auto target = std::find_if(
                unresolvedActors.begin(), unresolvedActors.end(),
                [this](const UnresolvedActorView& actor) {
                    return m_UnresolvedRemoveTarget &&
                           actor.sceneOrder == *m_UnresolvedRemoveTarget;
                });
            const std::string name =
                target == unresolvedActors.end() ? "this actor" : target->name;
            ImGui::TextWrapped(
                "Remove '%s' from the scene? This change will be saved with "
                "the next scene save.",
                name.c_str());
            if (ImGui::Button("Remove", ImVec2(180.0f, 0.0f))) {
                if (m_UnresolvedRemoveTarget) {
                    m_UnresolvedActorAction = UnresolvedActorAction{
                        UnresolvedActorActionType::Remove,
                        *m_UnresolvedRemoveTarget, {}};
                }
                m_UnresolvedRemoveTarget.reset();
                ImGui::CloseCurrentPopup();
            }
            ImGui::SameLine();
            if (ImGui::Button("Cancel", ImVec2(180.0f, 0.0f))) {
                m_UnresolvedRemoveTarget.reset();
                ImGui::CloseCurrentPopup();
            }
            ImGui::EndPopup();
        } else if (m_UnresolvedRemoveTarget) {
            m_UnresolvedRemoveTarget.reset();
        }
    }
    ImGui::End();
}

void AssetBrowser::DrawFolderDialog() {
    if (m_FolderDialogNeedsOpen) {
        ImGui::OpenPopup("Folder action");
        m_FolderDialogNeedsOpen = false;
    }
    if (!ImGui::BeginPopupModal("Folder action", nullptr,
                                ImGuiWindowFlags_AlwaysAutoResize)) {
        return;
    }

    const bool editingName =
        m_FolderDialogAction == FolderDialogAction::Create ||
        m_FolderDialogAction == FolderDialogAction::Rename;
    if (m_FolderDialogAction == FolderDialogAction::Delete) {
        ImGui::TextWrapped(
            "Permanently delete folder '%s' and everything inside it?",
            m_FolderDialogTarget.filename().string().c_str());
        ImGui::TextDisabled("This cannot be undone.");
    } else {
        ImGui::TextUnformatted(
            m_FolderDialogAction == FolderDialogAction::Create
                ? "Create a folder in the current location."
                : "Rename this folder.");
        ImGui::SetNextItemWidth(320.0f);
        ImGui::InputText("Name", m_FolderNameDraft,
                         sizeof(m_FolderNameDraft));
    }

    if (!m_FolderDialogError.empty()) {
        ImGui::TextWrapped("%s", m_FolderDialogError.c_str());
    }
    if (editingName) {
        const char* buttonLabel =
            m_FolderDialogAction == FolderDialogAction::Create ? "Create"
                                                               : "Rename";
        if (ImGui::Button(buttonLabel)) {
            const std::string name(m_FolderNameDraft);
            const std::filesystem::path namePath(name);
            if (name.empty() || name == "." || name == ".." ||
                name.find_first_of("/\\") != std::string::npos ||
                namePath.filename() != namePath) {
                m_FolderDialogError =
                    "Folder name must be a non-empty simple name.";
            } else {
                std::filesystem::path root;
                std::filesystem::path current;
                if (m_FolderDialogAction == FolderDialogAction::Create) {
                    if (ResolveProjectDirectories(
                            m_ProjectRoot, m_CurrentDirectory, root, current,
                            m_FolderDialogError)) {
                        std::error_code filesystemError;
                        const std::filesystem::path destination =
                            current / name;
                        const bool created =
                            std::filesystem::create_directory(
                                destination, filesystemError);
                        if (!created || filesystemError) {
                            m_FolderDialogError =
                                "Could not create folder '" + name + "'." +
                                (filesystemError
                                     ? " " + filesystemError.message()
                                     : std::string{});
                        } else {
                            SetStatusMessage("Created folder '" + name + "'.");
                            m_FolderDialogAction = FolderDialogAction::None;
                            ImGui::CloseCurrentPopup();
                            RefreshEntries();
                        }
                    }
                } else {
                    std::filesystem::path target;
                    const bool validTarget = ResolveProjectChildFolder(
                        m_ProjectRoot, m_CurrentDirectory,
                        m_FolderDialogTarget, root, current, target,
                        m_FolderDialogError);
                    const std::filesystem::path destination = current / name;
                    if (validTarget) {
                        if (destination == target) {
                            m_FolderDialogAction = FolderDialogAction::None;
                            ImGui::CloseCurrentPopup();
                        } else {
                            std::error_code filesystemError;
                            const bool destinationExists =
                                std::filesystem::exists(destination,
                                                        filesystemError);
                            if (filesystemError || destinationExists) {
                                m_FolderDialogError =
                                    "A folder or file with that name already "
                                    "exists." +
                                    (filesystemError
                                         ? " " + filesystemError.message()
                                         : std::string{});
                            } else {
                                std::filesystem::rename(target, destination,
                                                        filesystemError);
                                if (filesystemError) {
                                    m_FolderDialogError =
                                        "Could not rename folder: " +
                                        filesystemError.message();
                                } else {
                                    SetStatusMessage("Renamed folder to '" +
                                                     name + "'.");
                                    m_FolderDialogAction =
                                        FolderDialogAction::None;
                                    ImGui::CloseCurrentPopup();
                                    RefreshEntries();
                                }
                            }
                        }
                    }
                }
            }
        }
        ImGui::SameLine();
    } else if (m_FolderDialogAction == FolderDialogAction::Delete) {
        if (ImGui::Button("Delete")) {
            std::filesystem::path root;
            std::filesystem::path current;
            std::filesystem::path target;
            if (ResolveProjectChildFolder(
                    m_ProjectRoot, m_CurrentDirectory,
                    m_FolderDialogTarget, root, current, target,
                    m_FolderDialogError)) {
                std::error_code filesystemError;
                const std::string deletedName =
                    m_FolderDialogTarget.filename().string();
                std::filesystem::remove_all(target, filesystemError);
                if (filesystemError) {
                    m_FolderDialogError =
                        "Could not delete folder: " +
                        filesystemError.message();
                } else {
                    SetStatusMessage("Deleted folder '" + deletedName +
                                     "'.");
                    m_FolderDialogAction = FolderDialogAction::None;
                    ImGui::CloseCurrentPopup();
                    RefreshEntries();
                }
            }
        }
        ImGui::SameLine();
    }
    if (ImGui::Button("Cancel")) {
        m_FolderDialogAction = FolderDialogAction::None;
        m_FolderDialogError.clear();
        ImGui::CloseCurrentPopup();
    }
    ImGui::EndPopup();
}

void AssetBrowser::DrawViewModeMenu(CameraProjection cameraProjection) {
    constexpr ImGuiWindowFlags flags =
        ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_AlwaysAutoResize |
        ImGuiWindowFlags_NoSavedSettings |
        ImGuiWindowFlags_NoFocusOnAppearing |
        ImGuiWindowFlags_NoBringToFrontOnFocus;
    ImGui::SetNextWindowPos(ImVec2(12.0f, 32.0f), ImGuiCond_Always);
    ImGui::SetNextWindowBgAlpha(0.88f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 5.0f);
    bool capturesMouse = false;
    bool capturesKeyboard = false;
    if (ImGui::Begin("Viewport View Mode", nullptr, flags)) {
        if (ImGui::Button("View")) {
            ImGui::OpenPopup("ViewModePopup");
        }
        if (ImGui::BeginPopup("ViewModePopup")) {
            constexpr std::array<std::pair<EditorViewMode, const char*>, 3>
                modes{{{EditorViewMode::Lit, "Lit"},
                       {EditorViewMode::Unlit, "Unlit"},
                       {EditorViewMode::Wire, "Wireframe"}}};
            for (const auto& [mode, label] : modes) {
                if (ImGui::MenuItem(label, nullptr, m_ViewMode == mode)) {
                    m_ViewMode = mode;
                }
            }
            ImGui::EndPopup();
        }
        ImGui::SameLine();
        const char* projectionLabel =
            cameraProjection == CameraProjection::Perspective
                ? "Perspective"
                : "Orthographic";
        if (ImGui::Button(projectionLabel)) {
            m_ProjectionToggleRequested = true;
        }
        const bool popupOpen = ImGui::IsPopupOpen("ViewModePopup");
        capturesMouse = ImGui::IsWindowHovered() || popupOpen;
        capturesKeyboard = popupOpen || ImGui::IsAnyItemActive();
    }
    const ImVec2 toolbarPosition = ImGui::GetWindowPos();
    const ImVec2 toolbarSize = ImGui::GetWindowSize();
    m_ViewportToolbarVisible = true;
    m_ViewportToolbarMin = {toolbarPosition.x, toolbarPosition.y};
    m_ViewportToolbarMax = {toolbarPosition.x + toolbarSize.x,
                            toolbarPosition.y + toolbarSize.y};
    ImGui::End();
    ImGui::PopStyleVar();
    m_ViewModeMenuCapturesMouse = capturesMouse;
    m_ViewModeMenuCapturesKeyboard = capturesKeyboard;
}

void AssetBrowser::DrawLightsMenu() {
    bool capturesMouse = false;
    bool capturesKeyboard = false;
    if (ImGui::BeginMainMenuBar()) {
        capturesMouse = ImGui::IsWindowHovered();
        if (ImGui::BeginMenu("Lights")) {
            capturesMouse = true;
            if (ImGui::BeginMenu("Add Light...")) {
                capturesMouse = true;
                constexpr std::array<LightType, 5> lightTypes{
                    LightType::Directional, LightType::Spot, LightType::Point,
                    LightType::Sky, LightType::Rect};
                for (LightType type : lightTypes) {
                    const std::string label(LightTypeName(type));
                    if (ImGui::MenuItem(label.c_str())) {
                        m_PendingLightCreation = type;
                    }
                }
                ImGui::EndMenu();
            }
            capturesKeyboard =
                capturesMouse && ImGui::GetIO().WantCaptureKeyboard;
            ImGui::EndMenu();
        }
        capturesMouse = capturesMouse || ImGui::IsWindowHovered();
        capturesKeyboard =
            capturesKeyboard ||
            (capturesMouse && ImGui::GetIO().WantCaptureKeyboard);
        ImGui::EndMainMenuBar();
    }
    m_LightsMenuCapturesMouse = capturesMouse;
    m_LightsMenuCapturesKeyboard = capturesKeyboard;
}

void AssetBrowser::DrawSceneDropTarget() {
    if (ImGui::GetDragDropPayload() == nullptr) {
        return;
    }

    const ImGuiIO& io = ImGui::GetIO();
    const float panelHeight =
        m_Visible || m_VisibilityAlpha > 0.0f ? 250.0f : 0.0f;
    const float dropHeight = std::max(0.0f, io.DisplaySize.y - panelHeight);
    if (dropHeight <= 0.0f) {
        return;
    }

    ImGui::SetNextWindowPos(ImVec2(0.0f, 0.0f), ImGuiCond_Always);
    ImGui::SetNextWindowSize(ImVec2(io.DisplaySize.x, dropHeight),
                             ImGuiCond_Always);
    constexpr ImGuiWindowFlags flags =
        ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove |
        ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoSavedSettings |
        ImGuiWindowFlags_NoBringToFrontOnFocus |
        ImGuiWindowFlags_NoBackground;
    ImGui::Begin("SceneMeshDropTarget", nullptr, flags);
    ImGui::InvisibleButton("Drop model in scene",
                           ImGui::GetContentRegionAvail());
    if (ImGui::BeginDragDropTarget()) {
        if (const ImGuiPayload* payload =
                ImGui::AcceptDragDropPayload("MODEL_ASSET")) {
            const auto* path = static_cast<const char*>(payload->Data);
            if (path && payload->DataSize > 1) {
                m_PendingSceneDrop = std::filesystem::path(path);
            }
        }
        if (const ImGuiPayload* payload =
                ImGui::AcceptDragDropPayload("AQENMAT_ASSET")) {
            const auto* path = static_cast<const char*>(payload->Data);
            if (path && payload->DataSize > 1) {
                m_PendingMaterialDrop = std::filesystem::path(path);
            }
        }
        ImGui::EndDragDropTarget();
    }
    if (ImGui::IsItemHovered() && ImGui::GetDragDropPayload()) {
        ImGui::SetTooltip("Drop a mesh to add it, or a material to apply it");
    }
    ImGui::End();
}

void AssetBrowser::DrawWorkLog() {
    const ImGuiIO& io = ImGui::GetIO();
    constexpr float buttonWidth = 120.0f;
    constexpr float margin = 12.0f;
    ImGui::SetNextWindowPos(
        ImVec2(io.DisplaySize.x - buttonWidth - margin, margin),
        ImGuiCond_Always);
    ImGui::SetNextWindowSize(ImVec2(buttonWidth, 0.0f), ImGuiCond_Always);
    constexpr ImGuiWindowFlags buttonFlags =
        ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoSavedSettings |
        ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoFocusOnAppearing;
    ImGui::Begin("WorkLogButton", nullptr, buttonFlags);
    if (ImGui::Button(m_WorkLogOpen ? "Hide Work log" : "Work log",
                      ImVec2(buttonWidth, 0.0f))) {
        m_WorkLogOpen = !m_WorkLogOpen;
    }
    ImGui::End();

    if (!m_WorkLogOpen) {
        m_WorkLogWasOpen = false;
        return;
    }
    ImGui::SetNextWindowPos(
        ImVec2(std::max(margin, io.DisplaySize.x - 420.0f - margin),
               52.0f),
        ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize(ImVec2(420.0f, 280.0f),
                             ImGuiCond_FirstUseEver);
    if (ImGui::Begin("Work log", &m_WorkLogOpen)) {
        ImGui::BeginChild("WorkLogEntries", ImVec2(0.0f, 0.0f), false,
                          ImGuiWindowFlags_HorizontalScrollbar);
        const bool wasAtBottom =
            ImGui::GetScrollY() >= ImGui::GetScrollMaxY() - 1.0f;
        if (!m_WorkLog.empty()) {
            for (const std::string& entry : m_WorkLog) {
                ImGui::BulletText("%s", entry.c_str());
            }
            if (!m_WorkLogWasOpen ||
                (wasAtBottom && m_WorkLog.size() > m_LastRenderedLogSize)) {
                ImGui::SetScrollHereY(1.0f);
            }
        } else {
            ImGui::TextDisabled("No work log entries yet.");
        }
        ImGui::EndChild();
    }
    ImGui::End();
    m_LastRenderedLogSize = m_WorkLog.size();
    m_WorkLogWasOpen = m_WorkLogOpen;
}

void AssetBrowser::NavigateTo(const std::filesystem::path& directory) {
    std::error_code error;
    const std::filesystem::path resolved =
        std::filesystem::weakly_canonical(directory, error);
    if (error) {
        m_ScanError = "Could not open folder: " + error.message();
        return;
    }

    const std::filesystem::path relative =
        resolved.lexically_relative(m_ProjectRoot);
    if (relative.empty() ||
        (relative.begin() != relative.end() && *relative.begin() == "..")) {
        m_ScanError = "Navigation outside the project folder is not allowed.";
        return;
    }

    m_CurrentDirectory = resolved;
    m_SelectedAsset.clear();
    LoadPreview({});
    RefreshEntries();
}

void AssetBrowser::Render(const void* actorIdentity, std::string* actorName,
                          glm::vec3* position, glm::vec3* rotation,
                          glm::vec3* scale, ModelMaterial* material,
                          LightProperties* light,
                          CameraProjection cameraProjection,
                          const std::vector<UnresolvedActorView>&
                              unresolvedActors) {
    if (!m_Initialized) {
        return;
    }
    glfwMakeContextCurrent(m_MainWindow);
    ImGui::SetCurrentContext(m_MainImGuiContext);

    ImGui_ImplOpenGL3_NewFrame();
    ImGui_ImplGlfw_NewFrame();
    ImGui::NewFrame();
    m_ActiveSceneDrag.reset();
    DrawViewModeMenu(cameraProjection);
    DrawLightsMenu();

    const ImGuiIO& io = ImGui::GetIO();
    constexpr float fadeSpeed = 5.0f;
    const float fadeStep = fadeSpeed * io.DeltaTime;
    m_VisibilityAlpha =
        m_Visible ? std::min(1.0f, m_VisibilityAlpha + fadeStep)
                  : std::max(0.0f, m_VisibilityAlpha - fadeStep);
    if (m_Visible || m_VisibilityAlpha > 0.0f) {
        DrawSceneDropTarget();
    }

    constexpr float panelHeight = 330.0f;
    ImGui::SetNextWindowPos(ImVec2(0.0f, std::max(0.0f, io.DisplaySize.y -
                                                            panelHeight)),
                            ImGuiCond_Always);
    ImGui::SetNextWindowSize(
        ImVec2(io.DisplaySize.x, std::min(panelHeight, io.DisplaySize.y)),
        ImGuiCond_Always);
    constexpr ImGuiWindowFlags flags =
        ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize |
        ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoSavedSettings |
        ImGuiWindowFlags_NoBringToFrontOnFocus;

    const bool browserVisible = m_Visible || m_VisibilityAlpha > 0.0f;
    if (browserVisible) {
        ImGui::PushStyleVar(ImGuiStyleVar_Alpha, m_VisibilityAlpha);
    }
    if (browserVisible && ImGui::Begin("Asset Browser", nullptr, flags)) {
        ImGui::SetNextItemWidth(-1.0f);
        ImGui::InputTextWithHint("##AssetSearch", "Search this folder...",
                                 m_SearchText, sizeof(m_SearchText));

        const std::string currentPath =
            RelativeAssetPath(m_CurrentDirectory, m_ProjectRoot);
        ImGui::TextDisabled("Project / %s",
                            currentPath.empty() ? "." : currentPath.c_str());

        if (!m_ScanError.empty()) {
            ImGui::TextColored(ImVec4(1.0f, 0.45f, 0.35f, 1.0f), "%s",
                               m_ScanError.c_str());
        }

        const std::string query = ToLower(m_SearchText);
        const float paneHeight = -ImGui::GetFrameHeight();
        const float listWidth = ImGui::GetContentRegionAvail().x * 0.68f;
        ImGui::BeginChild("AssetList", ImVec2(listWidth, paneHeight), true);
        bool navigationRequested = false;
        std::filesystem::path requestedDirectory;
        std::size_t visibleFiles = 0;
        std::size_t visibleDirectories = 0;
        const ImVec2 gridAvailable = ImGui::GetContentRegionAvail();
        constexpr float minimumCardWidth = 132.0f;
        const int gridColumns = std::max(
            1, static_cast<int>(gridAvailable.x / minimumCardWidth));
        auto drawCard = [&](const std::filesystem::path& path,
                            bool isDirectory, bool parentDirectory = false) {
            const std::string name =
                parentDirectory ? ".." : path.filename().string();
            if (!parentDirectory && !query.empty() &&
                ToLower(name).find(query) == std::string::npos) {
                return;
            }
            if (isDirectory) {
                ++visibleDirectories;
            } else {
                ++visibleFiles;
            }

            ImGui::TableNextColumn();
            ImGui::PushID(path.string().c_str());
            const ImVec2 cardSize(ImGui::GetContentRegionAvail().x, 124.0f);
            const bool selected = !isDirectory && path == m_SelectedAsset;
            if (selected) {
                ImGui::PushStyleColor(ImGuiCol_Button,
                                      ImGui::GetStyleColorVec4(
                                          ImGuiCol_ButtonActive));
            }
            const bool clicked = ImGui::Button("##AssetCard", cardSize);
            const ImVec2 cardMin = ImGui::GetItemRectMin();
            const ImVec2 cardMax = ImGui::GetItemRectMax();
            const bool cardHovered = ImGui::IsItemHovered();
            if (isDirectory && !parentDirectory && cardHovered &&
                ImGui::IsMouseReleased(ImGuiMouseButton_Right)) {
                ImGui::OpenPopup("FolderContext");
            }
            if (isDirectory && !parentDirectory &&
                ImGui::BeginPopup("FolderContext")) {
                if (ImGui::MenuItem("Rename")) {
                    m_FolderDialogAction = FolderDialogAction::Rename;
                    m_FolderDialogTarget = path;
                    std::snprintf(m_FolderNameDraft,
                                  sizeof(m_FolderNameDraft), "%s",
                                  path.filename().string().c_str());
                    m_FolderDialogError.clear();
                    m_FolderDialogNeedsOpen = true;
                }
                if (ImGui::MenuItem("Delete")) {
                    m_FolderDialogAction = FolderDialogAction::Delete;
                    m_FolderDialogTarget = path;
                    m_FolderDialogError.clear();
                    m_FolderDialogNeedsOpen = true;
                }
                ImGui::EndPopup();
            }
            if (selected) {
                ImGui::PopStyleColor();
            }

            const ImVec2 iconMin(cardMin.x + 8.0f, cardMin.y + 6.0f);
            const ImVec2 iconMax(cardMax.x - 8.0f, cardMin.y + 82.0f);
            const ImVec2 iconSize(iconMax.x - iconMin.x,
                                  iconMax.y - iconMin.y);
            ImDrawList* drawList = ImGui::GetWindowDrawList();
            const ImU32 iconColor = ImGui::GetColorU32(ImGuiCol_Text);
            if (isDirectory) {
                const float centerX = (iconMin.x + iconMax.x) * 0.5f;
                const float centerY = (iconMin.y + iconMax.y) * 0.5f;
                const ImVec2 folder[] = {
                    ImVec2(centerX - 28.0f, centerY - 13.0f),
                    ImVec2(centerX - 7.0f, centerY - 13.0f),
                    ImVec2(centerX + 1.0f, centerY - 5.0f),
                    ImVec2(centerX + 28.0f, centerY - 5.0f),
                    ImVec2(centerX + 28.0f, centerY + 18.0f),
                    ImVec2(centerX - 28.0f, centerY + 18.0f)};
                drawList->AddPolyline(folder, 6, iconColor, ImDrawFlags_Closed,
                                      2.0f);
            } else if (IsMaterialAsset(path)) {
                const float centerX = (iconMin.x + iconMax.x) * 0.5f;
                const float centerY = (iconMin.y + iconMax.y) * 0.5f;
                drawList->AddCircleFilled(ImVec2(centerX, centerY), 26.0f,
                                          IM_COL32(70, 126, 176, 255), 32);
                drawList->AddText(
                    ImVec2(centerX - 13.0f, centerY - 6.0f),
                    IM_COL32(255, 255, 255, 255), "MAT");
            } else {
                AssetThumbnail& thumbnail = LoadThumbnail(path);
                if (thumbnail.texture != 0 && thumbnail.width > 0 &&
                    thumbnail.height > 0) {
                    const float fitScale =
                        std::min(iconSize.x / static_cast<float>(thumbnail.width),
                                 iconSize.y /
                                     static_cast<float>(thumbnail.height));
                    const ImVec2 imageSize(thumbnail.width * fitScale,
                                           thumbnail.height * fitScale);
                    const ImVec2 imagePosition(
                        iconMin.x + (iconSize.x - imageSize.x) * 0.5f,
                        iconMin.y + (iconSize.y - imageSize.y) * 0.5f);
                    drawList->AddImage(
                        reinterpret_cast<ImTextureID>(
                            static_cast<intptr_t>(thumbnail.texture)),
                        imagePosition,
                        ImVec2(imagePosition.x + imageSize.x,
                               imagePosition.y + imageSize.y),
                        ImVec2(0.0f, 1.0f), ImVec2(1.0f, 0.0f));
                } else if (thumbnail.mesh &&
                           !thumbnail.meshData.vertices.empty()) {
                    DrawMeshPreview(thumbnail.meshData, iconMin, iconSize);
                } else {
                    const float centerX = (iconMin.x + iconMax.x) * 0.5f;
                    const float centerY = (iconMin.y + iconMax.y) * 0.5f;
                    drawList->AddRect(
                        ImVec2(centerX - 24.0f, centerY - 28.0f),
                        ImVec2(centerX + 24.0f, centerY + 28.0f), iconColor,
                        4.0f, 0, 2.0f);
                    const std::string extension =
                        path.extension().string().empty()
                            ? "FILE"
                            : ToLower(path.extension().string()).substr(1);
                    drawList->AddText(
                        ImVec2(centerX - 19.0f, centerY - 6.0f), iconColor,
                        extension.c_str());
                }

            }

            if (!isDirectory && clicked) {
                m_SelectedAsset = path;
                LoadPreview(path);
            }
            if (!isDirectory &&
                (IsMeshAsset(path) || IsMaterialAsset(path)) &&
                ImGui::BeginDragDropSource()) {
                const std::string assetPath = path.string();
                ImGui::SetDragDropPayload(
                    IsMeshAsset(path) ? "MODEL_ASSET" : "AQENMAT_ASSET",
                    assetPath.c_str(), assetPath.size() + 1);
                if (IsMeshAsset(path)) {
                    m_ActiveSceneDrag = path;
                }
                ImGui::EndDragDropSource();
            }

            const ImVec2 labelMin(cardMin.x + 6.0f, cardMin.y + 88.0f);
            const ImVec2 labelMax(cardMax.x - 6.0f, cardMax.y - 2.0f);
            drawList->PushClipRect(labelMin, labelMax, true);
            drawList->AddText(labelMin, ImGui::GetColorU32(ImGuiCol_Text),
                              name.c_str());
            drawList->PopClipRect();
            if (isDirectory && cardHovered &&
                ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) {
                requestedDirectory = path;
                navigationRequested = true;
            }
            if (parentDirectory && clicked) {
                requestedDirectory = path;
                navigationRequested = true;
            }
            if (!isDirectory && IsMaterialAsset(path) && cardHovered &&
                ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) {
                ModelMaterial material;
                std::string error;
                if (LoadAqenMaterial(path, material, error)) {
                    m_MaterialDraft = std::move(material);
                    m_MaterialPreviewYaw = 0.0f;
                    m_MaterialPreviewPitch = 0.0f;
                    m_MaterialPreviewDragging = false;
                    m_MaterialPreviewDirty = true;
                    m_EditingMaterialPath = path;
                    if (m_MaterialDraft.name.empty()) {
                        m_MaterialDraft.name = path.stem().string();
                    }
                    std::snprintf(m_MaterialName, sizeof(m_MaterialName), "%s",
                                  m_MaterialDraft.name.c_str());
                    m_MaterialCreatorOpen = true;
                    m_MaterialError.clear();
                } else {
                    m_MaterialError = std::move(error);
                    m_PreviewError = m_MaterialError;
                }
            }
            ImGui::PopID();
        };

        if (ImGui::BeginTable("AssetIconGrid", gridColumns,
                              ImGuiTableFlags_SizingStretchSame |
                                  ImGuiTableFlags_ScrollY |
                                  ImGuiTableFlags_NoSavedSettings,
                              ImVec2(0.0f, 0.0f))) {
            if (m_CurrentDirectory != m_ProjectRoot) {
                drawCard(m_CurrentDirectory.parent_path(), true, true);
            }
            for (const std::filesystem::path& directory : m_Directories) {
                drawCard(directory, true);
            }
            for (const std::filesystem::path& file : m_Files) {
                drawCard(file, false);
            }
            if (ImGui::IsWindowHovered(ImGuiHoveredFlags_ChildWindows) &&
                !ImGui::IsAnyItemHovered() &&
                ImGui::IsMouseReleased(ImGuiMouseButton_Right)) {
                ImGui::OpenPopup("AssetListContext");
            }
            if (ImGui::BeginPopup("AssetListContext")) {
                if (ImGui::MenuItem("Add New Folder")) {
                    m_FolderDialogAction = FolderDialogAction::Create;
                    m_FolderDialogTarget = m_CurrentDirectory;
                    m_FolderNameDraft[0] = '\0';
                    m_FolderDialogError.clear();
                    m_FolderDialogNeedsOpen = true;
                }
                if (ImGui::MenuItem("Refresh")) {
                    RefreshEntries();
                }
                ImGui::EndPopup();
            }
            ImGui::EndTable();
        }
        if (visibleDirectories == 0 && visibleFiles == 0 &&
            m_ScanError.empty()) {
            ImGui::TextDisabled(query.empty() ? "This folder is empty."
                                              : "No matching entries.");
        }
        ImGui::EndChild();
        ImGui::SameLine();
        ImGui::BeginChild("AssetPreview", ImVec2(0.0f, paneHeight), true);
        ImGui::TextUnformatted("Preview");
        ImGui::Separator();
        if (ImGui::Button("Save Project")) {
            m_SaveRequested = true;
        }
        ImGui::SameLine();
        if (ImGui::Button("Create Material")) {
            m_MaterialCreatorOpen = true;
            m_EditingMaterialPath.clear();
            m_MaterialPreviewYaw = 0.0f;
            m_MaterialPreviewPitch = 0.0f;
            m_MaterialPreviewDragging = false;
            m_MaterialPreviewDirty = true;
            m_MaterialDraft = {};
            m_MaterialDraft.name = "New Material";
            m_MaterialDraft.textureScale = glm::vec3(1.0f);
            std::snprintf(m_MaterialName, sizeof(m_MaterialName),
                          "New Material");
            m_MaterialError.clear();
            DrawMaterialPreview(m_MaterialDraft, ImVec2(0.0f, 0.0f), 1.0f,
                                false,
                                MaterialPreviewTarget::MaterialCreator);
            m_MaterialPreviewDirty = false;
            glFinish();
        }
        ImGui::SameLine();
        if (ImGui::Button("Create Landscape")) {
            m_LandscapeCreatorOpen = true;
            m_LandscapeError.clear();
        }
        ImGui::SameLine();
        if (ImGui::Button("Upload Model")) {
            std::filesystem::path source;
            bool cancelled = false;
            std::string dialogError;
            if (OpenModelFileDialog(source, m_CurrentDirectory, cancelled,
                                    dialogError)) {
                std::string baseName = source.stem().string();
                if (baseName.empty()) {
                    baseName = "ImportedModel";
                }
                std::filesystem::path importDirectory =
                    m_CurrentDirectory / baseName;
                std::error_code existsError;
                for (unsigned int suffix = 2;
                     std::filesystem::exists(importDirectory, existsError) &&
                     !existsError;
                     ++suffix) {
                    importDirectory = m_CurrentDirectory /
                                      (baseName + "-" +
                                       std::to_string(suffix));
                }
                if (existsError) {
                    m_PreviewError =
                        "Could not check for an existing import folder: " +
                        existsError.message();
                } else {
                    const std::filesystem::path output =
                        importDirectory / (importDirectory.filename().string() +
                                           ".glb");
                    m_PendingImport = std::make_pair(source, output);
                    m_PreviewError.clear();
                }
            } else if (!cancelled) {
                m_PreviewError = std::move(dialogError);
            }
        }
        if (!m_PreviewError.empty()) {
            ImGui::TextWrapped("%s", m_PreviewError.c_str());
        }
        if (m_SelectedAsset.empty()) {
            ImGui::TextDisabled("Select an asset to preview it.");
        } else {
            const std::string selectedName =
                m_SelectedAsset.filename().string();
            ImGui::TextWrapped("%s", selectedName.c_str());
            if (m_PreviewTexture != 0) {
                ImGui::Text("%d x %d", m_PreviewWidth, m_PreviewHeight);
                const ImVec2 available = ImGui::GetContentRegionAvail();
                const float scale = std::min(
                    available.x / static_cast<float>(m_PreviewWidth),
                    std::max(0.0f, available.y -
                                       ImGui::GetTextLineHeightWithSpacing()) /
                        static_cast<float>(m_PreviewHeight));
                if (scale > 0.0f) {
                    const ImVec2 imageSize(m_PreviewWidth * scale,
                                           m_PreviewHeight * scale);
                    ImGui::Image(
                        reinterpret_cast<ImTextureID>(
                            static_cast<intptr_t>(m_PreviewTexture)),
                        imageSize, ImVec2(0.0f, 1.0f), ImVec2(1.0f, 0.0f));
                }
            } else if (!m_PreviewMesh.vertices.empty()) {
                ImGui::Text("%zu vertices, %zu triangles",
                            m_PreviewMesh.vertices.size(),
                            m_PreviewMesh.indices.size() / 3);
                const ImVec2 previewOrigin = ImGui::GetCursorScreenPos();
                const ImVec2 previewSize = ImGui::GetContentRegionAvail();
                DrawMeshPreview(m_PreviewMesh, previewOrigin, previewSize);
                ImGui::Dummy(previewSize);
            } else if (IsMaterialAsset(m_SelectedAsset)) {
                ModelMaterial previewMaterial;
                std::string materialError;
                if (LoadAqenMaterial(m_SelectedAsset, previewMaterial,
                                     materialError)) {
                    const ImVec2 available = ImGui::GetContentRegionAvail();
                    const float radius =
                        std::min(available.x * 0.35f, available.y * 0.42f);
                    const ImVec2 origin = ImGui::GetCursorScreenPos();
                    const ImVec2 center(origin.x + available.x * 0.5f,
                                        origin.y + radius + 8.0f);
                    DrawMaterialPreview(previewMaterial, center, radius);
                    ImGui::Dummy(
                        ImVec2(available.x, radius * 2.0f + 16.0f));
                    ImGui::TextWrapped("Material: %s",
                                       previewMaterial.name.c_str());
                    ImGui::TextWrapped(
                        "Drag this material onto an object in the scene.");
                } else {
                    ImGui::TextWrapped("%s", materialError.c_str());
                }
            } else if (!IsMeshAsset(m_SelectedAsset)) {
                ImGui::TextDisabled(
                    "Preview is available for models, supported images, and "
                    "materials.");
            }
        }
        ImGui::EndChild();

        if (navigationRequested) {
            NavigateTo(requestedDirectory);
        }
        DrawFolderDialog();
    }
    if (browserVisible) {
        ImGui::End();
        ImGui::PopStyleVar();
    }
    DrawWorkLog();
    DrawUnresolvedActors(unresolvedActors);
    if (actorIdentity && actorName && position && rotation && scale &&
        material) {
        if (m_InspectorActorIdentity != actorIdentity) {
            m_InspectorActorIdentity = actorIdentity;
            std::snprintf(m_InspectorName, sizeof(m_InspectorName), "%s",
                          actorName->c_str());
            for (int axis = 0; axis < 3; ++axis) {
                m_InspectorTransformDraft[axis] = (*position)[axis];
                m_InspectorRotationDraft[axis] =
                    RotationDegrees((*rotation)[axis]);
                m_InspectorTransformDraft[axis + 6] = (*scale)[axis];
            }
            if (light) {
                m_InspectorLightDraft[0] = light->color.x;
                m_InspectorLightDraft[1] = light->color.y;
                m_InspectorLightDraft[2] = light->color.z;
                m_InspectorLightDraft[3] = light->intensity;
                m_InspectorLightDraft[4] = light->range;
                m_InspectorLightDraft[5] = light->innerConeAngleDegrees;
                m_InspectorLightDraft[6] = light->outerConeAngleDegrees;
                m_InspectorLightDraft[7] = light->rectSize.x;
                m_InspectorLightDraft[8] = light->rectSize.y;
            }
        }
        DrawActorInspector(*actorName, *position, *rotation, *scale, *material,
                           light);
    } else {
        m_InspectorActorIdentity = nullptr;
        m_InspectorPanelVisible = false;
        m_InspectorPanelCapturesKeyboard = false;
    }
    if (m_DeleteConfirmationRequested) {
        DrawDeleteConfirmation();
    }
    ImGui::Render();
    ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
    RenderMaterialEditorWindow();
    RenderLandscapeWindow();
    if (m_MaterialCreatorOpen && m_MaterialPreviewDirty) {
        DrawMaterialPreview(m_MaterialDraft, ImVec2(0.0f, 0.0f), 1.0f, false,
                            MaterialPreviewTarget::MaterialCreator);
        m_MaterialPreviewDirty = false;
        glFinish();
    }
}

bool AssetBrowser::InitializeMaterialPreview(std::string& error) {
    constexpr int previewSize = 384;
    constexpr int longitudeSegments = 48;
    constexpr int latitudeSegments = 32;
    constexpr std::array<std::array<unsigned char, 4>, 5> fallbackPixels{{
        {190, 190, 190, 255},
        {128, 128, 255, 255},
        {128, 128, 128, 255},
        {0, 0, 0, 255},
        {255, 255, 255, 255},
    }};
    const char* vertexSource = R"(
        #version 460 core
        layout (location = 0) in vec3 aPosition;
        layout (location = 1) in vec3 aNormal;
        uniform mat4 uViewProjection;
        uniform mat4 uModel;
        out vec3 vWorldPosition;
        out vec3 vNormal;
        void main() {
            vec4 worldPosition = uModel * vec4(aPosition, 1.0);
            vWorldPosition = worldPosition.xyz;
            vNormal = mat3(transpose(inverse(uModel))) * aNormal;
            gl_Position = uViewProjection * worldPosition;
        }
    )";
    const char* fragmentSource = R"(
        #version 460 core
        out vec4 FragColor;
        in vec3 vWorldPosition;
        in vec3 vNormal;
        uniform sampler2D uDiffuseMap;
        uniform sampler2D uNormalMap;
        uniform sampler2D uRoughnessMap;
        uniform sampler2D uMetallicMap;
        uniform sampler2D uAmbientOcclusionMap;
        uniform vec3 uCameraPosition;
        uniform vec3 uMaterialTiling;
        const float PI = 3.14159265359;
        void main() {
            vec3 geometricNormal = normalize(vNormal);
            vec3 blendWeights = pow(abs(geometricNormal), vec3(4.0));
            blendWeights /= max(blendWeights.x + blendWeights.y +
                                blendWeights.z, 0.0001);
            vec3 axisSign = sign(geometricNormal);
            vec2 uvX = vWorldPosition.zy * uMaterialTiling.zy;
            vec2 uvY = vWorldPosition.xz * uMaterialTiling.xz;
            vec2 uvZ = vWorldPosition.xy * uMaterialTiling.xy;
            vec3 albedo =
                texture(uDiffuseMap, uvX).rgb * blendWeights.x +
                texture(uDiffuseMap, uvY).rgb * blendWeights.y +
                texture(uDiffuseMap, uvZ).rgb * blendWeights.z;
            float roughness = clamp(
                texture(uRoughnessMap, uvX).r * blendWeights.x +
                texture(uRoughnessMap, uvY).r * blendWeights.y +
                texture(uRoughnessMap, uvZ).r * blendWeights.z,
                0.04, 1.0);
            float metallic = clamp(
                texture(uMetallicMap, uvX).r * blendWeights.x +
                texture(uMetallicMap, uvY).r * blendWeights.y +
                texture(uMetallicMap, uvZ).r * blendWeights.z,
                0.0, 1.0);
            float ambientOcclusion =
                texture(uAmbientOcclusionMap, uvX).r * blendWeights.x +
                texture(uAmbientOcclusionMap, uvY).r * blendWeights.y +
                texture(uAmbientOcclusionMap, uvZ).r * blendWeights.z;
            vec3 mapNormalX = texture(uNormalMap, uvX).xyz * 2.0 - 1.0;
            vec3 mapNormalY = texture(uNormalMap, uvY).xyz * 2.0 - 1.0;
            vec3 mapNormalZ = texture(uNormalMap, uvZ).xyz * 2.0 - 1.0;
            vec3 normalX = vec3(axisSign.x * mapNormalX.z, mapNormalX.y,
                                axisSign.x * mapNormalX.x);
            vec3 normalY = vec3(mapNormalY.x, axisSign.y * mapNormalY.z,
                                axisSign.y * mapNormalY.y);
            vec3 normalZ = vec3(mapNormalZ.x, mapNormalZ.y,
                                axisSign.z * mapNormalZ.z);
            vec3 normal = normalize(normalX * blendWeights.x +
                                    normalY * blendWeights.y +
                                    normalZ * blendWeights.z);
            vec3 lightDirection = normalize(vec3(-0.45, 0.65, 1.0));
            vec3 viewDirection = normalize(uCameraPosition - vWorldPosition);
            vec3 halfwayDirection = normalize(viewDirection + lightDirection);
            float nDotL = max(dot(normal, lightDirection), 0.0);
            float nDotV = max(dot(normal, viewDirection), 0.0);
            float nDotH = max(dot(normal, halfwayDirection), 0.0);
            float hDotV = max(dot(halfwayDirection, viewDirection), 0.0);
            float alpha = roughness * roughness;
            float alphaSquared = alpha * alpha;
            float denominator = nDotH * nDotH * (alphaSquared - 1.0) + 1.0;
            float distribution = alphaSquared /
                max(PI * denominator * denominator, 0.0001);
            float k = (roughness + 1.0) * (roughness + 1.0) / 8.0;
            float geometryView = nDotV / (nDotV * (1.0 - k) + k);
            float geometryLight = nDotL / (nDotL * (1.0 - k) + k);
            vec3 f0 = mix(vec3(0.04), albedo, metallic);
            vec3 fresnel = f0 + (1.0 - f0) * pow(1.0 - hDotV, 5.0);
            vec3 specular = distribution * geometryView * geometryLight *
                fresnel / max(4.0 * nDotV * nDotL, 0.001);
            vec3 diffuse = (1.0 - fresnel) * (1.0 - metallic) *
                albedo / PI;
            vec3 color = (diffuse + specular) * vec3(5.0) * nDotL;
            color += albedo * 0.04 * ambientOcclusion;
            color = color / (color + vec3(1.0));
            color = pow(color, vec3(1.0 / 2.2));
            FragColor = vec4(color, 1.0);
        }
    )";

    GLint previousFramebuffer = 0;
    GLint previousTexture = 0;
    GLint previousVertexArray = 0;
    GLint previousArrayBuffer = 0;
    GLint previousRenderbuffer = 0;
    GLint previousActiveTexture = 0;
    glGetIntegerv(GL_FRAMEBUFFER_BINDING, &previousFramebuffer);
    glGetIntegerv(GL_TEXTURE_BINDING_2D, &previousTexture);
    glGetIntegerv(GL_VERTEX_ARRAY_BINDING, &previousVertexArray);
    glGetIntegerv(GL_ARRAY_BUFFER_BINDING, &previousArrayBuffer);
    glGetIntegerv(GL_RENDERBUFFER_BINDING, &previousRenderbuffer);
    glGetIntegerv(GL_ACTIVE_TEXTURE, &previousActiveTexture);

    const GLuint vertexShader =
        CompilePreviewShader(GL_VERTEX_SHADER, vertexSource, error);
    if (vertexShader == 0) {
        return false;
    }
    const GLuint fragmentShader =
        CompilePreviewShader(GL_FRAGMENT_SHADER, fragmentSource, error);
    if (fragmentShader == 0) {
        glDeleteShader(vertexShader);
        return false;
    }
    m_MaterialPreviewProgram = glCreateProgram();
    glAttachShader(m_MaterialPreviewProgram, vertexShader);
    glAttachShader(m_MaterialPreviewProgram, fragmentShader);
    glLinkProgram(m_MaterialPreviewProgram);
    glDeleteShader(vertexShader);
    glDeleteShader(fragmentShader);
    GLint linked = GL_FALSE;
    glGetProgramiv(m_MaterialPreviewProgram, GL_LINK_STATUS, &linked);
    if (linked == GL_FALSE) {
        GLint logLength = 0;
        glGetProgramiv(m_MaterialPreviewProgram, GL_INFO_LOG_LENGTH,
                       &logLength);
        std::string log(static_cast<std::size_t>(std::max(logLength, 1)), '\0');
        glGetProgramInfoLog(m_MaterialPreviewProgram, logLength, nullptr,
                            log.data());
        error = "Material preview shader linking failed: " + log;
        return false;
    }

    std::vector<float> vertices;
    std::vector<unsigned int> indices;
    vertices.reserve(static_cast<std::size_t>(longitudeSegments + 1) *
                     (latitudeSegments + 1) * 6);
    indices.reserve(static_cast<std::size_t>(longitudeSegments) *
                    latitudeSegments * 6);
    constexpr float pi = 3.14159265358979323846f;
    for (int latitude = 0; latitude <= latitudeSegments; ++latitude) {
        const float v = static_cast<float>(latitude) / latitudeSegments;
        const float angle = (v - 0.5f) * pi;
        const float ring = std::cos(angle);
        for (int longitude = 0; longitude <= longitudeSegments; ++longitude) {
            const float u = static_cast<float>(longitude) / longitudeSegments;
            const float around = u * 2.0f * pi;
            const float x = ring * std::sin(around);
            const float y = std::sin(angle);
            const float z = ring * std::cos(around);
            vertices.insert(vertices.end(), {x, y, z, x, y, z});
        }
    }
    for (int latitude = 0; latitude < latitudeSegments; ++latitude) {
        for (int longitude = 0; longitude < longitudeSegments; ++longitude) {
            const unsigned int first = static_cast<unsigned int>(
                latitude * (longitudeSegments + 1) + longitude);
            const unsigned int next =
                first + static_cast<unsigned int>(longitudeSegments + 1);
            indices.insert(indices.end(),
                           {first, first + 1, next, first + 1, next + 1,
                            next});
        }
    }
    m_MaterialPreviewIndexCount = static_cast<int>(indices.size());
    glGenBuffers(1, &m_MaterialPreviewVertexBuffer);
    glGenBuffers(1, &m_MaterialPreviewIndexBuffer);
    glBindBuffer(GL_ARRAY_BUFFER, m_MaterialPreviewVertexBuffer);
    glBufferData(GL_ARRAY_BUFFER,
                 static_cast<GLsizeiptr>(vertices.size() * sizeof(float)),
                 vertices.data(), GL_STATIC_DRAW);

    const auto restoreBindings = [&]() {
        glBindFramebuffer(GL_FRAMEBUFFER,
                          static_cast<GLuint>(previousFramebuffer));
        glBindVertexArray(static_cast<GLuint>(previousVertexArray));
        glBindBuffer(GL_ARRAY_BUFFER, static_cast<GLuint>(previousArrayBuffer));
        glBindRenderbuffer(GL_RENDERBUFFER,
                           static_cast<GLuint>(previousRenderbuffer));
        glBindTexture(GL_TEXTURE_2D, static_cast<GLuint>(previousTexture));
        glActiveTexture(static_cast<GLenum>(previousActiveTexture));
    };
    for (std::size_t i = 0; i < m_PreviewTargets.size(); ++i) {
        PreviewTarget& target = m_PreviewTargets[i];
        glGenVertexArrays(1, &target.vertexArray);
        glBindVertexArray(target.vertexArray);
        glBindBuffer(GL_ARRAY_BUFFER, m_MaterialPreviewVertexBuffer);
        glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, m_MaterialPreviewIndexBuffer);
        if (i == 0) {
            glBufferData(GL_ELEMENT_ARRAY_BUFFER,
                         static_cast<GLsizeiptr>(indices.size() *
                                                 sizeof(unsigned int)),
                         indices.data(), GL_STATIC_DRAW);
        }
        glEnableVertexAttribArray(0);
        glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 6 * sizeof(float),
                              nullptr);
        glEnableVertexAttribArray(1);
        glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, 6 * sizeof(float),
                              reinterpret_cast<void*>(3 * sizeof(float)));
        if (!CreatePreviewTarget(target, previewSize, error)) {
            restoreBindings();
            return false;
        }
    }

    glGenTextures(5, m_MaterialPreviewFallbackTextures);
    for (std::size_t i = 0; i < fallbackPixels.size(); ++i) {
        glBindTexture(GL_TEXTURE_2D, m_MaterialPreviewFallbackTextures[i]);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, 1, 1, 0, GL_RGBA,
                     GL_UNSIGNED_BYTE, fallbackPixels[i].data());
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
    }
    glBindVertexArray(0);
    restoreBindings();
    return true;
}

bool AssetBrowser::CreatePreviewTarget(PreviewTarget& target, int size,
                                       std::string& error) {
    glGenFramebuffers(1, &target.framebuffer);
    glBindFramebuffer(GL_FRAMEBUFFER, target.framebuffer);
    glGenTextures(1, &target.colorTexture);
    glBindTexture(GL_TEXTURE_2D, target.colorTexture);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, size, size, 0, GL_RGBA,
                 GL_UNSIGNED_BYTE, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D,
                           target.colorTexture, 0);
    glGenRenderbuffers(1, &target.depthBuffer);
    glBindRenderbuffer(GL_RENDERBUFFER, target.depthBuffer);
    glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT24, size, size);
    glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT,
                              GL_RENDERBUFFER, target.depthBuffer);
    if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) {
        error = "Could not create a material preview framebuffer.";
        return false;
    }
    return true;
}

void AssetBrowser::DestroyPreviewTarget(PreviewTarget& target) noexcept {
    if (target.vertexArray != 0) {
        glDeleteVertexArrays(1, &target.vertexArray);
    }
    if (target.framebuffer != 0) {
        glDeleteFramebuffers(1, &target.framebuffer);
    }
    if (target.colorTexture != 0) {
        glDeleteTextures(1, &target.colorTexture);
    }
    if (target.depthBuffer != 0) {
        glDeleteRenderbuffers(1, &target.depthBuffer);
    }
    target = {};
}

namespace {

// Saves the GL state touched by offscreen preview rendering and restores it
// on scope exit, including early returns.
class PreviewGlStateGuard {
public:
    PreviewGlStateGuard() {
        glGetIntegerv(GL_FRAMEBUFFER_BINDING, &m_Framebuffer);
        glGetIntegerv(GL_VIEWPORT, m_Viewport);
        glGetIntegerv(GL_CURRENT_PROGRAM, &m_Program);
        glGetIntegerv(GL_VERTEX_ARRAY_BINDING, &m_VertexArray);
        glGetIntegerv(GL_ACTIVE_TEXTURE, &m_ActiveTexture);
        glGetFloatv(GL_COLOR_CLEAR_VALUE, m_ClearColor);
        m_Depth = glIsEnabled(GL_DEPTH_TEST) == GL_TRUE;
        m_Blend = glIsEnabled(GL_BLEND) == GL_TRUE;
        m_Cull = glIsEnabled(GL_CULL_FACE) == GL_TRUE;
        for (std::size_t i = 0; i < m_Textures.size(); ++i) {
            glActiveTexture(GL_TEXTURE0 + static_cast<GLenum>(i));
            glGetIntegerv(GL_TEXTURE_BINDING_2D, &m_Textures[i]);
        }
    }

    PreviewGlStateGuard(const PreviewGlStateGuard&) = delete;
    PreviewGlStateGuard& operator=(const PreviewGlStateGuard&) = delete;

    ~PreviewGlStateGuard() {
        glBindVertexArray(static_cast<GLuint>(m_VertexArray));
        glBindFramebuffer(GL_FRAMEBUFFER, static_cast<GLuint>(m_Framebuffer));
        glViewport(m_Viewport[0], m_Viewport[1], m_Viewport[2], m_Viewport[3]);
        glUseProgram(static_cast<GLuint>(m_Program));
        for (std::size_t i = 0; i < m_Textures.size(); ++i) {
            glActiveTexture(GL_TEXTURE0 + static_cast<GLenum>(i));
            glBindTexture(GL_TEXTURE_2D, static_cast<GLuint>(m_Textures[i]));
        }
        glActiveTexture(static_cast<GLenum>(m_ActiveTexture));
        glClearColor(m_ClearColor[0], m_ClearColor[1], m_ClearColor[2],
                     m_ClearColor[3]);
        SetCapability(GL_DEPTH_TEST, m_Depth);
        SetCapability(GL_BLEND, m_Blend);
        SetCapability(GL_CULL_FACE, m_Cull);
    }

private:
    static void SetCapability(GLenum capability, bool enabled) {
        if (enabled) {
            glEnable(capability);
        } else {
            glDisable(capability);
        }
    }

    GLint m_Framebuffer = 0;
    GLint m_Viewport[4]{};
    GLint m_Program = 0;
    GLint m_VertexArray = 0;
    GLint m_ActiveTexture = 0;
    GLfloat m_ClearColor[4]{};
    std::array<GLint, 6> m_Textures{};
    bool m_Depth = false;
    bool m_Blend = false;
    bool m_Cull = false;
};

constexpr float MaterialPreviewDragSensitivity = 0.015f;
constexpr float MaterialPreviewMaxPitch = 1.45f;
constexpr float MaterialPreviewMaxRadius = 130.0f;
constexpr float MaterialPreviewWidthRatio = 0.34f;

}  // namespace

void AssetBrowser::RenderMaterialPreviewPass(
    const ModelMaterial& material, const PreviewTarget& previewTarget,
    const std::array<unsigned int, 5>& textures, bool applyOrbit) {
    constexpr int previewSize = 384;
    constexpr float previewCameraDistance = 4.0f;
    glBindFramebuffer(GL_FRAMEBUFFER, previewTarget.framebuffer);
    glViewport(0, 0, previewSize, previewSize);
    glEnable(GL_DEPTH_TEST);
    glDisable(GL_BLEND);
    glDisable(GL_CULL_FACE);
    glClearColor(0.0f, 0.0f, 0.0f, 0.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    const glm::mat4 projection =
        glm::perspective(glm::radians(35.0f), 1.0f, 0.1f, 10.0f);
    const float yaw = applyOrbit ? m_MaterialPreviewYaw : 0.0f;
    const float pitch = applyOrbit ? m_MaterialPreviewPitch : 0.0f;
    const float pitchCosine = std::cos(pitch);
    const glm::vec3 cameraPosition(
        previewCameraDistance * std::sin(yaw) * pitchCosine,
        previewCameraDistance * std::sin(pitch),
        previewCameraDistance * std::cos(yaw) * pitchCosine);
    const glm::mat4 view =
        glm::lookAt(cameraPosition, glm::vec3(0.0f),
                    glm::vec3(0.0f, 1.0f, 0.0f));
    const glm::mat4 model(1.0f);
    glUseProgram(m_MaterialPreviewProgram);
    glUniformMatrix4fv(
        glGetUniformLocation(m_MaterialPreviewProgram, "uViewProjection"), 1,
        GL_FALSE, glm::value_ptr(projection * view));
    glUniformMatrix4fv(glGetUniformLocation(m_MaterialPreviewProgram, "uModel"),
                       1, GL_FALSE, glm::value_ptr(model));
    glUniform3fv(
        glGetUniformLocation(m_MaterialPreviewProgram, "uMaterialTiling"), 1,
        glm::value_ptr(material.textureScale));
    glUniform3f(
        glGetUniformLocation(m_MaterialPreviewProgram, "uCameraPosition"),
        cameraPosition.x, cameraPosition.y, cameraPosition.z);
    constexpr std::array<const char*, 5> samplerNames{
        "uDiffuseMap", "uNormalMap", "uRoughnessMap", "uMetallicMap",
        "uAmbientOcclusionMap"};
    glBindVertexArray(previewTarget.vertexArray);
    for (std::size_t i = 0; i < textures.size(); ++i) {
        glActiveTexture(GL_TEXTURE0 + static_cast<GLenum>(i));
        glBindTexture(GL_TEXTURE_2D, textures[i]);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
        glUniform1i(
            glGetUniformLocation(m_MaterialPreviewProgram, samplerNames[i]),
            static_cast<GLint>(i));
    }
    glDrawElements(GL_TRIANGLES, m_MaterialPreviewIndexCount, GL_UNSIGNED_INT,
                   nullptr);
}

void AssetBrowser::DrawMaterialPreview(const ModelMaterial& material,
                                       ImVec2 center, float radius,
                                       bool drawImage,
                                       MaterialPreviewTarget target) {
    const ImVec2 topLeft(center.x - radius, center.y - radius);
    const ImVec2 bottomRight(center.x + radius, center.y + radius);
    if (!m_MaterialPreviewInitialized) {
        m_MaterialPreviewInitialized = true;
        if (!InitializeMaterialPreview(m_MaterialPreviewError)) {
            m_PreviewError = m_MaterialPreviewError;
        }
    }
    const PreviewTarget& previewTarget = GetPreviewTarget(target);
    const GLuint framebuffer = previewTarget.framebuffer;
    const GLuint colorTexture = previewTarget.colorTexture;
    if (!m_MaterialPreviewError.empty() || m_MaterialPreviewProgram == 0 ||
        framebuffer == 0 || colorTexture == 0) {
        if (drawImage) {
            ImGui::GetWindowDrawList()->AddRectFilled(
                topLeft, bottomRight, IM_COL32(35, 35, 35, 255), 4.0f);
            ImGui::GetWindowDrawList()->AddText(
                ImVec2(topLeft.x + 8.0f, topLeft.y + 8.0f),
                IM_COL32(255, 120, 100, 255),
                "Material preview unavailable");
        }
        return;
    }

    std::array<GLuint, 5> textures{};
    const std::array<const std::filesystem::path*, 5> paths{
        &material.diffuse, &material.normal, &material.roughness,
        &material.metallic, &material.ambientOcclusion};
    for (std::size_t i = 0; i < paths.size(); ++i) {
        if (!paths[i]->empty() && IsPreviewableImage(*paths[i])) {
            AssetThumbnail& thumbnail = LoadThumbnail(*paths[i]);
            textures[i] = thumbnail.texture;
        }
        if (textures[i] == 0) {
            textures[i] = m_MaterialPreviewFallbackTextures[i];
        }
    }

    {
        PreviewGlStateGuard stateGuard;
        RenderMaterialPreviewPass(material, previewTarget, textures,
                                  target == MaterialPreviewTarget::MaterialCreator);
    }
    if (drawImage) {
        ImGui::GetWindowDrawList()->AddImage(
            reinterpret_cast<ImTextureID>(
                static_cast<intptr_t>(colorTexture)),
            topLeft, bottomRight, ImVec2(0.0f, 1.0f), ImVec2(1.0f, 0.0f));
    }
}

void AssetBrowser::DrawMaterialCreator() {
    if (!m_MaterialCreatorOpen) {
        return;
    }

    const ImVec2 displaySize = ImGui::GetIO().DisplaySize;
    ImGui::SetNextWindowPos(ImVec2(0.0f, 0.0f), ImGuiCond_Always);
    ImGui::SetNextWindowSize(displaySize, ImGuiCond_Always);
    constexpr ImGuiWindowFlags editorFlags =
        ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove |
        ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoSavedSettings;
    if (!ImGui::Begin("Material Editor", nullptr, editorFlags)) {
        ImGui::End();
        return;
    }

    const float footerHeight = ImGui::GetFrameHeightWithSpacing() * 2.0f;
    ImGui::BeginChild("MaterialEditorFields",
                      ImVec2(0.0f, -footerHeight), false);
    ImGui::TextUnformatted("Material name");
    ImGui::SetNextItemWidth(-1.0f);
    if (ImGui::InputText("##MaterialName", m_MaterialName,
                         sizeof(m_MaterialName))) {
        m_MaterialDraft.name = m_MaterialName;
    }
    m_MaterialDraft.name = m_MaterialName;
    ImGui::Separator();
    ImGui::TextUnformatted("World texture density");
    ImGui::TextDisabled("Higher values repeat textures more frequently.");
    float textureScale[3] = {m_MaterialDraft.textureScale.x,
                             m_MaterialDraft.textureScale.y,
                             m_MaterialDraft.textureScale.z};
    if (ImGui::InputFloat3("X / Y / Z", textureScale, "%.3f")) {
        const bool valid =
            std::isfinite(textureScale[0]) &&
            std::isfinite(textureScale[1]) &&
            std::isfinite(textureScale[2]) && textureScale[0] > 0.0f &&
            textureScale[1] > 0.0f && textureScale[2] > 0.0f &&
            textureScale[0] <= 1000.0f && textureScale[1] <= 1000.0f &&
            textureScale[2] <= 1000.0f;
        if (valid) {
            m_MaterialDraft.textureScale =
                glm::vec3(textureScale[0], textureScale[1], textureScale[2]);
            m_MaterialPreviewDirty = true;
            m_MaterialError.clear();
        } else {
            m_MaterialError =
                "Texture density must be between 0 and 1000 on each axis.";
        }
    }
    ImGui::Separator();
    ImGui::TextUnformatted("Height displacement");
    ImGui::SetNextItemWidth(150.0f);
    ImGui::InputFloat("Strength", &m_MaterialDraft.heightScale, 0.01f, 0.1f,
                      "%.3f");
    if (!std::isfinite(m_MaterialDraft.heightScale) ||
        m_MaterialDraft.heightScale < 0.0f ||
        m_MaterialDraft.heightScale > 10.0f) {
        m_MaterialError = "Height displacement must be between 0 and 10.";
    } else if (ImGui::IsItemEdited()) {
        m_MaterialError.clear();
    }
    ImGui::TextDisabled(
        "Height is baked into mesh vertices when the material is applied.");
    ImGui::Checkbox("Two-sided material", &m_MaterialDraft.twoSided);

    const ImVec2 available = ImGui::GetContentRegionAvail();
    const float radius = std::min(MaterialPreviewMaxRadius,
                                  available.x * MaterialPreviewWidthRatio);
    const ImVec2 previewOrigin = ImGui::GetCursorScreenPos();
    const ImVec2 previewCenter(previewOrigin.x + available.x * 0.5f,
                               previewOrigin.y + radius + 8.0f);
    const ImVec2 previewTopLeft(previewCenter.x - radius,
                                previewCenter.y - radius);
    const float previewDiameter = radius * 2.0f;
    ImGui::SetCursorScreenPos(previewTopLeft);
    ImGui::InvisibleButton("##MaterialPreviewDrag",
                           ImVec2(previewDiameter, previewDiameter));
    const bool previewHovered = ImGui::IsItemHovered();
    const bool leftButtonDown =
        glfwGetMouseButton(m_MaterialEditorWindow, GLFW_MOUSE_BUTTON_LEFT) ==
        GLFW_PRESS;
    if (previewHovered && leftButtonDown && !m_MaterialPreviewDragging) {
        glfwGetCursorPos(m_MaterialEditorWindow, &m_MaterialPreviewLastMouseX,
                         &m_MaterialPreviewLastMouseY);
        m_MaterialPreviewDragging = true;
    }
    if (m_MaterialPreviewDragging && leftButtonDown) {
        double cursorX = 0.0;
        double cursorY = 0.0;
        glfwGetCursorPos(m_MaterialEditorWindow, &cursorX, &cursorY);
        m_MaterialPreviewYaw +=
            static_cast<float>(cursorX - m_MaterialPreviewLastMouseX) *
            MaterialPreviewDragSensitivity;
        m_MaterialPreviewPitch = std::clamp(
            m_MaterialPreviewPitch +
                static_cast<float>(cursorY - m_MaterialPreviewLastMouseY) *
                    MaterialPreviewDragSensitivity,
            -MaterialPreviewMaxPitch, MaterialPreviewMaxPitch);
        m_MaterialPreviewDirty = true;
        m_MaterialPreviewLastMouseX = cursorX;
        m_MaterialPreviewLastMouseY = cursorY;
    } else if (!leftButtonDown) {
        m_MaterialPreviewDragging = false;
    }
    if (previewHovered) {
        ImGui::SetTooltip(
            "Hold left mouse and drag to orbit the preview.");
    }
    ImGui::GetWindowDrawList()->AddImage(
        reinterpret_cast<ImTextureID>(
            static_cast<intptr_t>(
                GetPreviewTarget(MaterialPreviewTarget::MaterialCreator)
                    .colorTexture)),
        previewTopLeft,
        ImVec2(previewTopLeft.x + previewDiameter,
               previewTopLeft.y + previewDiameter),
        ImVec2(0.0f, 1.0f), ImVec2(1.0f, 0.0f));
    ImGui::SetCursorScreenPos(
        ImVec2(previewOrigin.x, previewOrigin.y + previewDiameter + 16.0f));

    ImGui::TextUnformatted("Texture maps");
    constexpr std::array<const char*, 6> mapLabels{
        "Diffuse", "Normal", "Roughness", "Metallic", "Ambient Occlusion",
        "Height"};
    const std::array<std::filesystem::path*, 6> mapPaths{
        &m_MaterialDraft.diffuse, &m_MaterialDraft.normal,
        &m_MaterialDraft.roughness, &m_MaterialDraft.metallic,
        &m_MaterialDraft.ambientOcclusion, &m_MaterialDraft.height};
    for (std::size_t i = 0; i < mapPaths.size(); ++i) {
        ImGui::PushID(static_cast<int>(i) + 40);
        ImGui::Spacing();
        ImGui::Text("%s", mapLabels[i]);
        ImGui::BeginGroup();
        constexpr ImVec2 thumbnailSize(72.0f, 52.0f);
        if (!mapPaths[i]->empty()) {
            AssetThumbnail& thumbnail = LoadThumbnail(*mapPaths[i]);
            if (thumbnail.texture != 0) {
                const float imageAspect =
                    static_cast<float>(thumbnail.width) /
                    static_cast<float>(thumbnail.height);
                ImVec2 imageSize = thumbnailSize;
                if (imageAspect > imageSize.x / imageSize.y) {
                    imageSize.y = imageSize.x / imageAspect;
                } else {
                    imageSize.x = imageSize.y * imageAspect;
                }
                ImGui::Image(
                    reinterpret_cast<ImTextureID>(
                        static_cast<intptr_t>(thumbnail.texture)),
                    imageSize);
            } else {
                ImGui::Button("Image unavailable", thumbnailSize);
            }
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip("%s", mapPaths[i]->string().c_str());
            }
        } else {
            const ImVec2 imageTopLeft = ImGui::GetCursorScreenPos();
            ImGui::Dummy(thumbnailSize);
            ImDrawList* drawList = ImGui::GetWindowDrawList();
            drawList->AddRectFilled(
                imageTopLeft,
                ImVec2(imageTopLeft.x + thumbnailSize.x,
                       imageTopLeft.y + thumbnailSize.y),
                IM_COL32(42, 46, 52, 255), 3.0f);
            drawList->AddText(
                ImVec2(imageTopLeft.x + 7.0f, imageTopLeft.y + 19.0f),
                IM_COL32(175, 180, 188, 255), "No texture");
        }
        ImGui::EndGroup();
        ImGui::SameLine(0.0f, 14.0f);
        ImGui::BeginGroup();
        ImGui::TextDisabled("%s",
                            mapPaths[i]->empty() ? "No texture selected"
                                                 : "Texture assigned");
        if (ImGui::Button("Choose...")) {
            std::filesystem::path source;
            bool cancelled = false;
            std::string dialogError;
            if (OpenTextureFileDialog(source, m_CurrentDirectory, cancelled,
                                      dialogError)) {
                std::error_code filesystemError;
                const std::filesystem::path resolved =
                    std::filesystem::weakly_canonical(source, filesystemError);
                int imageWidth = 0;
                int imageHeight = 0;
                if (filesystemError ||
                    !std::filesystem::is_regular_file(resolved,
                                                      filesystemError) ||
                    filesystemError ||
                    !stbi_info(resolved.string().c_str(), &imageWidth,
                               &imageHeight, nullptr) ||
                    imageWidth <= 0 || imageHeight <= 0) {
                    m_MaterialError =
                        "Selected file is not a readable supported image: '" +
                        source.string() + "'" +
                        (filesystemError ? ": " + filesystemError.message()
                                         : ".");
                } else {
                    *mapPaths[i] = resolved;
                    m_MaterialPreviewDirty = true;
                    m_MaterialError.clear();
                }
            } else if (!cancelled) {
                m_MaterialError = std::move(dialogError);
            }
        }
        ImGui::SameLine();
        if (ImGui::Button("Clear")) {
            mapPaths[i]->clear();
            m_MaterialPreviewDirty = true;
            m_MaterialError.clear();
        }
        ImGui::EndGroup();
        ImGui::PopID();
    }
    if (!m_MaterialError.empty()) {
        ImGui::TextWrapped("%s", m_MaterialError.c_str());
    }

    ImGui::EndChild();
    ImGui::Separator();
    if (ImGui::Button(m_EditingMaterialPath.empty() ? "Create material"
                                                    : "Save material")) {
        const std::filesystem::path namePath(m_MaterialName);
        const std::string materialName(m_MaterialName);
        const bool hasVisibleCharacter =
            materialName.find_first_not_of(" \t\r\n") != std::string::npos;
        if (namePath.empty() || namePath.filename() != namePath ||
            namePath == "." || namePath == ".." || !hasVisibleCharacter ||
            materialName.find_first_of("/\\") != std::string::npos) {
            m_MaterialError = "Enter a non-empty material name without folder "
                              "separators.";
        } else {
            std::error_code filesystemError;
            const std::filesystem::path materialDirectory =
                m_AssetRoot / "Materials";
            std::filesystem::create_directories(materialDirectory,
                                                filesystemError);
            if (filesystemError) {
                m_MaterialError =
                    "Could not create project Materials folder: " +
                    filesystemError.message();
            } else {
                const std::filesystem::path output =
                    m_EditingMaterialPath.empty()
                        ? materialDirectory /
                              (std::string(m_MaterialName) + ".aqenmat")
                        : m_EditingMaterialPath;
                if (SaveAqenMaterial(output, m_MaterialDraft, m_MaterialError,
                                     !m_EditingMaterialPath.empty())) {
                    m_MaterialError.clear();
                    m_MaterialCreatorOpen = false;
                    m_MaterialDraft.asset = output;
                    NavigateTo(materialDirectory);
                    m_SelectedAsset = output;
                    LoadPreview(output);
                    m_UpdatedMaterial =
                        std::make_pair(output, m_MaterialDraft);
                    SetStatusMessage(
                        std::string(m_EditingMaterialPath.empty()
                                        ? "Created material "
                                        : "Saved material ") +
                        std::string(m_MaterialName) + ".");
                    m_EditingMaterialPath.clear();
                    std::snprintf(m_MaterialName, sizeof(m_MaterialName),
                                  "New Material");
                    m_MaterialDraft = {};
                    m_MaterialDraft.name = m_MaterialName;
                    m_MaterialDraft.textureScale = glm::vec3(1.0f);
                }
            }
        }
    }
    ImGui::SameLine();
    if (ImGui::Button("Cancel")) {
        m_MaterialCreatorOpen = false;
        m_EditingMaterialPath.clear();
    }
    ImGui::End();
}

void AssetBrowser::DrawActorInspector(std::string& actorName,
                                      glm::vec3& position,
                                      glm::vec3& rotation,
                                      glm::vec3& scale,
                                      ModelMaterial& material,
                                      LightProperties* light) {
    const ImGuiIO& io = ImGui::GetIO();
    constexpr float panelWidth = 310.0f;
    constexpr float margin = 12.0f;
    const float availableWidth = std::max(0.0f, io.DisplaySize.x - margin * 2.0f);
    const float actualWidth = std::min(panelWidth, availableWidth);
    ImGui::SetNextWindowPos(
        ImVec2(std::max(margin, io.DisplaySize.x - actualWidth - margin), 52.0f),
        ImGuiCond_Always);
    ImGui::SetNextWindowSize(ImVec2(actualWidth, 660.0f), ImGuiCond_Always);
    constexpr ImGuiWindowFlags flags =
        ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoResize;
    const bool contentsVisible =
        ImGui::Begin("Object Details", nullptr, flags);
    const ImVec2 panelPosition = ImGui::GetWindowPos();
    const ImVec2 panelSize = ImGui::GetWindowSize();
    m_InspectorPanelVisible = true;
    m_InspectorPanelMin = {panelPosition.x, panelPosition.y};
    m_InspectorPanelMax = {panelPosition.x + panelSize.x,
                           panelPosition.y + panelSize.y};
    m_InspectorPanelCapturesKeyboard =
        ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows);
    if (!contentsVisible) {
        ImGui::End();
        return;
    }
    ImGui::TextUnformatted("Name");
    ImGui::SetNextItemWidth(-1.0f);
    const bool nameCommitted =
        ImGui::InputText("##ActorName", m_InspectorName,
                         sizeof(m_InspectorName),
                         ImGuiInputTextFlags_EnterReturnsTrue);
    if (nameCommitted || ImGui::IsItemDeactivatedAfterEdit()) {
        std::string updatedName(m_InspectorName);
        if (updatedName.empty()) {
            updatedName = actorName.empty() ? "Object" : actorName;
            std::snprintf(m_InspectorName, sizeof(m_InspectorName), "%s",
                          updatedName.c_str());
        }
        if (updatedName != actorName) {
            actorName = updatedName;
            SetStatusMessage("Renamed object to " + updatedName + ".");
        }
    }

    const auto drawVector = [&](const char* label, glm::vec3& vector,
                                int draftOffset) {
        ImGui::Separator();
        ImGui::TextUnformatted(label);
        constexpr std::array<const char*, 3> axes{"X", "Y", "Z"};
        if (!ImGui::BeginTable(label, 3,
                               ImGuiTableFlags_SizingStretchSame |
                                   ImGuiTableFlags_NoSavedSettings)) {
            return;
        }
        for (int axis = 0; axis < 3; ++axis) {
            ImGui::TableNextColumn();
            ImGui::PushID(draftOffset + axis);
            ImGui::TextUnformatted(axes[axis]);
            ImGui::SameLine();
            ImGui::SetNextItemWidth(-1.0f);
            const bool committed = ImGui::InputFloat(
                "##Value", &m_InspectorTransformDraft[draftOffset + axis],
                0.0f, 0.0f, "%.3f", ImGuiInputTextFlags_EnterReturnsTrue);
            const bool deactivated = ImGui::IsItemDeactivatedAfterEdit();
            const bool active = ImGui::IsItemActive();
            if (committed || deactivated) {
                const float updatedValue =
                    m_InspectorTransformDraft[draftOffset + axis];
                if (std::isfinite(updatedValue)) {
                    vector[axis] = updatedValue;
                } else {
                    m_InspectorTransformDraft[draftOffset + axis] =
                        vector[axis];
                    SetStatusMessage(std::string("Ignored invalid ") + label +
                                     " value.");
                }
            } else if (!active) {
                m_InspectorTransformDraft[draftOffset + axis] = vector[axis];
            }
            ImGui::PopID();
        }
        ImGui::EndTable();
    };
    const bool editorOnlyPosition =
        light && (light->type == LightType::Directional ||
                  light->type == LightType::Sky);
    drawVector(editorOnlyPosition ? "Editor Icon Position" : "Position",
               position, 0);

    const auto drawRotation = [&]() {
        ImGui::Separator();
        ImGui::TextUnformatted("Rotation (degrees, 0-359)");
        constexpr std::array<const char*, 3> rotationAxes{"X", "Y", "Z"};
        if (ImGui::BeginTable(
                "RotationAxes", 3,
                ImGuiTableFlags_SizingStretchSame |
                    ImGuiTableFlags_NoSavedSettings)) {
            for (int axis = 0; axis < 3; ++axis) {
                ImGui::TableNextColumn();
                ImGui::PushID(axis + 3);
                ImGui::TextUnformatted(rotationAxes[axis]);
                ImGui::SameLine();
                ImGui::SetNextItemWidth(-1.0f);
                const bool committed = ImGui::InputInt(
                    "##Value", &m_InspectorRotationDraft[axis], 0, 0,
                    ImGuiInputTextFlags_EnterReturnsTrue);
                const bool deactivated = ImGui::IsItemDeactivatedAfterEdit();
                const bool active = ImGui::IsItemActive();
                if (committed || deactivated) {
                    const int degrees =
                        std::clamp(m_InspectorRotationDraft[axis], 0, 359);
                    m_InspectorRotationDraft[axis] = degrees;
                    constexpr float degreesToRadians =
                        3.14159265358979323846f / 180.0f;
                    rotation[axis] = static_cast<float>(degrees) *
                                     degreesToRadians;
                } else if (!active) {
                    m_InspectorRotationDraft[axis] =
                        RotationDegrees(rotation[axis]);
                }
                ImGui::PopID();
            }
            ImGui::EndTable();
        }
    };

    if (light) {
        if (IsLightRotatable(light->type)) {
            drawRotation();
            ImGui::TextDisabled("Rotation controls the light's direction.");
        } else if (light->type == LightType::Sky) {
            ImGui::Separator();
            ImGui::TextDisabled(
                "Fixed incoming direction from +Y; light rays travel down -Y, "
                "perpendicular to the XZ ground grid.");
        }

        ImGui::Separator();
        ImGui::Text("Light Properties - %s",
                    std::string(LightTypeName(light->type)).c_str());
        const glm::vec3 originalColor = light->color;
        float color[3]{light->color.r, light->color.g, light->color.b};
        if (ImGui::ColorEdit3("Color", color)) {
            if (std::isfinite(color[0]) && std::isfinite(color[1]) &&
                std::isfinite(color[2])) {
                light->color = glm::clamp(
                    glm::vec3(color[0], color[1], color[2]), 0.0f, 1.0f);
            } else {
                light->color = originalColor;
                SetStatusMessage("Ignored invalid light color.");
            }
        }

        const char* applicability = "";
        switch (light->type) {
        case LightType::Directional:
            applicability =
                "Unlimited range; cone angles and surface dimensions do not "
                "apply.";
            break;
        case LightType::Spot:
            applicability =
                "Cone angles shape the beam; surface dimensions do not apply.";
            break;
        case LightType::Point:
            applicability =
                "Emits in every direction; rotation, cone angles, and surface "
                "dimensions do not apply.";
            break;
        case LightType::Sky:
            applicability =
                "Fixed +Y direction and unlimited range; rotation, cone "
                "angles, and surface dimensions do not apply.";
            break;
        case LightType::Rect:
            applicability =
                "One-sided emitting surface; cone angles do not apply.";
            break;
        }
        ImGui::TextDisabled("%s", applicability);

        const auto drawLightFloat = [&](const char* label,
                                        const char* description,
                                        int draftIndex, float& value,
                                        float minimum, float maximum) {
            ImGui::TextUnformatted(label);
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip("%s", description);
            }
            ImGui::SetNextItemWidth(-1.0f);
            ImGui::PushID(draftIndex + 100);
            const bool committed = ImGui::InputFloat(
                "##Value", &m_InspectorLightDraft[draftIndex], 0.0f, 0.0f,
                "%.3f", ImGuiInputTextFlags_EnterReturnsTrue);
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip("%s", description);
            }
            const bool deactivated = ImGui::IsItemDeactivatedAfterEdit();
            const bool active = ImGui::IsItemActive();
            if (committed || deactivated) {
                const float updated = m_InspectorLightDraft[draftIndex];
                if (std::isfinite(updated)) {
                    value = std::clamp(updated, minimum, maximum);
                    m_InspectorLightDraft[draftIndex] = value;
                } else {
                    m_InspectorLightDraft[draftIndex] = value;
                    SetStatusMessage(std::string("Ignored invalid ") + label +
                                     " value.");
                }
            } else if (!active) {
                m_InspectorLightDraft[draftIndex] = value;
            }
            ImGui::PopID();
        };
        drawLightFloat(
            "Intensity",
            "Renderer-relative strength. Higher values make the light "
            "brighter; 0 turns off its direct illumination.",
            3, light->intensity, 0.0f, 10000.0f);
        if (LightHasFiniteRange(light->type)) {
            drawLightFloat(
                "Range",
                "World units. The maximum distance the light affects "
                "surfaces; increasing it lets the light reach farther.",
                4, light->range, 0.1f, 1000.0f);
        }
        if (light->type == LightType::Spot) {
            drawLightFloat(
                "Inner Cone Angle (degrees)",
                "Degrees from the beam center. Increasing it widens the fully "
                "lit center and narrows the fade toward the outer angle.",
                5,
                           light->innerConeAngleDegrees, 0.0f,
                           light->outerConeAngleDegrees - 0.1f);
            drawLightFloat(
                "Outer Cone Angle (degrees)",
                "Degrees from the beam center. Increasing it widens the beam; "
                "surfaces beyond this angle receive no Spot Light.",
                6,
                           light->outerConeAngleDegrees,
                           std::max(1.0f,
                                    light->innerConeAngleDegrees + 0.1f),
                           89.0f);
        } else if (light->type == LightType::Rect) {
            drawLightFloat(
                "Surface Width",
                "World units. Increasing width makes the Rect Light's "
                "emitting surface wider and changes its area contribution.",
                7, light->rectSize.x, 0.1f, 1000.0f);
            drawLightFloat(
                "Surface Height",
                "World units. Increasing height makes the Rect Light's "
                "emitting surface taller and changes its area contribution.",
                8, light->rectSize.y, 0.1f, 1000.0f);
            ImGui::TextDisabled("The emitting side is the rotated front face.");
        }
        ImGui::End();
        return;
    }

    drawRotation();
    drawVector("Scale", scale, 6);
    ImGui::Separator();
    ImGui::TextUnformatted("Material");
    const auto applyMaterialAsset = [&](const std::filesystem::path& file) {
        ModelMaterial loadedMaterial;
        std::string error;
        if (!LoadAqenMaterial(file, loadedMaterial, error)) {
            m_MaterialError = std::move(error);
            return false;
        }
        material = std::move(loadedMaterial);
        m_MaterialError.clear();
        SetStatusMessage("Applied material '" + material.name + "' to " +
                         actorName + ".");
        return true;
    };
    const std::string currentMaterial =
        material.name.empty() ? "No material assigned" : material.name;
    const float materialSlotSize =
        std::min(190.0f, ImGui::GetContentRegionAvail().x);
    const ImVec2 slotTopLeft(
        ImGui::GetCursorScreenPos().x +
            (ImGui::GetContentRegionAvail().x - materialSlotSize) * 0.5f,
        ImGui::GetCursorScreenPos().y);
    ImGui::SetCursorScreenPos(slotTopLeft);
    ImGui::InvisibleButton("##MaterialDropSlot",
                           ImVec2(materialSlotSize, materialSlotSize));
    if (ImGui::BeginDragDropTarget()) {
        if (const ImGuiPayload* payload =
                ImGui::AcceptDragDropPayload("AQENMAT_ASSET")) {
            const auto* path = static_cast<const char*>(payload->Data);
            if (path && payload->DataSize > 1) {
                applyMaterialAsset(std::filesystem::path(path));
            }
        }
        ImGui::EndDragDropTarget();
    }
    const ImVec2 slotBottomRight(slotTopLeft.x + materialSlotSize,
                                 slotTopLeft.y + materialSlotSize);
    ImDrawList* drawList = ImGui::GetWindowDrawList();
    drawList->AddRectFilled(slotTopLeft, slotBottomRight,
                            IM_COL32(42, 46, 52, 255), 4.0f);
    const float previewRadius = materialSlotSize * 0.5f;
    const ImVec2 previewCenter(slotTopLeft.x + previewRadius,
                               slotTopLeft.y + previewRadius);
    DrawMaterialPreview(material, previewCenter, previewRadius, true,
                        MaterialPreviewTarget::ActorDetails);
    if (material.name.empty() && material.diffuse.empty() &&
        material.normal.empty() && material.roughness.empty() &&
        material.metallic.empty()) {
        const char* placeholder = "Drop material here";
        const ImVec2 placeholderSize = ImGui::CalcTextSize(placeholder);
        drawList->AddText(
            ImVec2(slotTopLeft.x + (materialSlotSize - placeholderSize.x) *
                                        0.5f,
                   slotTopLeft.y +
                       (materialSlotSize - placeholderSize.y) * 0.5f),
            IM_COL32(185, 190, 198, 255), placeholder);
    }
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("%s", currentMaterial.c_str());
    }
    drawList->AddRect(slotTopLeft, slotBottomRight,
                      ImGui::GetColorU32(ImGui::IsItemHovered()
                                             ? ImGuiCol_ButtonHovered
                                             : ImGuiCol_Border),
                      4.0f, 0, 2.0f);
    const ImVec2 textSize = ImGui::CalcTextSize(currentMaterial.c_str());
    const ImVec2 labelMin(slotTopLeft.x + 4.0f,
                          slotBottomRight.y - ImGui::GetFrameHeight() - 4.0f);
    drawList->AddRectFilled(labelMin, slotBottomRight,
                            IM_COL32(0, 0, 0, 190), 0.0f);
    drawList->PushClipRect(labelMin, slotBottomRight, true);
    drawList->AddText(
        ImVec2(slotTopLeft.x +
                   std::max(5.0f, (materialSlotSize - textSize.x) * 0.5f),
               labelMin.y + 2.0f),
        IM_COL32(255, 255, 255, 255), currentMaterial.c_str());
    drawList->PopClipRect();

    const float buttonSpacing = ImGui::GetStyle().ItemSpacing.x;
    const float buttonWidth =
        (ImGui::GetContentRegionAvail().x - buttonSpacing) * 0.5f;
    if (ImGui::Button("Browse...", ImVec2(buttonWidth, 0.0f))) {
        std::snprintf(m_MaterialSearch, sizeof(m_MaterialSearch), "%s", "");
        ImGui::OpenPopup("Select Material");
    }
    ImGui::SameLine();
    if (ImGui::Button("Clear", ImVec2(buttonWidth, 0.0f))) {
        material = {};
        m_MaterialError.clear();
        SetStatusMessage("Cleared material from " + actorName + ".");
    }
    if (ImGui::BeginPopup("Select Material")) {
        ImGui::SetNextItemWidth(-1.0f);
        ImGui::InputTextWithHint("##MaterialSearch", "Search materials...",
                                 m_MaterialSearch,
                                 sizeof(m_MaterialSearch));
        const std::string query = ToLower(m_MaterialSearch);
        std::vector<std::filesystem::path> materialFiles;
        std::error_code scanError;
        std::filesystem::recursive_directory_iterator iterator(
            m_AssetRoot, std::filesystem::directory_options::skip_permission_denied,
            scanError);
        const std::filesystem::recursive_directory_iterator end;
        while (!scanError && iterator != end) {
            std::error_code typeError;
            if (iterator->is_regular_file(typeError) && !typeError &&
                IsMaterialAsset(iterator->path())) {
                const std::string candidate =
                    ToLower(iterator->path().filename().string());
                if (query.empty() || candidate.find(query) != std::string::npos) {
                    materialFiles.push_back(iterator->path());
                }
            }
            iterator.increment(scanError);
        }
        std::sort(materialFiles.begin(), materialFiles.end(),
                  [](const auto& left, const auto& right) {
                      return ToLower(left.filename().string()) <
                             ToLower(right.filename().string());
                  });
        ImGui::BeginChild("MaterialSearchResults", ImVec2(360.0f, 220.0f),
                          true);
        if (scanError) {
            ImGui::TextWrapped("Could not search project materials: %s",
                               scanError.message().c_str());
        } else if (materialFiles.empty()) {
            ImGui::TextDisabled(query.empty() ? "No material assets found."
                                              : "No matching materials.");
        } else {
            for (const std::filesystem::path& file : materialFiles) {
                ModelMaterial candidate;
                std::string loadError;
                if (!LoadAqenMaterial(file, candidate, loadError)) {
                    ImGui::TextDisabled("%s (invalid: %s)",
                                        file.filename().string().c_str(),
                                        loadError.c_str());
                    continue;
                }
                const std::string label =
                    candidate.name.empty() ? file.stem().string()
                                           : candidate.name;
                if (ImGui::Selectable(label.c_str())) {
                    applyMaterialAsset(file);
                    ImGui::CloseCurrentPopup();
                }
                if (ImGui::IsItemHovered()) {
                    ImGui::SetTooltip("%s",
                                      RelativeAssetPath(file, m_ProjectRoot)
                                          .c_str());
                }
            }
        }
        ImGui::EndChild();
        ImGui::EndPopup();
    }
    ImGui::TextDisabled("Drag a material here or browse the project library.");
    if (!m_MaterialError.empty()) {
        ImGui::TextWrapped("%s", m_MaterialError.c_str());
    }
    ImGui::End();
}

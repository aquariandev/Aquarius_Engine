#pragma once

#include "Camera.hpp"
#include "EditorViewMode.hpp"
#include "ModelAssets.hpp"
#include "EditorScene.hpp"
#include "SceneRecovery.hpp"

#include <glm/glm.hpp>

#include <array>
#include <deque>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <utility>
#include <string>
#include <unordered_map>
#include <vector>

struct GLFWwindow;
struct ImFont;
struct ImGuiContext;
struct ImVec2;

struct UnresolvedActorView {
    std::size_t sceneOrder = 0;
    std::string name;
    std::filesystem::path path;
    std::string error;
    bool proceduralLandscape = false;
};

struct ModelPreviewMesh {
    std::vector<std::array<float, 8>> vertices;
    std::vector<std::uint32_t> indices;
};

class AssetBrowser {
public:
    bool Initialize(GLFWwindow* window, std::filesystem::path projectRoot,
                    std::filesystem::path assetRoot, std::string& error);
    void Shutdown() noexcept;
    void Render(const void* actorIdentity, std::string* actorName,
                glm::vec3* position,
                glm::vec3* rotation, glm::vec3* scale,
                ModelMaterial* material,
                LightProperties* light,
                CameraProjection cameraProjection,
                const std::vector<UnresolvedActorView>& unresolvedActors);
    std::optional<std::filesystem::path> TakePendingSceneDrop();
    std::optional<EditorSceneModel> TakePendingLandscape();
    std::optional<LightType> TakePendingLightCreation();
    bool TakeProjectionToggleRequest();
    std::optional<std::filesystem::path> TakePendingMaterialDrop();
    std::optional<std::pair<std::filesystem::path, ModelMaterial>>
    TakeUpdatedMaterial();
    std::optional<std::filesystem::path> GetActiveSceneDrag() const;
    bool TakeSaveRequest();
    std::optional<std::pair<std::filesystem::path, std::filesystem::path>>
    TakePendingImport();
    void CompleteModelImport(const std::filesystem::path& output,
                             std::string error);
    void SetPreviewError(std::string error);
    void SetStatusMessage(std::string message);
    void RequestDeleteConfirmation(std::string objectName);
    std::optional<bool> TakeDeleteConfirmationResult();
    std::optional<UnresolvedActorAction> TakeUnresolvedActorAction();
    bool WantsMouse() const;
    bool WantsKeyboard() const;
    EditorViewMode GetViewMode() const { return m_ViewMode; }
    bool IsVisible() const { return m_Visible; }
    void ToggleVisibility() { m_Visible = !m_Visible; }

private:
    enum class MaterialPreviewTarget {
        AssetBrowser,
        MaterialCreator,
        ActorDetails
    };

    // Offscreen render target plus the sphere VAO used for one preview context.
    struct PreviewTarget {
        unsigned int framebuffer = 0;
        unsigned int colorTexture = 0;
        unsigned int depthBuffer = 0;
        unsigned int vertexArray = 0;
    };

    PreviewTarget& GetPreviewTarget(MaterialPreviewTarget target) {
        return m_PreviewTargets[static_cast<std::size_t>(target)];
    }
    static bool CreatePreviewTarget(PreviewTarget& target, int size,
                                    std::string& error);
    static void DestroyPreviewTarget(PreviewTarget& target) noexcept;
    void RenderMaterialPreviewPass(const ModelMaterial& material,
                                   const PreviewTarget& previewTarget,
                                   const std::array<unsigned int, 5>& textures,
                                   bool applyOrbit);

    struct AssetThumbnail {
        unsigned int texture = 0;
        int width = 0;
        int height = 0;
        bool attempted = false;
        bool mesh = false;
        ModelPreviewMesh meshData;
    };

    void RefreshEntries();
    void NavigateTo(const std::filesystem::path& directory);
    void LoadPreview(const std::filesystem::path& file);
    AssetThumbnail& LoadThumbnail(const std::filesystem::path& file);
    bool LoadMeshPreview(const std::filesystem::path& file);
    void DrawMeshPreview(const ModelPreviewMesh& mesh, ImVec2 origin,
                         ImVec2 size);
    void DrawDeleteConfirmation();
    void DrawUnresolvedActors(
        const std::vector<UnresolvedActorView>& unresolvedActors);
    void DrawFolderDialog();
    void DrawViewModeMenu(CameraProjection cameraProjection);
    void DrawLightsMenu();
    void DrawSceneDropTarget();
    void DrawWorkLog();
    void DrawMaterialCreator();
    void DrawMaterialPreview(const ModelMaterial& material, ImVec2 center,
                             float radius, bool drawImage = true,
                             MaterialPreviewTarget target =
                                 MaterialPreviewTarget::AssetBrowser);
    bool InitializeMaterialEditorWindow(std::string& error);
    bool InitializeLandscapeWindow(std::string& error);
    void RenderMaterialEditorWindow();
    void RenderLandscapeWindow();
    void DestroyMaterialEditorWindow() noexcept;
    void DestroyLandscapeWindow() noexcept;
    ImGuiContext* GetAuxiliaryImGuiContext(GLFWwindow* window) const;
    bool InitializeMaterialPreview(std::string& error);
    static void MaterialEditorFocusCallback(GLFWwindow* window, int focused);
    static void MaterialEditorCursorEnterCallback(GLFWwindow* window,
                                                  int entered);
    static void MaterialEditorCursorPositionCallback(GLFWwindow* window,
                                                     double x, double y);
    static void MaterialEditorMouseButtonCallback(GLFWwindow* window,
                                                  int button, int action,
                                                  int mods);
    static void MaterialEditorScrollCallback(GLFWwindow* window,
                                             double xOffset,
                                             double yOffset);
    static void MaterialEditorKeyCallback(GLFWwindow* window, int key,
                                          int scanCode, int action, int mods);
    static void MaterialEditorCharacterCallback(GLFWwindow* window,
                                                unsigned int codepoint);
    void DrawActorInspector(std::string& actorName, glm::vec3& position,
                            glm::vec3& rotation, glm::vec3& scale,
                            ModelMaterial& material,
                            LightProperties* light);

    std::filesystem::path m_ProjectRoot;
    GLFWwindow* m_MainWindow = nullptr;
    GLFWwindow* m_MaterialEditorWindow = nullptr;
    GLFWwindow* m_LandscapeWindow = nullptr;
    ImGuiContext* m_MainImGuiContext = nullptr;
    ImGuiContext* m_MaterialEditorContext = nullptr;
    ImGuiContext* m_LandscapeImGuiContext = nullptr;
    std::filesystem::path m_WorkLogPath;
    std::filesystem::path m_AssetRoot;
    std::filesystem::path m_CurrentDirectory;
    std::vector<std::filesystem::path> m_Directories;
    std::vector<std::filesystem::path> m_Files;
    std::unordered_map<std::string, AssetThumbnail> m_Thumbnails;
    std::filesystem::path m_SelectedAsset;
    std::optional<std::filesystem::path> m_PendingSceneDrop;
    std::optional<EditorSceneModel> m_PendingLandscape;
    std::optional<LightType> m_PendingLightCreation;
    std::optional<std::filesystem::path> m_PendingMaterialDrop;
    std::optional<std::filesystem::path> m_ActiveSceneDrag;
    bool m_SaveRequested = false;
    bool m_ProjectionToggleRequested = false;
    std::optional<std::pair<std::filesystem::path, std::filesystem::path>>
        m_PendingImport;
    std::string m_ScanError;
    enum class FolderDialogAction {
        None,
        Create,
        Rename,
        Delete
    };
    FolderDialogAction m_FolderDialogAction = FolderDialogAction::None;
    bool m_FolderDialogNeedsOpen = false;
    std::filesystem::path m_FolderDialogTarget;
    char m_FolderNameDraft[256]{};
    std::string m_FolderDialogError;
    std::string m_PreviewError;
    std::deque<std::string> m_WorkLog;
    ModelPreviewMesh m_PreviewMesh;
    unsigned int m_PreviewTexture = 0;
    int m_PreviewWidth = 0;
    int m_PreviewHeight = 0;
    char m_SearchText[256]{};
    bool m_Initialized = false;
    bool m_Visible = true;
    float m_VisibilityAlpha = 1.0f;
    EditorViewMode m_ViewMode = EditorViewMode::Lit;
    bool m_ViewModeMenuCapturesMouse = false;
    bool m_ViewModeMenuCapturesKeyboard = false;
    bool m_ViewportToolbarVisible = false;
    glm::vec2 m_ViewportToolbarMin{0.0f};
    glm::vec2 m_ViewportToolbarMax{0.0f};
    bool m_LightsMenuCapturesMouse = false;
    bool m_LightsMenuCapturesKeyboard = false;
    bool m_DeleteConfirmationRequested = false;
    bool m_DeletePopupNeedsOpen = false;
    bool m_DeleteResultPending = false;
    bool m_DeleteConfirmed = false;
    std::string m_DeleteObjectName;
    std::optional<UnresolvedActorAction> m_UnresolvedActorAction;
    std::optional<std::size_t> m_UnresolvedRemoveTarget;
    bool m_UnresolvedRemovePopupNeedsOpen = false;
    bool m_UnresolvedPanelVisible = false;
    glm::vec2 m_UnresolvedPanelMin{0.0f};
    glm::vec2 m_UnresolvedPanelMax{0.0f};
    bool m_InspectorPanelVisible = false;
    bool m_InspectorPanelCapturesKeyboard = false;
    glm::vec2 m_InspectorPanelMin{0.0f};
    glm::vec2 m_InspectorPanelMax{0.0f};
    char m_InspectorName[256]{};
    ImFont* m_LargeDialogFont = nullptr;
    const void* m_InspectorActorIdentity = nullptr;
    float m_InspectorTransformDraft[9]{};
    int m_InspectorRotationDraft[3]{};
    float m_InspectorLightDraft[9]{};
    bool m_WorkLogOpen = false;
    bool m_WorkLogWasOpen = false;
    std::size_t m_LastRenderedLogSize = 0;
    std::string m_MaterialError;
    bool m_MaterialCreatorOpen = false;
    char m_MaterialName[128] = "New Material";
    char m_MaterialSearch[128]{};
    ModelMaterial m_MaterialDraft;
    std::filesystem::path m_EditingMaterialPath;
    std::optional<std::pair<std::filesystem::path, ModelMaterial>>
        m_UpdatedMaterial;
    unsigned int m_MaterialPreviewProgram = 0;
    // Sphere A (Material Creator) and sphere B (browser/details) are separate
    // VAOs over one shared vertex/index buffer pair.
    unsigned int m_MaterialPreviewVertexBuffer = 0;
    unsigned int m_MaterialPreviewIndexBuffer = 0;
    std::array<PreviewTarget, 3> m_PreviewTargets{};
    unsigned int m_MaterialPreviewFallbackTextures[5]{};
    int m_MaterialPreviewIndexCount = 0;
    float m_MaterialPreviewYaw = 0.0f;
    float m_MaterialPreviewPitch = 0.0f;
    bool m_MaterialPreviewDragging = false;
    bool m_MaterialPreviewDirty = true;
    double m_MaterialPreviewLastMouseX = 0.0;
    double m_MaterialPreviewLastMouseY = 0.0;
    bool m_MaterialPreviewInitialized = false;
    std::string m_MaterialPreviewError;
    bool m_LandscapeCreatorOpen = false;
    char m_LandscapeName[128] = "Landscape";
    float m_LandscapeWidth = 100.0f;
    float m_LandscapeDepth = 100.0f;
    int m_LandscapeSegmentsX = 64;
    int m_LandscapeSegmentsZ = 64;
    std::string m_LandscapeError;
};

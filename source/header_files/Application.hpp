#pragma once

#include "AssetBrowser.hpp"
#include "Camera.hpp"
#include "EditorScene.hpp"
#include "Model.hpp"
#include "Renderer.hpp"
#include "SceneRecovery.hpp"
#include "WindowMode.hpp"

#include <filesystem>
#include <memory>
#include <string>
#include <vector>

struct GLFWwindow;


class Application{
    public:
    
        explicit Application(std::filesystem::path projectRoot,
                             std::filesystem::path assetsDirectory,
                             bool startMaximized = false);
        ~Application();
    
        void Run();
        Model* SpawnModel(const std::filesystem::path& path,
                          const ModelMaterial& material,
                          std::string& error);
        bool SaveScene(std::string& error);
    
    
    private:
    
        GLFWwindow* m_Window = nullptr;
        std::filesystem::path m_ProjectRoot;
        std::filesystem::path m_AssetsDirectory;
        bool m_Initialized = false;
        bool m_SceneReadyForAutoSave = true;
        Renderer* m_Renderer = nullptr;
        AssetBrowser m_AssetBrowser;
        Camera m_Camera;
        struct SceneObject {
            std::unique_ptr<Model> model;
            EditorSceneModel source;
            std::size_t sceneOrder = 0;
        };
        bool m_CameraControlActive = false;
        bool m_CameraPanActive = false;
        bool m_MiddleMouseWasPressed = false;
        bool m_AssetBrowserToggleWasPressed = false;
        bool m_SaveWasPressed = false;
        bool m_F11WasPressed = false;
        bool m_LeftMouseWasPressed = false;
        bool m_DeleteWasPressed = false;
        bool m_TransformModeKeyWasPressed[4]{};
        TransformMode m_TransformMode = TransformMode::Move;
        Model* m_SelectedModel = nullptr;
        Model* m_DeleteTarget = nullptr;
        int m_TransformDragAxis = -1;
        glm::vec2 m_TransformDragScreenDirection{0.0f};
        float m_TransformDragPixelsPerUnit = 0.0f;
        TransformMode m_TransformDragMode = TransformMode::Move;
        glm::vec3 m_TransformDragStartPosition{0.0f};
        glm::vec3 m_TransformDragStartScale{1.0f};
        glm::vec3 m_TransformDragStartRotation{0.0f};
        glm::vec2 m_TransformDragStartCursor{0.0f};
        std::unique_ptr<Model> m_DragPreviewModel;
        std::filesystem::path m_DragPreviewPath;
        double m_PreviousMouseX = 0.0;
        double m_PreviousMouseY = 0.0;
        double m_PreviousPanMouseX = 0.0;
        double m_PreviousPanMouseY = 0.0;
        std::vector<SceneObject> m_Models;
        std::vector<UnresolvedSceneActor> m_UnresolvedModels;
        std::size_t m_NextSceneOrder = 0;

        void LoadScene();
        void HandleUnresolvedActorAction(
            const UnresolvedActorAction& action);
};
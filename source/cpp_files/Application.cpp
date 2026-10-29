#include <glad/glad.h>
#include <GLFW/glfw3.h>
#include <vector>
#include <string>
#include "Application.hpp"
#include "Renderer.hpp"

#include <iostream>
#include <cmath>
#include <algorithm>
#include <array>
#include <cctype>
#include <limits>
#include <optional>

#include "ModelAssets.hpp"

namespace {

bool LoadSceneModel(const EditorSceneModel& source,
                    const ModelMaterial& material, Model& model,
                    std::string& error) {
    if (source.proceduralLandscape) {
        return model.CreatePlane(source.landscapeWidth, source.landscapeDepth,
                                 source.landscapeSegmentsX,
                                 source.landscapeSegmentsZ, error, material);
    }
    return model.LoadFromFile(source.path, material, error);
}

bool IntersectRaySphere(const glm::vec3& origin, const glm::vec3& direction,
                        const glm::vec3& center, float radius,
                        float& distance) {
    const float directionLength = glm::length(direction);
    if (!std::isfinite(directionLength) || directionLength <= 0.000001f ||
        !std::isfinite(radius) || radius <= 0.0f) {
        return false;
    }
    const glm::vec3 rayDirection = direction / directionLength;
    const glm::vec3 originToCenter = origin - center;
    const float projection = glm::dot(originToCenter, rayDirection);
    const float discriminant =
        projection * projection -
        (glm::dot(originToCenter, originToCenter) - radius * radius);
    if (discriminant < 0.0f) {
        return false;
    }
    float hitDistance = -projection - std::sqrt(discriminant);
    if (hitDistance < 0.0f) {
        hitDistance = -projection + std::sqrt(discriminant);
    }
    if (hitDistance < 0.0f) {
        return false;
    }
    distance = hitDistance;
    return true;
}

} // namespace

Application::Application(std::filesystem::path projectRoot,
                         std::filesystem::path assetsDirectory,
                         bool startMaximized)
    : m_ProjectRoot(std::move(projectRoot)),
      m_AssetsDirectory(std::move(assetsDirectory)) {
    /* #if defined (__linux__) 
        // Na systemach Linux wymuszamy użycie X11 jako backendu GLFW.
        // Pomija to automatyczne wykrywanie Wayland/X11.
        glfwInitHint(GLFW_PLATFORM, GLFW_PLATFORM_X11);
    #endif */
    
    if (!glfwInit()){
        std::cout << "GLFW initialization failed\n";
        return;
    }

    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 4);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 6);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
    

    std::cout << "GLFW initialized successfully\n";
   

    // Tworzymy okno aplikacji o wymiarach 1280x720.
    // nullptr jako monitor oznacza zwykłe okno zamiast trybu fullscreen.
    // nullptr jako share oznacza brak współdzielenia kontekstu OpenGL z innym oknem.
    m_Window = glfwCreateWindow(
        1680,
        1080,
        "Aquarius Engine",
        nullptr, //glfwGetPrimaryMonitor() jeżeli będę chciał fullscreen
        nullptr
    );
    
    // Sprawdzamy, czy GLFW poprawnie utworzył obiekt okna.
    // nullptr oznacza, że alokacja lub inicjalizacja okna zakończyła się błędem.
    if (m_Window == nullptr){
        std::cout << "Error creating window\n";
        glfwTerminate();
        return;
    }
    if (startMaximized) {
        SetMaximized(m_Window, true);
    }
    
    glfwMakeContextCurrent(m_Window);
    

    if (!gladLoadGLLoader((GLADloadproc)glfwGetProcAddress)){
        std::cout << "Failed to initialize GLAD\n";
        return;
    }

    std::cout << "GLAD initialized successfully\n";
    std::cout<<"==========Aquarius Engine (powered by OpenGL)==========\n";
    std::cout<< "OpenGL Vendor: "<< glGetString(GL_VENDOR)<< "\n";
    std::cout<< "GPU Renderer: "<< glGetString(GL_RENDERER)<< "\n";
    std::cout<< "OpenGL Version: "<< glGetString(GL_VERSION)<< "\n";
    std::cout<<"=======================================================\n";
    try {
        m_Renderer = new Renderer();
    } catch (const std::exception& exception) {
        std::cerr << "Editor renderer initialization failed: "
                  << exception.what() << '\n';
        return;
    }
    std::string assetBrowserError;
    if (!m_AssetBrowser.Initialize(m_Window, m_ProjectRoot,
                                   m_AssetsDirectory, assetBrowserError)) {
        std::cerr << "Asset browser initialization failed: "
                  << assetBrowserError << '\n';
        return;
    }
    m_Initialized = true;
    LoadScene();

}


Application::~Application(){
    if (m_Initialized && m_SceneReadyForAutoSave) {
        std::string error;
        if (!SaveScene(error)) {
            m_AssetBrowser.SetStatusMessage("Save on close failed: " + error);
            std::cerr << "Could not save editor scene: " << error << '\n';
        } else {
            m_AssetBrowser.SetStatusMessage("Project saved on close.");
        }
    }
    m_AssetBrowser.Shutdown();
    m_Models.clear();
    if (m_Renderer){
        delete m_Renderer;
        m_Renderer = nullptr;
    }

    if (m_Window){
        glfwSetInputMode(m_Window, GLFW_CURSOR, GLFW_CURSOR_NORMAL);
        glfwDestroyWindow(m_Window);
        m_Window = nullptr;
    }

    glfwTerminate();
}


void Application::Run(){
    if (!m_Initialized){
        return;
    }
    
    double previousFrameTime = glfwGetTime();
    while (!glfwWindowShouldClose(m_Window)) {
        glfwPollEvents();
        const bool f11Pressed =
            glfwGetKey(m_Window, GLFW_KEY_F11) == GLFW_PRESS;
        if (f11Pressed && !m_F11WasPressed) {
            ToggleMaximized(m_Window);
        }
        m_F11WasPressed = f11Pressed;

        const double currentFrameTime = glfwGetTime();
        const float deltaTime = static_cast<float>(
            std::clamp(currentFrameTime - previousFrameTime, 0.0, 0.1));
        previousFrameTime = currentFrameTime;

        const bool altPressed =
            glfwGetKey(m_Window, GLFW_KEY_LEFT_ALT) == GLFW_PRESS ||
            glfwGetKey(m_Window, GLFW_KEY_RIGHT_ALT) == GLFW_PRESS;
        const bool browserTogglePressed =
            altPressed && glfwGetKey(m_Window, GLFW_KEY_SPACE) == GLFW_PRESS;
        if (browserTogglePressed && !m_AssetBrowserToggleWasPressed) {
            m_AssetBrowser.ToggleVisibility();
        }
        m_AssetBrowserToggleWasPressed = browserTogglePressed;
        const bool savePressed =
            (glfwGetKey(m_Window, GLFW_KEY_LEFT_CONTROL) == GLFW_PRESS ||
             glfwGetKey(m_Window, GLFW_KEY_RIGHT_CONTROL) == GLFW_PRESS) &&
            glfwGetKey(m_Window, GLFW_KEY_S) == GLFW_PRESS;
        if (savePressed && !m_SaveWasPressed) {
            std::string error;
            if (SaveScene(error)) {
                m_AssetBrowser.SetStatusMessage("Project saved.");
            } else {
                m_AssetBrowser.SetStatusMessage("Save failed: " + error);
                std::cerr << "Could not save editor scene: " << error << '\n';
            }
        }
        m_SaveWasPressed = savePressed;

        const bool deletePressed =
            glfwGetKey(m_Window, GLFW_KEY_DELETE) == GLFW_PRESS;
        if (deletePressed && !m_DeleteWasPressed &&
            m_SelectedModel != nullptr && !m_CameraControlActive &&
            !m_AssetBrowser.WantsKeyboard()) {
            m_DeleteTarget = m_SelectedModel;
            const auto selectedObject = std::find_if(
                m_Models.begin(), m_Models.end(),
                [this](const SceneObject& object) {
                    return object.model.get() == m_DeleteTarget;
                });
            if (selectedObject != m_Models.end()) {
                m_AssetBrowser.RequestDeleteConfirmation(
                    selectedObject->source.name);
            }
        }
        m_DeleteWasPressed = deletePressed;

        const auto findSelectedSceneObject = [this]() -> const SceneObject* {
            const auto selected = std::find_if(
                m_Models.begin(), m_Models.end(),
                [this](const SceneObject& object) {
                    return object.model.get() == m_SelectedModel;
                });
            return selected == m_Models.end() ? nullptr : &*selected;
        };
        const auto selectedObjectCanRotate = [&]() {
            const SceneObject* selected = findSelectedSceneObject();
            return !selected || !selected->source.light ||
                   IsLightRotatable(selected->source.light->type);
        };
        const auto selectedObjectCanScale = [&]() {
            const SceneObject* selected = findSelectedSceneObject();
            return !selected || !selected->source.light;
        };

        const bool rightMouseButtonPressed =
            glfwGetMouseButton(m_Window, GLFW_MOUSE_BUTTON_RIGHT) == GLFW_PRESS;
        const bool rightMouseHeld =
            rightMouseButtonPressed && !m_AssetBrowser.WantsMouse();
        if (rightMouseHeld != m_CameraControlActive) {
            m_CameraControlActive = rightMouseHeld;
            glfwSetInputMode(m_Window, GLFW_CURSOR,
                             rightMouseHeld ? GLFW_CURSOR_DISABLED
                                            : GLFW_CURSOR_NORMAL);
            if (rightMouseHeld) {
                glfwGetCursorPos(m_Window, &m_PreviousMouseX, &m_PreviousMouseY);
            }
        }

        const std::array<int, 4> transformKeys{
            GLFW_KEY_Q, GLFW_KEY_W, GLFW_KEY_E, GLFW_KEY_R};
        if (m_SelectedModel && !m_CameraControlActive &&
            !m_AssetBrowser.WantsKeyboard()) {
            for (std::size_t i = 0; i < transformKeys.size(); ++i) {
                const bool pressed =
                    glfwGetKey(m_Window, transformKeys[i]) == GLFW_PRESS;
                if (pressed && !m_TransformModeKeyWasPressed[i]) {
                    const TransformMode requestedMode =
                        static_cast<TransformMode>(i);
                    if ((requestedMode != TransformMode::Rotate ||
                         selectedObjectCanRotate()) &&
                        (requestedMode != TransformMode::Scale ||
                         selectedObjectCanScale())) {
                        m_TransformMode = requestedMode;
                    }
                }
                m_TransformModeKeyWasPressed[i] = pressed;
            }
        } else {
            for (std::size_t i = 0; i < transformKeys.size(); ++i) {
                m_TransformModeKeyWasPressed[i] =
                    glfwGetKey(m_Window, transformKeys[i]) == GLFW_PRESS;
            }
        }

        glm::vec3 movement(0.0f);
        if (m_CameraControlActive) {
            double mouseX = 0.0;
            double mouseY = 0.0;
            glfwGetCursorPos(m_Window, &mouseX, &mouseY);
            m_Camera.Rotate(static_cast<float>(mouseX - m_PreviousMouseX),
                            static_cast<float>(m_PreviousMouseY - mouseY));
            m_PreviousMouseX = mouseX;
            m_PreviousMouseY = mouseY;

            if (glfwGetKey(m_Window, GLFW_KEY_W) == GLFW_PRESS) movement.z += 1.0f;
            if (glfwGetKey(m_Window, GLFW_KEY_S) == GLFW_PRESS) movement.z -= 1.0f;
            if (glfwGetKey(m_Window, GLFW_KEY_A) == GLFW_PRESS) movement.x -= 1.0f;
            if (glfwGetKey(m_Window, GLFW_KEY_D) == GLFW_PRESS) movement.x += 1.0f;
            if (glfwGetKey(m_Window, GLFW_KEY_Q) == GLFW_PRESS) movement.y += 1.0f;
            if (glfwGetKey(m_Window, GLFW_KEY_E) == GLFW_PRESS) movement.y -= 1.0f;
            const bool speedBoost =
                glfwGetKey(m_Window, GLFW_KEY_LEFT_SHIFT) == GLFW_PRESS;
            m_Camera.Move(movement, deltaTime, speedBoost);
        }

            const bool leftMousePressed =
                glfwGetMouseButton(m_Window, GLFW_MOUSE_BUTTON_LEFT) == GLFW_PRESS;
            const bool middleMousePressed =
                glfwGetMouseButton(m_Window, GLFW_MOUSE_BUTTON_MIDDLE) == GLFW_PRESS;
            double cursorX = 0.0;
            double cursorY = 0.0;
            int windowWidth = 0;
            int windowHeight = 0;
            glfwGetCursorPos(m_Window, &cursorX, &cursorY);
            glfwGetWindowSize(m_Window, &windowWidth, &windowHeight);
            const bool mainWindowFocused =
                glfwGetWindowAttrib(m_Window, GLFW_FOCUSED) == GLFW_TRUE;
            const bool cursorInsideWindow =
                windowWidth > 0 && windowHeight > 0 && cursorX >= 0.0 &&
                cursorY >= 0.0 && cursorX < static_cast<double>(windowWidth) &&
                cursorY < static_cast<double>(windowHeight);
            const bool middleMouseJustPressed =
                middleMousePressed && !m_MiddleMouseWasPressed;
            if (!middleMousePressed || !mainWindowFocused || windowWidth <= 0 ||
                windowHeight <= 0) {
                m_CameraPanActive = false;
            } else if (middleMouseJustPressed) {
                m_CameraPanActive =
                    cursorInsideWindow && !leftMousePressed &&
                    !rightMouseButtonPressed && !m_CameraControlActive &&
                    !m_AssetBrowser.WantsMouse();
                if (m_CameraPanActive) {
                    m_PreviousPanMouseX = cursorX;
                    m_PreviousPanMouseY = cursorY;
                }
            }
            if (m_CameraPanActive &&
                (leftMousePressed || rightMouseButtonPressed)) {
                m_CameraPanActive = false;
            }
            if (m_CameraPanActive) {
                m_Camera.Pan(
                    static_cast<float>(cursorX - m_PreviousPanMouseX),
                    static_cast<float>(cursorY - m_PreviousPanMouseY),
                    static_cast<float>(windowHeight));
                m_PreviousPanMouseX = cursorX;
                m_PreviousPanMouseY = cursorY;
            }
            m_MiddleMouseWasPressed = middleMousePressed;

            std::vector<RenderLight> renderLights;
            renderLights.reserve(m_Models.size());
        for (const SceneObject& object : m_Models) {
            if (object.source.light) {
                renderLights.push_back(BuildRenderLight(
                    *object.source.light, object.model->position,
                    object.model->rotation));
            }
        }
        m_Renderer->BeginFrame(m_Camera, renderLights);
        if (leftMousePressed && !m_LeftMouseWasPressed &&
            !m_CameraControlActive && !m_AssetBrowser.WantsMouse()) {
            int hitAxis = -1;
            glm::vec2 axisScreenDirection;
            float pixelsPerUnit = 0.0f;
            if (m_SelectedModel &&
                m_TransformMode != TransformMode::Select &&
                (m_TransformMode != TransformMode::Rotate ||
                 selectedObjectCanRotate()) &&
            (m_TransformMode != TransformMode::Scale ||
             selectedObjectCanScale()) &&
            m_Renderer->HitTransformGizmo(
                    m_SelectedModel->position, m_TransformMode, cursorX,
                    cursorY, windowWidth, windowHeight, hitAxis,
                    axisScreenDirection, pixelsPerUnit)) {
                m_TransformDragAxis = hitAxis;
                m_TransformDragMode = m_TransformMode;
                m_TransformDragScreenDirection = axisScreenDirection;
                m_TransformDragPixelsPerUnit = pixelsPerUnit;
                m_TransformDragStartPosition = m_SelectedModel->position;
                m_TransformDragStartScale = m_SelectedModel->scale;
                m_TransformDragStartRotation = m_SelectedModel->rotation;
                m_TransformDragStartCursor =
                    glm::vec2(static_cast<float>(cursorX),
                              static_cast<float>(cursorY));
            } else {
                m_TransformDragAxis = -1;
                glm::vec3 rayOrigin;
                glm::vec3 rayDirection;
                m_Renderer->MakeScreenRay(cursorX, cursorY, windowWidth,
                                          windowHeight, rayOrigin,
                                          rayDirection);
                Model* closestModel = nullptr;
                float closestDistance = std::numeric_limits<float>::max();
                for (const SceneObject& object : m_Models) {
                    float hitDistance = 0.0f;
                    bool hit = false;
                    if (object.source.light) {
                        hit = IntersectRaySphere(
                            rayOrigin, rayDirection, object.model->position,
                            0.7f, hitDistance);
                    } else {
                        hit = object.model->IntersectRay(
                            rayOrigin, rayDirection, hitDistance);
                    }
                    if (hit &&
                        hitDistance < closestDistance) {
                        closestDistance = hitDistance;
                        closestModel = object.model.get();
                    }
                }
                m_SelectedModel = closestModel;
                if (!selectedObjectCanRotate() ||
                    !selectedObjectCanScale()) {
                    m_TransformMode = TransformMode::Move;
                }
            }
        }
        if (leftMousePressed && m_TransformDragAxis >= 0 &&
            m_SelectedModel && m_TransformDragPixelsPerUnit > 0.0f) {
            const glm::vec2 currentCursor(static_cast<float>(cursorX),
                                          static_cast<float>(cursorY));
            const float pixelMovement = glm::dot(
                currentCursor - m_TransformDragStartCursor,
                m_TransformDragScreenDirection);
            const float worldMovement =
                pixelMovement / m_TransformDragPixelsPerUnit;
            if (m_TransformDragMode == TransformMode::Move) {
                const glm::vec3 axis =
                    m_TransformDragAxis == 0
                        ? glm::vec3(1.0f, 0.0f, 0.0f)
                        : m_TransformDragAxis == 1
                              ? glm::vec3(0.0f, 1.0f, 0.0f)
                              : glm::vec3(0.0f, 0.0f, 1.0f);
                m_SelectedModel->position =
                    m_TransformDragStartPosition + axis * worldMovement;
            } else if (m_TransformDragMode == TransformMode::Scale) {
                const float scaleValue =
                    std::max(0.01f, worldMovement + 1.0f);
                if (m_TransformDragAxis == 0) {
                    m_SelectedModel->scale.x =
                        m_TransformDragStartScale.x * scaleValue;
                } else if (m_TransformDragAxis == 1) {
                    m_SelectedModel->scale.y =
                        m_TransformDragStartScale.y * scaleValue;
                } else {
                    m_SelectedModel->scale.z =
                        m_TransformDragStartScale.z * scaleValue;
                }
            } else if (m_TransformDragMode == TransformMode::Rotate) {
                if (m_TransformDragAxis == 0) {
                    m_SelectedModel->rotation.x =
                        m_TransformDragStartRotation.x + worldMovement;
                } else if (m_TransformDragAxis == 1) {
                    m_SelectedModel->rotation.y =
                        m_TransformDragStartRotation.y + worldMovement;
                } else {
                    m_SelectedModel->rotation.z =
                        m_TransformDragStartRotation.z + worldMovement;
                }
            }
        }
        if (!leftMousePressed) {
            if (m_TransformDragAxis >= 0 && m_SelectedModel &&
                (glm::length(m_SelectedModel->position -
                             m_TransformDragStartPosition) > 0.0001f ||
                 glm::length(m_SelectedModel->scale -
                             m_TransformDragStartScale) > 0.0001f ||
                 glm::length(m_SelectedModel->rotation -
                             m_TransformDragStartRotation) > 0.0001f)) {
                const auto movedObject = std::find_if(
                    m_Models.begin(), m_Models.end(),
                    [this](const SceneObject& object) {
                        return object.model.get() == m_SelectedModel;
                    });
                if (movedObject != m_Models.end()) {
                    m_AssetBrowser.SetStatusMessage(
                        (m_TransformDragMode == TransformMode::Move
                             ? "Moved "
                             : m_TransformDragMode == TransformMode::Scale
                                   ? "Scaled "
                                   : "Rotated ") +
                        movedObject->source.name +
                        " along the " +
                        (m_TransformDragAxis == 0
                             ? std::string("X")
                             : m_TransformDragAxis == 1
                                   ? std::string("Y")
                                   : std::string("Z")) +
                        " axis.");
                }
            }
            m_TransformDragAxis = -1;
        }
        m_LeftMouseWasPressed = leftMousePressed;
        std::vector<const Model*> shadowCasters;
        shadowCasters.reserve(m_Models.size());
        for (const SceneObject& object : m_Models) {
            if (!object.source.light) {
                shadowCasters.push_back(object.model.get());
            }
        }
        m_Renderer->RenderShadowMap(shadowCasters);
        m_Renderer->DrawGroundGrid();
        for (const SceneObject& object : m_Models) {
            if (!object.source.light) {
                m_Renderer->DrawModel(*object.model,
                                      object.model.get() == m_SelectedModel);
            }
        }
        if (const SceneObject* selected = findSelectedSceneObject();
            selected && selected->source.light) {
            m_Renderer->DrawLightVisualization(BuildRenderLight(
                *selected->source.light, selected->model->position,
                selected->model->rotation));
        }
        for (const SceneObject& object : m_Models) {
            if (object.source.light) {
                m_Renderer->DrawLightIcon(
                    object.source.light->type, object.model->position,
                    object.model.get() == m_SelectedModel);
            }
        }
        if (m_SelectedModel && m_TransformMode != TransformMode::Select &&
            (m_TransformMode != TransformMode::Rotate ||
             selectedObjectCanRotate()) &&
            (m_TransformMode != TransformMode::Scale ||
             selectedObjectCanScale()) &&
            m_Renderer->GetViewMode() != EditorViewMode::Wire) {
            m_Renderer->DrawTransformGizmo(m_SelectedModel->position,
                                           m_TransformMode);
        }
        const bool draggingMesh =
            glfwGetMouseButton(m_Window, GLFW_MOUSE_BUTTON_LEFT) == GLFW_PRESS;
        const auto activeDrag = m_AssetBrowser.GetActiveSceneDrag();
        if (activeDrag && m_DragPreviewPath != *activeDrag) {
            m_DragPreviewPath = *activeDrag;
            auto preview = std::make_unique<Model>();
            std::string error;
            ModelMaterial material;
            if (!preview->LoadFromFile(*activeDrag, material, error)) {
                m_DragPreviewModel.reset();
                m_AssetBrowser.SetPreviewError(
                    "Could not preview dragged mesh: " + error);
            } else {
                m_DragPreviewModel = std::move(preview);
            }
        } else if (!activeDrag) {
            m_DragPreviewModel.reset();
            m_DragPreviewPath.clear();
        }
        if (draggingMesh && m_DragPreviewModel) {
            double cursorX = 0.0;
            double cursorY = 0.0;
            int windowWidth = 0;
            int windowHeight = 0;
            glfwGetCursorPos(m_Window, &cursorX, &cursorY);
            glfwGetWindowSize(m_Window, &windowWidth, &windowHeight);
            const float browserHeight =
                m_AssetBrowser.IsVisible() ? 250.0f : 0.0f;
            if (cursorY < static_cast<double>(windowHeight) - browserHeight) {
                glm::vec3 rayOrigin;
                glm::vec3 rayDirection;
                m_Renderer->MakeScreenRay(cursorX, cursorY, windowWidth,
                                          windowHeight, rayOrigin,
                                          rayDirection);
                const float groundIntersection =
                    std::abs(rayDirection.y) > 0.00001f
                        ? -rayOrigin.y / rayDirection.y
                        : -1.0f;
                const glm::vec3 placement =
                    groundIntersection > 0.0f
                        ? rayOrigin + rayDirection * groundIntersection
                        : rayOrigin + rayDirection * 5.0f;
                m_DragPreviewModel->position = placement;
                m_Renderer->DrawModel(*m_DragPreviewModel, true);
            }
        }
        std::vector<UnresolvedActorView> unresolvedActorViews;
        unresolvedActorViews.reserve(m_UnresolvedModels.size());
        for (const UnresolvedSceneActor& unresolved : m_UnresolvedModels) {
            unresolvedActorViews.push_back(
                {unresolved.sceneOrder, unresolved.source.name,
                 unresolved.source.path, unresolved.error,
                 unresolved.source.proceduralLandscape});
        }
        const auto selectedActor = std::find_if(
            m_Models.begin(), m_Models.end(),
            [this](const SceneObject& object) {
                return object.model.get() == m_SelectedModel;
            });
        if (selectedActor != m_Models.end()) {
            const ModelMaterial originalMaterial =
                selectedActor->source.material;
            m_AssetBrowser.Render(
                selectedActor->model.get(), &selectedActor->source.name,
                &selectedActor->model->position,
                &selectedActor->model->rotation, &selectedActor->model->scale,
                &selectedActor->source.material,
                selectedActor->source.light
                    ? &*selectedActor->source.light
                    : nullptr,
                m_Camera.GetProjection(),
                unresolvedActorViews);
            const ModelMaterial& editedMaterial =
                selectedActor->source.material;
            const bool materialChanged =
                originalMaterial.name != editedMaterial.name ||
                originalMaterial.asset != editedMaterial.asset ||
                originalMaterial.diffuse != editedMaterial.diffuse ||
                originalMaterial.normal != editedMaterial.normal ||
                originalMaterial.roughness != editedMaterial.roughness ||
                originalMaterial.metallic != editedMaterial.metallic ||
                originalMaterial.ambientOcclusion !=
                    editedMaterial.ambientOcclusion ||
                originalMaterial.height != editedMaterial.height ||
                originalMaterial.heightScale != editedMaterial.heightScale ||
                originalMaterial.twoSided != editedMaterial.twoSided ||
                originalMaterial.textureScale != editedMaterial.textureScale;
            if (materialChanged && !selectedActor->source.light) {
                auto updatedModel = std::make_unique<Model>();
                std::string error;
                const bool loaded = LoadSceneModel(
                    selectedActor->source, editedMaterial, *updatedModel,
                    error);
                if (!loaded) {
                    selectedActor->source.material = originalMaterial;
                    m_AssetBrowser.SetPreviewError(
                        "Could not apply material changes: " + error);
                    std::cerr << "Could not apply material changes: " << error
                              << '\n';
                } else {
                    updatedModel->position = selectedActor->model->position;
                    updatedModel->rotation = selectedActor->model->rotation;
                    updatedModel->scale = selectedActor->model->scale;
                    selectedActor->model = std::move(updatedModel);
                    m_SelectedModel = selectedActor->model.get();
                }
            }
        } else {
            m_AssetBrowser.Render(nullptr, nullptr, nullptr, nullptr, nullptr,
                                 nullptr, nullptr, m_Camera.GetProjection(),
                                 unresolvedActorViews);
        }
        if (m_AssetBrowser.TakeProjectionToggleRequest()) {
            m_Camera.ToggleProjection();
        }
        if (const auto pendingLight =
                m_AssetBrowser.TakePendingLightCreation()) {
            const std::size_t lightCount = static_cast<std::size_t>(
                std::count_if(
                    m_Models.begin(), m_Models.end(),
                    [](const SceneObject& object) {
                        return object.source.light.has_value();
                    }));
            if (lightCount >= MaxSceneLights) {
                m_AssetBrowser.SetStatusMessage(
                    "The scene already contains the maximum of " +
                    std::to_string(MaxSceneLights) + " lights.");
            } else {
                EditorSceneModel source = MakeEditorSceneLight(*pendingLight);
                auto lightObject = std::make_unique<Model>();
                lightObject->position = source.position;
                lightObject->rotation = source.rotation;
                lightObject->scale = source.scale;
                Model* newLight = lightObject.get();
                m_Models.push_back(
                    {std::move(lightObject), std::move(source),
                     m_NextSceneOrder++});
                m_SelectedModel = newLight;
                m_TransformMode = TransformMode::Move;
                m_AssetBrowser.SetStatusMessage(
                    "Added " + std::string(LightTypeName(*pendingLight)) +
                    " to the scene.");
            }
        }
        m_Renderer->SetViewMode(m_AssetBrowser.GetViewMode());
        if (const auto unresolvedAction =
                m_AssetBrowser.TakeUnresolvedActorAction()) {
            HandleUnresolvedActorAction(*unresolvedAction);
        }
        if (const auto updatedMaterial =
                m_AssetBrowser.TakeUpdatedMaterial()) {
            std::error_code pathError;
            const std::filesystem::path editedPath =
                std::filesystem::weakly_canonical(updatedMaterial->first,
                                                  pathError);
            if (pathError) {
                m_AssetBrowser.SetPreviewError(
                    "Could not resolve edited material asset: " +
                    pathError.message());
            } else {
                for (SceneObject& object : m_Models) {
                    if (object.source.material.asset.empty()) {
                        continue;
                    }
                    std::error_code actorPathError;
                    const std::filesystem::path assignedPath =
                        std::filesystem::weakly_canonical(
                            object.source.material.asset, actorPathError);
                    if (actorPathError || assignedPath != editedPath) {
                        continue;
                    }
                    std::unique_ptr<Model> updatedModel =
                        std::make_unique<Model>();
                    std::string error;
                    const bool loaded = LoadSceneModel(
                        object.source, updatedMaterial->second, *updatedModel,
                        error);
                    if (!loaded) {
                        m_AssetBrowser.SetPreviewError(
                            "Could not refresh actor after material edit: " +
                            error);
                        std::cerr
                            << "Could not refresh actor after material edit: "
                            << error << '\n';
                        continue;
                    }
                    const bool wasSelected =
                        object.model.get() == m_SelectedModel;
                    updatedModel->position = object.model->position;
                    updatedModel->rotation = object.model->rotation;
                    updatedModel->scale = object.model->scale;
                    object.model = std::move(updatedModel);
                    object.source.material = updatedMaterial->second;
                    if (wasSelected) {
                        m_SelectedModel = object.model.get();
                    }
                }
            }
        }
        if (const std::optional<bool> deleteConfirmed =
                m_AssetBrowser.TakeDeleteConfirmationResult()) {
            if (*deleteConfirmed && m_DeleteTarget != nullptr) {
                const auto deletedObject = std::find_if(
                    m_Models.begin(), m_Models.end(),
                    [this](const SceneObject& object) {
                        return object.model.get() == m_DeleteTarget;
                    });
                if (deletedObject != m_Models.end()) {
                    m_AssetBrowser.SetStatusMessage(
                        "Deleted " + deletedObject->source.name +
                        " from the scene.");
                    m_Models.erase(deletedObject);
                }
                if (m_SelectedModel == m_DeleteTarget) {
                    m_SelectedModel = nullptr;
                }
            }
            m_DeleteTarget = nullptr;
        }
        if (m_AssetBrowser.TakeSaveRequest()) {
            std::string error;
            if (SaveScene(error)) {
                m_AssetBrowser.SetStatusMessage("Project saved.");
            } else {
                m_AssetBrowser.SetStatusMessage("Save failed: " + error);
                std::cerr << "Could not save editor scene: " << error << '\n';
            }
        }
        if (const auto import = m_AssetBrowser.TakePendingImport()) {
            std::string error;
            if (!ConvertModelToGlb(import->first, import->second, error)) {
                m_AssetBrowser.CompleteModelImport({}, std::move(error));
            } else {
                m_AssetBrowser.CompleteModelImport(import->second, {});
            }
        }
        if (const auto droppedMesh =
                m_AssetBrowser.TakePendingSceneDrop()) {
            ModelMaterial material;
            std::string error;
            Model* model = SpawnModel(*droppedMesh, material, error);
            if (!model) {
                m_AssetBrowser.SetPreviewError(
                    "Could not add dropped mesh to scene: " + error);
                std::cerr << "Could not add dropped mesh to scene: " << error
                          << '\n';
            } else {
                double cursorX = 0.0;
                double cursorY = 0.0;
                int windowWidth = 0;
                int windowHeight = 0;
                glfwGetCursorPos(m_Window, &cursorX, &cursorY);
                glfwGetWindowSize(m_Window, &windowWidth, &windowHeight);

                glm::vec3 rayOrigin;
                glm::vec3 rayDirection;
                m_Renderer->MakeScreenRay(cursorX, cursorY, windowWidth,
                                          windowHeight, rayOrigin,
                                          rayDirection);
                float groundIntersection = -1.0f;
                if (std::abs(rayDirection.y) > 0.00001f) {
                    groundIntersection = -rayOrigin.y / rayDirection.y;
                }
                model->position =
                    groundIntersection > 0.0f
                        ? rayOrigin + rayDirection * groundIntersection
                        : rayOrigin + rayDirection * 5.0f;
                m_SelectedModel = model;
                m_AssetBrowser.SetStatusMessage(
                    "Added " + droppedMesh->filename().string() +
                    " to the scene.");
            }
        }
        if (const auto landscape =
                m_AssetBrowser.TakePendingLandscape()) {
            auto model = std::make_unique<Model>();
            std::string error;
            if (!model->CreatePlane(
                    landscape->landscapeWidth, landscape->landscapeDepth,
                    landscape->landscapeSegmentsX,
                    landscape->landscapeSegmentsZ, error,
                    landscape->material)) {
                m_AssetBrowser.SetPreviewError(
                    "Could not create landscape plane: " + error);
                std::cerr << "Could not create landscape plane: " << error
                          << '\n';
            } else {
                double cursorX = 0.0;
                double cursorY = 0.0;
                int windowWidth = 0;
                int windowHeight = 0;
                glfwGetCursorPos(m_Window, &cursorX, &cursorY);
                glfwGetWindowSize(m_Window, &windowWidth, &windowHeight);
                glm::vec3 rayOrigin;
                glm::vec3 rayDirection;
                m_Renderer->MakeScreenRay(cursorX, cursorY, windowWidth,
                                          windowHeight, rayOrigin,
                                          rayDirection);
                if (std::abs(rayDirection.y) > 0.00001f) {
                    const float distance = -rayOrigin.y / rayDirection.y;
                    if (distance > 0.0f) {
                        model->position = rayOrigin + rayDirection * distance;
                    }
                }
                Model* spawnedModel = model.get();
                m_Models.push_back(
                    {std::move(model), *landscape, m_NextSceneOrder++});
                m_SelectedModel = spawnedModel;
            }
        }
        if (const auto droppedMaterial =
                m_AssetBrowser.TakePendingMaterialDrop()) {
            std::string error;
            ModelMaterial material;
            if (!LoadAqenMaterial(*droppedMaterial, material, error)) {
                m_AssetBrowser.SetPreviewError(
                    "Could not load dropped material: " + error);
                std::cerr << "Could not load dropped material: " << error
                          << '\n';
            } else {
                double cursorX = 0.0;
                double cursorY = 0.0;
                int windowWidth = 0;
                int windowHeight = 0;
                glfwGetCursorPos(m_Window, &cursorX, &cursorY);
                glfwGetWindowSize(m_Window, &windowWidth, &windowHeight);
                glm::vec3 rayOrigin;
                glm::vec3 rayDirection;
                m_Renderer->MakeScreenRay(cursorX, cursorY, windowWidth,
                                          windowHeight, rayOrigin,
                                          rayDirection);
                SceneObject* target = nullptr;
                float closestDistance = std::numeric_limits<float>::max();
                for (SceneObject& object : m_Models) {
                    float distance = 0.0f;
                    if (object.model->IntersectRay(rayOrigin, rayDirection,
                                                   distance) &&
                        distance >= 0.0f && distance < closestDistance) {
                        target = &object;
                        closestDistance = distance;
                    }
                }
                if (!target) {
                    m_AssetBrowser.SetPreviewError(
                        "Drop the material onto a visible model in the scene.");
                } else {
                    std::unique_ptr<Model> updatedModel =
                        std::make_unique<Model>();
                    const bool loaded = LoadSceneModel(
                        target->source, material, *updatedModel, error);
                    if (!loaded) {
                        m_AssetBrowser.SetPreviewError(
                            "Could not apply dropped material: " + error);
                        std::cerr << "Could not apply dropped material: "
                                  << error << '\n';
                    } else {
                        updatedModel->position = target->model->position;
                        updatedModel->rotation = target->model->rotation;
                        updatedModel->scale = target->model->scale;
                        target->model = std::move(updatedModel);
                        target->source.material = std::move(material);
                        m_SelectedModel = target->model.get();
                        m_AssetBrowser.SetStatusMessage(
                            "Applied material '" +
                            target->source.material.name + "' to " +
                            target->source.name + ".");
                    }
                }
            }
        }
        glfwSwapBuffers(m_Window);
        
    }
}

Model* Application::SpawnModel(const std::filesystem::path& path,
                               const ModelMaterial& material,
                               std::string& error) {
    if (!m_Initialized) {
        error = "Cannot spawn a model before OpenGL is initialized.";
        return nullptr;
    }
    std::error_code pathError;
    const std::filesystem::path absolutePath =
        std::filesystem::absolute(path, pathError).lexically_normal();
    if (pathError) {
        error = "Could not resolve model path: " + pathError.message();
        return nullptr;
    }
    ModelMaterial resolvedMaterial = material;
    std::string materialWarning;
    const auto appendMaterialWarning = [&](const std::string& warning) {
        if (!materialWarning.empty()) {
            materialWarning += " ";
        }
        materialWarning += warning;
    };
    if (!material.asset.empty()) {
        if (!LoadAqenMaterial(material.asset, resolvedMaterial, error)) {
            if (!IsMissingMaterialTextureError(error)) {
                return nullptr;
            }
            appendMaterialWarning(
                "The material asset references a missing texture; saved map "
                "paths and renderer defaults are being used.");
            resolvedMaterial = material;
            error.clear();
        }
    }
    const std::array<std::filesystem::path*, 6> materialMaps{
        &resolvedMaterial.diffuse, &resolvedMaterial.normal,
        &resolvedMaterial.roughness, &resolvedMaterial.metallic,
        &resolvedMaterial.ambientOcclusion, &resolvedMaterial.height};
    const std::array<const char*, 6> materialMapNames{
        "diffuse", "normal", "roughness", "metallic",
        "ambient occlusion", "height"};
    for (std::size_t i = 0; i < materialMaps.size(); ++i) {
        if (materialMaps[i]->empty()) {
            continue;
        }
        if (materialMaps[i]->is_relative()) {
            *materialMaps[i] =
                std::filesystem::absolute(*materialMaps[i], pathError)
                    .lexically_normal();
            if (pathError) {
                error = "Could not resolve " +
                        std::string(materialMapNames[i]) +
                        " texture path: " + pathError.message();
                return nullptr;
            }
        }
    }
    for (std::size_t i = 0; i < materialMaps.size(); ++i) {
        std::error_code textureError;
        if (!materialMaps[i]->empty() &&
            !std::filesystem::is_regular_file(*materialMaps[i], textureError)) {
            if (textureError) {
                error = "Could not inspect " +
                        std::string(materialMapNames[i]) + " texture '" +
                        materialMaps[i]->string() +
                        "': " + textureError.message();
                return nullptr;
            }
            appendMaterialWarning(
                "Missing " + std::string(materialMapNames[i]) +
                " texture '" + materialMaps[i]->string() +
                "'; the renderer's default map is being used.");
            materialMaps[i]->clear();
        }
    }

    EditorSceneModel source;
    source.path = absolutePath;
    source.name = absolutePath.stem().string();
    source.material = resolvedMaterial;
    if (pathError) {
        error = "Could not resolve material path: " + pathError.message();
        return nullptr;
    }

    auto model = std::make_unique<Model>();
    const bool loaded =
        model->LoadFromFile(absolutePath, resolvedMaterial, error);
    if (!loaded) {
        return nullptr;
    }
    Model* spawnedModel = model.get();
    m_Models.push_back(
        {std::move(model), std::move(source), m_NextSceneOrder++});
    if (!materialWarning.empty()) {
        m_AssetBrowser.SetStatusMessage(materialWarning);
        std::cerr << "Model loaded with material fallbacks: "
                  << materialWarning << '\n';
    }
    return spawnedModel;
}

void Application::LoadScene() {
    EditorScene scene;
    std::string error;
    if (!LoadEditorScene(m_ProjectRoot, scene, error)) {
        m_SceneReadyForAutoSave = false;
        std::cerr << "Could not load editor scene: " << error << '\n';
        m_AssetBrowser.SetStatusMessage("Scene load failed: " + error);
        return;
    }

    for (const std::string& warning : scene.warnings) {
        std::cerr << "Scene load warning: " << warning << '\n';
        m_AssetBrowser.SetStatusMessage(warning);
    }
    m_Camera.SetPose(scene.cameraPosition, scene.cameraYaw, scene.cameraPitch);
    m_NextSceneOrder = 0;
    m_UnresolvedModels = RestoreSceneActorRecords(
        scene.models,
        [this](const EditorSceneModel& savedModel, std::size_t sceneOrder,
               std::string& restoreError) {
            m_NextSceneOrder = sceneOrder;
            const EditorSceneModel restoredSource =
                PrepareSceneActorForSave(savedModel);
            Model* restoredModel = nullptr;
            if (savedModel.light) {
                auto model = std::make_unique<Model>();
                restoredModel = model.get();
                m_Models.push_back(
                    {std::move(model), restoredSource, sceneOrder});
            } else if (savedModel.proceduralLandscape) {
                auto model = std::make_unique<Model>();
                if (!model->CreatePlane(
                        savedModel.landscapeWidth, savedModel.landscapeDepth,
                        savedModel.landscapeSegmentsX,
                        savedModel.landscapeSegmentsZ, restoreError,
                        savedModel.material)) {
                    return false;
                }
                restoredModel = model.get();
                m_Models.push_back(
                    {std::move(model), restoredSource, sceneOrder});
            } else {
                restoredModel =
                    SpawnModel(savedModel.path, savedModel.material,
                               restoreError);
                if (!restoredModel) {
                    return false;
                }
                const auto restored = std::find_if(
                    m_Models.begin(), m_Models.end(),
                    [restoredModel](const SceneObject& object) {
                        return object.model.get() == restoredModel;
                    });
                if (restored != m_Models.end()) {
                    restored->source = restoredSource;
                    restored->sceneOrder = sceneOrder;
                }
            }
            restoredModel->position = savedModel.position;
            restoredModel->rotation = savedModel.rotation;
            restoredModel->scale = savedModel.scale;
            m_NextSceneOrder = sceneOrder + 1;
            return true;
        });
    m_NextSceneOrder = scene.models.size();
    for (const UnresolvedSceneActor& unresolved : m_UnresolvedModels) {
        const std::string warning =
            "Could not restore actor '" + unresolved.source.name + "': " +
            unresolved.error;
        std::cerr << warning << '\n';
        m_AssetBrowser.SetStatusMessage(warning);
    }
}

void Application::HandleUnresolvedActorAction(
    const UnresolvedActorAction& action) {
    auto unresolved = std::find_if(
        m_UnresolvedModels.begin(), m_UnresolvedModels.end(),
        [&action](const UnresolvedSceneActor& actor) {
            return actor.sceneOrder == action.sceneOrder;
        });
    if (unresolved == m_UnresolvedModels.end()) {
        return;
    }

    const std::size_t sceneOrder = unresolved->sceneOrder;
    const std::string actorName = unresolved->source.name;
    if (action.type == UnresolvedActorActionType::Remove) {
        if (ApplyUnresolvedRemovalAction(m_UnresolvedModels, action)) {
            m_AssetBrowser.SetStatusMessage(
                "Removed unresolved actor '" + actorName + "' from the scene.");
        }
        return;
    }
    if (action.type == UnresolvedActorActionType::Relink &&
        unresolved->source.proceduralLandscape) {
        return;
    }

    EditorSceneModel candidate = unresolved->source;
    if (action.type == UnresolvedActorActionType::Relink) {
        std::error_code pathError;
        candidate.path =
            std::filesystem::absolute(action.replacementPath, pathError)
                .lexically_normal();
        if (pathError) {
            unresolved->error =
                "Could not resolve replacement model path: " +
                pathError.message();
            m_AssetBrowser.SetStatusMessage(
                "Could not relink actor '" + actorName + "': " +
                unresolved->error);
            return;
        }
    }

    ModelMaterial materialForRestore = candidate.material;
    std::string materialRestoreError;
    const bool materialDescriptorReloaded =
        TryReloadUnresolvedMaterialDescriptor(
            candidate, materialForRestore, materialRestoreError);
    if (!materialDescriptorReloaded && !materialRestoreError.empty()) {
        m_AssetBrowser.SetStatusMessage(
            "Could not reload material descriptor for '" + actorName +
            "'; retrying with saved map paths: " + materialRestoreError);
    }

    std::unique_ptr<Model> restoredLandscape;
    Model* restoredModel = nullptr;
    const bool restored = TryRestoreUnresolvedSceneActor(
        *unresolved, candidate,
        [this, &restoredLandscape, &restoredModel, &materialForRestore](
            const EditorSceneModel& source, std::string& error) {
            if (source.proceduralLandscape) {
                restoredLandscape = std::make_unique<Model>();
                if (!LoadSceneModel(source, materialForRestore,
                                    *restoredLandscape, error)) {
                    restoredLandscape.reset();
                    return false;
                }
                return true;
            }
            restoredModel =
                SpawnModel(source.path, materialForRestore, error);
            return restoredModel != nullptr;
        });
    if (!restored) {
        m_AssetBrowser.SetStatusMessage(
            "Could not restore actor '" + actorName + "': " +
            unresolved->error);
        return;
    }

    const EditorSceneModel restoredSource =
        PrepareSceneActorForSave(unresolved->source);
    if (restoredLandscape) {
        restoredModel = restoredLandscape.get();
        m_Models.push_back(
            {std::move(restoredLandscape), restoredSource, sceneOrder});
    } else {
        const auto loaded = std::find_if(
            m_Models.begin(), m_Models.end(),
            [restoredModel](const SceneObject& object) {
                return object.model.get() == restoredModel;
            });
        if (loaded == m_Models.end()) {
            unresolved->error =
                "The restored actor could not be added to the scene.";
            m_AssetBrowser.SetStatusMessage(
                "Could not restore actor '" + actorName + "': " +
                unresolved->error);
            return;
        }
        loaded->source = restoredSource;
        loaded->sceneOrder = sceneOrder;
    }

    restoredModel->position = restoredSource.position;
    restoredModel->rotation = restoredSource.rotation;
    restoredModel->scale = restoredSource.scale;
    m_SelectedModel = restoredModel;
    RemoveUnresolvedSceneActor(m_UnresolvedModels, sceneOrder);
    m_AssetBrowser.SetStatusMessage(
        "Restored actor '" + actorName + "' to the scene.");
}

bool Application::SaveScene(std::string& error) {
    std::vector<OrderedSceneActor> orderedActors;
    orderedActors.reserve(m_Models.size());
    for (const SceneObject& object : m_Models) {
        EditorSceneModel savedModel = object.source;
        savedModel.position = object.model->position;
        savedModel.rotation = object.model->rotation;
        savedModel.scale = object.model->scale;
        orderedActors.push_back({object.sceneOrder, std::move(savedModel)});
    }
    return SaveEditorSceneWithRecovery(
        m_ProjectRoot, m_SceneReadyForAutoSave, m_Camera.GetPosition(),
        m_Camera.GetYaw(), m_Camera.GetPitch(), std::move(orderedActors),
        m_UnresolvedModels, error);
}

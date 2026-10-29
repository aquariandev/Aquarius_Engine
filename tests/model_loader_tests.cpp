#include "Model.hpp"
#include "ModelAssets.hpp"
#include "Renderer.hpp"
#include "SceneRecovery.hpp"
#include "ShadowMapping.hpp"

#include <GLFW/glfw3.h>
#include <glad/glad.h>
#include <glm/gtc/matrix_inverse.hpp>

#include <array>
#include <chrono>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>
#include <system_error>
#include <algorithm>
#include <vector>

namespace {

class TemporaryDirectory {
public:
    TemporaryDirectory() {
        const auto timestamp =
            std::chrono::steady_clock::now().time_since_epoch().count();
        m_Path = std::filesystem::temp_directory_path() /
                 ("aquarius-model-loader-" + std::to_string(timestamp));
        std::filesystem::create_directories(m_Path);
    }

    ~TemporaryDirectory() {
        std::error_code error;
        std::filesystem::remove_all(m_Path, error);
    }

    const std::filesystem::path& Path() const { return m_Path; }

private:
    std::filesystem::path m_Path;
};

void Require(bool condition, const std::string& message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

int SkipWithoutGraphicsContext() {
    glfwTerminate();
    std::cerr << "Skipped engine-loader test: no OpenGL context available.\n";
    return 77;
}

} // namespace

int main() {
    if (glfwInit() != GLFW_TRUE) {
        return SkipWithoutGraphicsContext();
    }
    glfwWindowHint(GLFW_VISIBLE, GLFW_FALSE);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 4);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 6);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
    GLFWwindow* window = glfwCreateWindow(64, 64, "Model loader test", nullptr,
                                         nullptr);
    if (!window) {
        return SkipWithoutGraphicsContext();
    }
    glfwMakeContextCurrent(window);
    if (!gladLoadGLLoader(
            reinterpret_cast<GLADloadproc>(glfwGetProcAddress))) {
        glfwDestroyWindow(window);
        return SkipWithoutGraphicsContext();
    }

    int result = 0;
    try {
        TemporaryDirectory temporary;
        const auto source = temporary.Path() / "triangle.obj";
        {
            std::ofstream object(source);
            object << "o triangle\n"
                      "v 0 0 0\n"
                      "v 1 0 0\n"
                      "v 0 1 0\n"
                      "vt 0 0\n"
                      "vt 1 0\n"
                      "vt 0 1\n"
                      "vn 0 0 1\n"
                      "f 1/1/1 2/2/1 3/3/1\n";
            Require(static_cast<bool>(object),
                    "Could not write the engine-loader OBJ fixture.");
        }
        const auto assetDirectory = temporary.Path() / "assets";
        std::filesystem::create_directory(assetDirectory);
        const auto destination = assetDirectory / "Triangle" / "Triangle.glb";
        std::string error;
        Require(ConvertModelToGlb(source, destination, error),
                "Could not convert fixture to GLB: " + error);

        Camera projectionCamera;
        projectionCamera.SetPose({2.0f, 4.0f, 8.0f}, -65.0f, 12.0f);
        const glm::vec3 originalCameraPosition =
            projectionCamera.GetPosition();
        const glm::mat4 originalView = projectionCamera.GetViewMatrix();
        const glm::mat4 perspectiveProjection =
            projectionCamera.GetProjectionMatrix(16.0f / 9.0f);
        projectionCamera.ToggleProjection();
        const glm::mat4 orthographicProjection =
            projectionCamera.GetProjectionMatrix(16.0f / 9.0f);
        Require(projectionCamera.GetProjection() ==
                        CameraProjection::Orthographic &&
                    std::abs(perspectiveProjection[3][3]) < 0.0001f &&
                    std::abs(orthographicProjection[3][3] - 1.0f) < 0.0001f,
                "Camera projection toggle did not switch between perspective "
                "and orthographic matrices.");
        Require(glm::length(projectionCamera.GetPosition() -
                            originalCameraPosition) < 0.0001f,
                "Changing projection moved the camera.");
        for (int column = 0; column < 4; ++column) {
            for (int row = 0; row < 4; ++row) {
                Require(std::abs(projectionCamera.GetViewMatrix()[column][row] -
                                 originalView[column][row]) < 0.0001f,
                        "Changing projection changed the camera orientation.");
            }
        }
        const float initialOrthographicHeight =
            projectionCamera.GetOrthographicHeight();
        projectionCamera.Move({0.0f, 0.0f, 1.0f}, 1.0f, false);
        Require(projectionCamera.GetOrthographicHeight() <
                    initialOrthographicHeight,
                "Forward camera movement did not zoom in orthographic mode.");
        const float zoomedOrthographicHeight =
            projectionCamera.GetOrthographicHeight();
        projectionCamera.ToggleProjection();
        projectionCamera.ToggleProjection();
        Require(projectionCamera.GetProjection() ==
                        CameraProjection::Orthographic &&
                    std::abs(projectionCamera.GetOrthographicHeight() -
                             zoomedOrthographicHeight) < 0.0001f,
                "Repeated projection switching corrupted orthographic zoom.");
        const glm::vec3 beforeOrthographicPan =
            projectionCamera.GetPosition();
        projectionCamera.Move({1.0f, 0.0f, 0.0f}, 0.5f, false);
        Require(glm::length(projectionCamera.GetPosition() -
                            beforeOrthographicPan) > 0.1f,
                "Camera strafe navigation stopped working orthographically.");
        projectionCamera.ToggleProjection();
        Require(projectionCamera.GetProjection() ==
                    CameraProjection::Perspective,
                "Camera could not switch back to perspective projection.");

        Camera orthographicPanCamera;
        orthographicPanCamera.SetPose({3.0f, 4.0f, 8.0f}, -65.0f, 12.0f);
        orthographicPanCamera.SetProjection(CameraProjection::Orthographic);
        const glm::mat4 inversePanView =
            glm::inverse(orthographicPanCamera.GetViewMatrix());
        const glm::vec3 viewRight =
            glm::normalize(glm::vec3(inversePanView[0]));
        const glm::vec3 viewUp =
            glm::normalize(glm::vec3(inversePanView[1]));
        const float panYaw = orthographicPanCamera.GetYaw();
        const float panPitch = orthographicPanCamera.GetPitch();
        const float viewportHeight = 800.0f;
        const float orthographicUnitsPerPixel =
            orthographicPanCamera.GetOrthographicHeight() / viewportHeight;
        const glm::vec3 orthographicPanStart =
            orthographicPanCamera.GetPosition();
        orthographicPanCamera.Pan(40.0f, -30.0f, viewportHeight);
        const glm::vec3 orthographicPanDelta =
            orthographicPanCamera.GetPosition() - orthographicPanStart;
        const glm::vec3 expectedOrthographicPan =
            (viewRight * 40.0f + viewUp * 30.0f) *
            orthographicUnitsPerPixel;
        Require(glm::length(orthographicPanDelta - expectedOrthographicPan) <
                    0.0001f,
                "Orthographic panning did not follow cursor movement in the "
                "camera's view plane.");
        Require(std::abs(orthographicPanCamera.GetYaw() - panYaw) < 0.0001f &&
                    std::abs(orthographicPanCamera.GetPitch() - panPitch) <
                        0.0001f,
                "Camera panning changed the camera orientation.");

        orthographicPanCamera.Move({0.0f, 0.0f, 1.0f}, 0.5f, false);
        const float zoomedOrthographicUnitsPerPixel =
            orthographicPanCamera.GetOrthographicHeight() / viewportHeight;
        const glm::vec3 zoomedPanStart =
            orthographicPanCamera.GetPosition();
        orthographicPanCamera.Pan(0.0f, 20.0f, viewportHeight);
        const glm::vec3 zoomedPanDelta =
            orthographicPanCamera.GetPosition() - zoomedPanStart;
        Require(glm::length(
                    zoomedPanDelta + viewUp * 20.0f *
                                         zoomedOrthographicUnitsPerPixel) <
                    0.0001f,
                "Orthographic panning did not scale with the current view "
                "height.");

        Camera nearPerspectivePanCamera;
        nearPerspectivePanCamera.SetPose({0.0f, 0.0f, 10.0f}, -90.0f, 0.0f);
        Camera farPerspectivePanCamera;
        farPerspectivePanCamera.SetPose({0.0f, 0.0f, 20.0f}, -90.0f, 0.0f);
        const glm::vec3 nearPerspectiveStart =
            nearPerspectivePanCamera.GetPosition();
        const glm::vec3 farPerspectiveStart =
            farPerspectivePanCamera.GetPosition();
        nearPerspectivePanCamera.Pan(100.0f, 0.0f, 1000.0f);
        farPerspectivePanCamera.Pan(100.0f, 0.0f, 1000.0f);
        const float nearPerspectiveDistance =
            glm::length(nearPerspectivePanCamera.GetPosition() -
                        nearPerspectiveStart);
        const float farPerspectiveDistance =
            glm::length(farPerspectivePanCamera.GetPosition() -
                        farPerspectiveStart);
        Require(std::abs(farPerspectiveDistance -
                         nearPerspectiveDistance * 2.0f) < 0.0001f,
                "Perspective panning did not scale with camera distance.");
        Camera rotatedPerspectivePanCamera;
        rotatedPerspectivePanCamera.SetPose({0.0f, 0.0f, 10.0f}, 0.0f, 0.0f);
        const glm::vec3 rotatedPerspectiveStart =
            rotatedPerspectivePanCamera.GetPosition();
        rotatedPerspectivePanCamera.Pan(100.0f, 0.0f, 1000.0f);
        const float rotatedPerspectiveDistance =
            glm::length(rotatedPerspectivePanCamera.GetPosition() -
                        rotatedPerspectiveStart);
        Require(std::abs(rotatedPerspectiveDistance -
                         nearPerspectiveDistance) < 0.0001f,
                "Perspective panning sensitivity changed with camera "
                "orientation.");
        const glm::vec3 beforeProjectionSwitchPan =
            orthographicPanCamera.GetPosition();
        orthographicPanCamera.ToggleProjection();
        orthographicPanCamera.Pan(10.0f, -5.0f, viewportHeight);
        Require(glm::length(orthographicPanCamera.GetPosition() -
                            beforeProjectionSwitchPan) > 0.0001f,
                "Panning stopped after switching to perspective projection.");

        {
            Model model;
            Require(model.LoadFromFile(destination, ModelMaterial{}, error),
                    "Engine Model loader could not reopen generated GLB: " +
                        error);

            EditorSceneModel validActor;
            validActor.name = "Valid actor";
            validActor.path = destination;
            EditorSceneModel missingActor;
            missingActor.name = "Missing actor";
            missingActor.path = temporary.Path() / "missing.glb";
            std::vector<std::unique_ptr<Model>> restoredModels;
            std::vector<EditorSceneModel> actorRecords{
                validActor, missingActor};
            std::vector<UnresolvedSceneActor> unresolved =
                RestoreSceneActorRecords(
                    actorRecords,
                    [&restoredModels](const EditorSceneModel& actor,
                                      std::size_t, std::string& loadError) {
                        auto restored = std::make_unique<Model>();
                        if (!restored->LoadFromFile(
                                actor.path, actor.material, loadError)) {
                            return false;
                        }
                        restoredModels.push_back(std::move(restored));
                        return true;
                    });
            Require(restoredModels.size() == 1 && unresolved.size() == 1 &&
                        unresolved.front().sceneOrder == 1 &&
                        unresolved.front().source.name == "Missing actor" &&
                        !unresolved.front().error.empty(),
                    "Failed GLB restoration did not preserve its scene actor.");

            EditorSceneModel failedReplacement = missingActor;
            failedReplacement.path = temporary.Path() / "bad-replacement.glb";
            Require(!TryRestoreUnresolvedSceneActor(
                        unresolved.front(), failedReplacement,
                        [](const EditorSceneModel& actor,
                           std::string& loadError) {
                            Model replacement;
                            return replacement.LoadFromFile(
                                actor.path, actor.material, loadError);
                        }),
                    "Invalid GLB replacement unexpectedly loaded.");
            Require(unresolved.front().source.path == missingActor.path &&
                        unresolved.front().source.name == missingActor.name,
                    "Failed GLB relink changed the retained source actor.");

            EditorSceneModel validReplacement = missingActor;
            validReplacement.path = destination;
            std::unique_ptr<Model> replacementModel;
            Require(TryRestoreUnresolvedSceneActor(
                        unresolved.front(), validReplacement,
                        [&replacementModel](const EditorSceneModel& actor,
                                            std::string& loadError) {
                            replacementModel = std::make_unique<Model>();
                            if (!replacementModel->LoadFromFile(
                                    actor.path, actor.material, loadError)) {
                                replacementModel.reset();
                                return false;
                            }
                            return true;
                        }),
                    "Valid GLB replacement failed to restore.");
            Require(unresolved.front().source.path == destination &&
                        unresolved.front().source.name == missingActor.name,
                    "Successful GLB relink lost actor identity.");

            Renderer renderer;
            Camera camera;
            renderer.BeginFrame(camera);
            glm::vec3 perspectiveOriginLeft;
            glm::vec3 perspectiveDirectionLeft;
            glm::vec3 perspectiveOriginRight;
            glm::vec3 perspectiveDirectionRight;
            renderer.MakeScreenRay(20.0, 32.0, 64, 64,
                                   perspectiveOriginLeft,
                                   perspectiveDirectionLeft);
            renderer.MakeScreenRay(44.0, 32.0, 64, 64,
                                   perspectiveOriginRight,
                                   perspectiveDirectionRight);
            Require(glm::length(perspectiveOriginLeft -
                                perspectiveOriginRight) < 0.0001f &&
                        glm::dot(perspectiveDirectionLeft,
                                 perspectiveDirectionRight) < 0.9999f,
                    "Perspective screen rays did not share the camera origin "
                    "and diverge by cursor position.");
            camera.ToggleProjection();
            renderer.BeginFrame(camera);
            glm::vec3 orthographicOriginLeft;
            glm::vec3 orthographicDirectionLeft;
            glm::vec3 orthographicOriginRight;
            glm::vec3 orthographicDirectionRight;
            renderer.MakeScreenRay(20.0, 32.0, 64, 64,
                                   orthographicOriginLeft,
                                   orthographicDirectionLeft);
            renderer.MakeScreenRay(44.0, 32.0, 64, 64,
                                   orthographicOriginRight,
                                   orthographicDirectionRight);
            Require(glm::length(orthographicOriginLeft -
                                orthographicOriginRight) > 0.1f &&
                        glm::dot(orthographicDirectionLeft,
                                 orthographicDirectionRight) > 0.9999f,
                    "Orthographic screen rays did not start at distinct "
                    "positions with parallel directions.");
            camera.ToggleProjection();
            std::string defaultSceneError;
            Require(CreateDefaultEditorScene(temporary.Path(),
                                             defaultSceneError),
                    "Could not create the renderer default-scene fixture: " +
                        defaultSceneError);
            EditorScene defaultScene;
            Require(LoadEditorScene(temporary.Path(), defaultScene,
                                    defaultSceneError) &&
                        defaultScene.models.size() == 2 &&
                        defaultScene.models[0].proceduralLandscape &&
                        defaultScene.models[1].light &&
                        defaultScene.models[1].light->type ==
                            LightType::Directional,
                    "Default-scene fixture did not contain its ground and "
                    "directional light.");
            const EditorSceneModel& groundSource = defaultScene.models[0];
            const EditorSceneModel& lightSource = defaultScene.models[1];
            Model defaultGround;
            Require(defaultGround.CreatePlane(
                        groundSource.landscapeWidth,
                        groundSource.landscapeDepth,
                        groundSource.landscapeSegmentsX,
                        groundSource.landscapeSegmentsZ, error,
                        groundSource.material),
                    "Could not create the default light receiver: " + error);
            defaultGround.position = groundSource.position;
            defaultGround.rotation = groundSource.rotation;
            defaultGround.scale = groundSource.scale;
            Camera lightingCamera;
            lightingCamera.SetPose(defaultScene.cameraPosition,
                                   defaultScene.cameraYaw, -14.0f);
            const std::vector<const Model*> defaultShadowCasters{
                &defaultGround};
            const auto renderCenterPixel =
                [&](const std::vector<RenderLight>& lights) {
                renderer.SetViewMode(EditorViewMode::Lit);
                renderer.BeginFrame(lightingCamera, lights);
                renderer.RenderShadowMap(defaultShadowCasters);
                renderer.DrawModel(defaultGround);
                glFinish();
                std::array<GLubyte, 4> pixel{};
                glReadBuffer(GL_BACK);
                glReadPixels(32, 32, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE,
                             pixel.data());
                return pixel;
            };
            const auto isBlack = [](const std::array<GLubyte, 4>& pixel) {
                return pixel[0] == 0 && pixel[1] == 0 && pixel[2] == 0;
            };
            const std::vector<RenderLight> noLights;
            const std::array<GLubyte, 4> noLightPixel =
                renderCenterPixel(noLights);
            Require(isBlack(noLightPixel),
                    "Non-emissive scene geometry received light with no "
                    "scene light sources.");

            const RenderLight defaultDirectional = BuildRenderLight(
                *lightSource.light, lightSource.position,
                lightSource.rotation);
            const std::vector<RenderLight> directionalOnly{
                defaultDirectional};
            const std::array<GLubyte, 4> litPixel =
                renderCenterPixel(directionalOnly);
            int brightnessDifference = 0;
            for (std::size_t channel = 0; channel < 3; ++channel) {
                brightnessDifference += std::abs(
                    static_cast<int>(litPixel[channel]) -
                    static_cast<int>(noLightPixel[channel]));
            }
            Require(brightnessDifference > 20,
                    "The saved default Directional Light did not change the "
                    "rendered ground receiver.");

            LightProperties zeroIntensityProperties = *lightSource.light;
            zeroIntensityProperties.intensity = 0.0f;
            const std::vector<RenderLight> zeroIntensityLight{
                BuildRenderLight(zeroIntensityProperties,
                                 lightSource.position,
                                 lightSource.rotation)};
            Require(isBlack(renderCenterPixel(zeroIntensityLight)),
                    "A zero-intensity light left non-emissive geometry "
                    "illuminated.");

            EditorSceneModel spotSource =
                MakeEditorSceneLight(LightType::Spot);
            spotSource.position = {0.0f, 5.0f, 0.0f};
            EditorSceneModel rectSource =
                MakeEditorSceneLight(LightType::Rect);
            rectSource.position = {0.0f, 5.0f, 0.0f};
            const RenderLight pointLight = BuildRenderLight(
                DefaultLightProperties(LightType::Point),
                glm::vec3(0.0f, 5.0f, 0.0f), glm::vec3(0.0f));
            const RenderLight spotLight = BuildRenderLight(
                *spotSource.light, spotSource.position, spotSource.rotation);
            const RenderLight rectLight = BuildRenderLight(
                *rectSource.light, rectSource.position, rectSource.rotation);
            const RenderLight skyLight = BuildRenderLight(
                DefaultLightProperties(LightType::Sky),
                glm::vec3(0.0f, 5.0f, 0.0f), glm::vec3(0.0f));

            const auto spotProjection =
                ShadowMapping::BuildProjection(spotLight,
                                               lightingCamera.GetPosition());
            Require(spotProjection && spotProjection->perspective &&
                        std::abs(spotProjection->farPlane -
                                 spotLight.properties.range) < 0.0001f &&
                        std::abs(spotProjection->verticalFieldOfViewDegrees -
                                 spotLight.properties.outerConeAngleDegrees *
                                     2.0f) < 0.0001f,
                    "Spot shadow projection did not match its range and "
                    "outer cone angle.");
            const auto directionalProjection =
                ShadowMapping::BuildProjection(defaultDirectional,
                                               lightingCamera.GetPosition());
            const auto skyProjection = ShadowMapping::BuildProjection(
                skyLight, lightingCamera.GetPosition());
            Require(directionalProjection &&
                        !directionalProjection->perspective &&
                        std::abs(directionalProjection->nearPlane - 1.0f) <
                            0.0001f &&
                        std::abs(directionalProjection->farPlane - 180.0f) <
                            0.0001f &&
                        skyProjection && !skyProjection->perspective,
                    "Directional and Sky shadow projections were not "
                    "orthographic.");
            const auto matricesDiffer = [](const glm::mat4& first,
                                           const glm::mat4& second) {
                for (int column = 0; column < 4; ++column) {
                    for (int row = 0; row < 4; ++row) {
                        if (std::abs(first[column][row] -
                                     second[column][row]) > 0.0001f) {
                            return true;
                        }
                    }
                }
                return false;
            };
            RenderLight widerSpot = spotLight;
            widerSpot.properties.range = 40.0f;
            widerSpot.properties.outerConeAngleDegrees = 45.0f;
            const auto widerSpotProjection =
                ShadowMapping::BuildProjection(widerSpot,
                                               lightingCamera.GetPosition());
            RenderLight movedSpot = spotLight;
            movedSpot.position.x += 3.0f;
            const auto movedSpotProjection =
                ShadowMapping::BuildProjection(movedSpot,
                                               lightingCamera.GetPosition());
            RenderLight rotatedSpot = spotLight;
            rotatedSpot.emissionDirection =
                glm::normalize(glm::vec3(-1.0f, 0.0f, 0.0f));
            const auto rotatedSpotProjection =
                ShadowMapping::BuildProjection(rotatedSpot,
                                               lightingCamera.GetPosition());
            Require(widerSpotProjection &&
                        std::abs(widerSpotProjection->farPlane - 40.0f) <
                            0.0001f &&
                        std::abs(
                            widerSpotProjection->verticalFieldOfViewDegrees -
                            90.0f) < 0.0001f &&
                        matricesDiffer(spotProjection->viewProjection,
                                       widerSpotProjection->viewProjection) &&
                        movedSpotProjection &&
                        matricesDiffer(spotProjection->viewProjection,
                                       movedSpotProjection->viewProjection) &&
                        rotatedSpotProjection &&
                        matricesDiffer(spotProjection->viewProjection,
                                       rotatedSpotProjection->viewProjection),
                    "Spot shadow projection did not update for its range, "
                    "cone, position, or rotation.");
            const std::vector<RenderLight> shadowPriorityLights{
                defaultDirectional, pointLight, spotLight};
            const auto shadowPriorityIndex =
                ShadowMapping::FindActiveLightIndex(shadowPriorityLights);
            Require(shadowPriorityIndex && *shadowPriorityIndex == 2 &&
                        !ShadowMapping::SupportsShadowCasting(
                            LightType::Point) &&
                        !ShadowMapping::SupportsShadowCasting(
                            LightType::Rect),
                    "Shadow selection did not prioritize Spot or reject "
                    "unsupported light types.");

            RenderLight spotShadowLight = spotLight;
            spotShadowLight.position = {8.0f, 8.0f, 8.0f};
            spotShadowLight.properties.range = 30.0f;
            spotShadowLight.properties.outerConeAngleDegrees = 35.0f;
            spotShadowLight.emissionDirection =
                glm::normalize(glm::vec3(-1.0f, -1.0f, -1.0f));
            RenderLight directionalShadowLight = defaultDirectional;
            directionalShadowLight.emissionDirection =
                glm::normalize(glm::vec3(-1.0f, -1.0f, -1.0f));
            Model shadowBlocker;
            Require(shadowBlocker.LoadFromFile(destination, ModelMaterial{},
                                               error),
                    "Could not load the shadow test blocker: " + error);
            shadowBlocker.position = {0.5f, 0.0f, 2.0f};
            shadowBlocker.scale = {6.0f, 6.0f, 1.0f};
            const auto renderShadowReceiverPixel =
                [&](const std::vector<RenderLight>& lights,
                    bool includeBlocker) {
                    std::vector<const Model*> shadowCasters{&defaultGround};
                    if (includeBlocker) {
                        shadowCasters.push_back(&shadowBlocker);
                    }
                    renderer.SetViewMode(EditorViewMode::Lit);
                    renderer.BeginFrame(lightingCamera, lights);
                    renderer.RenderShadowMap(shadowCasters);
                    renderer.DrawModel(defaultGround);
                    glFinish();
                    std::array<GLubyte, 4> pixel{};
                    glReadBuffer(GL_BACK);
                    glReadPixels(32, 32, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE,
                                 pixel.data());
                    return pixel;
                };
            const auto pixelBrightness =
                [](const std::array<GLubyte, 4>& pixel) {
                    return static_cast<int>(pixel[0]) +
                           static_cast<int>(pixel[1]) +
                           static_cast<int>(pixel[2]);
                };
            const std::vector<RenderLight> spotShadowLights{
                spotShadowLight};
            const int spotUnblockedBrightness = pixelBrightness(
                renderShadowReceiverPixel(spotShadowLights, false));
            const int spotBlockedBrightness = pixelBrightness(
                renderShadowReceiverPixel(spotShadowLights, true));
            const int spotClearedBrightness = pixelBrightness(
                renderShadowReceiverPixel(spotShadowLights, false));
            Require(spotUnblockedBrightness > 20 &&
                        spotBlockedBrightness * 2 < spotUnblockedBrightness &&
                        spotClearedBrightness * 2 > spotUnblockedBrightness,
                    "Spot Light did not shadow the ground receiver or left "
                    "stale shadow data after the blocker was removed.");

            const std::vector<RenderLight> directionalShadowLights{
                directionalShadowLight};
            const int directionalUnblockedBrightness = pixelBrightness(
                renderShadowReceiverPixel(directionalShadowLights, false));
            const int directionalBlockedBrightness = pixelBrightness(
                renderShadowReceiverPixel(directionalShadowLights, true));
            Require(directionalUnblockedBrightness > 20 &&
                        directionalBlockedBrightness * 2 <
                            directionalUnblockedBrightness,
                    "Directional Light did not shadow the ground receiver "
                    "in its light direction.");
            const int pointOnlyBrightness = pixelBrightness(
                renderShadowReceiverPixel({pointLight}, false));
            const int pointAndSpotBrightness = pixelBrightness(
                renderShadowReceiverPixel({pointLight, spotShadowLight}, true));
            Require(pointAndSpotBrightness + 2 >= pointOnlyBrightness,
                    "The Spot Light shadow incorrectly suppressed the "
                    "unshadowed Point Light contribution.");

            const std::array<RenderLight, 4> localAndSkyLights{
                pointLight, spotLight, rectLight, skyLight};
            std::array<std::array<GLubyte, 4>, 4> individualLightPixels{};
            for (std::size_t i = 0; i < localAndSkyLights.size(); ++i) {
                const std::vector<RenderLight> oneLight{
                    localAndSkyLights[i]};
                individualLightPixels[i] = renderCenterPixel(oneLight);
                Require(!isBlack(individualLightPixels[i]),
                        "A Point, Spot, Rect, or Sky Light failed to "
                        "illuminate the ground receiver.");
            }
            const std::vector<RenderLight> multipleLights{
                pointLight, spotLight, rectLight, skyLight,
                defaultDirectional};
            const std::array<GLubyte, 4> combinedLightPixel =
                renderCenterPixel(multipleLights);
            const int pointBrightness =
                static_cast<int>(individualLightPixels[0][0]) +
                static_cast<int>(individualLightPixels[0][1]) +
                static_cast<int>(individualLightPixels[0][2]);
            const int combinedBrightness =
                static_cast<int>(combinedLightPixel[0]) +
                static_cast<int>(combinedLightPixel[1]) +
                static_cast<int>(combinedLightPixel[2]);
            Require(combinedBrightness >= pointBrightness,
                    "Combining light sources reduced the rendered "
                    "contribution.");

            const auto renderBackgroundPixel =
                [&](EditorViewMode mode,
                    const std::vector<RenderLight>& lights) {
                renderer.SetViewMode(mode);
                Camera backgroundCamera;
                renderer.BeginFrame(backgroundCamera, lights);
                renderer.DrawGroundGrid();
                glFinish();
                std::array<GLubyte, 4> pixel{};
                glReadBuffer(GL_BACK);
                glReadPixels(32, 60, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE,
                             pixel.data());
                return pixel;
            };
            const std::array<GLubyte, 4> emptyLitBackground =
                renderBackgroundPixel(EditorViewMode::Lit, noLights);
            const std::array<GLubyte, 4> litBackground =
                renderBackgroundPixel(EditorViewMode::Lit, directionalOnly);
            const std::array<GLubyte, 4> emptyUnlitBackground =
                renderBackgroundPixel(EditorViewMode::Unlit, noLights);
            Require(isBlack(emptyLitBackground) && isBlack(litBackground) &&
                        isBlack(emptyUnlitBackground),
                    "Scene background was not pure black in both view modes "
                    "and light configurations.");
            renderer.SetViewMode(EditorViewMode::Lit);

            const EditorSceneModel shadowLightSource =
                MakeEditorSceneLight(LightType::Directional);
            const std::vector<RenderLight> shadowLights{
                BuildRenderLight(*shadowLightSource.light,
                                 shadowLightSource.position,
                                 shadowLightSource.rotation)};
            const std::vector<const Model*> shadowCasters{&model};
            renderer.BeginFrame(camera, shadowLights);
            std::vector<RenderLight> excessiveLights(MaxSceneLights + 1);
            bool excessiveLightsRejected = false;
            try {
                renderer.BeginFrame(camera, excessiveLights);
            } catch (const std::invalid_argument&) {
                excessiveLightsRejected = true;
            }
            Require(excessiveLightsRejected,
                    "Renderer silently accepted more lights than it supports.");
            renderer.RenderShadowMap(shadowCasters);
            constexpr int shadowMapSize = 4096;
            std::vector<float> shadowDepth(
                static_cast<std::size_t>(shadowMapSize) * shadowMapSize);
            glActiveTexture(GL_TEXTURE5);
            glGetTexImage(GL_TEXTURE_2D, 0, GL_DEPTH_COMPONENT, GL_FLOAT,
                          shadowDepth.data());
            glActiveTexture(GL_TEXTURE0);
            Require(std::any_of(shadowDepth.begin(), shadowDepth.end(),
                                [](float depth) { return depth < 0.999f; }),
                    "Directional shadow map remained empty after drawing a "
                    "model.");
            renderer.DrawGroundGrid();
            renderer.DrawModel(model);
            constexpr std::array<LightType, 5> lightTypes{
                LightType::Directional, LightType::Spot, LightType::Point,
                LightType::Sky, LightType::Rect};
            std::vector<RenderLight> sceneLights;
            sceneLights.reserve(lightTypes.size());
            for (std::size_t i = 0; i < lightTypes.size(); ++i) {
                EditorSceneModel editorLight =
                    MakeEditorSceneLight(lightTypes[i]);
                editorLight.position = {
                    (static_cast<float>(i) - 2.0f) * 0.75f, 2.5f, 1.0f};
                sceneLights.push_back(BuildRenderLight(
                    *editorLight.light, editorLight.position,
                    editorLight.rotation));
            }
            renderer.BeginFrame(camera, sceneLights);
            renderer.RenderShadowMap(shadowCasters);
            renderer.DrawGroundGrid();
            renderer.DrawModel(model);
            for (std::size_t i = 0; i < lightTypes.size(); ++i) {
                renderer.DrawLightIcon(
                    lightTypes[i], sceneLights[i].position, i == 0);
                renderer.DrawLightVisualization(sceneLights[i]);
            }
            glFinish();
            const GLenum lightRenderError = glGetError();
            Require(lightRenderError == GL_NO_ERROR,
                    "OpenGL reported error " +
                        std::to_string(lightRenderError) +
                        " while rendering scene lights and their icons.");
            renderer.SetViewMode(EditorViewMode::Wire);
            renderer.DrawModel(model);
            GLint polygonMode[2]{};
            glGetIntegerv(GL_POLYGON_MODE, polygonMode);
            Require(polygonMode[0] == GL_FILL &&
                        polygonMode[1] == GL_FILL,
                    "Wire mode did not restore filled polygon rendering.");

            renderer.SetViewMode(EditorViewMode::Unlit);
            renderer.DrawGroundGrid();
            renderer.DrawModel(model);
            Require(glIsEnabled(GL_BLEND) == GL_FALSE,
                    "Ground-grid rendering did not restore blend state.");
            renderer.SetViewMode(EditorViewMode::Lit);
            renderer.DrawGroundGrid();
            renderer.DrawModel(model);
            const auto terrainMap = temporary.Path() / "terrain.ppm";
            {
                std::ofstream image(terrainMap, std::ios::binary);
                image << "P6\n2 2\n255\n";
                constexpr char pixels[]{0, 0, 0, static_cast<char>(255),
                                        static_cast<char>(255),
                                        static_cast<char>(255),
                                        static_cast<char>(255),
                                        static_cast<char>(255),
                                        static_cast<char>(255), 0, 0, 0};
                image.write(pixels, sizeof(pixels));
                Require(static_cast<bool>(image),
                        "Could not write landscape texture fixture.");
            }
            ModelMaterial terrainMaterial;
            terrainMaterial.diffuse = terrainMap;
            terrainMaterial.height = terrainMap;
            terrainMaterial.heightScale = 0.2f;
            Model landscape;
            const bool landscapeCreated = landscape.CreatePlane(
                32.0f, 16.0f, 8, 4, error, terrainMaterial);
            Require(landscapeCreated,
                    "Could not create an in-memory landscape plane: " + error);
            renderer.DrawModel(landscape);
            const GLenum glError = glGetError();
            Require(glError == GL_NO_ERROR,
                    "OpenGL reported error " + std::to_string(glError) +
                        " while rendering the directional shadow pass.");
        }
    } catch (const std::exception& exception) {
        std::cerr << "Model loader test failed: " << exception.what() << '\n';
        result = 1;
    }

    glfwDestroyWindow(window);
    glfwTerminate();
    if (result == 0) {
        std::cout << "Engine Model loader GLB test passed.\n";
    }
    return result;
}

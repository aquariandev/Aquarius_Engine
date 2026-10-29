#pragma once

#include "Camera.hpp"
#include "EditorViewMode.hpp"
#include "Light.hpp"
#include "Model.hpp"
#include "ShadowMapping.hpp"

#include <glm/glm.hpp>

#include <array>
#include <vector>

enum class TransformMode {
    Select,
    Move,
    Scale,
    Rotate
};

class Renderer{
public:

    Renderer();
    ~Renderer();

    void BeginFrame(const Camera& camera,
                    const std::vector<RenderLight>& lights = {});
    void EndFrame();
    void SetViewMode(EditorViewMode mode) { m_ViewMode = mode; }
    EditorViewMode GetViewMode() const { return m_ViewMode; }
    void RenderShadowMap(const std::vector<const Model*>& models);
    void DrawGroundGrid() const;
    void DrawModel(const Model& model, bool selected = false) const;
    void DrawLightIcon(LightType type, const glm::vec3& position,
                       bool selected) const;
    void DrawLightVisualization(const RenderLight& light) const;
    void DrawTransformGizmo(const glm::vec3& position,
                            TransformMode mode) const;
    bool HitTransformGizmo(const glm::vec3& position, TransformMode mode,
                           double cursorX, double cursorY, int windowWidth,
                           int windowHeight, int& axis,
                           glm::vec2& screenDirection,
                           float& pixelsPerUnit) const;
    void MakeScreenRay(double cursorX, double cursorY, int windowWidth,
                       int windowHeight, glm::vec3& origin,
                       glm::vec3& direction) const;

private:
    GLuint m_ShaderProgram = 0;
    GLuint m_OutlineShaderProgram = 0;
    GLuint m_GizmoShaderProgram = 0;
    GLuint m_ShadowShaderProgram = 0;
    GLuint m_LightIconShaderProgram = 0;
    GLuint m_LightIconVertexArray = 0;
    std::array<GLuint, 5> m_LightIconTextures{};
    GLuint m_GizmoVertexArray = 0;
    GLuint m_GizmoVertexBuffer = 0;
    GLuint m_GridShaderProgram = 0;
    GLuint m_GridVertexArray = 0;
    GLuint m_ShadowFramebuffer = 0;
    GLuint m_ShadowDepthTexture = 0;
    glm::mat4 m_ViewProjection{1.0f};
    glm::mat4 m_InverseViewProjection{1.0f};
    ShadowMapping::Projection m_ShadowProjection;
    glm::vec3 m_CameraPosition{0.0f};
    glm::vec3 m_CameraRight{1.0f, 0.0f, 0.0f};
    glm::vec3 m_CameraUp{0.0f, 1.0f, 0.0f};
    CameraProjection m_CameraProjection = CameraProjection::Perspective;
    glm::vec3 m_LightDirection{0.45f, 1.0f, 0.35f};
    struct LightUniformLocations {
        GLint type = -1;
        GLint position = -1;
        GLint direction = -1;
        GLint color = -1;
        GLint intensity = -1;
        GLint range = -1;
        GLint innerConeCosine = -1;
        GLint outerConeCosine = -1;
        GLint rectHalfAxisX = -1;
        GLint rectHalfAxisY = -1;
    };
    std::array<LightUniformLocations, MaxSceneLights> m_LightUniforms{};
    GLint m_LightCountLocation = -1;
    GLint m_ShadowLightIndexLocation = -1;
    GLint m_LightIconViewProjectionLocation = -1;
    GLint m_LightIconCenterLocation = -1;
    GLint m_LightIconCameraRightLocation = -1;
    GLint m_LightIconCameraUpLocation = -1;
    GLint m_LightIconTextureLocation = -1;
    GLint m_LightIconSelectedLocation = -1;
    int m_LightCount = 0;
    int m_ShadowLightIndex = -1;
    EditorViewMode m_ViewMode = EditorViewMode::Lit;

};
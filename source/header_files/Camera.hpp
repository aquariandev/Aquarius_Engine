#pragma once

#include <glm/glm.hpp>

enum class CameraProjection {
    Perspective,
    Orthographic
};

class Camera {
public:
    void Move(const glm::vec3& inputDirection, float deltaTime,
              bool speedBoost);
    void Pan(float mouseDeltaX, float mouseDeltaY, float viewportHeight);
    void Rotate(float mouseDeltaX, float mouseDeltaY);

    glm::mat4 GetViewMatrix() const;
    glm::mat4 GetProjectionMatrix(float aspectRatio,
                                  float nearPlane = 0.1f,
                                  float farPlane = 500.0f) const;
    const glm::vec3& GetPosition() const { return m_Position; }
    float GetYaw() const { return m_Yaw; }
    float GetPitch() const { return m_Pitch; }
    void SetPose(const glm::vec3& position, float yaw, float pitch);
    CameraProjection GetProjection() const { return m_Projection; }
    void SetProjection(CameraProjection projection) {
        m_Projection = projection;
    }
    void ToggleProjection();
    float GetOrthographicHeight() const { return m_OrthographicHeight; }

private:
    glm::vec3 m_Position{0.0f, 2.5f, 5.0f};
    glm::vec3 m_Forward{0.0f, 0.0f, -1.0f};
    glm::vec3 m_Right{1.0f, 0.0f, 0.0f};
    float m_Yaw = -90.0f;
    float m_Pitch = 0.0f;
    CameraProjection m_Projection = CameraProjection::Perspective;
    // Matches the 45-degree perspective view's span at a 10-unit focus depth.
    float m_OrthographicHeight = 8.284271f;
    float m_MouseSensitivity = 0.1f;
    float m_MovementSpeed = 4.0f;

    void UpdateDirectionVectors();
};

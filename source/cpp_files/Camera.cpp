#include "Camera.hpp"

#include <glm/gtc/matrix_transform.hpp>
#include <algorithm>
#include <cmath>

namespace {

constexpr float PerspectiveFieldOfViewDegrees = 45.0f;
constexpr float MinimumPerspectivePanDepth = 0.1f;

} // namespace

void Camera::Move(const glm::vec3& inputDirection, float deltaTime,
                  bool speedBoost) {
    const float directionLength = glm::length(inputDirection);
    if (directionLength <= 0.0f || deltaTime <= 0.0f) {
        return;
    }

    const glm::vec3 direction = inputDirection / directionLength;
    const float speedMultiplier = speedBoost ? 2.0f : 1.0f;
    const float movement = m_MovementSpeed * speedMultiplier * deltaTime;
    if (m_Projection == CameraProjection::Orthographic) {
        m_OrthographicHeight = std::clamp(
            m_OrthographicHeight *
                std::exp(-direction.z * movement * 0.2f),
            0.05f, 10000.0f);
        m_Position +=
            (m_Right * direction.x +
             glm::vec3(0.0f, 1.0f, 0.0f) * direction.y) *
            movement;
        return;
    }
    m_Position +=
        (m_Forward * direction.z + m_Right * direction.x +
         glm::vec3(0.0f, 1.0f, 0.0f) * direction.y) *
        movement;
}

void Camera::Pan(float mouseDeltaX, float mouseDeltaY, float viewportHeight) {
    if (!std::isfinite(mouseDeltaX) || !std::isfinite(mouseDeltaY) ||
        !std::isfinite(viewportHeight) || viewportHeight <= 0.0f) {
        return;
    }

    float worldUnitsPerPixel = 0.0f;
    if (m_Projection == CameraProjection::Orthographic) {
        worldUnitsPerPixel = m_OrthographicHeight / viewportHeight;
    } else {
        const float cameraDistance = glm::length(m_Position);
        if (!std::isfinite(cameraDistance)) {
            return;
        }
        const float perspectiveViewHeight =
            2.0f * std::max(cameraDistance, MinimumPerspectivePanDepth) *
            std::tan(glm::radians(PerspectiveFieldOfViewDegrees) * 0.5f);
        worldUnitsPerPixel = perspectiveViewHeight / viewportHeight;
    }

    const glm::vec3 viewUp =
        glm::normalize(glm::cross(m_Right, m_Forward));
    m_Position +=
        (m_Right * mouseDeltaX - viewUp * mouseDeltaY) * worldUnitsPerPixel;
}

void Camera::Rotate(float mouseDeltaX, float mouseDeltaY) {
    m_Yaw += mouseDeltaX * m_MouseSensitivity;
    m_Pitch = std::clamp(m_Pitch + mouseDeltaY * m_MouseSensitivity, -89.0f,
                         89.0f);
    UpdateDirectionVectors();
}

void Camera::UpdateDirectionVectors() {
    constexpr float degreesToRadians = 0.01745329251994329577f;
    const float yaw = m_Yaw * degreesToRadians;
    const float pitch = m_Pitch * degreesToRadians;
    m_Forward = glm::normalize(glm::vec3(
        std::cos(yaw) * std::cos(pitch), std::sin(pitch),
        std::sin(yaw) * std::cos(pitch)));
    m_Right = glm::normalize(
        glm::cross(m_Forward, glm::vec3(0.0f, 1.0f, 0.0f)));
}

glm::mat4 Camera::GetViewMatrix() const {
    return glm::lookAt(m_Position, m_Position + m_Forward,
                       glm::vec3(0.0f, 1.0f, 0.0f));
}

glm::mat4 Camera::GetProjectionMatrix(float aspectRatio, float nearPlane,
                                      float farPlane) const {
    const float safeAspectRatio =
        std::isfinite(aspectRatio) && aspectRatio > 0.0f ? aspectRatio : 1.0f;
    if (m_Projection == CameraProjection::Perspective) {
        return glm::perspective(glm::radians(PerspectiveFieldOfViewDegrees),
                                safeAspectRatio, nearPlane, farPlane);
    }
    const float halfHeight = m_OrthographicHeight * 0.5f;
    const float halfWidth = halfHeight * safeAspectRatio;
    return glm::ortho(-halfWidth, halfWidth, -halfHeight, halfHeight,
                      nearPlane, farPlane);
}

void Camera::ToggleProjection() {
    m_Projection =
        m_Projection == CameraProjection::Perspective
            ? CameraProjection::Orthographic
            : CameraProjection::Perspective;
}

void Camera::SetPose(const glm::vec3& position, float yaw, float pitch) {
    m_Position = position;
    m_Yaw = yaw;
    m_Pitch = std::clamp(pitch, -89.0f, 89.0f);
    UpdateDirectionVectors();
}

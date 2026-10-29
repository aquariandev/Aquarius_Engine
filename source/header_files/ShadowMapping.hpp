#pragma once

#include "Light.hpp"

#include <array>
#include <cmath>
#include <cstddef>
#include <optional>
#include <vector>

namespace ShadowMapping {

inline constexpr int Resolution = 4096;
inline constexpr float DirectionalHalfExtent = 45.0f;
inline constexpr float DirectionalNearPlane = 1.0f;
inline constexpr float DirectionalFarPlane = 180.0f;
inline constexpr float DirectionalEyeDistance = 100.0f;
inline constexpr float MaximumSpotNearPlane = 0.1f;

struct Projection {
    glm::mat4 viewProjection{1.0f};
    glm::vec3 directionToLight{0.0f, 1.0f, 0.0f};
    glm::vec3 lightPosition{0.0f};
    float nearPlane = DirectionalNearPlane;
    float farPlane = DirectionalFarPlane;
    float tangentHalfFieldOfView = 0.0f;
    float orthographicWorldTexelSize =
        2.0f * DirectionalHalfExtent / static_cast<float>(Resolution);
    float verticalFieldOfViewDegrees = 0.0f;
    LightType type = LightType::Directional;
    bool perspective = false;
};

inline bool SupportsShadowCasting(LightType type) noexcept {
    return type == LightType::Directional || type == LightType::Spot ||
           type == LightType::Sky;
}

inline bool IsEnabled(const RenderLight& light) {
    const LightProperties& properties = light.properties;
    return IsValidLightProperties(properties) && properties.intensity > 0.0f &&
           (properties.color.x > 0.0f || properties.color.y > 0.0f ||
            properties.color.z > 0.0f);
}

inline std::optional<std::size_t> FindActiveLightIndex(
    const std::vector<RenderLight>& lights) {
    constexpr std::array<LightType, 3> priority{
        LightType::Spot, LightType::Directional, LightType::Sky};
    for (LightType type : priority) {
        for (std::size_t i = 0; i < lights.size(); ++i) {
            if (lights[i].properties.type == type && IsEnabled(lights[i])) {
                return i;
            }
        }
    }
    return std::nullopt;
}

inline std::optional<Projection> BuildProjection(
    const RenderLight& light, const glm::vec3& cameraPosition,
    int resolution = Resolution,
    float directionalHalfExtent = DirectionalHalfExtent) {
    if (!SupportsShadowCasting(light.properties.type) ||
        !IsValidLightProperties(light.properties) || resolution <= 0 ||
        !std::isfinite(directionalHalfExtent) ||
        directionalHalfExtent <= 0.0f ||
        !std::isfinite(cameraPosition.x) ||
        !std::isfinite(cameraPosition.y) ||
        !std::isfinite(cameraPosition.z) ||
        !std::isfinite(light.position.x) ||
        !std::isfinite(light.position.y) ||
        !std::isfinite(light.position.z)) {
        return std::nullopt;
    }

    const float directionLength = glm::length(light.emissionDirection);
    if (!std::isfinite(directionLength) || directionLength <= 0.000001f) {
        return std::nullopt;
    }
    const glm::vec3 emissionDirection =
        light.emissionDirection / directionLength;
    const glm::vec3 worldUp(0.0f, 1.0f, 0.0f);
    Projection projection;
    projection.type = light.properties.type;
    projection.lightPosition = light.position;
    projection.directionToLight = -emissionDirection;

    if (light.properties.type == LightType::Spot) {
        projection.perspective = true;
        projection.nearPlane =
            std::min(MaximumSpotNearPlane, light.properties.range * 0.05f);
        projection.farPlane = light.properties.range;
        projection.verticalFieldOfViewDegrees =
            2.0f * light.properties.outerConeAngleDegrees;
        projection.tangentHalfFieldOfView = std::tan(glm::radians(
            light.properties.outerConeAngleDegrees));
        projection.orthographicWorldTexelSize = 0.0f;

        const glm::vec3 up =
            std::abs(glm::dot(emissionDirection, worldUp)) > 0.99f
                ? glm::vec3(0.0f, 0.0f, 1.0f)
                : worldUp;
        const glm::mat4 lightView =
            glm::lookAt(light.position, light.position + emissionDirection,
                        up);
        const glm::mat4 lightProjection = glm::perspective(
            glm::radians(projection.verticalFieldOfViewDegrees), 1.0f,
            projection.nearPlane, projection.farPlane);
        projection.viewProjection = lightProjection * lightView;
        return projection;
    }

    const glm::vec3 lightForward = emissionDirection;
    const glm::vec3 shadowUp =
        std::abs(glm::dot(lightForward, worldUp)) > 0.99f
            ? glm::vec3(0.0f, 0.0f, 1.0f)
            : worldUp;
    const glm::vec3 lightRight =
        glm::normalize(glm::cross(lightForward, shadowUp));
    const glm::vec3 lightUp = glm::normalize(
        glm::cross(lightRight, lightForward));
    const float texelWorldSize =
        (2.0f * directionalHalfExtent) / static_cast<float>(resolution);
    glm::vec3 shadowCenter = cameraPosition;
    const float centerRight = glm::dot(shadowCenter, lightRight);
    const float centerUp = glm::dot(shadowCenter, lightUp);
    shadowCenter +=
        lightRight *
            (std::round(centerRight / texelWorldSize) * texelWorldSize -
             centerRight) +
        lightUp *
            (std::round(centerUp / texelWorldSize) * texelWorldSize -
             centerUp);
    const glm::mat4 lightView = glm::lookAt(
        shadowCenter + projection.directionToLight * DirectionalEyeDistance,
        shadowCenter, shadowUp);
    const glm::mat4 lightProjection =
        glm::ortho(-directionalHalfExtent, directionalHalfExtent,
                   -directionalHalfExtent, directionalHalfExtent,
                   projection.nearPlane, projection.farPlane);
    projection.orthographicWorldTexelSize = texelWorldSize;
    projection.viewProjection = lightProjection * lightView;
    return projection;
}

} // namespace ShadowMapping

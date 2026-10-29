#pragma once

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

#include <cmath>
#include <cstddef>
#include <string_view>

inline constexpr std::size_t MaxSceneLights = 32;

enum class LightType : int {
    Directional = 0,
    Spot = 1,
    Point = 2,
    Sky = 3,
    Rect = 4
};

struct LightProperties {
    LightType type = LightType::Directional;
    glm::vec3 color{1.0f};
    float intensity = 4.0f;
    float range = 20.0f;
    float innerConeAngleDegrees = 15.0f;
    float outerConeAngleDegrees = 30.0f;
    glm::vec2 rectSize{3.0f, 2.0f};
};

inline constexpr bool IsValidLightType(LightType type) noexcept {
    switch (type) {
    case LightType::Directional:
    case LightType::Spot:
    case LightType::Point:
    case LightType::Sky:
    case LightType::Rect:
        return true;
    }
    return false;
}

inline constexpr std::string_view LightTypeName(LightType type) noexcept {
    switch (type) {
    case LightType::Directional:
        return "Directional Light";
    case LightType::Spot:
        return "Spot Light";
    case LightType::Point:
        return "Point Light";
    case LightType::Sky:
        return "Sky Light";
    case LightType::Rect:
        return "Rect Light";
    }
    return "Unknown Light";
}

inline constexpr std::string_view LightIconFilename(LightType type) noexcept {
    switch (type) {
    case LightType::Directional:
        return "directional_light.png";
    case LightType::Spot:
        return "spot_light.png";
    case LightType::Point:
        return "point_light.png";
    case LightType::Sky:
        return "sky_light.png";
    case LightType::Rect:
        return "rect_light.png";
    }
    return {};
}

inline constexpr bool IsLightRotatable(LightType type) noexcept {
    return type == LightType::Directional || type == LightType::Spot ||
           type == LightType::Rect;
}

inline constexpr bool LightHasFiniteRange(LightType type) noexcept {
    return type == LightType::Point || type == LightType::Spot ||
           type == LightType::Rect;
}

inline LightProperties DefaultLightProperties(LightType type) {
    LightProperties properties;
    properties.type = type;
    switch (type) {
    case LightType::Directional:
        properties.intensity = 4.0f;
        break;
    case LightType::Spot:
        properties.intensity = 180.0f;
        properties.range = 25.0f;
        properties.innerConeAngleDegrees = 15.0f;
        properties.outerConeAngleDegrees = 30.0f;
        break;
    case LightType::Point:
        properties.intensity = 180.0f;
        properties.range = 25.0f;
        break;
    case LightType::Sky:
        properties.intensity = 0.8f;
        break;
    case LightType::Rect:
        properties.intensity = 25.0f;
        properties.range = 20.0f;
        properties.rectSize = {3.0f, 2.0f};
        break;
    }
    return properties;
}

inline bool IsValidLightProperties(const LightProperties& properties) {
    if (!IsValidLightType(properties.type) ||
        !std::isfinite(properties.color.x) ||
        !std::isfinite(properties.color.y) ||
        !std::isfinite(properties.color.z) ||
        properties.color.x < 0.0f || properties.color.x > 1.0f ||
        properties.color.y < 0.0f || properties.color.y > 1.0f ||
        properties.color.z < 0.0f || properties.color.z > 1.0f ||
        !std::isfinite(properties.intensity) || properties.intensity < 0.0f ||
        properties.intensity > 10000.0f) {
        return false;
    }

    if (LightHasFiniteRange(properties.type) &&
        (!std::isfinite(properties.range) || properties.range < 0.1f ||
         properties.range > 1000.0f)) {
        return false;
    }
    if (properties.type == LightType::Spot &&
        (!std::isfinite(properties.innerConeAngleDegrees) ||
         !std::isfinite(properties.outerConeAngleDegrees) ||
         properties.innerConeAngleDegrees < 0.0f ||
         properties.outerConeAngleDegrees < 1.0f ||
         properties.outerConeAngleDegrees > 89.0f ||
         properties.innerConeAngleDegrees >
             properties.outerConeAngleDegrees - 0.1f)) {
        return false;
    }
    if (properties.type == LightType::Rect &&
        (!std::isfinite(properties.rectSize.x) ||
         !std::isfinite(properties.rectSize.y) ||
         properties.rectSize.x < 0.1f || properties.rectSize.y < 0.1f ||
         properties.rectSize.x > 1000.0f ||
         properties.rectSize.y > 1000.0f)) {
        return false;
    }
    return true;
}

struct RenderLight {
    LightProperties properties;
    glm::vec3 position{0.0f};
    glm::vec3 emissionDirection{0.0f, 0.0f, -1.0f};
    glm::vec3 rectHalfAxisX{0.0f};
    glm::vec3 rectHalfAxisY{0.0f};
};

inline RenderLight BuildRenderLight(const LightProperties& properties,
                                    const glm::vec3& position,
                                    const glm::vec3& rotation) {
    RenderLight light;
    light.properties = properties;
    light.position = position;
    if (properties.type == LightType::Sky) {
        light.emissionDirection = {0.0f, -1.0f, 0.0f};
        return light;
    }

    glm::mat4 rotationMatrix(1.0f);
    rotationMatrix =
        glm::rotate(rotationMatrix, rotation.x, {1.0f, 0.0f, 0.0f});
    rotationMatrix =
        glm::rotate(rotationMatrix, rotation.y, {0.0f, 1.0f, 0.0f});
    rotationMatrix =
        glm::rotate(rotationMatrix, rotation.z, {0.0f, 0.0f, 1.0f});
    const glm::mat3 orientation(rotationMatrix);
    light.emissionDirection =
        glm::normalize(orientation * glm::vec3(0.0f, 0.0f, -1.0f));
    if (properties.type == LightType::Rect) {
        light.rectHalfAxisX =
            orientation * glm::vec3(properties.rectSize.x * 0.5f, 0.0f, 0.0f);
        light.rectHalfAxisY =
            orientation * glm::vec3(0.0f, properties.rectSize.y * 0.5f, 0.0f);
    }
    return light;
}

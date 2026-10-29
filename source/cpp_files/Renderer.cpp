#include <algorithm>
#include <array>
#include <cmath>
#include <glad/glad.h>
#include <GLFW/glfw3.h>
#include "Renderer.hpp"
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <stb_image.h>

#include <filesystem>
#include <limits>
#include <stdexcept>
#include <string>
#include <vector>

#ifndef AQUARIUS_LIGHT_ICON_DIRECTORY
#define AQUARIUS_LIGHT_ICON_DIRECTORY "source/graphics/light_icons"
#endif

namespace {

GLuint CompileShader(GLenum type, const char* source) {
    const GLuint shader = glCreateShader(type);
    glShaderSource(shader, 1, &source, nullptr);
    glCompileShader(shader);
    GLint success = GL_FALSE;
    glGetShaderiv(shader, GL_COMPILE_STATUS, &success);
    if (success == GL_FALSE) {
        GLint length = 0;
        glGetShaderiv(shader, GL_INFO_LOG_LENGTH, &length);
        std::string log(static_cast<std::size_t>(length), '\0');
        glGetShaderInfoLog(shader, length, nullptr, log.data());
        glDeleteShader(shader);
        throw std::runtime_error("Shader compilation failed: " + log);
    }
    return shader;
}

GLuint CreateLightIconProgram() {
    constexpr const char* vertexShaderSource = R"(
        #version 460 core
        uniform mat4 uViewProjection;
        uniform vec3 uCenter;
        uniform vec3 uCameraRight;
        uniform vec3 uCameraUp;
        out vec2 vTextureCoordinates;
        void main() {
            const vec2 positions[6] = vec2[6](
                vec2(-0.5, -0.5), vec2(0.5, -0.5), vec2(0.5, 0.5),
                vec2(-0.5, -0.5), vec2(0.5, 0.5), vec2(-0.5, 0.5));
            vec2 position = positions[gl_VertexID];
            vec3 worldPosition =
                uCenter + uCameraRight * position.x * 1.25 +
                uCameraUp * position.y * 1.25;
            vTextureCoordinates =
                vec2(position.x + 0.5, 0.5 - position.y);
            gl_Position =
                uViewProjection * vec4(worldPosition, 1.0);
        }
    )";
    constexpr const char* fragmentShaderSource = R"(
        #version 460 core
        in vec2 vTextureCoordinates;
        uniform sampler2D uIconTexture;
        uniform int uSelected;
        out vec4 FragColor;
        void main() {
            vec4 iconColor = texture(uIconTexture, vTextureCoordinates);
            if (iconColor.a < 0.01) {
                discard;
            }
            if (uSelected != 0) {
                iconColor.rgb =
                    mix(iconColor.rgb, vec3(0.45, 0.72, 1.0), 0.25);
            }
            FragColor = iconColor;
        }
    )";
    const GLuint vertexShader =
        CompileShader(GL_VERTEX_SHADER, vertexShaderSource);
    const GLuint fragmentShader =
        CompileShader(GL_FRAGMENT_SHADER, fragmentShaderSource);
    const GLuint program = glCreateProgram();
    glAttachShader(program, vertexShader);
    glAttachShader(program, fragmentShader);
    glLinkProgram(program);
    GLint success = GL_FALSE;
    glGetProgramiv(program, GL_LINK_STATUS, &success);
    glDeleteShader(vertexShader);
    glDeleteShader(fragmentShader);
    if (success == GL_FALSE) {
        GLint length = 0;
        glGetProgramiv(program, GL_INFO_LOG_LENGTH, &length);
        std::string log(static_cast<std::size_t>(length), '\0');
        glGetProgramInfoLog(program, length, nullptr, log.data());
        glDeleteProgram(program);
        throw std::runtime_error("Light icon shader program linking failed: " +
                                 log);
    }
    return program;
}

GLuint CreatePbrProgram() {
    constexpr const char* vertexShaderSource = R"(
    #version 460 core
    layout (location = 0) in vec3 aPosition;
    layout (location = 1) in vec3 aNormal;
    layout (location = 2) in vec2 aTextureCoordinates;
    uniform mat4 uViewProjection;
    uniform mat4 uLightViewProjection;
    uniform mat4 uModel;
    out vec3 vWorldPosition;
    out vec3 vNormal;
    out vec2 vTextureCoordinates;
    out vec4 vLightPosition;
    void main() {
        vec4 worldPosition = uModel * vec4(aPosition, 1.0);
        vWorldPosition = worldPosition.xyz;
        vNormal = mat3(transpose(inverse(uModel))) * aNormal;
        vTextureCoordinates = aTextureCoordinates;
        vLightPosition = uLightViewProjection * worldPosition;
        gl_Position = uViewProjection * worldPosition;
    })";

    constexpr const char* fragmentShaderSource = R"(
        #version 460 core
        out vec4 FragColor;
        in vec3 vWorldPosition;
        in vec3 vNormal;
        in vec2 vTextureCoordinates;
        in vec4 vLightPosition;
        uniform sampler2D uDiffuseMap;
        uniform sampler2D uNormalMap;
        uniform sampler2D uRoughnessMap;
        uniform sampler2D uMetallicMap;
        uniform sampler2DShadow uShadowMap;
        uniform int uViewMode;
        uniform vec3 uCameraPosition;
        uniform int uLightCount;
        uniform int uShadowLightIndex;
        uniform int uShadowPerspective;
        uniform float uShadowNearPlane;
        uniform float uShadowFarPlane;
        uniform float uShadowTanHalfFov;
        uniform float uShadowOrthographicWorldTexelSize;
        uniform int uLightTypes[32];
        uniform vec3 uLightPositions[32];
        uniform vec3 uLightDirections[32];
        uniform vec3 uLightColors[32];
        uniform float uLightIntensities[32];
        uniform float uLightRanges[32];
        uniform float uLightInnerConeCosines[32];
        uniform float uLightOuterConeCosines[32];
        uniform vec3 uRectHalfAxisXs[32];
        uniform vec3 uRectHalfAxisYs[32];
        uniform vec3 uMaterialTiling;
        uniform vec3 uBaseColorFactor;
        uniform float uRoughnessFactor;
        uniform float uMetallicFactor;
        const float PI = 3.14159265359;
        float CalculateShadow(vec4 lightPosition, vec3 normal,
                              vec3 directionToLight) {
            if (lightPosition.w <= 0.0) {
                return 0.0;
            }
            vec3 projected = lightPosition.xyz / lightPosition.w;
            projected = projected * 0.5 + 0.5;
            if (projected.z < 0.0 || projected.z > 1.0 ||
                projected.x < 0.0 ||
                projected.x > 1.0 || projected.y < 0.0 ||
                projected.y > 1.0) {
                return 0.0;
            }
            float lightAngle =
                max(dot(normalize(normal), normalize(directionToLight)), 0.0);
            vec2 texelSize = 1.0 / vec2(textureSize(uShadowMap, 0));
            float worldTexelSize =
                uShadowPerspective != 0
                    ? 2.0 * lightPosition.w * uShadowTanHalfFov *
                          texelSize.x
                    : uShadowOrthographicWorldTexelSize;
            float worldBias =
                worldTexelSize * mix(1.0, 0.25, lightAngle);
            float depthRange =
                max(uShadowFarPlane - uShadowNearPlane, 0.000001);
            float lightDepth = max(lightPosition.w, uShadowNearPlane);
            float depthScale =
                uShadowPerspective != 0
                    ? uShadowNearPlane * uShadowFarPlane /
                          (depthRange * lightDepth * lightDepth)
                    : 1.0 / depthRange;
            float bias =
                max(worldBias * depthScale, 1.0 / 16777215.0);
            float visibility = 0.0;
            for (int x = -2; x <= 2; ++x) {
                for (int y = -2; y <= 2; ++y) {
                    visibility += texture(
                        uShadowMap,
                        vec3(projected.xy + vec2(x, y) * texelSize,
                             projected.z - bias));
                }
            }
            return 1.0 - visibility * (1.0 / 25.0);
        }
        vec3 EvaluateDirectLight(vec3 albedo, float roughness,
                                 float metallic, vec3 normal,
                                 vec3 viewDirection, vec3 lightDirection,
                                 vec3 radiance) {
            float nDotL = max(dot(normal, lightDirection), 0.0);
            float nDotV = max(dot(normal, viewDirection), 0.0);
            if (nDotL <= 0.0 || nDotV <= 0.0) {
                return vec3(0.0);
            }
            vec3 halfwayDirection =
                normalize(viewDirection + lightDirection);
            float nDotH = max(dot(normal, halfwayDirection), 0.0);
            float hDotV = max(dot(halfwayDirection, viewDirection), 0.0);
            float alpha = roughness * roughness;
            float alphaSquared = alpha * alpha;
            float denominator =
                nDotH * nDotH * (alphaSquared - 1.0) + 1.0;
            float distribution =
                alphaSquared /
                max(PI * denominator * denominator, 0.0001);
            float k = (roughness + 1.0) * (roughness + 1.0) / 8.0;
            float geometryView =
                nDotV / (nDotV * (1.0 - k) + k);
            float geometryLight =
                nDotL / (nDotL * (1.0 - k) + k);
            vec3 f0 = mix(vec3(0.04), albedo, metallic);
            vec3 fresnel =
                f0 + (1.0 - f0) * pow(1.0 - hDotV, 5.0);
            vec3 specular =
                distribution * geometryView * geometryLight * fresnel /
                max(4.0 * nDotV * nDotL, 0.001);
            vec3 diffuse =
                (1.0 - fresnel) * (1.0 - metallic) * albedo / PI;
            return (diffuse + specular) * radiance * nDotL;
        }
        void main() {
            if (uViewMode == 2) {
                FragColor = vec4(0.015, 0.045, 0.38, 1.0);
                return;
            }
            vec3 geometricNormal = normalize(vNormal);
            vec3 blendWeights = pow(abs(geometricNormal), vec3(4.0));
            blendWeights /= max(blendWeights.x + blendWeights.y +
                                    blendWeights.z,
                                0.0001);
            vec3 axisSign = sign(geometricNormal);
            vec2 uvX = vWorldPosition.zy * uMaterialTiling.zy;
            vec2 uvY = vWorldPosition.xz * uMaterialTiling.xz;
            vec2 uvZ = vWorldPosition.xy * uMaterialTiling.xy;

            vec3 albedo =
                texture(uDiffuseMap, uvX).rgb * blendWeights.x +
                texture(uDiffuseMap, uvY).rgb * blendWeights.y +
                texture(uDiffuseMap, uvZ).rgb * blendWeights.z;
            albedo *= uBaseColorFactor;
            if (uViewMode == 1) {
                FragColor = vec4(albedo, 1.0);
                return;
            }
            float roughness = clamp(
                (texture(uRoughnessMap, uvX).r * blendWeights.x +
                    texture(uRoughnessMap, uvY).r * blendWeights.y +
                    texture(uRoughnessMap, uvZ).r * blendWeights.z) *
                    uRoughnessFactor,
                0.04, 1.0);
            float metallic = clamp(
                (texture(uMetallicMap, uvX).r * blendWeights.x +
                    texture(uMetallicMap, uvY).r * blendWeights.y +
                    texture(uMetallicMap, uvZ).r * blendWeights.z) *
                    uMetallicFactor,
                0.0, 1.0);
            vec3 mapNormalX =
                texture(uNormalMap, uvX).xyz * 2.0 - 1.0;
            vec3 mapNormalY =
                texture(uNormalMap, uvY).xyz * 2.0 - 1.0;
            vec3 mapNormalZ =
                texture(uNormalMap, uvZ).xyz * 2.0 - 1.0;
            vec3 normalX =
                vec3(axisSign.x * mapNormalX.z, mapNormalX.y,
                     axisSign.x * mapNormalX.x);
            vec3 normalY =
                vec3(mapNormalY.x, axisSign.y * mapNormalY.z,
                     axisSign.y * mapNormalY.y);
            vec3 normalZ =
                vec3(mapNormalZ.x, mapNormalZ.y,
                     axisSign.z * mapNormalZ.z);
            vec3 normal = normalize(normalX * blendWeights.x +
                                    normalY * blendWeights.y +
                                    normalZ * blendWeights.z);
            vec3 viewDirection = normalize(uCameraPosition - vWorldPosition);
            vec3 color = vec3(0.0);
            for (int i = 0; i < uLightCount; ++i) {
                int type = uLightTypes[i];
                vec3 lightColor =
                    uLightColors[i] * uLightIntensities[i];
                if (type == 0 || type == 3) {
                    vec3 lightDirection =
                        normalize(-uLightDirections[i]);
                    float shadow =
                        i == uShadowLightIndex
                            ? CalculateShadow(vLightPosition, geometricNormal,
                                              lightDirection)
                            : 0.0;
                    vec3 lightContribution = EvaluateDirectLight(
                        albedo, roughness, metallic, normal, viewDirection,
                        lightDirection, lightColor);
                    color += lightContribution * (1.0 - shadow);
                } else if (type == 1 || type == 2) {
                    vec3 toLight =
                        uLightPositions[i] - vWorldPosition;
                    float distanceToLight = length(toLight);
                    float range = uLightRanges[i];
                    if (distanceToLight > 0.0001 &&
                        distanceToLight < range) {
                        float normalizedDistance =
                            distanceToLight / range;
                        float rangeFade =
                            pow(max(1.0 -
                                        normalizedDistance *
                                            normalizedDistance *
                                            normalizedDistance *
                                            normalizedDistance,
                                    0.0),
                                2.0);
                        float attenuation =
                            rangeFade /
                            max(distanceToLight * distanceToLight, 0.01);
                        float cone = 1.0;
                        if (type == 1) {
                            float emittedCosine = dot(
                                normalize(uLightDirections[i]),
                                -toLight / distanceToLight);
                            cone = smoothstep(
                                uLightOuterConeCosines[i],
                                uLightInnerConeCosines[i], emittedCosine);
                        }
                        float shadow =
                            type == 1 && i == uShadowLightIndex
                                ? CalculateShadow(
                                      vLightPosition, geometricNormal,
                                      toLight / distanceToLight)
                                : 0.0;
                        vec3 lightContribution = EvaluateDirectLight(
                            albedo, roughness, metallic, normal,
                            viewDirection, toLight / distanceToLight,
                            lightColor * attenuation * cone);
                        color += lightContribution * (1.0 - shadow);
                    }
                } else if (type == 4) {
                    float area = 4.0 * length(uRectHalfAxisXs[i]) *
                                 length(uRectHalfAxisYs[i]);
                    for (int sampleX = 0; sampleX < 2; ++sampleX) {
                        for (int sampleY = 0; sampleY < 2; ++sampleY) {
                            float offsetX = sampleX == 0 ? -0.5 : 0.5;
                            float offsetY = sampleY == 0 ? -0.5 : 0.5;
                            vec3 samplePosition =
                                uLightPositions[i] +
                                uRectHalfAxisXs[i] * offsetX +
                                uRectHalfAxisYs[i] * offsetY;
                            vec3 toSample =
                                samplePosition - vWorldPosition;
                            float distanceToSample = length(toSample);
                            float range = uLightRanges[i];
                            if (distanceToSample <= 0.0001 ||
                                distanceToSample >= range) {
                                continue;
                            }
                            vec3 emitterToSurface =
                                -toSample / distanceToSample;
                            float emitterCosine = max(
                                dot(normalize(uLightDirections[i]),
                                    emitterToSurface),
                                0.0);
                            if (emitterCosine <= 0.0) {
                                continue;
                            }
                            float normalizedDistance =
                                distanceToSample / range;
                            float rangeFade =
                                pow(max(1.0 -
                                            normalizedDistance *
                                                normalizedDistance *
                                                normalizedDistance *
                                                normalizedDistance,
                                        0.0),
                                    2.0);
                            float attenuation =
                                rangeFade /
                                max(distanceToSample * distanceToSample,
                                    0.01);
                            vec3 radiance =
                                lightColor * attenuation * emitterCosine *
                                area * 0.25;
                            color += EvaluateDirectLight(
                                albedo, roughness, metallic, normal,
                                viewDirection, toSample / distanceToSample,
                                radiance);
                        }
                    }
                }
            }
            color = color / (color + vec3(1.0));
            color = pow(color, vec3(1.0 / 2.2));
            FragColor = vec4(color, 1.0);
        }
    )";

    const GLuint vertexShader =
        CompileShader(GL_VERTEX_SHADER, vertexShaderSource);
    const GLuint fragmentShader =
        CompileShader(GL_FRAGMENT_SHADER, fragmentShaderSource);
    const GLuint program = glCreateProgram();
    glAttachShader(program, vertexShader);
    glAttachShader(program, fragmentShader);
    glLinkProgram(program);

    GLint success = GL_FALSE;
    glGetProgramiv(program, GL_LINK_STATUS, &success);
    glDeleteShader(vertexShader);
    glDeleteShader(fragmentShader);
    if (success == GL_FALSE) {
        GLint length = 0;
        glGetProgramiv(program, GL_INFO_LOG_LENGTH, &length);
        std::string log(static_cast<std::size_t>(length), '\0');
        glGetProgramInfoLog(program, length, nullptr, log.data());
        glDeleteProgram(program);
        throw std::runtime_error("Shader program linking failed: " + log);
    }

    glUseProgram(program);
    glUniform1i(glGetUniformLocation(program, "uDiffuseMap"), 0);
    glUniform1i(glGetUniformLocation(program, "uNormalMap"), 1);
    glUniform1i(glGetUniformLocation(program, "uRoughnessMap"), 2);
    glUniform1i(glGetUniformLocation(program, "uMetallicMap"), 3);
    glUniform1i(glGetUniformLocation(program, "uShadowMap"), 5);
    return program;
}

GLuint CreateShadowProgram() {
    constexpr const char* vertexShaderSource = R"(
        #version 460 core
        layout (location = 0) in vec3 aPosition;
        uniform mat4 uLightViewProjection;
        uniform mat4 uModel;
        void main() {
            gl_Position = uLightViewProjection * uModel *
                          vec4(aPosition, 1.0);
        }
    )";
    constexpr const char* fragmentShaderSource = R"(
        #version 460 core
        void main() {}
    )";
    const GLuint vertexShader =
        CompileShader(GL_VERTEX_SHADER, vertexShaderSource);
    const GLuint fragmentShader =
        CompileShader(GL_FRAGMENT_SHADER, fragmentShaderSource);
    const GLuint program = glCreateProgram();
    glAttachShader(program, vertexShader);
    glAttachShader(program, fragmentShader);
    glLinkProgram(program);
    GLint success = GL_FALSE;
    glGetProgramiv(program, GL_LINK_STATUS, &success);
    glDeleteShader(vertexShader);
    glDeleteShader(fragmentShader);
    if (success == GL_FALSE) {
        GLint length = 0;
        glGetProgramiv(program, GL_INFO_LOG_LENGTH, &length);
        std::string log(static_cast<std::size_t>(length), '\0');
        glGetProgramInfoLog(program, length, nullptr, log.data());
        glDeleteProgram(program);
        throw std::runtime_error("Shadow shader program linking failed: " +
                                 log);
    }
    return program;
}

GLuint CreateOutlineProgram() {
    constexpr const char* vertexShaderSource = R"(
        #version 460 core
        layout (location = 0) in vec3 aPosition;
        layout (location = 1) in vec3 aNormal;
        uniform mat4 uViewProjection;
        uniform mat4 uModel;
        uniform float uOutlineWidth;
        void main() {
            vec4 worldPosition = uModel * vec4(aPosition, 1.0);
            vec3 worldNormal =
                normalize(mat3(transpose(inverse(uModel))) * aNormal);
            worldPosition.xyz += worldNormal * uOutlineWidth;
            gl_Position = uViewProjection * worldPosition;
        }
    )";
    constexpr const char* fragmentShaderSource = R"(
        #version 460 core
        out vec4 FragColor;
        void main() {
            FragColor = vec4(0.015, 0.045, 0.38, 1.0);
        }
    )";

    const GLuint vertexShader =
        CompileShader(GL_VERTEX_SHADER, vertexShaderSource);
    const GLuint fragmentShader =
        CompileShader(GL_FRAGMENT_SHADER, fragmentShaderSource);
    const GLuint program = glCreateProgram();
    glAttachShader(program, vertexShader);
    glAttachShader(program, fragmentShader);
    glLinkProgram(program);

    GLint success = GL_FALSE;
    glGetProgramiv(program, GL_LINK_STATUS, &success);
    glDeleteShader(vertexShader);
    glDeleteShader(fragmentShader);
    if (success == GL_FALSE) {
        GLint length = 0;
        glGetProgramiv(program, GL_INFO_LOG_LENGTH, &length);
        std::string log(static_cast<std::size_t>(length), '\0');
        glGetProgramInfoLog(program, length, nullptr, log.data());
        glDeleteProgram(program);
        throw std::runtime_error("Outline shader program linking failed: " +
                                 log);
    }
    return program;
}

GLuint CreateGizmoProgram() {
    constexpr const char* vertexShaderSource = R"(
        #version 460 core
        layout (location = 0) in vec3 aPosition;
        layout (location = 1) in vec3 aColor;
        uniform mat4 uViewProjection;
        out vec3 vColor;
        void main() {
            vColor = aColor;
            gl_Position = uViewProjection * vec4(aPosition, 1.0);
        }
    )";
    constexpr const char* fragmentShaderSource = R"(
        #version 460 core
        in vec3 vColor;
        out vec4 FragColor;
        void main() {
            FragColor = vec4(vColor, 1.0);
        }
    )";
    const GLuint vertexShader =
        CompileShader(GL_VERTEX_SHADER, vertexShaderSource);
    const GLuint fragmentShader =
        CompileShader(GL_FRAGMENT_SHADER, fragmentShaderSource);
    const GLuint program = glCreateProgram();
    glAttachShader(program, vertexShader);
    glAttachShader(program, fragmentShader);
    glLinkProgram(program);
    GLint success = GL_FALSE;
    glGetProgramiv(program, GL_LINK_STATUS, &success);
    glDeleteShader(vertexShader);
    glDeleteShader(fragmentShader);
    if (success == GL_FALSE) {
        GLint length = 0;
        glGetProgramiv(program, GL_INFO_LOG_LENGTH, &length);
        std::string log(static_cast<std::size_t>(length), '\0');
        glGetProgramInfoLog(program, length, nullptr, log.data());
        glDeleteProgram(program);
        throw std::runtime_error("Gizmo shader program linking failed: " +
                                 log);
    }
    return program;
}

GLuint CreateGridProgram() {
    constexpr const char* vertexShaderSource = R"(
        #version 460 core
        void main() {
            vec2 position = gl_VertexID == 0 ? vec2(-1.0, -1.0) :
                            gl_VertexID == 1 ? vec2(3.0, -1.0) :
                                               vec2(-1.0, 3.0);
            gl_Position = vec4(position, 0.0, 1.0);
        }
    )";
    constexpr const char* fragmentShaderSource = R"(
        #version 460 core
        layout(location = 0) out vec4 FragColor;
        uniform mat4 uInverseViewProjection;
        uniform mat4 uViewProjection;
        uniform mat4 uLightViewProjection;
        uniform sampler2DShadow uShadowMap;
        uniform vec3 uLightDirection;
        uniform vec3 uCameraPosition;
        uniform vec2 uViewportSize;
        uniform int uViewMode;
        uniform int uUseShadows;
        uniform int uOrthographic;
        uniform int uShadowPerspective;
        uniform float uShadowNearPlane;
        uniform float uShadowFarPlane;
        uniform float uShadowTanHalfFov;
        uniform float uShadowOrthographicWorldTexelSize;

        float CalculateGroundShadow(vec3 worldPosition) {
            vec4 lightPosition =
                uLightViewProjection * vec4(worldPosition, 1.0);
            if (lightPosition.w <= 0.0) {
                return 0.0;
            }
            vec3 projected = lightPosition.xyz / lightPosition.w;
            projected = projected * 0.5 + 0.5;
            if (projected.z < 0.0 || projected.z > 1.0 ||
                projected.x < 0.0 ||
                projected.x > 1.0 || projected.y < 0.0 ||
                projected.y > 1.0) {
                return 0.0;
            }
            vec2 texelSize = 1.0 / vec2(textureSize(uShadowMap, 0));
            float lightAngle =
                max(dot(vec3(0.0, 1.0, 0.0), normalize(uLightDirection)), 0.0);
            float worldTexelSize =
                uShadowPerspective != 0
                    ? 2.0 * lightPosition.w * uShadowTanHalfFov *
                          texelSize.x
                    : uShadowOrthographicWorldTexelSize;
            float worldBias =
                worldTexelSize * mix(1.0, 0.25, lightAngle);
            float depthRange =
                max(uShadowFarPlane - uShadowNearPlane, 0.000001);
            float lightDepth = max(lightPosition.w, uShadowNearPlane);
            float depthScale =
                uShadowPerspective != 0
                    ? uShadowNearPlane * uShadowFarPlane /
                          (depthRange * lightDepth * lightDepth)
                    : 1.0 / depthRange;
            float bias =
                max(worldBias * depthScale, 1.0 / 16777215.0);
            float visibility = 0.0;
            for (int x = -2; x <= 2; ++x) {
                for (int y = -2; y <= 2; ++y) {
                    visibility += texture(
                        uShadowMap,
                        vec3(projected.xy + vec2(x, y) * texelSize,
                             projected.z - bias));
                }
            }
            return 1.0 - visibility * (1.0 / 25.0);
        }

        void main() {
            vec2 screenPosition = gl_FragCoord.xy / uViewportSize * 2.0 - 1.0;
            vec4 nearPoint = uInverseViewProjection *
                             vec4(screenPosition, -1.0, 1.0);
            vec4 farPoint = uInverseViewProjection *
                            vec4(screenPosition, 1.0, 1.0);
            if (abs(nearPoint.w) < 0.00001 ||
                abs(farPoint.w) < 0.00001) {
                discard;
            }
            nearPoint /= nearPoint.w;
            farPoint /= farPoint.w;
            vec3 rayOrigin = uOrthographic != 0
                                 ? nearPoint.xyz
                                 : uCameraPosition;
            vec3 rayDirection =
                normalize(farPoint.xyz - nearPoint.xyz);
            if (abs(rayDirection.y) < 0.00001) {
                discard;
            }

            float distanceToGround = -rayOrigin.y / rayDirection.y;
            if (distanceToGround <= 0.0 || distanceToGround > 300.0) {
                discard;
            }

            vec3 worldPosition =
                rayOrigin + rayDirection * distanceToGround;
            vec2 gridPosition = worldPosition.xz;
            vec2 gridDerivative = max(fwidth(gridPosition), vec2(0.00001));
            vec2 minorDistance =
                abs(fract(gridPosition - 0.5) - 0.5) / gridDerivative;
            float minorLine =
                1.0 - smoothstep(0.7, 1.4, min(minorDistance.x,
                                               minorDistance.y));

            vec2 majorPosition = gridPosition * 0.1;
            vec2 majorDerivative = max(fwidth(majorPosition), vec2(0.00001));
            vec2 majorDistance =
                abs(fract(majorPosition - 0.5) - 0.5) / majorDerivative;
            float majorLine =
                1.0 - smoothstep(0.7, 1.4, min(majorDistance.x,
                                               majorDistance.y));

            float distanceFade = 1.0 - smoothstep(180.0, 300.0,
                                                   distanceToGround);
            float lineCoverage = max(minorLine * 0.7, majorLine * 0.9);
            float shadow = uViewMode == 0 && uUseShadows != 0
                               ? CalculateGroundShadow(worldPosition)
                               : 0.0;
            vec3 groundColor = vec3(0.0);
            float lineBrightness =
                mix(0.36, 0.45, majorLine) * (1.0 - shadow * 0.62);
            vec3 color = mix(groundColor, vec3(lineBrightness),
                             lineCoverage * distanceFade);
            float alpha = mix(0.08, 0.58, lineCoverage) * distanceFade;
            vec4 groundClip =
                uViewProjection * vec4(worldPosition, 1.0);
            gl_FragDepth = groundClip.z / groundClip.w * 0.5 + 0.5;
            FragColor = vec4(color, alpha);
        }
    )";

    const GLuint vertexShader =
        CompileShader(GL_VERTEX_SHADER, vertexShaderSource);
    const GLuint fragmentShader =
        CompileShader(GL_FRAGMENT_SHADER, fragmentShaderSource);
    const GLuint program = glCreateProgram();
    glAttachShader(program, vertexShader);
    glAttachShader(program, fragmentShader);
    glLinkProgram(program);

    GLint success = GL_FALSE;
    glGetProgramiv(program, GL_LINK_STATUS, &success);
    glDeleteShader(vertexShader);
    glDeleteShader(fragmentShader);
    if (success == GL_FALSE) {
        GLint length = 0;
        glGetProgramiv(program, GL_INFO_LOG_LENGTH, &length);
        std::string log(static_cast<std::size_t>(length), '\0');
        glGetProgramInfoLog(program, length, nullptr, log.data());
        glDeleteProgram(program);
        throw std::runtime_error("Grid shader program linking failed: " + log);
    }
    return program;
}

} // namespace

Renderer::Renderer() {
    try {
        m_ShaderProgram = CreatePbrProgram();
        m_OutlineShaderProgram = CreateOutlineProgram();
        m_ShadowShaderProgram = CreateShadowProgram();
        m_GizmoShaderProgram = CreateGizmoProgram();
        m_GridShaderProgram = CreateGridProgram();
        m_LightIconShaderProgram = CreateLightIconProgram();

        m_LightCountLocation =
            glGetUniformLocation(m_ShaderProgram, "uLightCount");
        m_ShadowLightIndexLocation =
            glGetUniformLocation(m_ShaderProgram, "uShadowLightIndex");
        for (std::size_t i = 0; i < m_LightUniforms.size(); ++i) {
            const std::string index = "[" + std::to_string(i) + "]";
            LightUniformLocations& uniforms = m_LightUniforms[i];
            uniforms.type = glGetUniformLocation(
                m_ShaderProgram, ("uLightTypes" + index).c_str());
            uniforms.position = glGetUniformLocation(
                m_ShaderProgram, ("uLightPositions" + index).c_str());
            uniforms.direction = glGetUniformLocation(
                m_ShaderProgram, ("uLightDirections" + index).c_str());
            uniforms.color = glGetUniformLocation(
                m_ShaderProgram, ("uLightColors" + index).c_str());
            uniforms.intensity = glGetUniformLocation(
                m_ShaderProgram, ("uLightIntensities" + index).c_str());
            uniforms.range = glGetUniformLocation(
                m_ShaderProgram, ("uLightRanges" + index).c_str());
            uniforms.innerConeCosine = glGetUniformLocation(
                m_ShaderProgram, ("uLightInnerConeCosines" + index).c_str());
            uniforms.outerConeCosine = glGetUniformLocation(
                m_ShaderProgram, ("uLightOuterConeCosines" + index).c_str());
            uniforms.rectHalfAxisX = glGetUniformLocation(
                m_ShaderProgram, ("uRectHalfAxisXs" + index).c_str());
            uniforms.rectHalfAxisY = glGetUniformLocation(
                m_ShaderProgram, ("uRectHalfAxisYs" + index).c_str());
        }

        m_LightIconViewProjectionLocation =
            glGetUniformLocation(m_LightIconShaderProgram, "uViewProjection");
        m_LightIconCenterLocation =
            glGetUniformLocation(m_LightIconShaderProgram, "uCenter");
        m_LightIconCameraRightLocation =
            glGetUniformLocation(m_LightIconShaderProgram, "uCameraRight");
        m_LightIconCameraUpLocation =
            glGetUniformLocation(m_LightIconShaderProgram, "uCameraUp");
        m_LightIconTextureLocation =
            glGetUniformLocation(m_LightIconShaderProgram, "uIconTexture");
        m_LightIconSelectedLocation =
            glGetUniformLocation(m_LightIconShaderProgram, "uSelected");
        glUseProgram(m_LightIconShaderProgram);
        glUniform1i(m_LightIconTextureLocation, 0);
        glGenVertexArrays(1, &m_LightIconVertexArray);

        constexpr std::array<LightType, 5> lightTypes{
            LightType::Directional, LightType::Spot, LightType::Point,
            LightType::Sky, LightType::Rect};
        for (LightType type : lightTypes) {
            const std::size_t index = static_cast<std::size_t>(type);
            const std::filesystem::path iconPath =
                std::filesystem::path(AQUARIUS_LIGHT_ICON_DIRECTORY) /
                std::string(LightIconFilename(type));
            int width = 0;
            int height = 0;
            const std::string iconPathString = iconPath.string();
            stbi_uc* pixels =
                stbi_load(iconPathString.c_str(), &width, &height, nullptr, 4);
            if (!pixels || width <= 0 || height <= 0) {
                if (pixels) {
                    stbi_image_free(pixels);
                }
                const char* failureReason = stbi_failure_reason();
                throw std::runtime_error(
                    "Could not load light icon '" + iconPathString +
                    "': " +
                    (failureReason ? failureReason : "unknown image error"));
            }
            glGenTextures(1, &m_LightIconTextures[index]);
            glBindTexture(GL_TEXTURE_2D, m_LightIconTextures[index]);
            glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, width, height, 0,
                         GL_RGBA, GL_UNSIGNED_BYTE, pixels);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
            stbi_image_free(pixels);
        }
        glBindTexture(GL_TEXTURE_2D, 0);

        glGenFramebuffers(1, &m_ShadowFramebuffer);
        glGenTextures(1, &m_ShadowDepthTexture);
        glBindTexture(GL_TEXTURE_2D, m_ShadowDepthTexture);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_DEPTH_COMPONENT24,
                     ShadowMapping::Resolution, ShadowMapping::Resolution, 0,
                     GL_DEPTH_COMPONENT, GL_FLOAT, nullptr);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_BORDER);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_BORDER);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_COMPARE_MODE,
                        GL_COMPARE_REF_TO_TEXTURE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_COMPARE_FUNC, GL_LEQUAL);
        constexpr std::array<float, 4> borderColor{1.0f, 1.0f, 1.0f, 1.0f};
        glTexParameterfv(GL_TEXTURE_2D, GL_TEXTURE_BORDER_COLOR,
                         borderColor.data());
        glBindFramebuffer(GL_FRAMEBUFFER, m_ShadowFramebuffer);
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT,
                               GL_TEXTURE_2D, m_ShadowDepthTexture, 0);
        glDrawBuffer(GL_NONE);
        glReadBuffer(GL_NONE);
        if (glCheckFramebufferStatus(GL_FRAMEBUFFER) !=
            GL_FRAMEBUFFER_COMPLETE) {
            throw std::runtime_error("Shadow framebuffer is incomplete.");
        }
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
        glGenVertexArrays(1, &m_GridVertexArray);
        glGenVertexArrays(1, &m_GizmoVertexArray);
        glGenBuffers(1, &m_GizmoVertexBuffer);
        glBindVertexArray(m_GizmoVertexArray);
        glBindBuffer(GL_ARRAY_BUFFER, m_GizmoVertexBuffer);
        glEnableVertexAttribArray(0);
        glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(float) * 6,
                              nullptr);
        glEnableVertexAttribArray(1);
        glVertexAttribPointer(
            1, 3, GL_FLOAT, GL_FALSE, sizeof(float) * 6,
            reinterpret_cast<void*>(sizeof(float) * 3));
        glBindVertexArray(0);
    } catch (...) {
        if (m_GizmoVertexBuffer != 0) {
            glDeleteBuffers(1, &m_GizmoVertexBuffer);
        }
        if (m_GizmoVertexArray != 0) {
            glDeleteVertexArrays(1, &m_GizmoVertexArray);
        }
        if (m_LightIconVertexArray != 0) {
            glDeleteVertexArrays(1, &m_LightIconVertexArray);
        }
        for (GLuint texture : m_LightIconTextures) {
            if (texture != 0) {
                glDeleteTextures(1, &texture);
            }
        }
        if (m_GridVertexArray != 0) {
            glDeleteVertexArrays(1, &m_GridVertexArray);
        }
        if (m_GridShaderProgram != 0) {
            glDeleteProgram(m_GridShaderProgram);
        }
        if (m_ShaderProgram != 0) {
            glDeleteProgram(m_ShaderProgram);
        }
        if (m_OutlineShaderProgram != 0) {
            glDeleteProgram(m_OutlineShaderProgram);
        }
        if (m_GizmoShaderProgram != 0) {
            glDeleteProgram(m_GizmoShaderProgram);
        }
        if (m_LightIconShaderProgram != 0) {
            glDeleteProgram(m_LightIconShaderProgram);
        }
        if (m_ShadowDepthTexture != 0) {
            glDeleteTextures(1, &m_ShadowDepthTexture);
        }
        if (m_ShadowFramebuffer != 0) {
            glDeleteFramebuffers(1, &m_ShadowFramebuffer);
        }
        if (m_ShadowShaderProgram != 0) {
            glDeleteProgram(m_ShadowShaderProgram);
        }
        throw;
    }
}

Renderer::~Renderer(){
    glDeleteBuffers(1, &m_GizmoVertexBuffer);
    glDeleteVertexArrays(1, &m_GizmoVertexArray);
    glDeleteVertexArrays(1, &m_GridVertexArray);
    glDeleteVertexArrays(1, &m_LightIconVertexArray);
    for (GLuint texture : m_LightIconTextures) {
        if (texture != 0) {
            glDeleteTextures(1, &texture);
        }
    }
    if (m_GridShaderProgram != 0) {
        glDeleteProgram(m_GridShaderProgram);
    }
    if (m_ShaderProgram != 0) {
        glDeleteProgram(m_ShaderProgram);
    }
    if (m_OutlineShaderProgram != 0) {
        glDeleteProgram(m_OutlineShaderProgram);
    }
    if (m_GizmoShaderProgram != 0) {
        glDeleteProgram(m_GizmoShaderProgram);
    }
    if (m_ShadowDepthTexture != 0) {
        glDeleteTextures(1, &m_ShadowDepthTexture);
    }
    if (m_ShadowFramebuffer != 0) {
        glDeleteFramebuffers(1, &m_ShadowFramebuffer);
    }
    if (m_ShadowShaderProgram != 0) {
        glDeleteProgram(m_ShadowShaderProgram);
    }
    if (m_LightIconShaderProgram != 0) {
        glDeleteProgram(m_LightIconShaderProgram);
    }
}

void Renderer::BeginFrame(const Camera& camera,
                          const std::vector<RenderLight>& lights) {
    if (lights.size() > MaxSceneLights) {
        throw std::invalid_argument(
            "Renderer supports at most 32 scene lights.");
    }
    glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    glEnable(GL_DEPTH_TEST);
    int width = 1;
    int height = 1;
    if (GLFWwindow* window = glfwGetCurrentContext()) {
        glfwGetFramebufferSize(window, &width, &height);
    }
    width = width > 0 ? width : 1;
    height = height > 0 ? height : 1;
    glViewport(0, 0, width, height);
    const float aspectRatio =
        static_cast<float>(width) / static_cast<float>(height);
    m_CameraProjection = camera.GetProjection();
    const glm::mat4 projection =
        camera.GetProjectionMatrix(aspectRatio, 0.1f, 500.0f);
    m_CameraPosition = camera.GetPosition();
    m_ViewProjection = projection * camera.GetViewMatrix();
    m_InverseViewProjection = glm::inverse(m_ViewProjection);
    const glm::mat4 inverseView = glm::inverse(camera.GetViewMatrix());
    m_CameraRight = glm::normalize(glm::vec3(inverseView[0]));
    m_CameraUp = glm::normalize(glm::vec3(inverseView[1]));
    m_LightDirection = glm::normalize(glm::vec3(0.45f, 1.0f, 0.35f));
    m_LightCount = static_cast<int>(lights.size());
    m_ShadowLightIndex = -1;
    m_ShadowProjection = ShadowMapping::Projection{};
    if (const auto shadowLightIndex =
            ShadowMapping::FindActiveLightIndex(lights);
        shadowLightIndex) {
        m_ShadowLightIndex = static_cast<int>(*shadowLightIndex);
        const auto projection = ShadowMapping::BuildProjection(
            lights[*shadowLightIndex], m_CameraPosition);
        if (!projection) {
            throw std::invalid_argument(
                "The active shadow-casting light has invalid projection data.");
        }
        m_ShadowProjection = *projection;
        m_LightDirection = m_ShadowProjection.directionToLight;
    }
    glUseProgram(m_ShaderProgram);
    glUniform3f(glGetUniformLocation(m_ShaderProgram, "uCameraPosition"),
                m_CameraPosition.x, m_CameraPosition.y, m_CameraPosition.z);
    glUniform1i(m_LightCountLocation, m_LightCount);
    glUniform1i(m_ShadowLightIndexLocation, m_ShadowLightIndex);
    for (int i = 0; i < m_LightCount; ++i) {
        const RenderLight& light = lights[static_cast<std::size_t>(i)];
        const LightUniformLocations& uniforms =
            m_LightUniforms[static_cast<std::size_t>(i)];
        glUniform1i(uniforms.type,
                    static_cast<GLint>(light.properties.type));
        glUniform3fv(uniforms.position, 1,
                     glm::value_ptr(light.position));
        glUniform3fv(uniforms.direction, 1,
                     glm::value_ptr(light.emissionDirection));
        glUniform3fv(uniforms.color, 1,
                     glm::value_ptr(light.properties.color));
        glUniform1f(uniforms.intensity, light.properties.intensity);
        glUniform1f(uniforms.range, light.properties.range);
        glUniform1f(
            uniforms.innerConeCosine,
            std::cos(glm::radians(
                light.properties.innerConeAngleDegrees)));
        glUniform1f(
            uniforms.outerConeCosine,
            std::cos(glm::radians(
                light.properties.outerConeAngleDegrees)));
        glUniform3fv(uniforms.rectHalfAxisX, 1,
                     glm::value_ptr(light.rectHalfAxisX));
        glUniform3fv(uniforms.rectHalfAxisY, 1,
                     glm::value_ptr(light.rectHalfAxisY));
    }
}

void Renderer::RenderShadowMap(const std::vector<const Model*>& models) {
    if (m_ViewMode != EditorViewMode::Lit || m_ShadowLightIndex < 0) {
        return;
    }

    GLint previousFramebuffer = 0;
    GLint previousViewport[4]{};
    GLint previousDepthFunction = GL_LESS;
    GLint previousCullMode = GL_BACK;
    GLboolean previousDepthWrite = GL_TRUE;
    const GLboolean cullingEnabled = glIsEnabled(GL_CULL_FACE);
    const GLboolean polygonOffsetEnabled = glIsEnabled(GL_POLYGON_OFFSET_FILL);
    glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING, &previousFramebuffer);
    glGetIntegerv(GL_VIEWPORT, previousViewport);
    glGetIntegerv(GL_DEPTH_FUNC, &previousDepthFunction);
    glGetIntegerv(GL_CULL_FACE_MODE, &previousCullMode);
    glGetBooleanv(GL_DEPTH_WRITEMASK, &previousDepthWrite);

    glBindFramebuffer(GL_FRAMEBUFFER, m_ShadowFramebuffer);
    glViewport(0, 0, ShadowMapping::Resolution, ShadowMapping::Resolution);
    glEnable(GL_DEPTH_TEST);
    glDepthFunc(GL_LESS);
    glDepthMask(GL_TRUE);
    glEnable(GL_CULL_FACE);
    glCullFace(GL_BACK);
    glEnable(GL_POLYGON_OFFSET_FILL);
    glPolygonOffset(1.1f, 2.0f);
    glClear(GL_DEPTH_BUFFER_BIT);
    glUseProgram(m_ShadowShaderProgram);
    for (const Model* model : models) {
        if (model) {
            model->DrawDepth(m_ShadowShaderProgram,
                             m_ShadowProjection.viewProjection);
        }
    }

    glDisable(GL_POLYGON_OFFSET_FILL);
    if (polygonOffsetEnabled) {
        glEnable(GL_POLYGON_OFFSET_FILL);
    }
    glCullFace(static_cast<GLenum>(previousCullMode));
    if (!cullingEnabled) {
        glDisable(GL_CULL_FACE);
    }
    glDepthFunc(static_cast<GLenum>(previousDepthFunction));
    glDepthMask(previousDepthWrite);
    glBindFramebuffer(GL_FRAMEBUFFER,
                      static_cast<GLuint>(previousFramebuffer));
    glViewport(previousViewport[0], previousViewport[1], previousViewport[2],
                previousViewport[3]);

    glUseProgram(m_ShaderProgram);
    glUniformMatrix4fv(
        glGetUniformLocation(m_ShaderProgram, "uLightViewProjection"), 1,
        GL_FALSE, &m_ShadowProjection.viewProjection[0][0]);
    glUniform1i(
        glGetUniformLocation(m_ShaderProgram, "uShadowPerspective"),
        m_ShadowProjection.perspective ? 1 : 0);
    glUniform1f(glGetUniformLocation(m_ShaderProgram, "uShadowNearPlane"),
                m_ShadowProjection.nearPlane);
    glUniform1f(glGetUniformLocation(m_ShaderProgram, "uShadowFarPlane"),
                m_ShadowProjection.farPlane);
    glUniform1f(
        glGetUniformLocation(m_ShaderProgram, "uShadowTanHalfFov"),
        m_ShadowProjection.tangentHalfFieldOfView);
    glUniform1f(
        glGetUniformLocation(m_ShaderProgram,
                             "uShadowOrthographicWorldTexelSize"),
        m_ShadowProjection.orthographicWorldTexelSize);
    glActiveTexture(GL_TEXTURE5);
    glBindTexture(GL_TEXTURE_2D, m_ShadowDepthTexture);
    glActiveTexture(GL_TEXTURE0);
}

void Renderer::EndFrame(){

}

void Renderer::DrawGroundGrid() const {
    if (m_ViewMode == EditorViewMode::Wire) {
        return;
    }

    int width = 1;
    int height = 1;
    if (GLFWwindow* window = glfwGetCurrentContext()) {
        glfwGetFramebufferSize(window, &width, &height);
    }
    width = width > 0 ? width : 1;
    height = height > 0 ? height : 1;

    glUseProgram(m_GridShaderProgram);
    glUniformMatrix4fv(
        glGetUniformLocation(m_GridShaderProgram, "uInverseViewProjection"),
        1, GL_FALSE, &m_InverseViewProjection[0][0]);
    glUniformMatrix4fv(
        glGetUniformLocation(m_GridShaderProgram, "uViewProjection"), 1,
        GL_FALSE, &m_ViewProjection[0][0]);
    glUniform3f(glGetUniformLocation(m_GridShaderProgram, "uCameraPosition"),
                m_CameraPosition.x, m_CameraPosition.y, m_CameraPosition.z);
    glUniform2f(glGetUniformLocation(m_GridShaderProgram, "uViewportSize"),
                static_cast<float>(width), static_cast<float>(height));
    glUniformMatrix4fv(
        glGetUniformLocation(m_GridShaderProgram, "uLightViewProjection"), 1,
        GL_FALSE, &m_ShadowProjection.viewProjection[0][0]);
    glUniform3fv(glGetUniformLocation(m_GridShaderProgram, "uLightDirection"),
                 1, &m_LightDirection[0]);
    glUniform1i(glGetUniformLocation(m_GridShaderProgram, "uShadowMap"), 5);
    glUniform1i(glGetUniformLocation(m_GridShaderProgram, "uViewMode"),
                static_cast<GLint>(m_ViewMode));
    glUniform1i(glGetUniformLocation(m_GridShaderProgram, "uUseShadows"),
                m_ShadowLightIndex >= 0 &&
                        (m_ShadowProjection.type == LightType::Directional ||
                         m_ShadowProjection.type == LightType::Sky)
                    ? 1
                    : 0);
    glUniform1i(glGetUniformLocation(m_GridShaderProgram, "uOrthographic"),
                m_CameraProjection == CameraProjection::Orthographic ? 1 : 0);
    glUniform1i(glGetUniformLocation(m_GridShaderProgram, "uShadowPerspective"),
                m_ShadowProjection.perspective ? 1 : 0);
    glUniform1f(glGetUniformLocation(m_GridShaderProgram, "uShadowNearPlane"),
                m_ShadowProjection.nearPlane);
    glUniform1f(glGetUniformLocation(m_GridShaderProgram, "uShadowFarPlane"),
                m_ShadowProjection.farPlane);
    glUniform1f(
        glGetUniformLocation(m_GridShaderProgram, "uShadowTanHalfFov"),
        m_ShadowProjection.tangentHalfFieldOfView);
    glUniform1f(
        glGetUniformLocation(m_GridShaderProgram,
                             "uShadowOrthographicWorldTexelSize"),
        m_ShadowProjection.orthographicWorldTexelSize);
    glActiveTexture(GL_TEXTURE5);
    glBindTexture(GL_TEXTURE_2D, m_ShadowDepthTexture);
    glActiveTexture(GL_TEXTURE0);
    const GLboolean blendEnabled = glIsEnabled(GL_BLEND);
    GLboolean previousDepthWrite = GL_TRUE;
    GLint previousBlendSrcRgb = GL_ONE;
    GLint previousBlendDstRgb = GL_ZERO;
    GLint previousBlendSrcAlpha = GL_ONE;
    GLint previousBlendDstAlpha = GL_ZERO;
    GLint previousBlendEquationRgb = GL_FUNC_ADD;
    GLint previousBlendEquationAlpha = GL_FUNC_ADD;
    glGetBooleanv(GL_DEPTH_WRITEMASK, &previousDepthWrite);
    glGetIntegerv(GL_BLEND_SRC_RGB, &previousBlendSrcRgb);
    glGetIntegerv(GL_BLEND_DST_RGB, &previousBlendDstRgb);
    glGetIntegerv(GL_BLEND_SRC_ALPHA, &previousBlendSrcAlpha);
    glGetIntegerv(GL_BLEND_DST_ALPHA, &previousBlendDstAlpha);
    glGetIntegerv(GL_BLEND_EQUATION_RGB, &previousBlendEquationRgb);
    glGetIntegerv(GL_BLEND_EQUATION_ALPHA, &previousBlendEquationAlpha);
    glEnable(GL_BLEND);
    glBlendEquation(GL_FUNC_ADD);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glDepthMask(GL_FALSE);
    glBindVertexArray(m_GridVertexArray);
    glDrawArrays(GL_TRIANGLES, 0, 3);
    glBindVertexArray(0);
    glDepthMask(previousDepthWrite);
    glBlendEquationSeparate(static_cast<GLenum>(previousBlendEquationRgb),
                            static_cast<GLenum>(previousBlendEquationAlpha));
    glBlendFuncSeparate(static_cast<GLenum>(previousBlendSrcRgb),
                        static_cast<GLenum>(previousBlendDstRgb),
                        static_cast<GLenum>(previousBlendSrcAlpha),
                        static_cast<GLenum>(previousBlendDstAlpha));
    if (blendEnabled) {
        glEnable(GL_BLEND);
    } else {
        glDisable(GL_BLEND);
    }
    glUniform1i(glGetUniformLocation(m_GridShaderProgram, "uShadowMap"), 5);
}

void Renderer::DrawModel(const Model& model, bool selected) const {
    const bool wireMode = m_ViewMode == EditorViewMode::Wire;
    const GLboolean cullingEnabled = glIsEnabled(GL_CULL_FACE);
    GLint previousCullMode = GL_BACK;
    glGetIntegerv(GL_CULL_FACE_MODE, &previousCullMode);
    if (wireMode) {
        glPolygonMode(GL_FRONT_AND_BACK, GL_LINE);
        glDisable(GL_CULL_FACE);
    } else {
        glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);
    }
    glUseProgram(m_ShaderProgram);
    glUniform1i(glGetUniformLocation(m_ShaderProgram, "uViewMode"),
                static_cast<GLint>(m_ViewMode));
    model.Draw(m_ShaderProgram, m_ViewProjection, wireMode);
    if (wireMode) {
        glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);
        glCullFace(static_cast<GLenum>(previousCullMode));
        if (cullingEnabled) {
            glEnable(GL_CULL_FACE);
        } else {
            glDisable(GL_CULL_FACE);
        }
    }
    if (!selected || wireMode) {
        return;
    }

    GLint previousDepthFunction = GL_LESS;
    GLboolean previousDepthWrite = GL_TRUE;
    glGetIntegerv(GL_DEPTH_FUNC, &previousDepthFunction);
    glGetBooleanv(GL_DEPTH_WRITEMASK, &previousDepthWrite);

    glEnable(GL_CULL_FACE);
    glCullFace(GL_FRONT);
    glDepthFunc(GL_LEQUAL);
    glDepthMask(GL_FALSE);
    model.DrawOutline(m_OutlineShaderProgram, m_ViewProjection, 0.025f);

    glDepthMask(previousDepthWrite);
    glDepthFunc(static_cast<GLenum>(previousDepthFunction));
    glCullFace(static_cast<GLenum>(previousCullMode));
    if (!cullingEnabled) {
        glDisable(GL_CULL_FACE);
    }
}

void Renderer::DrawLightIcon(LightType type, const glm::vec3& position,
                             bool selected) const {
    if (!IsValidLightType(type)) {
        return;
    }
    const std::size_t textureIndex = static_cast<std::size_t>(type);
    if (m_LightIconTextures[textureIndex] == 0) {
        return;
    }

    const GLboolean cullingEnabled = glIsEnabled(GL_CULL_FACE);
    const GLboolean blendEnabled = glIsEnabled(GL_BLEND);
    GLboolean previousDepthWrite = GL_TRUE;
    GLint previousBlendSrcRgb = GL_ONE;
    GLint previousBlendDstRgb = GL_ZERO;
    GLint previousBlendSrcAlpha = GL_ONE;
    GLint previousBlendDstAlpha = GL_ZERO;
    GLint previousBlendEquationRgb = GL_FUNC_ADD;
    GLint previousBlendEquationAlpha = GL_FUNC_ADD;
    GLint previousActiveTexture = GL_TEXTURE0;
    GLint previousTexture = 0;
    GLint previousProgram = 0;
    GLint previousVertexArray = 0;
    glGetBooleanv(GL_DEPTH_WRITEMASK, &previousDepthWrite);
    glGetIntegerv(GL_BLEND_SRC_RGB, &previousBlendSrcRgb);
    glGetIntegerv(GL_BLEND_DST_RGB, &previousBlendDstRgb);
    glGetIntegerv(GL_BLEND_SRC_ALPHA, &previousBlendSrcAlpha);
    glGetIntegerv(GL_BLEND_DST_ALPHA, &previousBlendDstAlpha);
    glGetIntegerv(GL_BLEND_EQUATION_RGB, &previousBlendEquationRgb);
    glGetIntegerv(GL_BLEND_EQUATION_ALPHA, &previousBlendEquationAlpha);
    glGetIntegerv(GL_ACTIVE_TEXTURE, &previousActiveTexture);
    glGetIntegerv(GL_CURRENT_PROGRAM, &previousProgram);
    glGetIntegerv(GL_VERTEX_ARRAY_BINDING, &previousVertexArray);
    glActiveTexture(GL_TEXTURE0);
    glGetIntegerv(GL_TEXTURE_BINDING_2D, &previousTexture);

    glUseProgram(m_LightIconShaderProgram);
    glUniformMatrix4fv(m_LightIconViewProjectionLocation, 1, GL_FALSE,
                       &m_ViewProjection[0][0]);
    glUniform3fv(m_LightIconCenterLocation, 1, glm::value_ptr(position));
    glUniform3fv(m_LightIconCameraRightLocation, 1,
                 glm::value_ptr(m_CameraRight));
    glUniform3fv(m_LightIconCameraUpLocation, 1,
                 glm::value_ptr(m_CameraUp));
    glUniform1i(m_LightIconSelectedLocation, selected ? 1 : 0);
    glBindTexture(GL_TEXTURE_2D, m_LightIconTextures[textureIndex]);
    glEnable(GL_BLEND);
    glBlendEquation(GL_FUNC_ADD);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glDisable(GL_CULL_FACE);
    glDepthMask(GL_FALSE);
    glBindVertexArray(m_LightIconVertexArray);
    glDrawArrays(GL_TRIANGLES, 0, 6);
    glBindVertexArray(static_cast<GLuint>(previousVertexArray));

    glDepthMask(previousDepthWrite);
    glBlendEquationSeparate(static_cast<GLenum>(previousBlendEquationRgb),
                            static_cast<GLenum>(previousBlendEquationAlpha));
    glBlendFuncSeparate(static_cast<GLenum>(previousBlendSrcRgb),
                        static_cast<GLenum>(previousBlendDstRgb),
                        static_cast<GLenum>(previousBlendSrcAlpha),
                        static_cast<GLenum>(previousBlendDstAlpha));
    if (!cullingEnabled) {
        glDisable(GL_CULL_FACE);
    } else {
        glEnable(GL_CULL_FACE);
    }
    if (!blendEnabled) {
        glDisable(GL_BLEND);
    } else {
        glEnable(GL_BLEND);
    }
    glBindTexture(GL_TEXTURE_2D, static_cast<GLuint>(previousTexture));
    glActiveTexture(static_cast<GLenum>(previousActiveTexture));
    glUseProgram(static_cast<GLuint>(previousProgram));
}

void Renderer::DrawLightVisualization(const RenderLight& light) const {
    struct EditorLineVertex {
        glm::vec3 position;
        glm::vec3 color;
    };

    std::vector<EditorLineVertex> vertices;
    const auto addLine = [&vertices](const glm::vec3& start,
                                     const glm::vec3& end,
                                     const glm::vec3& color) {
        vertices.push_back({start, color});
        vertices.push_back({end, color});
    };

    const glm::vec3 position = light.position;
    glm::vec3 color{0.85f, 0.75f, 0.2f};
    switch (light.properties.type) {
    case LightType::Point: {
        const float radius = light.properties.range;
        if (!std::isfinite(radius) || radius <= 0.0f) {
            return;
        }
        constexpr int segments = 48;
        constexpr float tau = 6.28318530718f;
        const std::array<glm::vec3, 3> firstAxes{
            glm::vec3(1.0f, 0.0f, 0.0f),
            glm::vec3(1.0f, 0.0f, 0.0f),
            glm::vec3(0.0f, 1.0f, 0.0f)};
        const std::array<glm::vec3, 3> secondAxes{
            glm::vec3(0.0f, 1.0f, 0.0f),
            glm::vec3(0.0f, 0.0f, 1.0f),
            glm::vec3(0.0f, 0.0f, 1.0f)};
        for (std::size_t plane = 0; plane < firstAxes.size(); ++plane) {
            for (int segment = 0; segment < segments; ++segment) {
                const float angleA =
                    tau * static_cast<float>(segment) / segments;
                const float angleB =
                    tau * static_cast<float>(segment + 1) / segments;
                const glm::vec3 pointA =
                    position +
                    radius * (std::cos(angleA) * firstAxes[plane] +
                              std::sin(angleA) * secondAxes[plane]);
                const glm::vec3 pointB =
                    position +
                    radius * (std::cos(angleB) * firstAxes[plane] +
                              std::sin(angleB) * secondAxes[plane]);
                addLine(pointA, pointB, color);
            }
        }
        break;
    }
    case LightType::Spot: {
        const float range = light.properties.range;
        if (!std::isfinite(range) || range <= 0.0f) {
            return;
        }
        color = {1.0f, 0.48f, 0.12f};
        const glm::vec3 direction =
            glm::normalize(light.emissionDirection);
        const glm::vec3 reference =
            std::abs(glm::dot(direction, glm::vec3(0.0f, 1.0f, 0.0f))) >
                    0.95f
                ? glm::vec3(1.0f, 0.0f, 0.0f)
                : glm::vec3(0.0f, 1.0f, 0.0f);
        const glm::vec3 side =
            glm::normalize(glm::cross(direction, reference));
        const glm::vec3 up = glm::normalize(glm::cross(side, direction));
        constexpr int ringSegments = 48;
        constexpr int meridians = 12;
        constexpr float tau = 6.28318530718f;
        const auto addCone = [&](float angleDegrees,
                                 const glm::vec3& coneColor) {
            const float ringRadius =
                range * std::tan(glm::radians(angleDegrees));
            const glm::vec3 endCenter = position + direction * range;
            for (int segment = 0; segment < ringSegments; ++segment) {
                const float angleA =
                    tau * static_cast<float>(segment) / ringSegments;
                const float angleB =
                    tau * static_cast<float>(segment + 1) / ringSegments;
                const glm::vec3 radialA =
                    std::cos(angleA) * side + std::sin(angleA) * up;
                const glm::vec3 radialB =
                    std::cos(angleB) * side + std::sin(angleB) * up;
                addLine(endCenter + radialA * ringRadius,
                        endCenter + radialB * ringRadius, coneColor);
            }
            for (int meridian = 0; meridian < meridians; ++meridian) {
                const float angle =
                    tau * static_cast<float>(meridian) / meridians;
                const glm::vec3 radial =
                    std::cos(angle) * side + std::sin(angle) * up;
                addLine(position,
                        endCenter + radial * ringRadius, coneColor);
            }
        };
        addCone(light.properties.outerConeAngleDegrees, color);
        addCone(light.properties.innerConeAngleDegrees, color * 0.55f);
        break;
    }
    case LightType::Rect: {
        color = {0.82f, 0.38f, 0.96f};
        const glm::vec3& halfAxisX = light.rectHalfAxisX;
        const glm::vec3& halfAxisY = light.rectHalfAxisY;
        const std::array<glm::vec3, 4> corners{
            position - halfAxisX - halfAxisY,
            position + halfAxisX - halfAxisY,
            position + halfAxisX + halfAxisY,
            position - halfAxisX + halfAxisY};
        for (std::size_t corner = 0; corner < corners.size(); ++corner) {
            addLine(corners[corner],
                    corners[(corner + 1) % corners.size()], color);
        }
        break;
    }
    case LightType::Directional:
    case LightType::Sky: {
        color = light.properties.type == LightType::Sky
                    ? glm::vec3(0.28f, 0.9f, 0.72f)
                    : glm::vec3(0.22f, 0.78f, 1.0f);
        const glm::vec3 direction =
            glm::normalize(light.emissionDirection);
        const glm::vec3 reference =
            std::abs(glm::dot(direction, glm::vec3(0.0f, 1.0f, 0.0f))) >
                    0.95f
                ? glm::vec3(1.0f, 0.0f, 0.0f)
                : glm::vec3(0.0f, 1.0f, 0.0f);
        const glm::vec3 side =
            glm::normalize(glm::cross(direction, reference));
        const glm::vec3 end = position + direction * 2.5f;
        const glm::vec3 arrowBase = end - direction * 0.4f;
        addLine(position, end, color);
        addLine(end, arrowBase + side * 0.18f, color);
        addLine(end, arrowBase - side * 0.18f, color);
        break;
    }
    }

    if (vertices.empty()) {
        return;
    }
    GLint previousArrayBuffer = 0;
    GLint previousProgram = 0;
    GLint previousVertexArray = 0;
    GLfloat previousLineWidth = 1.0f;
    GLboolean previousDepthWrite = GL_TRUE;
    const GLboolean depthEnabled = glIsEnabled(GL_DEPTH_TEST);
    glGetIntegerv(GL_ARRAY_BUFFER_BINDING, &previousArrayBuffer);
    glGetIntegerv(GL_CURRENT_PROGRAM, &previousProgram);
    glGetIntegerv(GL_VERTEX_ARRAY_BINDING, &previousVertexArray);
    glGetFloatv(GL_LINE_WIDTH, &previousLineWidth);
    glGetBooleanv(GL_DEPTH_WRITEMASK, &previousDepthWrite);

    glBindVertexArray(m_GizmoVertexArray);
    glBindBuffer(GL_ARRAY_BUFFER, m_GizmoVertexBuffer);
    glBufferData(GL_ARRAY_BUFFER,
                 static_cast<GLsizeiptr>(vertices.size() *
                                         sizeof(EditorLineVertex)),
                 vertices.data(), GL_STREAM_DRAW);
    glUseProgram(m_GizmoShaderProgram);
    glUniformMatrix4fv(
        glGetUniformLocation(m_GizmoShaderProgram, "uViewProjection"), 1,
        GL_FALSE, &m_ViewProjection[0][0]);
    glDisable(GL_DEPTH_TEST);
    glDepthMask(GL_FALSE);
    glLineWidth(2.0f);
    glDrawArrays(GL_LINES, 0, static_cast<GLsizei>(vertices.size()));
    glLineWidth(previousLineWidth);
    glDepthMask(previousDepthWrite);
    if (depthEnabled) {
        glEnable(GL_DEPTH_TEST);
    } else {
        glDisable(GL_DEPTH_TEST);
    }
    glBindBuffer(GL_ARRAY_BUFFER, static_cast<GLuint>(previousArrayBuffer));
    glBindVertexArray(static_cast<GLuint>(previousVertexArray));
    glUseProgram(static_cast<GLuint>(previousProgram));
}

void Renderer::DrawTransformGizmo(const glm::vec3& position,
                                  TransformMode mode) const {
    struct GizmoVertex {
        glm::vec3 position;
        glm::vec3 color;
    };
    if (mode == TransformMode::Select) {
        return;
    }
    const std::array<glm::vec3, 3> axes{
        glm::vec3(1.0f, 0.0f, 0.0f), glm::vec3(0.0f, 1.0f, 0.0f),
        glm::vec3(0.0f, 0.0f, 1.0f)};
    const std::array<glm::vec3, 3> colors{
        glm::vec3(0.95f, 0.16f, 0.12f), glm::vec3(0.18f, 0.85f, 0.24f),
        glm::vec3(0.16f, 0.42f, 1.0f)};
    std::vector<GizmoVertex> lineVertices;
    std::vector<GizmoVertex> triangleVertices;
    if (mode == TransformMode::Rotate) {
        constexpr int ringSegments = 96;
        constexpr float ringRadius = 1.25f;
        constexpr float tau = 6.28318530718f;
        for (std::size_t axisIndex = 0; axisIndex < axes.size(); ++axisIndex) {
            const glm::vec3 first =
                axes[(axisIndex + 1) % axes.size()];
            const glm::vec3 second =
                axes[(axisIndex + 2) % axes.size()];
            for (int segment = 0; segment < ringSegments; ++segment) {
                const float angleA =
                    tau * static_cast<float>(segment) / ringSegments;
                const float angleB =
                    tau * static_cast<float>(segment + 1) / ringSegments;
                lineVertices.push_back(
                    {position + ringRadius *
                                    (std::cos(angleA) * first +
                                     std::sin(angleA) * second),
                     colors[axisIndex]});
                lineVertices.push_back(
                    {position + ringRadius *
                                    (std::cos(angleB) * first +
                                     std::sin(angleB) * second),
                     colors[axisIndex]});
            }
        }
    } else {
        constexpr float shaftLength = 1.05f;
        constexpr float arrowLength = 0.28f;
        constexpr float arrowRadius = 0.09f;
        constexpr int arrowSegments = 10;
        constexpr float tau = 6.28318530718f;
        for (std::size_t axisIndex = 0; axisIndex < axes.size(); ++axisIndex) {
            const glm::vec3 axis = axes[axisIndex];
            const glm::vec3 color = colors[axisIndex];
            const glm::vec3 base = position + axis * shaftLength;
            lineVertices.push_back({position, color});
            lineVertices.push_back({base, color});

            if (mode == TransformMode::Scale) {
                constexpr float halfSize = 0.12f;
                const glm::vec3 center = position + axis * 1.24f;
                const std::array<glm::vec3, 8> corners{
                    center + glm::vec3(-halfSize, -halfSize, -halfSize),
                    center + glm::vec3(halfSize, -halfSize, -halfSize),
                    center + glm::vec3(halfSize, halfSize, -halfSize),
                    center + glm::vec3(-halfSize, halfSize, -halfSize),
                    center + glm::vec3(-halfSize, -halfSize, halfSize),
                    center + glm::vec3(halfSize, -halfSize, halfSize),
                    center + glm::vec3(halfSize, halfSize, halfSize),
                    center + glm::vec3(-halfSize, halfSize, halfSize)};
                constexpr std::array<unsigned int, 36> cubeIndices{
                    0, 2, 1, 0, 3, 2, 4, 5, 6, 4, 6, 7,
                    0, 1, 5, 0, 5, 4, 3, 7, 6, 3, 6, 2,
                    0, 4, 7, 0, 7, 3, 1, 2, 6, 1, 6, 5};
                for (unsigned int index : cubeIndices) {
                    triangleVertices.push_back(
                        {corners[index], color});
                }
                continue;
            }

            const glm::vec3 tip = position + axis * (shaftLength + arrowLength);
            const glm::vec3 reference =
                std::abs(glm::dot(axis, glm::vec3(0.0f, 1.0f, 0.0f))) > 0.9f
                    ? glm::vec3(1.0f, 0.0f, 0.0f)
                    : glm::vec3(0.0f, 1.0f, 0.0f);
            const glm::vec3 side = glm::normalize(glm::cross(axis, reference));
            const glm::vec3 up = glm::normalize(glm::cross(axis, side));
            for (int segment = 0; segment < arrowSegments; ++segment) {
                const float angleA =
                    tau * static_cast<float>(segment) / arrowSegments;
                const float angleB =
                    tau * static_cast<float>(segment + 1) / arrowSegments;
                const glm::vec3 ringA =
                    base + arrowRadius *
                               (std::cos(angleA) * side +
                                std::sin(angleA) * up);
                const glm::vec3 ringB =
                    base + arrowRadius *
                               (std::cos(angleB) * side +
                                std::sin(angleB) * up);
                triangleVertices.push_back({tip, color});
                triangleVertices.push_back({ringA, color});
                triangleVertices.push_back({ringB, color});
            }
        }
    }

    std::vector<GizmoVertex> vertices;
    vertices.reserve(lineVertices.size() + triangleVertices.size());
    vertices.insert(vertices.end(), lineVertices.begin(), lineVertices.end());
    vertices.insert(vertices.end(), triangleVertices.begin(),
                    triangleVertices.end());
    glBindBuffer(GL_ARRAY_BUFFER, m_GizmoVertexBuffer);
    glBufferData(GL_ARRAY_BUFFER,
                 static_cast<GLsizeiptr>(vertices.size() *
                                         sizeof(GizmoVertex)),
                 vertices.data(), GL_STREAM_DRAW);
    glUseProgram(m_GizmoShaderProgram);
    glUniformMatrix4fv(
        glGetUniformLocation(m_GizmoShaderProgram, "uViewProjection"), 1,
        GL_FALSE, &m_ViewProjection[0][0]);
    const GLboolean depthEnabled = glIsEnabled(GL_DEPTH_TEST);
    GLboolean previousDepthWrite = GL_TRUE;
    glGetBooleanv(GL_DEPTH_WRITEMASK, &previousDepthWrite);
    glDisable(GL_DEPTH_TEST);
    glBindVertexArray(m_GizmoVertexArray);
    glLineWidth(3.0f);
    glDrawArrays(GL_LINES, 0, static_cast<GLsizei>(lineVertices.size()));
    if (!triangleVertices.empty()) {
        glDrawArrays(GL_TRIANGLES, static_cast<GLsizei>(lineVertices.size()),
                     static_cast<GLsizei>(triangleVertices.size()));
    }
    glBindVertexArray(0);
    glLineWidth(1.0f);
    glDepthMask(previousDepthWrite);
    if (depthEnabled) {
        glEnable(GL_DEPTH_TEST);
    }
}

bool Renderer::HitTransformGizmo(const glm::vec3& position,
                                 TransformMode mode, double cursorX,
                                 double cursorY, int windowWidth,
                                 int windowHeight, int& axis,
                                 glm::vec2& screenDirection,
                                 float& pixelsPerUnit) const {
    if (windowWidth <= 0 || windowHeight <= 0) {
        return false;
    }
    constexpr float worldLength = 1.33f;
    const std::array<glm::vec3, 3> axes{
        glm::vec3(1.0f, 0.0f, 0.0f), glm::vec3(0.0f, 1.0f, 0.0f),
        glm::vec3(0.0f, 0.0f, 1.0f)};
    const auto project = [&](const glm::vec3& worldPosition,
                             glm::vec2& screenPosition) {
        const glm::vec4 clip =
            m_ViewProjection * glm::vec4(worldPosition, 1.0f);
        if (clip.w <= 0.0f) {
            return false;
        }
        const glm::vec3 ndc = glm::vec3(clip) / clip.w;
        screenPosition = glm::vec2(
            (ndc.x + 1.0f) * 0.5f * static_cast<float>(windowWidth),
            (1.0f - ndc.y) * 0.5f * static_cast<float>(windowHeight));
        return true;
    };

    glm::vec2 center;
    if (!project(position, center)) {
        return false;
    }
    const glm::vec2 cursor(static_cast<float>(cursorX),
                           static_cast<float>(cursorY));
    float closestDistance = 11.0f;
    bool found = false;
    if (mode == TransformMode::Rotate) {
        constexpr int samples = 96;
        constexpr float ringRadius = 1.25f;
        constexpr float tau = 6.28318530718f;
        for (std::size_t axisIndex = 0; axisIndex < axes.size(); ++axisIndex) {
            const glm::vec3 first = axes[(axisIndex + 1) % axes.size()];
            const glm::vec3 second = axes[(axisIndex + 2) % axes.size()];
            for (int sample = 0; sample < samples; ++sample) {
                const float angle =
                    tau * static_cast<float>(sample) / samples;
                const float nextAngle =
                    tau * static_cast<float>(sample + 1) / samples;
                glm::vec2 point;
                glm::vec2 nextPoint;
                if (!project(position + ringRadius *
                                            (std::cos(angle) * first +
                                             std::sin(angle) * second),
                             point) ||
                    !project(position + ringRadius *
                                            (std::cos(nextAngle) * first +
                                             std::sin(nextAngle) * second),
                             nextPoint)) {
                    continue;
                }
                const glm::vec2 segment = nextPoint - point;
                const float segmentLengthSquared =
                    glm::dot(segment, segment);
                if (segmentLengthSquared <= 0.0001f) {
                    continue;
                }
                const float amount = std::clamp(
                    glm::dot(cursor - point, segment) / segmentLengthSquared,
                    0.0f, 1.0f);
                const glm::vec2 nearest = point + segment * amount;
                const float distance = glm::length(cursor - nearest);
                if (distance < closestDistance) {
                    closestDistance = distance;
                    axis = static_cast<int>(axisIndex);
                    screenDirection = glm::normalize(segment);
                    pixelsPerUnit =
                        glm::length(point - center) / ringRadius;
                    found = true;
                }
            }
        }
        if (found) {
            pixelsPerUnit *= ringRadius;
        }
        return found;
    }

    const float handleLength =
        mode == TransformMode::Scale ? 1.36f : worldLength;
    if (mode == TransformMode::Scale) {
        constexpr float cubeHalfSize = 0.12f;
        constexpr float cubeCenterDistance = 1.24f;
        constexpr float hitPadding = 5.0f;
        float closestCubeDistance = std::numeric_limits<float>::max();
        bool cubeHit = false;
        for (std::size_t i = 0; i < axes.size(); ++i) {
            const glm::vec3 cubeCenter =
                position + axes[i] * cubeCenterDistance;
            glm::vec2 minBounds(std::numeric_limits<float>::max());
            glm::vec2 maxBounds(std::numeric_limits<float>::lowest());
            bool projectedAllCorners = true;
            for (int corner = 0; corner < 8; ++corner) {
                const glm::vec3 cornerOffset(
                    (corner & 1) ? cubeHalfSize : -cubeHalfSize,
                    (corner & 2) ? cubeHalfSize : -cubeHalfSize,
                    (corner & 4) ? cubeHalfSize : -cubeHalfSize);
                glm::vec2 screenCorner;
                if (!project(cubeCenter + cornerOffset, screenCorner)) {
                    projectedAllCorners = false;
                    break;
                }
                minBounds = glm::min(minBounds, screenCorner);
                maxBounds = glm::max(maxBounds, screenCorner);
            }
            if (!projectedAllCorners ||
                cursor.x < minBounds.x - hitPadding ||
                cursor.x > maxBounds.x + hitPadding ||
                cursor.y < minBounds.y - hitPadding ||
                cursor.y > maxBounds.y + hitPadding) {
                continue;
            }
            glm::vec2 endpoint;
            if (!project(position + axes[i] * handleLength, endpoint)) {
                continue;
            }
            const glm::vec2 delta = endpoint - center;
            const float projectedLength = glm::length(delta);
            if (projectedLength <= 1.0f) {
                continue;
            }
            const glm::vec2 direction = delta / projectedLength;
            const glm::vec2 projectedCenter =
                center + direction *
                             (projectedLength * cubeCenterDistance /
                              handleLength);
            const float distance = glm::length(cursor - projectedCenter);
            if (distance < closestCubeDistance) {
                closestCubeDistance = distance;
                axis = static_cast<int>(i);
                screenDirection = direction;
                pixelsPerUnit = projectedLength / handleLength;
                cubeHit = true;
            }
        }
        if (cubeHit) {
            return true;
        }
    }
    for (std::size_t i = 0; i < axes.size(); ++i) {
        glm::vec2 endpoint;
        if (!project(position + axes[i] * handleLength, endpoint)) {
            continue;
        }
        const glm::vec2 delta = endpoint - center;
        const float projectedLength = glm::length(delta);
        if (projectedLength <= 1.0f) {
            continue;
        }
        const glm::vec2 direction = delta / projectedLength;
        const float along = glm::dot(cursor - center, direction);
        const float handleStart =
            mode == TransformMode::Scale ? projectedLength - 13.0f : 18.0f;
        if (along < handleStart || along > projectedLength + 9.0f) {
            continue;
        }
        const float distance =
            glm::length(cursor - (center + direction * along));
        if (distance < closestDistance) {
            closestDistance = distance;
            axis = static_cast<int>(i);
            screenDirection = direction;
            pixelsPerUnit = projectedLength / handleLength;
            found = true;
        }
    }
    return found;
}

void Renderer::MakeScreenRay(double cursorX, double cursorY,
                             int windowWidth, int windowHeight,
                             glm::vec3& origin,
                             glm::vec3& direction) const {
    if (windowWidth <= 0 || windowHeight <= 0) {
        direction = glm::vec3(0.0f);
        return;
    }
    const float normalizedX =
        static_cast<float>(2.0 * cursorX / windowWidth - 1.0);
    const float normalizedY =
        static_cast<float>(1.0 - 2.0 * cursorY / windowHeight);
    glm::vec4 nearPoint =
        m_InverseViewProjection * glm::vec4(normalizedX, normalizedY, -1.0f, 1.0f);
    glm::vec4 farPoint =
        m_InverseViewProjection * glm::vec4(normalizedX, normalizedY, 1.0f, 1.0f);
    if (std::abs(nearPoint.w) < 0.000001f ||
        std::abs(farPoint.w) < 0.000001f) {
        direction = glm::vec3(0.0f);
        return;
    }
    nearPoint /= nearPoint.w;
    farPoint /= farPoint.w;
    origin = m_CameraProjection == CameraProjection::Orthographic
                 ? glm::vec3(nearPoint)
                 : m_CameraPosition;
    direction = glm::normalize(glm::vec3(farPoint - nearPoint));
}

#pragma once

#include <glad/glad.h>
#include <glm/glm.hpp>

#include <filesystem>
#include <cstdint>
#include <string>
#include <vector>

struct ModelMaterial {
    std::string name;
    std::filesystem::path asset;
    std::filesystem::path diffuse;
    std::filesystem::path normal;
    std::filesystem::path roughness;
    std::filesystem::path metallic;
    std::filesystem::path ambientOcclusion;
    std::filesystem::path height;
    glm::vec3 textureScale{1.0f};
    float heightScale = 0.1f;
    bool twoSided = true;
};

class Model {
public:
    Model() = default;
    ~Model();

    Model(const Model&) = delete;
    Model& operator=(const Model&) = delete;
    Model(Model&& other) noexcept;
    Model& operator=(Model&& other) noexcept;

    bool LoadFromFile(const std::filesystem::path& path,
                      const ModelMaterial& material,
                      std::string& error);
    bool CreatePlane(float width, float depth, unsigned int segmentsX,
                     unsigned int segmentsZ, std::string& error,
                     const ModelMaterial& material = {});
    void Draw(GLuint shaderProgram, const glm::mat4& viewProjection,
              bool ignoreMaterialCulling = false) const;
    void DrawDepth(GLuint shaderProgram,
                   const glm::mat4& lightViewProjection) const;
    void DrawOutline(GLuint shaderProgram, const glm::mat4& viewProjection,
                     float width) const;
    bool IntersectRay(const glm::vec3& origin, const glm::vec3& direction,
                      float& distance) const;

    glm::vec3 position{0.0f, 0.0f, 0.0f};
    glm::vec3 rotation{0.0f, 0.0f, 0.0f};
    glm::vec3 scale{1.0f, 1.0f, 1.0f};
    glm::vec3 textureScale{1.0f};
    bool twoSided = true;

private:
    struct Mesh {
        GLuint vertexArray = 0;
        GLuint vertexBuffer = 0;
        GLuint indexBuffer = 0;
        GLsizei indexCount = 0;
        GLuint diffuseTexture = 0;
        GLuint normalTexture = 0;
        GLuint roughnessTexture = 0;
        GLuint metallicTexture = 0;
        GLuint ambientOcclusionTexture = 0;
        glm::vec3 baseColorFactor{1.0f};
        float roughnessFactor = 1.0f;
        float metallicFactor = 1.0f;
        std::vector<glm::vec3> positions;
        std::vector<std::uint32_t> indices;
    };

    void Release() noexcept;
    glm::mat4 GetModelMatrix() const;
    std::vector<Mesh> m_Meshes;
};

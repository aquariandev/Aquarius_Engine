#include "Model.hpp"

#include <assimp/Importer.hpp>
#include <assimp/GltfMaterial.h>
#include <assimp/material.h>
#include <assimp/postprocess.h>
#include <assimp/scene.h>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>
#define STB_IMAGE_IMPLEMENTATION
#include <stb_image.h>

#include <algorithm>
#include <array>
#include <cstddef>
#include <limits>
#include <utility>

namespace {

struct Vertex {
    glm::vec3 position;
    glm::vec3 normal;
    glm::vec2 textureCoordinates;
};

struct HeightMap {
    int width = 0;
    int height = 0;
    std::vector<unsigned char> pixels;

    float Sample(const glm::vec2& coordinates) const {
        if (width <= 0 || height <= 0 || pixels.empty()) {
            return 0.5f;
        }
        const float u = coordinates.x - std::floor(coordinates.x);
        const float v = coordinates.y - std::floor(coordinates.y);
        const int x = std::min(static_cast<int>(u * width), width - 1);
        const int y = std::min(static_cast<int>(v * height), height - 1);
        return pixels[static_cast<std::size_t>(y) * width + x] / 255.0f;
    }
};

bool DecodeHeightMap(const std::filesystem::path& path,
                     const aiTexture* embedded, HeightMap& heightMap,
                     std::string& error) {
    int width = 0;
    int height = 0;
    stbi_uc* pixels = nullptr;
    if (embedded && embedded->mHeight == 0) {
        if (!embedded->pcData || embedded->mWidth == 0 ||
            embedded->mWidth >
                static_cast<unsigned int>(std::numeric_limits<int>::max())) {
            error = "Embedded height texture has an invalid size.";
            return false;
        }
        pixels = stbi_load_from_memory(
            reinterpret_cast<const stbi_uc*>(embedded->pcData),
            static_cast<int>(embedded->mWidth), &width, &height, nullptr, 1);
    } else if (embedded) {
        if (!embedded->pcData ||
            embedded->mWidth >
                static_cast<unsigned int>(std::numeric_limits<int>::max()) ||
            embedded->mHeight >
                static_cast<unsigned int>(std::numeric_limits<int>::max()) ||
            static_cast<std::uint64_t>(embedded->mWidth) * embedded->mHeight >
                std::numeric_limits<std::size_t>::max() / 4) {
            error = "Embedded height texture has an invalid size.";
            return false;
        }
        width = static_cast<int>(embedded->mWidth);
        height = static_cast<int>(embedded->mHeight);
        heightMap.pixels.resize(static_cast<std::size_t>(width) * height);
        for (std::size_t i = 0; i < heightMap.pixels.size(); ++i) {
            heightMap.pixels[i] = static_cast<unsigned char>(
                (static_cast<unsigned int>(embedded->pcData[i].r) +
                 embedded->pcData[i].g + embedded->pcData[i].b) /
                3);
        }
    } else {
        pixels = stbi_load(path.string().c_str(), &width, &height, nullptr, 1);
    }

    if (pixels) {
        if (width <= 0 || height <= 0 ||
            static_cast<std::uint64_t>(width) * height >
                std::numeric_limits<std::size_t>::max()) {
            stbi_image_free(pixels);
            error = "Height texture has an invalid size.";
            return false;
        }
        heightMap.pixels.assign(pixels, pixels +
            static_cast<std::size_t>(width) * height);
        stbi_image_free(pixels);
    } else if (heightMap.pixels.empty()) {
        error = "Could not load height texture '" + path.string() + "': " +
                (stbi_failure_reason() ? stbi_failure_reason()
                                       : "unknown image error");
        return false;
    }
    heightMap.width = width;
    heightMap.height = height;
    return true;
}

GLuint CreateFallbackTexture(const std::array<unsigned char, 4>& color) {
    GLuint texture = 0;
    glGenTextures(1, &texture);
    glBindTexture(GL_TEXTURE_2D, texture);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, 1, 1, 0, GL_RGBA,
                 GL_UNSIGNED_BYTE, color.data());
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
    return texture;
}

bool UploadTexture(const stbi_uc* pixels, int width, int height,
                   GLuint& texture, bool srgb) {
    if (!pixels || width <= 0 || height <= 0) {
        return false;
    }
    glGenTextures(1, &texture);
    glBindTexture(GL_TEXTURE_2D, texture);
    glTexImage2D(GL_TEXTURE_2D, 0, srgb ? GL_SRGB8_ALPHA8 : GL_RGBA8, width,
                 height, 0, GL_RGBA, GL_UNSIGNED_BYTE, pixels);
    glGenerateMipmap(GL_TEXTURE_2D);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER,
                    GL_LINEAR_MIPMAP_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
    if (GLAD_GL_EXT_texture_filter_anisotropic) {
        GLfloat maximumAnisotropy = 1.0f;
        glGetFloatv(GL_MAX_TEXTURE_MAX_ANISOTROPY_EXT, &maximumAnisotropy);
        glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_MAX_ANISOTROPY_EXT,
                        std::min(maximumAnisotropy, 8.0f));
    }
    return true;
}

bool LoadTexture(const std::filesystem::path& path, GLuint& texture,
                 std::string& error, bool srgb) {
    int width = 0;
    int height = 0;
    int channels = 0;
    stbi_uc* pixels = stbi_load(path.string().c_str(), &width, &height,
                                &channels, STBI_rgb_alpha);
    if (!pixels) {
        error = "Could not load texture '" + path.string() +
                "': " +
                (stbi_failure_reason() ? stbi_failure_reason()
                                       : "unknown image error");
        return false;
    }

    const bool uploaded = UploadTexture(pixels, width, height, texture, srgb);
    stbi_image_free(pixels);
    return uploaded;
}

bool LoadTexture(const aiTexture* embedded, GLuint& texture, bool srgb) {
    if (!embedded) {
        return false;
    }
    if (embedded->mHeight == 0) {
        int width = 0;
        int height = 0;
        int channels = 0;
        stbi_uc* pixels = stbi_load_from_memory(
            reinterpret_cast<const stbi_uc*>(embedded->pcData),
            static_cast<int>(embedded->mWidth), &width, &height, &channels,
            STBI_rgb_alpha);
        if (!pixels) {
            return false;
        }
        const bool uploaded = UploadTexture(pixels, width, height, texture, srgb);
        stbi_image_free(pixels);
        return uploaded;
    }

    std::vector<stbi_uc> pixels(
        static_cast<std::size_t>(embedded->mWidth) * embedded->mHeight * 4);
    for (std::size_t i = 0;
         i < static_cast<std::size_t>(embedded->mWidth) * embedded->mHeight;
         ++i) {
        pixels[i * 4] = embedded->pcData[i].r;
        pixels[i * 4 + 1] = embedded->pcData[i].g;
        pixels[i * 4 + 2] = embedded->pcData[i].b;
        pixels[i * 4 + 3] = embedded->pcData[i].a;
    }
    return UploadTexture(pixels.data(), static_cast<int>(embedded->mWidth),
                         static_cast<int>(embedded->mHeight), texture, srgb);
}

glm::mat4 ToGlm(const aiMatrix4x4& matrix) {
    glm::mat4 result(1.0f);
    result[0][0] = matrix.a1;
    result[1][0] = matrix.a2;
    result[2][0] = matrix.a3;
    result[3][0] = matrix.a4;
    result[0][1] = matrix.b1;
    result[1][1] = matrix.b2;
    result[2][1] = matrix.b3;
    result[3][1] = matrix.b4;
    result[0][2] = matrix.c1;
    result[1][2] = matrix.c2;
    result[2][2] = matrix.c3;
    result[3][2] = matrix.c4;
    result[0][3] = matrix.d1;
    result[1][3] = matrix.d2;
    result[2][3] = matrix.d3;
    result[3][3] = matrix.d4;
    return result;
}

} // namespace

Model::~Model() {
    Release();
}

Model::Model(Model&& other) noexcept
    : position(other.position),
      rotation(other.rotation),
      scale(other.scale),
      textureScale(other.textureScale),
      twoSided(other.twoSided),
      m_Meshes(std::move(other.m_Meshes)) {
    other.m_Meshes.clear();
}

Model& Model::operator=(Model&& other) noexcept {
    if (this != &other) {
        Release();
        position = other.position;
        rotation = other.rotation;
        scale = other.scale;
        textureScale = other.textureScale;
        twoSided = other.twoSided;
        m_Meshes = std::move(other.m_Meshes);
        other.m_Meshes.clear();
    }
    return *this;
}

void Model::Release() noexcept {
    for (const Mesh& mesh : m_Meshes) {
        glDeleteVertexArrays(1, &mesh.vertexArray);
        glDeleteBuffers(1, &mesh.vertexBuffer);
        glDeleteBuffers(1, &mesh.indexBuffer);
        glDeleteTextures(1, &mesh.diffuseTexture);
        glDeleteTextures(1, &mesh.normalTexture);
        glDeleteTextures(1, &mesh.roughnessTexture);
        glDeleteTextures(1, &mesh.metallicTexture);
        glDeleteTextures(1, &mesh.ambientOcclusionTexture);
    }
    m_Meshes.clear();
}

bool Model::LoadFromFile(const std::filesystem::path& path,
                         const ModelMaterial& material,
                         std::string& error) {
    Release();
    error.clear();
    textureScale = material.textureScale;
    twoSided = material.twoSided;
    if (!std::isfinite(material.heightScale) || material.heightScale < 0.0f ||
        material.heightScale > 10.0f) {
        error = "Height displacement must be between 0 and 10.";
        return false;
    }

    Assimp::Importer importer;
    const aiScene* scene = importer.ReadFile(
        path.string(),
        aiProcess_Triangulate | aiProcess_GenSmoothNormals |
            aiProcess_JoinIdenticalVertices | aiProcess_ImproveCacheLocality |
            aiProcess_FlipUVs);
    if (!scene || !scene->mRootNode ||
        (scene->mFlags & AI_SCENE_FLAGS_INCOMPLETE) != 0) {
        error = "Could not import model '" + path.string() +
                "': " + importer.GetErrorString();
        return false;
    }

    const std::filesystem::path directory = path.parent_path();
    const auto loadMaterialTexture = [&](const aiMaterial* sourceMaterial,
                                         aiTextureType type,
                                         const std::filesystem::path& overridePath,
                                         const std::array<unsigned char, 4>& fallback,
                                         GLuint& texture,
                                         bool srgb,
                                         GLenum redChannel) -> bool {
        std::filesystem::path texturePath = overridePath;
        const aiTexture* embeddedTexture = nullptr;
        aiTextureType importedType = type;
        if (texturePath.empty() && sourceMaterial &&
            sourceMaterial->GetTextureCount(importedType) == 0 &&
            type == aiTextureType_NORMALS &&
            sourceMaterial->GetTextureCount(aiTextureType_HEIGHT) > 0) {
            importedType = aiTextureType_HEIGHT;
        }
        if (texturePath.empty() && sourceMaterial &&
            sourceMaterial->GetTextureCount(importedType) == 0 &&
            type == aiTextureType_DIFFUSE &&
            sourceMaterial->GetTextureCount(aiTextureType_BASE_COLOR) > 0) {
            importedType = aiTextureType_BASE_COLOR;
        }
        if (texturePath.empty() && sourceMaterial &&
            sourceMaterial->GetTextureCount(importedType) > 0) {
            aiString relativePath;
            if (sourceMaterial->GetTexture(importedType, 0, &relativePath) ==
                AI_SUCCESS) {
                embeddedTexture = scene->GetEmbeddedTexture(relativePath.C_Str());
                if (!embeddedTexture) {
                    std::string importedPath = relativePath.C_Str();
                    std::replace(importedPath.begin(), importedPath.end(), '\\',
                                 '/');
                    texturePath = importedPath;
                }
            }
        }

        if (embeddedTexture) {
            if (!LoadTexture(embeddedTexture, texture, srgb)) {
                error = "Could not decode an embedded texture in '" +
                        path.string() + "'.";
                return false;
            }
        } else if (!texturePath.empty()) {
            if (texturePath.is_relative()) {
                texturePath = directory / texturePath;
            }
            if (!LoadTexture(texturePath, texture, error, srgb)) {
                return false;
            }
        } else {
            texture = CreateFallbackTexture(fallback);
        }
        if (redChannel != GL_RED) {
            glBindTexture(GL_TEXTURE_2D, texture);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_SWIZZLE_R, redChannel);
        }
        return true;
    };

    const auto processNode = [&](const auto& self, const aiNode* node,
                                 const glm::mat4& parentTransform) -> bool {
        const glm::mat4 nodeTransform =
            parentTransform * ToGlm(node->mTransformation);
        for (unsigned int nodeMesh = 0; nodeMesh < node->mNumMeshes; ++nodeMesh) {
            const aiMesh* sourceMesh = scene->mMeshes[node->mMeshes[nodeMesh]];
            const aiMaterial* sourceMaterial =
                sourceMesh->mMaterialIndex < scene->mNumMaterials
                    ? scene->mMaterials[sourceMesh->mMaterialIndex]
                    : nullptr;
            HeightMap heightMap;
            const aiTexture* embeddedHeight = nullptr;
            std::filesystem::path heightPath = material.height;
            if (heightPath.empty() && sourceMaterial) {
                aiTextureType heightType = aiTextureType_DISPLACEMENT;
                if (sourceMaterial->GetTextureCount(heightType) == 0) {
                    heightType = aiTextureType_HEIGHT;
                }
                if (sourceMaterial->GetTextureCount(heightType) > 0) {
                    aiString textureReference;
                    if (sourceMaterial->GetTexture(heightType, 0,
                                                   &textureReference) ==
                        AI_SUCCESS) {
                        embeddedHeight =
                            scene->GetEmbeddedTexture(textureReference.C_Str());
                        if (!embeddedHeight) {
                            std::string pathText = textureReference.C_Str();
                            std::replace(pathText.begin(), pathText.end(), '\\',
                                         '/');
                            heightPath = pathText;
                            if (heightPath.is_relative()) {
                                heightPath = directory / heightPath;
                            }
                        }
                    }
                }
            } else if (!heightPath.empty() && heightPath.is_relative()) {
                heightPath = directory / heightPath;
            }
            if (embeddedHeight || !heightPath.empty()) {
                if (!DecodeHeightMap(heightPath, embeddedHeight, heightMap,
                                     error)) {
                    return false;
                }
            }
            std::vector<Vertex> vertices;
            std::vector<unsigned int> indices;
            vertices.reserve(sourceMesh->mNumVertices);
            indices.reserve(sourceMesh->mNumFaces * 3);
            const glm::mat3 normalTransform =
                glm::transpose(glm::inverse(glm::mat3(nodeTransform)));

            for (unsigned int i = 0; i < sourceMesh->mNumVertices; ++i) {
                aiVector3D position = sourceMesh->mVertices[i];
                const aiVector3D& normal = sourceMesh->mNormals[i];
                const aiVector3D textureCoordinates =
                    sourceMesh->HasTextureCoords(0)
                        ? sourceMesh->mTextureCoords[0][i]
                        : aiVector3D(0.0f, 0.0f, 0.0f);
                if (!heightMap.pixels.empty()) {
                    position += normal *
                                ((heightMap.Sample(
                                      {textureCoordinates.x,
                                       textureCoordinates.y}) -
                                  0.5f) *
                                 material.heightScale);
                }
                Vertex vertex;
                vertex.position =
                    glm::vec3(nodeTransform *
                              glm::vec4(position.x, position.y, position.z,
                                        1.0f));
                vertex.normal =
                    glm::normalize(normalTransform *
                                   glm::vec3(normal.x, normal.y, normal.z));
                vertex.textureCoordinates =
                    glm::vec2(textureCoordinates.x, textureCoordinates.y);
                vertices.push_back(vertex);
            }

            for (unsigned int face = 0; face < sourceMesh->mNumFaces; ++face) {
                for (unsigned int index = 0;
                     index < sourceMesh->mFaces[face].mNumIndices; ++index) {
                    indices.push_back(sourceMesh->mFaces[face].mIndices[index]);
                }
            }

            Mesh mesh;
            mesh.indexCount = static_cast<GLsizei>(indices.size());
            mesh.positions.reserve(vertices.size());
            for (const Vertex& vertex : vertices) {
                mesh.positions.push_back(vertex.position);
            }
            mesh.indices.assign(indices.begin(), indices.end());
            glGenVertexArrays(1, &mesh.vertexArray);
            glGenBuffers(1, &mesh.vertexBuffer);
            glGenBuffers(1, &mesh.indexBuffer);
            glBindVertexArray(mesh.vertexArray);
            glBindBuffer(GL_ARRAY_BUFFER, mesh.vertexBuffer);
            glBufferData(GL_ARRAY_BUFFER,
                         static_cast<GLsizeiptr>(vertices.size() *
                                                 sizeof(Vertex)),
                         vertices.data(), GL_STATIC_DRAW);
            glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, mesh.indexBuffer);
            glBufferData(GL_ELEMENT_ARRAY_BUFFER,
                         static_cast<GLsizeiptr>(indices.size() *
                                                 sizeof(unsigned int)),
                         indices.data(), GL_STATIC_DRAW);
            glEnableVertexAttribArray(0);
            glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(Vertex),
                                  reinterpret_cast<void*>(
                                      offsetof(Vertex, position)));
            glEnableVertexAttribArray(1);
            glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, sizeof(Vertex),
                                  reinterpret_cast<void*>(
                                      offsetof(Vertex, normal)));
            glEnableVertexAttribArray(2);
            glVertexAttribPointer(
                2, 2, GL_FLOAT, GL_FALSE, sizeof(Vertex),
                reinterpret_cast<void*>(offsetof(Vertex, textureCoordinates)));
            glBindVertexArray(0);

            const bool hasPackedMetallicRoughness =
                sourceMaterial &&
                sourceMaterial->GetTextureCount(aiTextureType_UNKNOWN) > 0;
            const bool usePackedRoughness =
                hasPackedMetallicRoughness && material.roughness.empty();
            const bool usePackedMetallic =
                hasPackedMetallicRoughness && material.metallic.empty();
            if (sourceMaterial && material.asset.empty() &&
                material.diffuse.empty() && material.normal.empty() &&
                material.roughness.empty() && material.metallic.empty()) {
                aiColor4D baseColor;
                if (sourceMaterial->Get(AI_MATKEY_BASE_COLOR, baseColor) ==
                    AI_SUCCESS) {
                    mesh.baseColorFactor = {
                        baseColor.r, baseColor.g, baseColor.b};
                }
                sourceMaterial->Get(AI_MATKEY_ROUGHNESS_FACTOR,
                                    mesh.roughnessFactor);
                sourceMaterial->Get(AI_MATKEY_METALLIC_FACTOR,
                                    mesh.metallicFactor);
            }
            if (!loadMaterialTexture(sourceMaterial, aiTextureType_DIFFUSE,
                                     material.diffuse, {255, 255, 255, 255},
                                     mesh.diffuseTexture, true, GL_RED) ||
                !loadMaterialTexture(sourceMaterial, aiTextureType_NORMALS,
                                     material.normal, {128, 128, 255, 255},
                                     mesh.normalTexture, false, GL_RED) ||
                !loadMaterialTexture(sourceMaterial,
                                     usePackedRoughness
                                         ? aiTextureType_UNKNOWN
                                         : aiTextureType_DIFFUSE_ROUGHNESS,
                                     material.roughness, {128, 128, 128, 255},
                                     mesh.roughnessTexture, false,
                                     usePackedRoughness ? GL_GREEN : GL_RED) ||
                !loadMaterialTexture(
                    sourceMaterial,
                    usePackedMetallic ? aiTextureType_UNKNOWN
                                      : aiTextureType_METALNESS,
                    material.metallic, {0, 0, 0, 255},
                    mesh.metallicTexture, false,
                    usePackedMetallic ? GL_BLUE : GL_RED) ||
                !loadMaterialTexture(
                    sourceMaterial, aiTextureType_AMBIENT_OCCLUSION,
                    material.ambientOcclusion, {255, 255, 255, 255},
                    mesh.ambientOcclusionTexture, false, GL_RED)) {
                glDeleteVertexArrays(1, &mesh.vertexArray);
                glDeleteBuffers(1, &mesh.vertexBuffer);
                glDeleteBuffers(1, &mesh.indexBuffer);
                glDeleteTextures(1, &mesh.diffuseTexture);
                glDeleteTextures(1, &mesh.normalTexture);
                glDeleteTextures(1, &mesh.roughnessTexture);
                glDeleteTextures(1, &mesh.metallicTexture);
                glDeleteTextures(1, &mesh.ambientOcclusionTexture);
                return false;
            }
            m_Meshes.push_back(mesh);
        }

        for (unsigned int child = 0; child < node->mNumChildren; ++child) {
            if (!self(self, node->mChildren[child], nodeTransform)) {
                return false;
            }
        }
        return true;
    };

    if (!processNode(processNode, scene->mRootNode, glm::mat4(1.0f))) {
        Release();
        return false;
    }
    if (m_Meshes.empty()) {
        error = "The model '" + path.string() + "' contains no meshes.";
        return false;
    }
    return true;
}

bool Model::CreatePlane(float width, float depth, unsigned int segmentsX,
                        unsigned int segmentsZ, std::string& error,
                        const ModelMaterial& material) {
    Release();
    error.clear();
    if (!std::isfinite(width) || !std::isfinite(depth) || width <= 0.0f ||
        depth <= 0.0f || width > 10000.0f || depth > 10000.0f ||
        segmentsX == 0 || segmentsX > 256 || segmentsZ == 0 ||
        segmentsZ > 256 || !std::isfinite(material.heightScale) ||
        material.heightScale < 0.0f || material.heightScale > 10.0f) {
        error = "Landscape dimensions must be between 0 and 10000, with "
                "subdivisions between 1 and 256 and height strength between "
                "0 and 10.";
        return false;
    }

    HeightMap heightMap;
    if (!material.height.empty() &&
        !DecodeHeightMap(material.height, nullptr, heightMap, error)) {
        return false;
    }
    std::vector<Vertex> vertices;
    std::vector<unsigned int> indices;
    const std::size_t vertexCount =
        static_cast<std::size_t>(segmentsX + 1) * (segmentsZ + 1);
    vertices.reserve(vertexCount);
    indices.reserve(static_cast<std::size_t>(segmentsX) * segmentsZ * 6);
    for (unsigned int z = 0; z <= segmentsZ; ++z) {
        const float v = static_cast<float>(z) / segmentsZ;
        for (unsigned int x = 0; x <= segmentsX; ++x) {
            const float u = static_cast<float>(x) / segmentsX;
            const float height =
                heightMap.pixels.empty()
                    ? 0.0f
                    : (heightMap.Sample({u, v}) - 0.5f) *
                          material.heightScale;
            vertices.push_back({
                {-width * 0.5f + u * width, height,
                 -depth * 0.5f + v * depth},
                {0.0f, 1.0f, 0.0f},
                {u, v}});
        }
    }
    const unsigned int rowLength = segmentsX + 1;
    for (unsigned int z = 0; z < segmentsZ; ++z) {
        for (unsigned int x = 0; x < segmentsX; ++x) {
            const unsigned int topLeft = z * rowLength + x;
            const unsigned int topRight = topLeft + 1;
            const unsigned int bottomLeft = topLeft + rowLength;
            const unsigned int bottomRight = bottomLeft + 1;
            indices.insert(indices.end(),
                           {topLeft, bottomLeft, topRight,
                            topRight, bottomLeft, bottomRight});
        }
    }

    Mesh mesh;
    mesh.indexCount = static_cast<GLsizei>(indices.size());
    mesh.positions.reserve(vertices.size());
    for (const Vertex& vertex : vertices) {
        mesh.positions.push_back(vertex.position);
    }
    mesh.indices.assign(indices.begin(), indices.end());
    glGenVertexArrays(1, &mesh.vertexArray);
    glGenBuffers(1, &mesh.vertexBuffer);
    glGenBuffers(1, &mesh.indexBuffer);
    glBindVertexArray(mesh.vertexArray);
    glBindBuffer(GL_ARRAY_BUFFER, mesh.vertexBuffer);
    glBufferData(GL_ARRAY_BUFFER,
                 static_cast<GLsizeiptr>(vertices.size() * sizeof(Vertex)),
                 vertices.data(), GL_STATIC_DRAW);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, mesh.indexBuffer);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER,
                 static_cast<GLsizeiptr>(indices.size() *
                                         sizeof(unsigned int)),
                 indices.data(), GL_STATIC_DRAW);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(Vertex),
                          reinterpret_cast<void*>(offsetof(Vertex, position)));
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, sizeof(Vertex),
                          reinterpret_cast<void*>(offsetof(Vertex, normal)));
    glEnableVertexAttribArray(2);
    glVertexAttribPointer(
        2, 2, GL_FLOAT, GL_FALSE, sizeof(Vertex),
        reinterpret_cast<void*>(offsetof(Vertex, textureCoordinates)));
    glBindVertexArray(0);

    const auto loadMap = [&](const std::filesystem::path& path,
                             const std::array<unsigned char, 4>& fallback,
                             bool srgb, GLenum channel,
                             GLuint& texture) {
        if (path.empty()) {
            texture = CreateFallbackTexture(fallback);
        } else if (!LoadTexture(path, texture, error, srgb)) {
            return false;
        }
        if (texture != 0 && channel != GL_RED) {
            glBindTexture(GL_TEXTURE_2D, texture);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_SWIZZLE_R, channel);
        }
        return texture != 0;
    };
    const bool mapsLoaded =
        loadMap(material.diffuse, {255, 255, 255, 255}, true, GL_RED,
                mesh.diffuseTexture) &&
        loadMap(material.normal, {128, 128, 255, 255}, false, GL_RED,
                mesh.normalTexture) &&
        loadMap(material.roughness, {128, 128, 128, 255}, false, GL_RED,
                mesh.roughnessTexture) &&
        loadMap(material.metallic, {0, 0, 0, 255}, false, GL_RED,
                mesh.metallicTexture) &&
        loadMap(material.ambientOcclusion, {255, 255, 255, 255}, false,
                GL_RED, mesh.ambientOcclusionTexture);
    if (mesh.vertexArray == 0 || mesh.vertexBuffer == 0 ||
        mesh.indexBuffer == 0 || mesh.diffuseTexture == 0 ||
        mesh.normalTexture == 0 || mesh.roughnessTexture == 0 ||
        mesh.metallicTexture == 0 || mesh.ambientOcclusionTexture == 0 ||
        !mapsLoaded || glGetError() != GL_NO_ERROR) {
        glDeleteVertexArrays(1, &mesh.vertexArray);
        glDeleteBuffers(1, &mesh.vertexBuffer);
        glDeleteBuffers(1, &mesh.indexBuffer);
        glDeleteTextures(1, &mesh.diffuseTexture);
        glDeleteTextures(1, &mesh.normalTexture);
        glDeleteTextures(1, &mesh.roughnessTexture);
        glDeleteTextures(1, &mesh.metallicTexture);
        glDeleteTextures(1, &mesh.ambientOcclusionTexture);
        if (error.empty()) {
            error = "OpenGL could not create the landscape mesh.";
        }
        return false;
    }
    m_Meshes.push_back(std::move(mesh));
    textureScale = material.textureScale;
    twoSided = material.twoSided;
    return true;
}

glm::mat4 Model::GetModelMatrix() const {
    glm::mat4 modelMatrix(1.0f);
    modelMatrix = glm::translate(modelMatrix, position);
    modelMatrix = glm::rotate(modelMatrix, rotation.x, {1.0f, 0.0f, 0.0f});
    modelMatrix = glm::rotate(modelMatrix, rotation.y, {0.0f, 1.0f, 0.0f});
    modelMatrix = glm::rotate(modelMatrix, rotation.z, {0.0f, 0.0f, 1.0f});
    modelMatrix = glm::scale(modelMatrix, scale);
    return modelMatrix;
}

void Model::Draw(GLuint shaderProgram, const glm::mat4& viewProjection,
                 bool ignoreMaterialCulling) const {
    const glm::mat4 modelMatrix = GetModelMatrix();

    glUseProgram(shaderProgram);
    glUniformMatrix4fv(glGetUniformLocation(shaderProgram, "uViewProjection"),
                       1, GL_FALSE, glm::value_ptr(viewProjection));
    glUniformMatrix4fv(glGetUniformLocation(shaderProgram, "uModel"), 1,
                       GL_FALSE, glm::value_ptr(modelMatrix));
    glUniform3fv(glGetUniformLocation(shaderProgram, "uMaterialTiling"), 1,
                 glm::value_ptr(textureScale));

    const GLboolean cullingEnabled = glIsEnabled(GL_CULL_FACE);
    GLint previousCullMode = GL_BACK;
    glGetIntegerv(GL_CULL_FACE_MODE, &previousCullMode);
    if (twoSided || ignoreMaterialCulling) {
        glDisable(GL_CULL_FACE);
    } else {
        glEnable(GL_CULL_FACE);
        glCullFace(GL_BACK);
    }
    const std::array<GLuint, 5> textureUnits{
        GL_TEXTURE0, GL_TEXTURE1, GL_TEXTURE2, GL_TEXTURE3, GL_TEXTURE4};
    for (const Mesh& mesh : m_Meshes) {
        glUniform3fv(
            glGetUniformLocation(shaderProgram, "uBaseColorFactor"), 1,
            glm::value_ptr(mesh.baseColorFactor));
        glUniform1f(glGetUniformLocation(shaderProgram, "uRoughnessFactor"),
                    mesh.roughnessFactor);
        glUniform1f(glGetUniformLocation(shaderProgram, "uMetallicFactor"),
                    mesh.metallicFactor);
        const std::array<GLuint, 5> textureIds{
            mesh.diffuseTexture, mesh.normalTexture, mesh.roughnessTexture,
            mesh.metallicTexture, mesh.ambientOcclusionTexture};
        glBindVertexArray(mesh.vertexArray);
        for (std::size_t i = 0; i < textureUnits.size(); ++i) {
            glActiveTexture(textureUnits[i]);
            glBindTexture(GL_TEXTURE_2D, textureIds[i]);
        }
        glDrawElements(GL_TRIANGLES, mesh.indexCount, GL_UNSIGNED_INT,
                       nullptr);
    }
    glBindVertexArray(0);
    glCullFace(static_cast<GLenum>(previousCullMode));
    if (cullingEnabled) {
        glEnable(GL_CULL_FACE);
    } else {
        glDisable(GL_CULL_FACE);
    }
}

void Model::DrawDepth(GLuint shaderProgram,
                      const glm::mat4& lightViewProjection) const {
    const glm::mat4 modelMatrix = GetModelMatrix();
    glUseProgram(shaderProgram);
    glUniformMatrix4fv(
        glGetUniformLocation(shaderProgram, "uLightViewProjection"), 1,
        GL_FALSE, glm::value_ptr(lightViewProjection));
    glUniformMatrix4fv(glGetUniformLocation(shaderProgram, "uModel"), 1,
                       GL_FALSE, glm::value_ptr(modelMatrix));
    const GLboolean cullingEnabled = glIsEnabled(GL_CULL_FACE);
    GLint previousCullMode = GL_BACK;
    glGetIntegerv(GL_CULL_FACE_MODE, &previousCullMode);
    if (twoSided) {
        glDisable(GL_CULL_FACE);
    } else {
        glEnable(GL_CULL_FACE);
        glCullFace(GL_BACK);
    }
    for (const Mesh& mesh : m_Meshes) {
        glBindVertexArray(mesh.vertexArray);
        glDrawElements(GL_TRIANGLES, mesh.indexCount, GL_UNSIGNED_INT,
                       nullptr);
    }
    glBindVertexArray(0);
    glCullFace(static_cast<GLenum>(previousCullMode));
    if (cullingEnabled) {
        glEnable(GL_CULL_FACE);
    } else {
        glDisable(GL_CULL_FACE);
    }
}

void Model::DrawOutline(GLuint shaderProgram,
                        const glm::mat4& viewProjection, float width) const {
    const glm::mat4 modelMatrix = GetModelMatrix();
    glUseProgram(shaderProgram);
    glUniformMatrix4fv(glGetUniformLocation(shaderProgram, "uViewProjection"),
                       1, GL_FALSE, glm::value_ptr(viewProjection));
    glUniformMatrix4fv(glGetUniformLocation(shaderProgram, "uModel"), 1,
                       GL_FALSE, glm::value_ptr(modelMatrix));
    glUniform1f(glGetUniformLocation(shaderProgram, "uOutlineWidth"), width);
    for (const Mesh& mesh : m_Meshes) {
        glBindVertexArray(mesh.vertexArray);
        glDrawElements(GL_TRIANGLES, mesh.indexCount, GL_UNSIGNED_INT,
                       nullptr);
    }
    glBindVertexArray(0);
}

bool Model::IntersectRay(const glm::vec3& origin,
                         const glm::vec3& direction,
                         float& distance) const {
    const glm::mat4 modelMatrix = GetModelMatrix();
    const glm::mat4 inverseModelMatrix = glm::inverse(modelMatrix);
    const glm::vec3 localOrigin =
        glm::vec3(inverseModelMatrix * glm::vec4(origin, 1.0f));
    const glm::vec3 localDirection =
        glm::vec3(inverseModelMatrix * glm::vec4(direction, 0.0f));
    float closestDistance = std::numeric_limits<float>::max();
    bool foundIntersection = false;

    for (const Mesh& mesh : m_Meshes) {
        for (std::size_t i = 0; i + 2 < mesh.indices.size(); i += 3) {
            const glm::vec3& a = mesh.positions[mesh.indices[i]];
            const glm::vec3& b = mesh.positions[mesh.indices[i + 1]];
            const glm::vec3& c = mesh.positions[mesh.indices[i + 2]];
            const glm::vec3 edgeOne = b - a;
            const glm::vec3 edgeTwo = c - a;
            const glm::vec3 crossDirection =
                glm::cross(localDirection, edgeTwo);
            const float determinant = glm::dot(edgeOne, crossDirection);
            if (std::abs(determinant) < 0.000001f) {
                continue;
            }

            const float inverseDeterminant = 1.0f / determinant;
            const glm::vec3 fromVertex = localOrigin - a;
            const float u =
                glm::dot(fromVertex, crossDirection) * inverseDeterminant;
            if (u < 0.0f || u > 1.0f) {
                continue;
            }

            const glm::vec3 crossFromVertex = glm::cross(fromVertex, edgeOne);
            const float v =
                glm::dot(localDirection, crossFromVertex) * inverseDeterminant;
            if (v < 0.0f || u + v > 1.0f) {
                continue;
            }

            const float rayParameter =
                glm::dot(edgeTwo, crossFromVertex) * inverseDeterminant;
            if (rayParameter <= 0.0f) {
                continue;
            }
            const glm::vec3 localHit =
                localOrigin + localDirection * rayParameter;
            const glm::vec3 worldHit =
                glm::vec3(modelMatrix * glm::vec4(localHit, 1.0f));
            const float worldDistance = glm::length(worldHit - origin);
            if (worldDistance < closestDistance) {
                closestDistance = worldDistance;
                foundIntersection = true;
            }
        }
    }

    if (foundIntersection) {
        distance = closestDistance;
    }
    return foundIntersection;
}

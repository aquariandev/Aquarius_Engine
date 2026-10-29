#pragma once

#include "Light.hpp"
#include "Model.hpp"

#include <filesystem>
#include <optional>
#include <string>
#include <vector>

struct EditorSceneModel {
    std::filesystem::path path;
    std::string name;
    ModelMaterial material;
    // Runtime-only recovery data; these fields are not part of AQENSCENE.
    std::filesystem::path originalSerializedMaterialAsset;
    bool materialDescriptorLoadFailed = false;
    glm::vec3 position{0.0f};
    glm::vec3 rotation{0.0f};
    glm::vec3 scale{1.0f};
    bool proceduralLandscape = false;
    float landscapeWidth = 0.0f;
    float landscapeDepth = 0.0f;
    unsigned int landscapeSegmentsX = 0;
    unsigned int landscapeSegmentsZ = 0;
    std::optional<LightProperties> light;
};

struct EditorScene {
    glm::vec3 cameraPosition{0.0f, 2.5f, 5.0f};
    float cameraYaw = -90.0f;
    float cameraPitch = 0.0f;
    std::vector<EditorSceneModel> models;
    std::vector<std::string> warnings;
};

bool LoadEditorScene(const std::filesystem::path& projectRoot,
                     EditorScene& scene, std::string& error);
bool SaveEditorScene(const std::filesystem::path& projectRoot,
                     const EditorScene& scene, std::string& error);
EditorSceneModel MakeEditorSceneLight(LightType type);
bool CreateDefaultEditorScene(const std::filesystem::path& projectRoot,
                              std::string& error);

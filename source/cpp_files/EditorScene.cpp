#include "EditorScene.hpp"
#include "ModelAssets.hpp"

#include <cmath>
#include <fstream>
#include <iomanip>
#include <system_error>

namespace {

constexpr std::size_t maxSceneModels = 10'000;
constexpr unsigned int sceneVersion = 7;

std::filesystem::path StoredPath(const std::filesystem::path& path,
                                 const std::filesystem::path& root) {
    if (path.empty() || !path.is_absolute()) {
        return path;
    }
    const std::filesystem::path relative = path.lexically_relative(root);
    if (!relative.empty() &&
        (relative.begin() == relative.end() ||
         *relative.begin() != std::filesystem::path(".."))) {
        return relative;
    }
    return path;
}

std::filesystem::path ResolvedPath(const std::filesystem::path& path,
                                   const std::filesystem::path& root) {
    return path.empty() || path.is_absolute() ? path : (root / path).lexically_normal();
}

bool ReadVector(std::istream& input, glm::vec3& vector) {
    return static_cast<bool>(input >> vector.x >> vector.y >> vector.z) &&
           std::isfinite(vector.x) && std::isfinite(vector.y) &&
           std::isfinite(vector.z);
}

bool WriteVector(std::ostream& output, const glm::vec3& vector) {
    return static_cast<bool>(output << vector.x << ' ' << vector.y << ' '
                                    << vector.z);
}

bool ReadLight(std::istream& input, EditorSceneModel& model) {
    int typeValue = -1;
    LightProperties properties;
    if (!(input >> typeValue) || typeValue < static_cast<int>(LightType::Directional) ||
        typeValue > static_cast<int>(LightType::Rect)) {
        return false;
    }
    properties.type = static_cast<LightType>(typeValue);
    if (!(input >> std::quoted(model.name)) ||
        !ReadVector(input, model.position) ||
        (IsLightRotatable(properties.type) &&
         !ReadVector(input, model.rotation)) ||
        !ReadVector(input, model.scale) ||
        !ReadVector(input, properties.color) ||
        !(input >> properties.intensity)) {
        return false;
    }

    if (properties.type == LightType::Point) {
        if (!(input >> properties.range)) {
            return false;
        }
    } else if (properties.type == LightType::Spot) {
        if (!(input >> properties.range >> properties.innerConeAngleDegrees >>
              properties.outerConeAngleDegrees)) {
            return false;
        }
    } else if (properties.type == LightType::Rect) {
        if (!(input >> properties.range >> properties.rectSize.x >>
              properties.rectSize.y)) {
            return false;
        }
    }
    if (!IsValidLightProperties(properties)) {
        return false;
    }
    model.light = properties;
    return true;
}

} // namespace

EditorSceneModel MakeEditorSceneLight(LightType type) {
    EditorSceneModel light;
    light.name = std::string(LightTypeName(type));
    light.position = {0.0f, 10.0f, 0.0f};
    light.light = DefaultLightProperties(type);
    if (type == LightType::Directional) {
        light.rotation = {glm::radians(-60.0f), glm::radians(38.0f), 0.0f};
    } else if (type == LightType::Spot || type == LightType::Rect) {
        light.rotation = {glm::radians(-90.0f), 0.0f, 0.0f};
    }
    return light;
}

bool CreateDefaultEditorScene(const std::filesystem::path& projectRoot,
                              std::string& error) {
    EditorScene scene;
    scene.cameraPosition = {0.0f, 5.0f, 20.0f};
    EditorSceneModel ground;
    ground.name = "Ground";
    ground.proceduralLandscape = true;
    ground.landscapeWidth = 32.0f;
    ground.landscapeDepth = 32.0f;
    ground.landscapeSegmentsX = 1;
    ground.landscapeSegmentsZ = 1;
    scene.models.push_back(std::move(ground));
    scene.models.push_back(MakeEditorSceneLight(LightType::Directional));
    return SaveEditorScene(projectRoot, scene, error);
}

bool LoadEditorScene(const std::filesystem::path& projectRoot,
                     EditorScene& scene, std::string& error) {
    error.clear();
    const std::filesystem::path scenePath = projectRoot / "editor.aqenscene";
    std::error_code filesystemError;
    if (!std::filesystem::exists(scenePath, filesystemError)) {
        if (filesystemError) {
            error = "Could not inspect editor scene file: " +
                    filesystemError.message();
            return false;
        }
        scene = {};
        return true;
    }

    std::ifstream input(scenePath);
    if (!input) {
        error = "Could not open editor scene file '" + scenePath.string() + "'.";
        return false;
    }

    std::string magic;
    unsigned int version = 0;
    EditorScene loaded;
    std::size_t modelCount = 0;
    if (!(input >> magic >> version) || magic != "AQENSCENE" ||
        (version < 1 || version > sceneVersion) ||
        !(input >> loaded.cameraPosition.x >> loaded.cameraPosition.y >>
          loaded.cameraPosition.z >> loaded.cameraYaw >> loaded.cameraPitch) ||
        !std::isfinite(loaded.cameraPosition.x) ||
        !std::isfinite(loaded.cameraPosition.y) ||
        !std::isfinite(loaded.cameraPosition.z) ||
        !std::isfinite(loaded.cameraYaw) ||
        !std::isfinite(loaded.cameraPitch) || loaded.cameraPitch < -89.0f ||
        loaded.cameraPitch > 89.0f || !(input >> modelCount) ||
        modelCount > maxSceneModels) {
        error = "Editor scene has an invalid header, camera, or model count.";
        return false;
    }

    loaded.models.reserve(modelCount);
    std::size_t lightCount = 0;
    for (std::size_t i = 0; i < modelCount; ++i) {
        EditorSceneModel model;
        std::string recordType = "MODEL";
        if (version >= 7 && !(input >> recordType)) {
            error = "Editor scene contains invalid data at entry " +
                    std::to_string(i + 1) + ".";
            return false;
        }
        if (recordType == "LIGHT") {
            if (!ReadLight(input, model) || ++lightCount > MaxSceneLights) {
                error = "Editor scene contains invalid light data at entry " +
                        std::to_string(i + 1) + ".";
                return false;
            }
            loaded.models.push_back(std::move(model));
            continue;
        }
        if (recordType != "MODEL") {
            error = "Editor scene contains an unsupported record type at entry " +
                    std::to_string(i + 1) + ".";
            return false;
        }
        std::string modelPath;
        std::string diffusePath;
        std::string normalPath;
        std::string roughnessPath;
        std::string metallicPath;
        std::string ambientOcclusionPath;
        std::string heightPath;
        std::string modelName;
        std::string materialName;
        std::string materialAssetPath;
        int proceduralLandscape = 0;
        if ((version >= 2 && !(input >> std::quoted(modelName))) ||
            (version >= 3 && !(input >> std::quoted(materialName))) ||
            (version >= 4 &&
             !(input >> std::quoted(materialAssetPath))) ||
            !(input >> std::quoted(modelPath) >> std::quoted(diffusePath) >>
              std::quoted(normalPath) >> std::quoted(roughnessPath) >>
              std::quoted(metallicPath)) ||
            (version >= 5 &&
             (!(input >> std::quoted(ambientOcclusionPath) >>
               std::quoted(heightPath) >> model.material.heightScale >>
               model.material.twoSided) ||
              !std::isfinite(model.material.heightScale) ||
              model.material.heightScale < 0.0f ||
              model.material.heightScale > 10.0f)) ||
            !ReadVector(input, model.position) ||
            !ReadVector(input, model.rotation) ||
            !ReadVector(input, model.scale) ||
            (version >= 6 &&
             (!(input >> proceduralLandscape >>
               model.landscapeWidth >> model.landscapeDepth >>
               model.landscapeSegmentsX >> model.landscapeSegmentsZ) ||
              proceduralLandscape < 0 || proceduralLandscape > 1 ||
              (proceduralLandscape == 1 &&
               (!std::isfinite(model.landscapeWidth) ||
                !std::isfinite(model.landscapeDepth) ||
                model.landscapeWidth <= 0.0f ||
                model.landscapeDepth <= 0.0f ||
                model.landscapeWidth > 10000.0f ||
                model.landscapeDepth > 10000.0f ||
                model.landscapeSegmentsX == 0 ||
                model.landscapeSegmentsX > 256 ||
                model.landscapeSegmentsZ == 0 ||
                model.landscapeSegmentsZ > 256)) ||
              (proceduralLandscape == 1 && !modelPath.empty())))) {
            error = "Editor scene contains invalid model data at entry " +
                    std::to_string(i + 1) + ".";
            return false;
        }
        model.proceduralLandscape = proceduralLandscape == 1;
        model.path = ResolvedPath(modelPath, projectRoot);
        model.name = modelName.empty()
                         ? (model.path.empty() ? "Landscape"
                                               : model.path.stem().string())
                         : std::move(modelName);
        model.material.diffuse = ResolvedPath(diffusePath, projectRoot);
        model.material.normal = ResolvedPath(normalPath, projectRoot);
        model.material.roughness = ResolvedPath(roughnessPath, projectRoot);
        model.material.metallic = ResolvedPath(metallicPath, projectRoot);
        model.material.ambientOcclusion =
            ResolvedPath(ambientOcclusionPath, projectRoot);
        model.material.height = ResolvedPath(heightPath, projectRoot);
        model.material.name = std::move(materialName);
        model.originalSerializedMaterialAsset =
            ResolvedPath(materialAssetPath, projectRoot);
        model.material.asset =
            model.originalSerializedMaterialAsset;
        if (version >= 4 && !model.material.asset.empty()) {
            ModelMaterial descriptorMaterial;
            std::string materialError;
            if (LoadAqenMaterial(model.material.asset, descriptorMaterial,
                                 materialError)) {
                model.material = std::move(descriptorMaterial);
            } else {
                loaded.warnings.push_back(
                    "Could not restore material for actor '" + model.name +
                    "' from '" + model.material.asset.string() +
                    "'; using the map paths saved in the scene instead. " +
                    materialError);
                model.materialDescriptorLoadFailed = true;
                if (!IsMissingMaterialTextureError(materialError)) {
                    model.material.asset.clear();
                }
            }
        }
        loaded.models.push_back(std::move(model));
    }

    input >> std::ws;
    if (!input.eof()) {
        error = "Editor scene contains unexpected trailing data.";
        return false;
    }
    scene = std::move(loaded);
    return true;
}

bool SaveEditorScene(const std::filesystem::path& projectRoot,
                     const EditorScene& scene, std::string& error) {
    error.clear();
    if (scene.models.size() > maxSceneModels ||
        !std::isfinite(scene.cameraPosition.x) ||
        !std::isfinite(scene.cameraPosition.y) ||
        !std::isfinite(scene.cameraPosition.z) ||
        !std::isfinite(scene.cameraYaw) || !std::isfinite(scene.cameraPitch)) {
        error = "Editor scene contains invalid camera data or too many models.";
        return false;
    }
    std::size_t lightCount = 0;
    for (const EditorSceneModel& model : scene.models) {
        if (model.light &&
            (!IsValidLightProperties(*model.light) ||
             !model.path.empty() || model.proceduralLandscape ||
             !std::isfinite(model.position.x) ||
             !std::isfinite(model.position.y) ||
             !std::isfinite(model.position.z) ||
             !std::isfinite(model.rotation.x) ||
             !std::isfinite(model.rotation.y) ||
             !std::isfinite(model.rotation.z) ||
             !std::isfinite(model.scale.x) ||
             !std::isfinite(model.scale.y) ||
             !std::isfinite(model.scale.z) ||
             (!IsLightRotatable(model.light->type) &&
              (model.rotation.x != 0.0f || model.rotation.y != 0.0f ||
               model.rotation.z != 0.0f)))) {
            error = "Editor scene contains invalid light data.";
            return false;
        }
        lightCount += model.light.has_value() ? 1U : 0U;
    }
    if (lightCount > MaxSceneLights) {
        error = "Editor scene contains too many lights.";
        return false;
    }

    const std::filesystem::path scenePath = projectRoot / "editor.aqenscene";
    const std::filesystem::path temporaryPath = scenePath.string() + ".tmp";
    std::ofstream output(temporaryPath, std::ios::trunc);
    if (!output) {
        error = "Could not write temporary scene file '" +
                temporaryPath.string() + "'.";
        return false;
    }

    output << "AQENSCENE " << sceneVersion << '\n' << std::setprecision(9)
           << scene.cameraPosition.x << ' ' << scene.cameraPosition.y << ' '
           << scene.cameraPosition.z << ' ' << scene.cameraYaw << ' '
           << scene.cameraPitch << '\n'
           << scene.models.size() << '\n';
    for (const EditorSceneModel& model : scene.models) {
        if (model.light) {
            const LightProperties& light = *model.light;
            output << "LIGHT " << static_cast<int>(light.type) << ' '
                   << std::quoted(model.name) << ' ';
            WriteVector(output, model.position);
            output << ' ';
            if (IsLightRotatable(light.type)) {
                WriteVector(output, model.rotation);
                output << ' ';
            }
            WriteVector(output, model.scale);
            output << ' ';
            WriteVector(output, light.color);
            output << ' ' << light.intensity;
            if (light.type == LightType::Point) {
                output << ' ' << light.range;
            } else if (light.type == LightType::Spot) {
                output << ' ' << light.range << ' '
                       << light.innerConeAngleDegrees << ' '
                       << light.outerConeAngleDegrees;
            } else if (light.type == LightType::Rect) {
                output << ' ' << light.range << ' ' << light.rectSize.x << ' '
                       << light.rectSize.y;
            }
            output << '\n';
            continue;
        }
        if (!std::isfinite(model.position.x) ||
            !std::isfinite(model.position.y) ||
            !std::isfinite(model.position.z) ||
            !std::isfinite(model.rotation.x) ||
            !std::isfinite(model.rotation.y) ||
            !std::isfinite(model.rotation.z) ||
            !std::isfinite(model.scale.x) ||
            !std::isfinite(model.scale.y) ||
            !std::isfinite(model.scale.z) ||
            (model.proceduralLandscape &&
             (!model.path.empty() || !std::isfinite(model.landscapeWidth) ||
              !std::isfinite(model.landscapeDepth) ||
              model.landscapeWidth <= 0.0f || model.landscapeDepth <= 0.0f ||
              model.landscapeWidth > 10000.0f ||
              model.landscapeDepth > 10000.0f ||
              model.landscapeSegmentsX == 0 ||
              model.landscapeSegmentsX > 256 ||
              model.landscapeSegmentsZ == 0 ||
              model.landscapeSegmentsZ > 256))) {
            output.close();
            std::filesystem::remove(temporaryPath);
            error = "Editor scene contains invalid model transforms.";
            return false;
        }
        output << "MODEL " << std::quoted(model.name) << ' '
               << std::quoted(model.material.name) << ' '
               << std::quoted(StoredPath(model.material.asset, projectRoot)
                                  .generic_string())
               << ' '
               << std::quoted(StoredPath(model.path, projectRoot).generic_string())
               << ' '
               << std::quoted(StoredPath(model.material.diffuse, projectRoot)
                                  .generic_string())
               << ' '
               << std::quoted(StoredPath(model.material.normal, projectRoot)
                                  .generic_string())
               << ' '
               << std::quoted(StoredPath(model.material.roughness, projectRoot)
                                  .generic_string())
               << ' '
               << std::quoted(StoredPath(model.material.metallic, projectRoot)
                                  .generic_string())
               << ' '
               << std::quoted(
                      StoredPath(model.material.ambientOcclusion, projectRoot)
                          .generic_string())
               << ' '
               << std::quoted(StoredPath(model.material.height, projectRoot)
                                  .generic_string())
               << ' ' << model.material.heightScale << ' '
               << model.material.twoSided
               << ' ';
        WriteVector(output, model.position);
        output << ' ';
        WriteVector(output, model.rotation);
        output << ' ';
        WriteVector(output, model.scale);
        output << ' ' << model.proceduralLandscape << ' '
               << model.landscapeWidth << ' ' << model.landscapeDepth << ' '
               << model.landscapeSegmentsX << ' '
               << model.landscapeSegmentsZ << '\n';
    }
    output.close();
    if (!output) {
        std::filesystem::remove(temporaryPath);
        error = "Could not finish writing editor scene file.";
        return false;
    }

    std::error_code filesystemError;
    std::filesystem::rename(temporaryPath, scenePath, filesystemError);
    if (filesystemError) {
        std::filesystem::remove(temporaryPath);
        error = "Could not replace editor scene file: " +
                filesystemError.message();
        return false;
    }
    return true;
}

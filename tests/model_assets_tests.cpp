#include "ModelAssets.hpp"
#include "EditorScene.hpp"
#include "SceneRecovery.hpp"

#include <assimp/Importer.hpp>
#include <assimp/postprocess.h>
#include <assimp/scene.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <optional>
#include <stdexcept>
#include <string>
#include <system_error>
#include <unordered_set>

namespace {

class TemporaryDirectory {
public:
    TemporaryDirectory() {
        const auto timestamp =
            std::chrono::steady_clock::now().time_since_epoch().count();
        m_Path = std::filesystem::temp_directory_path() /
                 ("aquarius-model-assets-" + std::to_string(timestamp));
        std::filesystem::create_directories(m_Path);
    }

    ~TemporaryDirectory() {
        std::error_code error;
        std::filesystem::remove_all(m_Path, error);
    }

    const std::filesystem::path& Path() const { return m_Path; }

private:
    std::filesystem::path m_Path;
};

void Require(bool condition, const std::string& message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

void RequireSerializedActorEqual(const EditorSceneModel& actual,
                                 const EditorSceneModel& expected,
                                 const std::string& context) {
    Require(actual.path == expected.path, context + ": actor path changed.");
    Require(actual.name == expected.name, context + ": actor name changed.");
    Require(actual.material.name == expected.material.name &&
                actual.material.asset == expected.material.asset &&
                actual.material.diffuse == expected.material.diffuse &&
                actual.material.normal == expected.material.normal &&
                actual.material.roughness == expected.material.roughness &&
                actual.material.metallic == expected.material.metallic &&
                actual.material.ambientOcclusion ==
                    expected.material.ambientOcclusion &&
                actual.material.height == expected.material.height &&
                actual.material.heightScale ==
                    expected.material.heightScale &&
                actual.material.twoSided == expected.material.twoSided,
            context + ": serialized material properties changed.");
    Require(actual.position.x == expected.position.x &&
                actual.position.y == expected.position.y &&
                actual.position.z == expected.position.z &&
                actual.rotation.x == expected.rotation.x &&
                actual.rotation.y == expected.rotation.y &&
                actual.rotation.z == expected.rotation.z &&
                actual.scale.x == expected.scale.x &&
                actual.scale.y == expected.scale.y &&
                actual.scale.z == expected.scale.z,
            context + ": actor transform changed.");
    Require(actual.proceduralLandscape ==
                    expected.proceduralLandscape &&
                actual.landscapeWidth == expected.landscapeWidth &&
                actual.landscapeDepth == expected.landscapeDepth &&
                actual.landscapeSegmentsX ==
                    expected.landscapeSegmentsX &&
                actual.landscapeSegmentsZ ==
                    expected.landscapeSegmentsZ,
            context + ": landscape properties changed.");
    Require(actual.light.has_value() == expected.light.has_value(),
            context + ": light component presence changed.");
    if (actual.light) {
        const LightProperties& actualLight = *actual.light;
        const LightProperties& expectedLight = *expected.light;
        Require(actualLight.type == expectedLight.type &&
                    actualLight.color.x == expectedLight.color.x &&
                    actualLight.color.y == expectedLight.color.y &&
                    actualLight.color.z == expectedLight.color.z &&
                    actualLight.intensity == expectedLight.intensity &&
                    actualLight.range == expectedLight.range &&
                    actualLight.innerConeAngleDegrees ==
                        expectedLight.innerConeAngleDegrees &&
                    actualLight.outerConeAngleDegrees ==
                        expectedLight.outerConeAngleDegrees &&
                    actualLight.rectSize.x == expectedLight.rectSize.x &&
                    actualLight.rectSize.y == expectedLight.rectSize.y,
                context + ": serialized light properties changed.");
    }
}

std::string ReadTextFile(const std::filesystem::path& file) {
    std::ifstream input(file, std::ios::binary);
    Require(static_cast<bool>(input), "Could not read file '" + file.string() +
                                          "'.");
    return std::string(std::istreambuf_iterator<char>(input),
                       std::istreambuf_iterator<char>());
}

void WriteSimpleObj(const std::filesystem::path& file) {
    std::ofstream output(file);
    output << "o triangle\n"
              "v 0 0 0\n"
              "v 1 0 0\n"
              "v 0 1 0\n"
              "vt 0 0\n"
              "vt 1 0\n"
              "vt 0 1\n"
              "vn 0 0 1\n"
              "f 1/1/1 2/2/1 3/3/1\n";
    Require(static_cast<bool>(output), "Could not write simple OBJ fixture.");
}

void WriteTexturedMultiMaterialObj(const std::filesystem::path& directory) {
    {
        std::ofstream material(directory / "sample.mtl");
        material << "newmtl red\n"
                    "Kd 1.0 0.0 0.0\n"
                    "map_Kd checker.ppm\n"
                    "newmtl green\n"
                    "Kd 0.0 1.0 0.0\n"
                    "map_Kd checker.ppm\n";
        Require(static_cast<bool>(material),
                "Could not write material-library fixture.");
    }
    {
        std::ofstream texture(directory / "checker.ppm", std::ios::binary);
        texture << "P3\n2 2\n255\n"
                   "255 0 0  0 255 0\n"
                   "0 0 255  255 255 255\n";
        Require(static_cast<bool>(texture), "Could not write texture fixture.");
    }
    {
        std::ofstream object(directory / "multi.obj");
        object << "mtllib sample.mtl\n"
                  "v 0 0 0\n"
                  "v 1 0 0\n"
                  "v 0 1 0\n"
                  "v 2 0 0\n"
                  "v 3 0 0\n"
                  "v 2 1 0\n"
                  "vt 0 0\n"
                  "vt 1 0\n"
                  "vt 0 1\n"
                  "vn 0 0 1\n"
                  "usemtl red\n"
                  "f 1/1/1 2/2/1 3/3/1\n"
                  "usemtl green\n"
                  "f 4/1/1 5/2/1 6/3/1\n";
        Require(static_cast<bool>(object), "Could not write multi-material OBJ.");
    }
}

const aiScene* ReadScene(const std::filesystem::path& model,
                         Assimp::Importer& importer) {
    const aiScene* scene = importer.ReadFile(
        model.string(), aiProcess_ValidateDataStructure);
    Require(scene && scene->mRootNode &&
                (scene->mFlags & AI_SCENE_FLAGS_INCOMPLETE) == 0,
            "Assimp could not reopen the exported GLB: " +
                std::string(importer.GetErrorString()));
    return scene;
}

void TestSimpleObjConversion() {
    TemporaryDirectory temporary;
    const auto source = temporary.Path() / "triangle.obj";
    WriteSimpleObj(source);
    const auto assetDirectory = temporary.Path() / "assets";
    std::filesystem::create_directory(assetDirectory);
    const auto destination = assetDirectory / "Triangle" / "Triangle.glb";

    std::string error;
    Require(ConvertModelToGlb(source, destination, error),
            "Simple OBJ conversion failed: " + error);
    Require(std::filesystem::is_regular_file(destination),
            "Simple OBJ conversion did not publish its GLB.");

    Assimp::Importer importer;
    const aiScene* scene = ReadScene(destination, importer);
    Require(scene->mNumMeshes == 1,
            "Simple OBJ conversion did not preserve its one mesh.");
    Require(scene->mMeshes[0]->mNumFaces > 0,
            "The exported GLB contains no triangle faces.");
    Require(scene->mMeshes[0]->HasNormals(),
            "The exported GLB did not preserve normals.");
    Require(scene->mMeshes[0]->HasTextureCoords(0),
            "The exported GLB did not preserve UV coordinates.");
}

void TestTexturesAndMultipleMaterials() {
    TemporaryDirectory temporary;
    WriteTexturedMultiMaterialObj(temporary.Path());
    const auto assetDirectory = temporary.Path() / "assets";
    std::filesystem::create_directory(assetDirectory);
    const auto destination = assetDirectory / "Textured" / "Textured.glb";

    std::string error;
    Require(ConvertModelToGlb(temporary.Path() / "multi.obj", destination,
                              error),
            "Textured OBJ conversion failed: " + error);

    Assimp::Importer importer;
    const aiScene* scene = ReadScene(destination, importer);
    Require(scene->mNumMeshes >= 2,
            "The exported GLB did not preserve both source meshes.");

    std::unordered_set<unsigned int> usedMaterials;
    bool foundTexture = false;
    for (unsigned int i = 0; i < scene->mNumMeshes; ++i) {
        usedMaterials.insert(scene->mMeshes[i]->mMaterialIndex);
    }
    Require(usedMaterials.size() >= 2,
            "The exported GLB did not preserve distinct material assignments.");

    for (unsigned int materialIndex = 0;
         materialIndex < scene->mNumMaterials; ++materialIndex) {
        const aiMaterial* material = scene->mMaterials[materialIndex];
        for (int typeIndex = aiTextureType_NONE;
             typeIndex <= aiTextureType_UNKNOWN; ++typeIndex) {
            const auto type = static_cast<aiTextureType>(typeIndex);
            for (unsigned int textureIndex = 0;
                 textureIndex < material->GetTextureCount(type);
                 ++textureIndex) {
                aiString reference;
                Require(material->GetTexture(type, textureIndex, &reference) ==
                            AI_SUCCESS,
                        "Could not read an exported material texture.");
                foundTexture = true;
                if (scene->GetEmbeddedTexture(reference.C_Str())) {
                    continue;
                }

                std::string pathText = reference.C_Str();
                std::replace(pathText.begin(), pathText.end(), '\\', '/');
                const std::filesystem::path relativeTexture(pathText);
                Require(!relativeTexture.is_absolute(),
                        "Exported GLB has an absolute texture reference.");
                for (const auto& component : relativeTexture) {
                    Require(component != "..",
                            "Exported GLB texture escapes its import folder.");
                }
                Require(std::filesystem::is_regular_file(
                            destination.parent_path() / relativeTexture),
                        "Exported GLB references a texture that was not "
                        "packaged beside the model.");
            }
        }
    }
    Require(foundTexture,
            "The exported GLB lost its source material texture.");
}

void TestInvalidInputAndDestinationConflict() {
    TemporaryDirectory temporary;
    const auto assetDirectory = temporary.Path() / "assets";
    std::filesystem::create_directory(assetDirectory);
    const auto destination = assetDirectory / "Invalid" / "Invalid.glb";
    const auto invalidSource = temporary.Path() / "broken.obj";
    {
        std::ofstream invalid(invalidSource);
        invalid << "this is not a valid model";
    }

    std::string error;
    Require(!ConvertModelToGlb(invalidSource, destination, error),
            "Invalid model input unexpectedly converted.");
    Require(!std::filesystem::exists(destination.parent_path()),
            "Failed import left a partially generated asset folder.");

    const auto missingTextureMaterial =
        temporary.Path() / "missing-texture.mtl";
    {
        std::ofstream material(missingTextureMaterial);
        material << "newmtl broken\n"
                    "map_Kd absent.png\n";
    }
    const auto missingTextureSource =
        temporary.Path() / "missing-texture.obj";
    {
        std::ofstream object(missingTextureSource);
        object << "mtllib missing-texture.mtl\n"
                  "v 0 0 0\n"
                  "v 1 0 0\n"
                  "v 0 1 0\n"
                  "usemtl broken\n"
                  "f 1 2 3\n";
    }
    const auto missingTextureDestination =
        assetDirectory / "MissingTexture" / "MissingTexture.glb";
    Require(!ConvertModelToGlb(missingTextureSource,
                               missingTextureDestination, error),
            "Model with a missing external texture unexpectedly converted.");
    Require(!std::filesystem::exists(
                missingTextureDestination.parent_path()),
            "Texture packaging failure left a partial model folder.");
    for (const auto& entry :
         std::filesystem::directory_iterator(assetDirectory)) {
        Require(!entry.path().filename().string().starts_with("MissingTexture"),
                "Texture packaging failure left a staging directory.");
    }

    const auto existingDirectory = assetDirectory / "Existing";
    std::filesystem::create_directory(existingDirectory);
    const auto sentinel = existingDirectory / "keep.txt";
    {
        std::ofstream output(sentinel);
        output << "preserve";
    }
    const auto validSource = temporary.Path() / "valid.obj";
    WriteSimpleObj(validSource);
    Require(!ConvertModelToGlb(validSource, existingDirectory / "Existing.glb",
                               error),
            "Import unexpectedly overwrote an existing asset folder.");
    Require(std::filesystem::is_regular_file(sentinel),
            "Destination conflict handling damaged existing user data.");
}

void TestExtendedMaterialAndScenePersistence() {
    TemporaryDirectory temporary;
    const auto projectRoot = temporary.Path() / "Project";
    const auto assetRoot = projectRoot / "assets";
    std::filesystem::create_directories(assetRoot / "Materials");
    const auto texture = assetRoot / "test-map.ppm";
    {
        std::ofstream image(texture);
        image << "P3\n1 1\n255\n127 127 127\n";
    }

    ModelMaterial material;
    material.name = "Terrain";
    material.diffuse = texture;
    material.ambientOcclusion = texture;
    material.height = texture;
    material.textureScale = glm::vec3(2.0f, 3.0f, 4.0f);
    material.heightScale = 0.35f;
    material.twoSided = false;
    const auto materialFile = assetRoot / "Materials" / "Terrain.aqenmat";
    std::string error;
    Require(SaveAqenMaterial(materialFile, material, error),
            "Could not save extended material: " + error);

    ModelMaterial loadedMaterial;
    Require(LoadAqenMaterial(materialFile, loadedMaterial, error),
            "Could not reload extended material: " + error);
    Require(loadedMaterial.ambientOcclusion.filename() == texture.filename() &&
                loadedMaterial.height.filename() == texture.filename() &&
                loadedMaterial.heightScale == material.heightScale &&
                !loadedMaterial.twoSided,
            "Material descriptor did not preserve AO, height, strength, or "
            "two-sided setting.");

    EditorScene scene;
    EditorSceneModel actor;
    actor.path = assetRoot / "Landscapes" / "Terrain" / "Terrain.glb";
    actor.name = "Terrain Plane";
    actor.material = loadedMaterial;
    actor.position = {1.0f, 0.0f, 2.0f};
    scene.models.push_back(actor);
    EditorSceneModel landscape;
    landscape.name = "Procedural Landscape";
    landscape.proceduralLandscape = true;
    landscape.landscapeWidth = 32.0f;
    landscape.landscapeDepth = 16.0f;
    landscape.landscapeSegmentsX = 8;
    landscape.landscapeSegmentsZ = 4;
    scene.models.push_back(landscape);
    Require(SaveEditorScene(projectRoot, scene, error),
            "Could not save extended editor scene: " + error);

    EditorScene loadedScene;
    Require(LoadEditorScene(projectRoot, loadedScene, error),
            "Could not reload extended editor scene: " + error);
    Require(loadedScene.models.size() == 2 &&
                loadedScene.models[0].material.height.filename() ==
                    texture.filename() &&
                loadedScene.models[0].material.ambientOcclusion.filename() ==
                    texture.filename() &&
                loadedScene.models[0].material.heightScale ==
                    material.heightScale &&
                !loadedScene.models[0].material.twoSided,
            "Scene persistence lost extended material settings.");
    Require(loadedScene.models[1].proceduralLandscape &&
                loadedScene.models[1].path.empty() &&
                loadedScene.models[1].landscapeWidth == 32.0f &&
                loadedScene.models[1].landscapeDepth == 16.0f &&
                loadedScene.models[1].landscapeSegmentsX == 8 &&
                loadedScene.models[1].landscapeSegmentsZ == 4,
            "Scene persistence lost procedural landscape settings.");
}

void TestLightCreationDefaultsAndIconResources() {
    constexpr std::array<LightType, 5> lightTypes{
        LightType::Directional, LightType::Spot, LightType::Point,
        LightType::Sky, LightType::Rect};
    const std::filesystem::path iconDirectory =
        AQUARIUS_LIGHT_ICON_SOURCE_DIRECTORY;
    for (LightType type : lightTypes) {
        const EditorSceneModel object = MakeEditorSceneLight(type);
        Require(object.light.has_value() &&
                    object.light->type == type &&
                    object.name == LightTypeName(type),
                "Light creation did not retain its type and name.");
        Require(object.position.x == 0.0f && object.position.y == 10.0f &&
                    object.position.z == 0.0f,
                "New lights are not spawned ten units above the XZ ground.");
        Require(IsValidLightProperties(*object.light) &&
                    object.light->intensity > 0.0f,
                "A light type has invalid or unusable defaults.");
        const std::string iconName(LightIconFilename(type));
        Require(!iconName.empty() &&
                    std::filesystem::is_regular_file(iconDirectory / iconName),
                "A light type does not resolve to its existing icon asset.");
    }

    Require(IsLightRotatable(LightType::Directional) &&
                IsLightRotatable(LightType::Spot) &&
                IsLightRotatable(LightType::Rect) &&
                !IsLightRotatable(LightType::Sky) &&
                !IsLightRotatable(LightType::Point),
            "Light rotation support does not match the light types.");
    const RenderLight sky = BuildRenderLight(
        DefaultLightProperties(LightType::Sky), glm::vec3(0.0f),
        glm::vec3(1.0f, 2.0f, 3.0f));
    Require(sky.emissionDirection.x == 0.0f &&
                sky.emissionDirection.y == -1.0f &&
                sky.emissionDirection.z == 0.0f,
            "Sky Light direction changed with its object rotation.");
}

void TestLightVisualizationUsesLightingParameters() {
    LightProperties pointProperties =
        DefaultLightProperties(LightType::Point);
    pointProperties.range = 31.0f;
    const glm::vec3 pointPosition{2.0f, 3.0f, 4.0f};
    const RenderLight point =
        BuildRenderLight(pointProperties, pointPosition, glm::vec3(0.0f));
    Require(point.properties.range == 31.0f &&
                point.position.x == pointPosition.x &&
                point.position.y == pointPosition.y &&
                point.position.z == pointPosition.z,
            "Point Light visualization inputs do not preserve its center and "
            "effective range.");

    EditorSceneModel spotObject = MakeEditorSceneLight(LightType::Spot);
    spotObject.light->range = 44.0f;
    spotObject.light->innerConeAngleDegrees = 10.0f;
    spotObject.light->outerConeAngleDegrees = 35.0f;
    const RenderLight spot = BuildRenderLight(
        *spotObject.light, spotObject.position, spotObject.rotation);
    Require(std::abs(spot.properties.range - 44.0f) < 0.0001f &&
                std::abs(spot.properties.outerConeAngleDegrees - 35.0f) <
                    0.0001f &&
                glm::length(spot.emissionDirection -
                            glm::vec3(0.0f, -1.0f, 0.0f)) < 0.0001f,
            "Spot Light visualization inputs do not match its range, cone, "
            "and rotated beam direction.");

    LightProperties rectProperties =
        DefaultLightProperties(LightType::Rect);
    rectProperties.rectSize = {6.0f, 4.0f};
    const RenderLight rect = BuildRenderLight(
        rectProperties, pointPosition,
        glm::vec3(0.0f, 0.0f, glm::radians(90.0f)));
    Require(std::abs(glm::length(rect.rectHalfAxisX) - 3.0f) < 0.0001f &&
                std::abs(glm::length(rect.rectHalfAxisY) - 2.0f) < 0.0001f &&
                std::abs(glm::dot(rect.rectHalfAxisX,
                                  rect.rectHalfAxisY)) < 0.0001f &&
                glm::length(rect.position - pointPosition) < 0.0001f,
            "Rect Light visualization inputs do not match its dimensions, "
            "position, and rotation.");
}

void TestLightPropertiesRoundTrip() {
    TemporaryDirectory temporary;
    const std::filesystem::path projectRoot =
        temporary.Path() / "LightProject";
    std::filesystem::create_directories(projectRoot);
    EditorScene scene;
    constexpr std::array<LightType, 5> lightTypes{
        LightType::Directional, LightType::Spot, LightType::Point,
        LightType::Sky, LightType::Rect};
    for (LightType type : lightTypes) {
        EditorSceneModel light = MakeEditorSceneLight(type);
        light.position += glm::vec3(1.0f, 2.0f, 3.0f);
        light.scale = {1.25f, 0.75f, 1.5f};
        light.light->color = {0.25f, 0.5f, 0.75f};
        light.light->intensity = 12.5f;
        if (type == LightType::Directional) {
            light.rotation = {0.1f, 0.2f, 0.3f};
        } else if (type == LightType::Spot) {
            light.light->range = 42.0f;
            light.light->innerConeAngleDegrees = 12.0f;
            light.light->outerConeAngleDegrees = 37.0f;
        } else if (type == LightType::Point) {
            light.light->range = 18.0f;
        } else if (type == LightType::Rect) {
            light.rotation = {0.2f, 0.3f, 0.4f};
            light.light->range = 27.0f;
            light.light->rectSize = {6.0f, 2.5f};
        }
        scene.models.push_back(std::move(light));
    }

    std::string error;
    Require(SaveEditorScene(projectRoot, scene, error),
            "Could not save lights: " + error);
    EditorScene reopened;
    Require(LoadEditorScene(projectRoot, reopened, error),
            "Could not reopen lights: " + error);
    Require(reopened.models.size() == scene.models.size(),
            "Light round-trip changed the scene object count.");
    for (std::size_t i = 0; i < scene.models.size(); ++i) {
        RequireSerializedActorEqual(reopened.models[i], scene.models[i],
                                    "Light round-trip");
    }
}

void TestDefaultProjectLightCreationDoesNotDuplicate() {
    TemporaryDirectory temporary;
    const std::filesystem::path projectRoot =
        temporary.Path() / "DefaultLightProject";
    std::filesystem::create_directories(projectRoot);
    std::string error;
    Require(CreateDefaultEditorScene(projectRoot, error),
            "Could not create the default light scene: " + error);

    EditorScene initial;
    Require(LoadEditorScene(projectRoot, initial, error),
            "Could not open the initial default-light scene: " + error);
    const auto defaultLightCount = [](const EditorScene& scene) {
        return std::count_if(
            scene.models.begin(), scene.models.end(),
            [](const EditorSceneModel& object) {
                return object.light.has_value();
            });
    };
    Require(initial.models.size() == 2 &&
                initial.models[0].name == "Ground" &&
                initial.models[0].proceduralLandscape &&
                initial.models[1].light &&
                initial.models[1].light->type == LightType::Directional &&
                initial.models[1].position.y == 10.0f &&
                defaultLightCount(initial) == 1 &&
                initial.cameraPosition.x == 0.0f &&
                initial.cameraPosition.y == 5.0f &&
                initial.cameraPosition.z == 20.0f,
            "A new project did not start with one elevated light and a ground "
            "receiver in the initial camera view.");
    Require(SaveEditorScene(projectRoot, initial, error),
            "Could not save the initial default-light scene: " + error);

    EditorScene reopened;
    Require(LoadEditorScene(projectRoot, reopened, error) &&
                reopened.models.size() == 2 &&
                reopened.models[0].name == "Ground" &&
                reopened.models[1].light &&
                reopened.models[1].light->type == LightType::Directional &&
                defaultLightCount(reopened) == 1,
            "Saving and reopening duplicated or lost the default light.");
}

void TestLegacySceneWithoutLightsAndInvalidLightRecovery() {
    TemporaryDirectory temporary;
    const std::filesystem::path projectRoot =
        temporary.Path() / "LegacyLightProject";
    std::filesystem::create_directories(projectRoot);
    const std::filesystem::path scenePath =
        projectRoot / "editor.aqenscene";
    {
        std::ofstream legacy(scenePath);
        legacy << "AQENSCENE 6\n0 2.5 5 -90 0\n0\n";
        Require(static_cast<bool>(legacy),
                "Could not write the legacy scene fixture.");
    }

    std::string error;
    EditorScene legacyScene;
    Require(LoadEditorScene(projectRoot, legacyScene, error) &&
                legacyScene.models.empty(),
            "A legacy scene without lights did not load unchanged.");
    Require(SaveEditorScene(projectRoot, legacyScene, error),
            "Could not save the legacy scene after opening: " + error);
    EditorScene legacyReopened;
    Require(LoadEditorScene(projectRoot, legacyReopened, error) &&
                legacyReopened.models.empty(),
            "Opening a legacy scene injected a default light.");

    {
        std::ofstream unsupported(scenePath, std::ios::trunc);
        unsupported << "AQENSCENE 7\n0 2.5 5 -90 0\n1\n"
                       "LIGHT 99 \"Unsupported Light\" "
                       "0 10 0 1 1 1 1 1 1 4\n";
        Require(static_cast<bool>(unsupported),
                "Could not write the unsupported light fixture.");
    }
    const std::string originalContents = ReadTextFile(scenePath);
    EditorScene invalidScene;
    Require(!LoadEditorScene(projectRoot, invalidScene, error) &&
                !error.empty(),
            "An unsupported light record was accepted.");
    Require(!SaveEditorSceneWithRecovery(projectRoot, false, glm::vec3(0.0f),
                                         0.0f, 0.0f, {}, {}, error) &&
                ReadTextFile(scenePath) == originalContents,
            "An unsupported light record was not protected from overwrite.");
}

void TestFailedRestorationIsRetained() {
    EditorSceneModel validActor;
    validActor.name = "Restored actor";
    EditorSceneModel missingActor;
    missingActor.name = "Missing actor";
    missingActor.path = "assets/Missing.glb";

    std::vector<std::string> restoredActors;
    const std::vector<EditorSceneModel> records{validActor, missingActor};
    const std::vector<UnresolvedSceneActor> unresolved =
        RestoreSceneActorRecords(
            records,
            [&restoredActors](const EditorSceneModel& actor, std::size_t,
                              std::string& error) {
                if (actor.name == "Missing actor") {
                    error = "test restoration failure";
                    return false;
                }
                restoredActors.push_back(actor.name);
                return true;
            });
    Require(restoredActors.size() == 1 &&
                restoredActors.front() == "Restored actor",
            "A valid actor was not retained when another actor failed.");
    Require(unresolved.size() == 1 && unresolved.front().sceneOrder == 1 &&
                unresolved.front().source.name == "Missing actor" &&
                unresolved.front().source.path == missingActor.path &&
                unresolved.front().error == "test restoration failure",
            "Failed actor data or its restoration error was not retained.");
}

void TestUnresolvedActorsSaveAndReopenInOrder() {
    TemporaryDirectory temporary;
    const std::filesystem::path projectRoot = temporary.Path() / "Project";
    std::filesystem::create_directories(projectRoot);

    EditorSceneModel first;
    first.name = "First";
    first.path = "assets/First.glb";
    first.position = {1.0f, 2.0f, 3.0f};

    EditorSceneModel unresolvedSource;
    unresolvedSource.name = "Preserved Missing Actor";
    unresolvedSource.path = "assets/models/Missing.glb";
    unresolvedSource.material.name = "Retained Material";
    unresolvedSource.material.diffuse = "assets/maps/diffuse.png";
    unresolvedSource.material.normal = "assets/maps/normal.png";
    unresolvedSource.material.roughness = "assets/maps/roughness.png";
    unresolvedSource.material.metallic = "assets/maps/metallic.png";
    unresolvedSource.material.ambientOcclusion = "assets/maps/ao.png";
    unresolvedSource.material.height = "assets/maps/height.png";
    unresolvedSource.material.heightScale = 0.65f;
    unresolvedSource.material.twoSided = false;
    unresolvedSource.position = {4.0f, 5.0f, 6.0f};
    unresolvedSource.rotation = {0.1f, 0.2f, 0.3f};
    unresolvedSource.scale = {2.0f, 3.0f, 4.0f};

    EditorSceneModel unresolvedLandscape;
    unresolvedLandscape.name = "Preserved Broken Landscape";
    unresolvedLandscape.proceduralLandscape = true;
    unresolvedLandscape.landscapeWidth = 72.0f;
    unresolvedLandscape.landscapeDepth = 36.0f;
    unresolvedLandscape.landscapeSegmentsX = 18;
    unresolvedLandscape.landscapeSegmentsZ = 9;
    unresolvedLandscape.material.name = "Broken landscape material";
    unresolvedLandscape.material.height = "assets/maps/landscape-height.png";
    unresolvedLandscape.material.heightScale = 0.4f;
    unresolvedLandscape.position = {-4.0f, 1.0f, 6.0f};

    EditorSceneModel landscape;
    landscape.name = "Loaded Landscape";
    landscape.proceduralLandscape = true;
    landscape.landscapeWidth = 48.0f;
    landscape.landscapeDepth = 24.0f;
    landscape.landscapeSegmentsX = 12;
    landscape.landscapeSegmentsZ = 6;
    landscape.position = {-1.0f, 0.0f, 1.0f};

    EditorSceneModel appended;
    appended.name = "New Actor";
    appended.path = "assets/New.glb";

    const std::vector<UnresolvedSceneActor> unresolved{
        {1, unresolvedSource, "model missing"},
        {3, unresolvedLandscape, "landscape texture missing"}};
    EditorScene scene;
    scene.models = MergeOrderedSceneActors(
        {{0, first}, {2, landscape}, {4, appended}}, unresolved);
    std::string error;
    Require(SaveEditorSceneWithRecovery(
                projectRoot, true, scene.cameraPosition, scene.cameraYaw,
                scene.cameraPitch,
                {{0, first}, {2, landscape}, {4, appended}}, unresolved,
                error),
            "Could not save a scene with unresolved actors: " + error);

    EditorScene reopened;
    Require(LoadEditorScene(projectRoot, reopened, error),
            "Could not reopen a scene with unresolved actors: " + error);
    Require(reopened.models.size() == 5,
            "Saving discarded an unresolved scene actor.");
    Require(reopened.models[0].name == "First" &&
                reopened.models[1].name == "Preserved Missing Actor" &&
                reopened.models[2].name == "Loaded Landscape" &&
                reopened.models[3].name == "Preserved Broken Landscape" &&
                reopened.models[4].name == "New Actor",
            "Loaded, unresolved, and newly appended actors changed order.");

    EditorSceneModel expected = unresolvedSource;
    expected.path = projectRoot / unresolvedSource.path;
    expected.material.diffuse = projectRoot / unresolvedSource.material.diffuse;
    expected.material.normal = projectRoot / unresolvedSource.material.normal;
    expected.material.roughness =
        projectRoot / unresolvedSource.material.roughness;
    expected.material.metallic =
        projectRoot / unresolvedSource.material.metallic;
    expected.material.ambientOcclusion =
        projectRoot / unresolvedSource.material.ambientOcclusion;
    expected.material.height = projectRoot / unresolvedSource.material.height;
    RequireSerializedActorEqual(reopened.models[1], expected,
                                "Unresolved actor round-trip");

    EditorSceneModel expectedLandscape = landscape;
    RequireSerializedActorEqual(reopened.models[2], expectedLandscape,
                                "Landscape round-trip");
    EditorSceneModel expectedUnresolvedLandscape = unresolvedLandscape;
    expectedUnresolvedLandscape.material.height =
        projectRoot / unresolvedLandscape.material.height;
    RequireSerializedActorEqual(reopened.models[3],
                                expectedUnresolvedLandscape,
                                "Unresolved landscape round-trip");

    std::vector<OrderedSceneActor> restoredActors;
    const std::vector<UnresolvedSceneActor> unresolvedAgain =
        RestoreSceneActorRecords(
            reopened.models,
            [&restoredActors](const EditorSceneModel& actor,
                              std::size_t sceneOrder, std::string& restoreError) {
                if (actor.name == "Preserved Missing Actor" ||
                    actor.name == "Preserved Broken Landscape") {
                    restoreError = "still unavailable";
                    return false;
                }
                restoredActors.push_back({sceneOrder, actor});
                return true;
            });
    Require(restoredActors.size() == 3 && unresolvedAgain.size() == 2,
            "Repeated restoration changed the loaded/unresolved actor split.");
    Require(SaveEditorSceneWithRecovery(
                projectRoot, true, reopened.cameraPosition, reopened.cameraYaw,
                reopened.cameraPitch, std::move(restoredActors),
                unresolvedAgain, error),
            "Could not save the repeated unresolved scene: " + error);
    EditorScene reopenedAgain;
    Require(LoadEditorScene(projectRoot, reopenedAgain, error),
            "Could not reopen the repeated unresolved scene: " + error);
    Require(reopenedAgain.models.size() == 5 &&
                reopenedAgain.models[0].name == "First" &&
                reopenedAgain.models[1].name == "Preserved Missing Actor" &&
                reopenedAgain.models[2].name == "Loaded Landscape" &&
                reopenedAgain.models[3].name ==
                    "Preserved Broken Landscape" &&
                reopenedAgain.models[4].name == "New Actor",
            "Repeated save/reopen duplicated, dropped, or reordered actors.");
    RequireSerializedActorEqual(reopenedAgain.models[1], expected,
                                "Repeated unresolved actor round-trip");
    RequireSerializedActorEqual(reopenedAgain.models[3],
                                expectedUnresolvedLandscape,
                                "Repeated unresolved landscape round-trip");
}

void TestUnresolvedActorKeepsOriginalMaterialAssetReference() {
    TemporaryDirectory temporary;
    const std::filesystem::path projectRoot = temporary.Path() / "Project";
    std::filesystem::create_directories(projectRoot / "assets" / "models");

    EditorScene initialScene;
    EditorSceneModel actor;
    actor.name = "Missing model with missing material";
    actor.path = "assets/models/Missing.glb";
    actor.material.name = "Retained material";
    actor.material.asset = "assets/Materials/Missing.aqenmat";
    actor.material.diffuse = "assets/maps/fallback.png";
    actor.position = {3.0f, 4.0f, 5.0f};
    initialScene.models.push_back(actor);

    std::string error;
    Require(SaveEditorScene(projectRoot, initialScene, error),
            "Could not write the material-reference fixture: " + error);

    EditorScene parsedScene;
    Require(LoadEditorScene(projectRoot, parsedScene, error),
            "Could not parse the material-reference fixture: " + error);
    Require(parsedScene.models.size() == 1 &&
                parsedScene.models[0].material.asset.empty() &&
                parsedScene.models[0].originalSerializedMaterialAsset ==
                    projectRoot / actor.material.asset,
            "The fixture did not exercise descriptor failure and reference "
            "preservation.");

    const std::vector<UnresolvedSceneActor> unresolved =
        RestoreSceneActorRecords(
            parsedScene.models,
            [](const EditorSceneModel&, std::size_t, std::string& restoreError) {
                restoreError = "model asset is missing";
                return false;
            });
    Require(unresolved.size() == 1,
            "Failed model restoration did not produce an unresolved actor.");
    Require(SaveEditorSceneWithRecovery(
                projectRoot, true, parsedScene.cameraPosition,
                parsedScene.cameraYaw, parsedScene.cameraPitch, {}, unresolved,
                error),
            "Could not save the unresolved material-reference fixture: " +
                error);

    const std::filesystem::path savedScenePath =
        projectRoot / "editor.aqenscene";
    const std::string savedContents = ReadTextFile(savedScenePath);
    Require(savedContents.find(
                "\"assets/Materials/Missing.aqenmat\"") !=
                std::string::npos,
            "Serialized unresolved actor lost its original material asset "
            "reference.");

    EditorScene reopened;
    Require(LoadEditorScene(projectRoot, reopened, error),
            "Could not reopen the unresolved material-reference fixture: " +
                error);
    Require(reopened.models.size() == 1 &&
                reopened.models[0].originalSerializedMaterialAsset ==
                    projectRoot / actor.material.asset &&
                reopened.models[0].name == actor.name &&
                reopened.models[0].path == projectRoot / actor.path &&
                reopened.models[0].position.x == actor.position.x &&
                reopened.models[0].position.y == actor.position.y &&
                reopened.models[0].position.z == actor.position.z,
            "Reopening the saved unresolved actor lost retained source data.");

    UnresolvedSceneActor recovered = unresolved.front();
    EditorSceneModel retryCandidate = recovered.source;
    ModelMaterial materialForRestore;
    std::string descriptorError;
    Require(!TryReloadUnresolvedMaterialDescriptor(
                retryCandidate, materialForRestore, descriptorError) &&
                !descriptorError.empty() && materialForRestore.asset.empty(),
            "Unavailable descriptor did not leave saved-map fallback data.");
    Require(TryRestoreUnresolvedSceneActor(
                recovered, retryCandidate,
                [&recovered](const EditorSceneModel& candidate, std::string&) {
                    return candidate.path == recovered.source.path;
                }),
            "Simulated successful model recovery failed.");
    const EditorSceneModel recoveredSnapshot =
        PrepareSceneActorForSave(recovered.source);
    Require(recoveredSnapshot.material.asset ==
                    projectRoot / actor.material.asset &&
                recoveredSnapshot.originalSerializedMaterialAsset.empty() &&
                !recoveredSnapshot.materialDescriptorLoadFailed,
            "Successful fallback recovery discarded the retained descriptor "
            "reference.");
    Require(SaveEditorSceneWithRecovery(
                projectRoot, true, reopened.cameraPosition, reopened.cameraYaw,
                reopened.cameraPitch, {{0, recoveredSnapshot}}, {}, error),
            "Could not save the successfully recovered actor: " + error);
    const std::string recoveredContents = ReadTextFile(savedScenePath);
    Require(recoveredContents.find(
                "\"assets/Materials/Missing.aqenmat\"") !=
                std::string::npos,
            "Saving after successful fallback recovery erased the original "
            "material reference.");
    EditorScene recoveredAgain;
    Require(LoadEditorScene(projectRoot, recoveredAgain, error) &&
                recoveredAgain.models.size() == 1 &&
                recoveredAgain.models[0].originalSerializedMaterialAsset ==
                    projectRoot / actor.material.asset,
            "The material reference did not survive reopening after "
            "successful fallback recovery.");
    const EditorSceneModel afterReopen =
        PrepareSceneActorForSave(recoveredAgain.models[0]);
    Require(afterReopen.material.asset ==
                projectRoot / actor.material.asset,
            "A repeated save snapshot discarded the retained material path.");
    Require(SaveEditorSceneWithRecovery(
                projectRoot, true, recoveredAgain.cameraPosition,
                recoveredAgain.cameraYaw, recoveredAgain.cameraPitch,
                {{0, afterReopen}}, {}, error),
            "Could not repeat the recovered-scene save: " + error);
    Require(ReadTextFile(savedScenePath).find(
                "\"assets/Materials/Missing.aqenmat\"") !=
                std::string::npos,
            "A repeated save after reopening erased the original material "
            "reference.");
}

void TestRecoveryReloadsARepairedMaterialDescriptor() {
    TemporaryDirectory temporary;
    const std::filesystem::path projectRoot = temporary.Path() / "Project";
    const std::filesystem::path assets = projectRoot / "assets";
    const std::filesystem::path materials = assets / "Materials";
    const std::filesystem::path maps = assets / "maps";
    std::filesystem::create_directories(materials);
    std::filesystem::create_directories(assets / "models");
    std::filesystem::create_directories(maps);

    const std::filesystem::path descriptorPath =
        materials / "Recoverable.aqenmat";
    EditorScene initialScene;
    EditorSceneModel actor;
    actor.name = "Actor with repaired material";
    actor.path = "assets/models/Recoverable.glb";
    actor.material.name = "Saved fallback material";
    actor.material.asset = "assets/Materials/Recoverable.aqenmat";
    actor.material.diffuse = "assets/maps/fallback.ppm";
    actor.position = {2.0f, 3.0f, 4.0f};
    actor.rotation = {0.2f, 0.4f, 0.6f};
    actor.scale = {1.5f, 2.5f, 3.5f};
    initialScene.models.push_back(actor);
    std::string error;
    Require(SaveEditorScene(projectRoot, initialScene, error),
            "Could not create descriptor-repair fixture: " + error);

    EditorScene parsed;
    Require(LoadEditorScene(projectRoot, parsed, error) &&
                parsed.models.size() == 1 &&
                parsed.models[0].materialDescriptorLoadFailed &&
                parsed.models[0].material.asset.empty(),
            "Missing descriptor did not create the expected unresolved "
            "material state.");
    std::vector<UnresolvedSceneActor> unresolved =
        RestoreSceneActorRecords(
            parsed.models,
            [](const EditorSceneModel&, std::size_t, std::string& restoreError) {
                restoreError = "model is not available yet";
                return false;
            });
    Require(unresolved.size() == 1,
            "Missing model was not retained for material-repair retry.");

    const std::filesystem::path texture = maps / "repaired.ppm";
    {
        std::ofstream image(texture);
        image << "P3\n1 1\n255\n180 120 60\n";
        Require(static_cast<bool>(image),
                "Could not write repaired material texture.");
    }
    ModelMaterial repairedMaterial;
    repairedMaterial.name = "Repaired material";
    repairedMaterial.diffuse = texture;
    repairedMaterial.textureScale = glm::vec3(2.0f, 3.0f, 4.0f);
    repairedMaterial.heightScale = 0.7f;
    repairedMaterial.twoSided = false;
    Require(SaveAqenMaterial(descriptorPath, repairedMaterial, error),
            "Could not repair material descriptor: " + error);

    EditorSceneModel candidate = unresolved.front().source;
    ModelMaterial materialForRestore;
    Require(TryReloadUnresolvedMaterialDescriptor(
                candidate, materialForRestore, error),
            "Recovery did not reload the repaired material descriptor: " +
                error);
    Require(candidate.material.asset == descriptorPath &&
                materialForRestore.name == repairedMaterial.name &&
                materialForRestore.textureScale ==
                    repairedMaterial.textureScale,
            "Retry did not use the newly valid descriptor contents.");
    EditorSceneModel withNewerMaterial = candidate;
    withNewerMaterial.originalSerializedMaterialAsset =
        projectRoot / "assets/Materials/Older.aqenmat";
    withNewerMaterial.materialDescriptorLoadFailed = true;
    const EditorSceneModel newerMaterialSnapshot =
        PrepareSceneActorForSave(withNewerMaterial);
    Require(newerMaterialSnapshot.material.asset == descriptorPath,
            "Save snapshot overwrote a newer valid material reference with "
            "stale original metadata.");
    Require(TryRestoreUnresolvedSceneActor(
                unresolved.front(), candidate,
                [&materialForRestore](const EditorSceneModel& source,
                                      std::string& restoreError) {
                    if (source.material.asset.empty() ||
                        materialForRestore.name != "Repaired material") {
                        restoreError = "repaired material was not supplied";
                        return false;
                    }
                    return true;
                }),
            "Successful recovery with the repaired descriptor failed.");

    const EditorSceneModel recovered =
        PrepareSceneActorForSave(unresolved.front().source);
    Require(recovered.material.asset == descriptorPath &&
                recovered.originalSerializedMaterialAsset.empty() &&
                !recovered.materialDescriptorLoadFailed,
            "Save snapshot replaced the newer valid descriptor reference.");
    Require(SaveEditorSceneWithRecovery(
                projectRoot, true, parsed.cameraPosition, parsed.cameraYaw,
                parsed.cameraPitch, {{0, recovered}}, {}, error),
            "Could not save recovered actor with repaired material: " + error);

    EditorScene reopened;
    Require(LoadEditorScene(projectRoot, reopened, error) &&
                reopened.models.size() == 1 &&
                reopened.models[0].name == actor.name &&
                reopened.models[0].material.asset == descriptorPath &&
                reopened.models[0].material.name == repairedMaterial.name &&
                reopened.models[0].material.textureScale ==
                    repairedMaterial.textureScale &&
                reopened.models[0].material.heightScale ==
                    repairedMaterial.heightScale &&
                reopened.models[0].material.twoSided ==
                    repairedMaterial.twoSided &&
                reopened.models[0].position.x == actor.position.x &&
                reopened.models[0].position.y == actor.position.y &&
                reopened.models[0].position.z == actor.position.z &&
                reopened.models[0].rotation.y == actor.rotation.y &&
                reopened.models[0].scale.z == actor.scale.z,
            "Reopened recovered actor lost descriptor or transform data.");
}

void TestWholeSceneParseFailureBlocksRecoveredSave() {
    TemporaryDirectory temporary;
    const std::filesystem::path projectRoot = temporary.Path() / "Project";
    std::filesystem::create_directories(projectRoot);
    const std::filesystem::path scenePath =
        projectRoot / "editor.aqenscene";
    {
        std::ofstream malformed(scenePath, std::ios::binary);
        malformed << "AQENSCENE 6\ntruncated original scene\n";
        Require(static_cast<bool>(malformed),
                "Could not write malformed original scene.");
    }
    const std::string originalContents = ReadTextFile(scenePath);

    EditorScene parsedScene;
    std::string error;
    Require(!LoadEditorScene(projectRoot, parsedScene, error),
            "Malformed full-scene input unexpectedly parsed.");
    Require(!SaveEditorSceneWithRecovery(
                projectRoot, false, glm::vec3(99.0f), 45.0f, 20.0f,
                {{0, EditorSceneModel{}}}, {}, error),
            "A scene with a parse failure was allowed to save.");
    Require(ReadTextFile(scenePath) == originalContents,
            "Parse-failure protection replaced the original scene file.");
}

void TestUnresolvedRelinkIsTransactional() {
    EditorSceneModel original;
    original.name = "Relink me";
    original.path = "assets/original.glb";
    original.material.name = "Original material";
    original.material.diffuse = "assets/maps/original.png";
    original.position = {7.0f, 8.0f, 9.0f};
    original.rotation = {0.4f, 0.5f, 0.6f};
    original.scale = {1.5f, 2.5f, 3.5f};
    UnresolvedSceneActor unresolved{4, original, "original load error"};

    EditorSceneModel replacement = original;
    replacement.path = "assets/replacement.glb";
    Require(!TryRestoreUnresolvedSceneActor(
                unresolved, replacement,
                [](const EditorSceneModel&, std::string& error) {
                    error = "replacement could not be loaded";
                    return false;
                }),
            "A failed replacement was reported as restored.");
    RequireSerializedActorEqual(unresolved.source, original,
                                "Failed relink");
    Require(unresolved.error == "replacement could not be loaded",
            "Failed relink did not retain its latest error.");

    Require(TryRestoreUnresolvedSceneActor(
                unresolved, replacement,
                [](const EditorSceneModel& candidate, std::string& error) {
                    if (candidate.path != "assets/replacement.glb") {
                        error = "unexpected replacement path";
                        return false;
                    }
                    return true;
                }),
            "A valid replacement was not accepted.");
    Require(unresolved.source.path == replacement.path &&
                unresolved.source.name == original.name &&
                unresolved.source.material.name == original.material.name &&
                unresolved.source.material.diffuse ==
                    original.material.diffuse &&
                unresolved.source.position.x == original.position.x &&
                unresolved.source.position.y == original.position.y &&
                unresolved.source.position.z == original.position.z &&
                unresolved.source.rotation.y == original.rotation.y &&
                unresolved.source.scale.z == original.scale.z &&
                unresolved.error.empty(),
            "Successful relink did not preserve the actor's other data.");
}

void TestUnresolvedRemovalRequiresExplicitRequest() {
    std::vector<UnresolvedSceneActor> unresolved{
        {2, EditorSceneModel{}, "first"},
        {5, EditorSceneModel{}, "second"},
        {8, EditorSceneModel{}, "third"}};
    const std::optional<UnresolvedActorAction> cancelledAction;
    Require(!cancelledAction &&
                unresolved.size() == 3 &&
                !ApplyUnresolvedRemovalAction(
                    unresolved, UnresolvedActorAction{
                                    UnresolvedActorActionType::Retry, 5, {}}),
            "Cancelled or non-removal action changed the unresolved actors.");
    const UnresolvedActorAction confirmedAction{
        UnresolvedActorActionType::Remove, 5, {}};
    Require(ApplyUnresolvedRemovalAction(unresolved, confirmedAction),
            "Confirmed removal did not remove the selected actor.");
    Require(unresolved.size() == 2 && unresolved[0].sceneOrder == 2 &&
                unresolved[1].sceneOrder == 8,
            "Confirmed removal affected an actor other than the target.");
    Require(!ApplyUnresolvedRemovalAction(unresolved, confirmedAction),
            "Removing an already removed actor unexpectedly succeeded.");
}

void TestProceduralLandscapeCanRetryOrBeRemoved() {
    EditorSceneModel landscape;
    landscape.name = "Broken landscape";
    landscape.proceduralLandscape = true;
    landscape.landscapeWidth = 80.0f;
    landscape.landscapeDepth = 40.0f;
    landscape.landscapeSegmentsX = 20;
    landscape.landscapeSegmentsZ = 10;
    landscape.material.name = "Terrain material";
    landscape.material.height = "assets/height.png";
    landscape.material.heightScale = 0.8f;
    landscape.position = {1.0f, 2.0f, 3.0f};
    UnresolvedSceneActor unresolved{6, landscape, "height map unavailable"};

    Require(!TryRestoreUnresolvedSceneActor(
                unresolved, landscape,
                [](const EditorSceneModel& candidate, std::string& error) {
                    Require(candidate.proceduralLandscape &&
                                candidate.path.empty(),
                            "Landscape retry received an invalid model path.");
                    error = "height map still unavailable";
                    return false;
                }),
            "A failed landscape retry was reported as successful.");
    RequireSerializedActorEqual(unresolved.source, landscape,
                                "Failed landscape retry");

    Require(TryRestoreUnresolvedSceneActor(
                unresolved, landscape,
                [](const EditorSceneModel& candidate, std::string&) {
                    return candidate.proceduralLandscape &&
                           candidate.landscapeWidth == 80.0f &&
                           candidate.landscapeSegmentsZ == 10;
                }),
            "A repaired landscape could not be retried.");
    RequireSerializedActorEqual(unresolved.source, landscape,
                                "Successful landscape retry");

    std::vector<UnresolvedSceneActor> unresolvedLandscapes{
        {6, landscape, "height map still unavailable"}};
    // Cancellation leaves the landscape untouched; explicit confirmation
    // removes only the chosen unresolved record.
    Require(unresolvedLandscapes.size() == 1 &&
                unresolvedLandscapes.front().source.landscapeWidth == 80.0f,
            "Cancelled landscape removal lost its parameters.");
    Require(RemoveUnresolvedSceneActor(unresolvedLandscapes, 6) &&
                unresolvedLandscapes.empty(),
            "Confirmed landscape removal did not remove the target.");
}

} // namespace

int main() {
    try {
        TestSimpleObjConversion();
        TestTexturesAndMultipleMaterials();
        TestInvalidInputAndDestinationConflict();
        TestExtendedMaterialAndScenePersistence();
        TestLightCreationDefaultsAndIconResources();
        TestLightVisualizationUsesLightingParameters();
        TestLightPropertiesRoundTrip();
        TestDefaultProjectLightCreationDoesNotDuplicate();
        TestLegacySceneWithoutLightsAndInvalidLightRecovery();
        TestFailedRestorationIsRetained();
        TestUnresolvedActorsSaveAndReopenInOrder();
        TestUnresolvedActorKeepsOriginalMaterialAssetReference();
        TestRecoveryReloadsARepairedMaterialDescriptor();
        TestWholeSceneParseFailureBlocksRecoveredSave();
        TestUnresolvedRelinkIsTransactional();
        TestUnresolvedRemovalRequiresExplicitRequest();
        TestProceduralLandscapeCanRetryOrBeRemoved();
    } catch (const std::exception& exception) {
        std::cerr << "Model asset test failed: " << exception.what() << '\n';
        return 1;
    }
    std::cout << "All model asset tests passed.\n";
    return 0;
}

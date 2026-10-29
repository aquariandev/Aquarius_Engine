#include "ModelAssets.hpp"

#include <assimp/Exporter.hpp>
#include <assimp/Importer.hpp>
#include <assimp/cexport.h>
#include <assimp/material.h>
#include <assimp/postprocess.h>
#include <assimp/scene.h>

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <fstream>
#include <iomanip>
#include <memory>
#include <unordered_map>
#include <utility>

namespace {

std::uint32_t ReadU32(const std::uint8_t* data) {
    return static_cast<std::uint32_t>(data[0]) |
           (static_cast<std::uint32_t>(data[1]) << 8) |
           (static_cast<std::uint32_t>(data[2]) << 16) |
           (static_cast<std::uint32_t>(data[3]) << 24);
}

bool ReadMaterialDescriptor(const std::filesystem::path& file,
                           ModelMaterial& material, std::string& error) {
    material = {};
    error.clear();
    std::ifstream input(file);
    std::string magicText;
    unsigned int version = 0;
    std::array<std::string, 6> paths;
    if (!input || !(input >> magicText >> version) ||
        magicText != "AQENMAT" ||
        (version != 1 && version != 2 && version != 3 && version != 4 &&
         version != 5) ||
        (version >= 2 && !(input >> std::quoted(material.name))) ||
        (version >= 3 &&
         (!(input >> material.textureScale.x >> material.textureScale.y >>
           material.textureScale.z) ||
          !std::isfinite(material.textureScale.x) ||
          !std::isfinite(material.textureScale.y) ||
          !std::isfinite(material.textureScale.z) ||
          material.textureScale.x <= 0.0f ||
          material.textureScale.y <= 0.0f ||
          material.textureScale.z <= 0.0f ||
          material.textureScale.x > 1000.0f ||
          material.textureScale.y > 1000.0f ||
          material.textureScale.z > 1000.0f)) ||
        (version >= 5 &&
         (!(input >> material.heightScale) ||
          !std::isfinite(material.heightScale) ||
          material.heightScale < 0.0f || material.heightScale > 10.0f ||
          !(input >> material.twoSided))) ||
        !(input >> std::quoted(paths[0]) >> std::quoted(paths[1]) >>
          std::quoted(paths[2]) >> std::quoted(paths[3])) ||
        (version >= 5 &&
         !(input >> std::quoted(paths[4]) >> std::quoted(paths[5])))) {
        error = "Material descriptor '" + file.string() +
                "' has an invalid format.";
        return false;
    }
    input >> std::ws;
    if (!input.eof()) {
        error = "Material descriptor '" + file.string() +
                "' contains unexpected trailing data.";
        return false;
    }

    std::array<std::filesystem::path, 6> resolvedPaths;
    std::error_code filesystemError;
    for (std::size_t i = 0; i < paths.size(); ++i) {
        if (paths[i].empty()) {
            continue;
        }
        const std::filesystem::path texturePath(paths[i]);
        if ((texturePath.is_absolute() && version < 4) ||
            (!texturePath.is_absolute() &&
             std::find(texturePath.begin(), texturePath.end(),
                       std::filesystem::path("..")) != texturePath.end())) {
            error = "Material descriptor paths must stay inside the material "
                    "folder unless they are absolute paths in AQENMAT 4.";
            return false;
        }
        const std::filesystem::path candidate =
            texturePath.is_absolute()
                ? texturePath
                : (file.parent_path() / texturePath).lexically_normal();
        resolvedPaths[i] =
            std::filesystem::weakly_canonical(candidate, filesystemError);
        if (filesystemError ||
            !std::filesystem::is_regular_file(resolvedPaths[i],
                                              filesystemError) ||
            filesystemError) {
            error = MissingMaterialTexturePrefix + resolvedPaths[i].string() +
                    "' is missing or unreadable." +
                    (filesystemError ? " " + filesystemError.message() : "");
            return false;
        }
    }
    material.diffuse = std::move(resolvedPaths[0]);
    material.normal = std::move(resolvedPaths[1]);
    material.roughness = std::move(resolvedPaths[2]);
    material.metallic = std::move(resolvedPaths[3]);
    material.ambientOcclusion = std::move(resolvedPaths[4]);
    material.height = std::move(resolvedPaths[5]);
    std::error_code pathError;
    material.asset = std::filesystem::weakly_canonical(file, pathError);
    if (pathError) {
        error = "Could not resolve material asset path '" + file.string() +
                "': " + pathError.message();
        return false;
    }
    return true;
}

} // namespace

bool ConvertModelToGlb(const std::filesystem::path& source,
                       const std::filesystem::path& destination,
                       std::string& error) {
    error.clear();
    std::string extension = source.extension().string();
    std::transform(extension.begin(), extension.end(), extension.begin(),
                   [](unsigned char character) {
                       return static_cast<char>(std::tolower(character));
                   });
    if (extension != ".fbx" && extension != ".obj" &&
        extension != ".gltf" && extension != ".glb") {
        error = "Supported model formats are FBX, OBJ, glTF, and GLB.";
        return false;
    }
    std::string outputExtension = destination.extension().string();
    std::transform(outputExtension.begin(), outputExtension.end(),
                   outputExtension.begin(), [](unsigned char character) {
                       return static_cast<char>(std::tolower(character));
                   });
    if (outputExtension != ".glb") {
        error = "The converted model destination must use the .glb extension.";
        return false;
    }

    Assimp::Importer importer;
    const aiScene* scene = importer.ReadFile(
        source.string(), aiProcess_Triangulate | aiProcess_GenSmoothNormals |
                             aiProcess_JoinIdenticalVertices |
                             aiProcess_ImproveCacheLocality |
                             aiProcess_ValidateDataStructure);
    if (!scene || !scene->mRootNode ||
        (scene->mFlags & AI_SCENE_FLAGS_INCOMPLETE) != 0 ||
        scene->mNumMeshes == 0) {
        error = "Could not import model '" + source.string() +
                "': " + importer.GetErrorString();
        return false;
    }

    const std::filesystem::path finalDirectory = destination.parent_path();
    std::error_code filesystemError;
    if (finalDirectory.empty() ||
        !std::filesystem::is_directory(finalDirectory.parent_path(),
                                       filesystemError) ||
        filesystemError ||
        std::filesystem::exists(finalDirectory, filesystemError) ||
        filesystemError) {
        error = "The model import destination must be a new folder inside an "
                "existing project asset directory.";
        if (filesystemError) {
            error += " " + filesystemError.message();
        }
        return false;
    }

    std::filesystem::path stagingDirectory =
        finalDirectory.string() + ".importing";
    for (unsigned int suffix = 2;
         std::filesystem::exists(stagingDirectory, filesystemError) &&
         !filesystemError;
         ++suffix) {
        stagingDirectory = finalDirectory.string() + ".importing-" +
                           std::to_string(suffix);
    }
    if (filesystemError ||
        !std::filesystem::create_directory(stagingDirectory, filesystemError) ||
        filesystemError) {
        error = "Could not create a temporary model import folder." +
                (filesystemError ? " " + filesystemError.message() : "");
        return false;
    }
    const auto failImport = [&](std::string message) {
        std::error_code cleanupError;
        std::filesystem::remove_all(stagingDirectory, cleanupError);
        error = std::move(message);
        if (cleanupError) {
            error += " Temporary files could not be fully removed: " +
                     cleanupError.message();
        }
        return false;
    };

    aiScene* copiedScene = nullptr;
    aiCopyScene(scene, &copiedScene);
    if (!copiedScene) {
        return failImport("Could not prepare the imported scene for GLB export.");
    }
    std::unique_ptr<aiScene, void (*)(const aiScene*)> exportScene(
        copiedScene, &aiFreeScene);

    std::unordered_map<std::string, std::filesystem::path> copiedTextures;
    std::filesystem::path textureDirectory;
    unsigned int nextTextureId = 0;
    for (unsigned int materialIndex = 0;
         materialIndex < exportScene->mNumMaterials; ++materialIndex) {
        aiMaterial* material = exportScene->mMaterials[materialIndex];
        for (int textureType = aiTextureType_NONE;
             textureType <= aiTextureType_UNKNOWN; ++textureType) {
            const auto type = static_cast<aiTextureType>(textureType);
            const unsigned int textureCount = material->GetTextureCount(type);
            for (unsigned int textureIndex = 0;
                 textureIndex < textureCount; ++textureIndex) {
                aiString textureReference;
                if (material->GetTexture(type, textureIndex,
                                         &textureReference) != AI_SUCCESS) {
                    return failImport(
                        "Could not read an imported material texture path.");
                }
                if (scene->GetEmbeddedTexture(textureReference.C_Str())) {
                    continue;
                }

                std::string referenceText = textureReference.C_Str();
                std::replace(referenceText.begin(), referenceText.end(), '\\',
                             '/');
                std::filesystem::path sourceTexture(referenceText);
                if (sourceTexture.is_relative()) {
                    sourceTexture = source.parent_path() / sourceTexture;
                }
                std::error_code textureError;
                const std::filesystem::path resolvedTexture =
                    std::filesystem::weakly_canonical(sourceTexture,
                                                      textureError);
                if (textureError ||
                    !std::filesystem::is_regular_file(resolvedTexture,
                                                      textureError) ||
                    textureError) {
                    return failImport(
                        "Could not locate imported texture '" +
                        sourceTexture.string() + "'" +
                        (textureError ? ": " + textureError.message() : "."));
                }

                const std::string textureKey = resolvedTexture.generic_string();
                auto copiedTexture = copiedTextures.find(textureKey);
                if (copiedTexture == copiedTextures.end()) {
                    if (textureDirectory.empty()) {
                        textureDirectory = stagingDirectory / "textures";
                        std::filesystem::create_directory(textureDirectory,
                                                          textureError);
                        if (textureError) {
                            return failImport(
                                "Could not create the imported texture folder: " +
                                textureError.message());
                        }
                    }
                    std::filesystem::path textureName =
                        "texture_" + std::to_string(nextTextureId++);
                    textureName += resolvedTexture.extension();
                    const std::filesystem::path copiedPath =
                        textureDirectory / textureName;
                    std::filesystem::copy_file(
                        resolvedTexture, copiedPath,
                        std::filesystem::copy_options::none, textureError);
                    if (textureError) {
                        return failImport(
                            "Could not package imported texture '" +
                            resolvedTexture.string() + "': " +
                            textureError.message());
                    }
                    copiedTexture =
                        copiedTextures.emplace(textureKey, copiedPath).first;
                }

                const std::filesystem::path relativeTexture =
                    copiedTexture->second.lexically_relative(stagingDirectory);
                aiString packagedReference;
                packagedReference.Set(relativeTexture.generic_string());
                material->RemoveProperty(AI_MATKEY_TEXTURE(type, textureIndex));
                if (material->AddProperty(
                        &packagedReference,
                        AI_MATKEY_TEXTURE(type, textureIndex)) != AI_SUCCESS) {
                    return failImport(
                        "Could not update a material texture path for GLB "
                        "packaging.");
                }
            }
        }
    }

    const std::filesystem::path stagedModel =
        stagingDirectory / destination.filename();
    Assimp::Exporter exporter;
    if (exporter.Export(exportScene.get(), "glb2", stagedModel.string()) !=
        AI_SUCCESS) {
        const std::string exportError = exporter.GetErrorString();
        return failImport("Could not export GLB model '" +
                          destination.filename().string() + "'" +
                          (exportError.empty() ? "." : ": " + exportError));
    }

    std::ifstream output(stagedModel, std::ios::binary | std::ios::ate);
    if (!output) {
        return failImport("The GLB exporter did not create its output file.");
    }
    const std::streamoff fileSize = output.tellg();
    std::array<std::uint8_t, 12> header{};
    output.seekg(0);
    output.read(reinterpret_cast<char*>(header.data()),
                static_cast<std::streamsize>(header.size()));
    if (!output || fileSize < static_cast<std::streamoff>(header.size()) ||
        header[0] != 'g' || header[1] != 'l' || header[2] != 'T' ||
        header[3] != 'F' || ReadU32(header.data() + 4) != 2 ||
        ReadU32(header.data() + 8) != static_cast<std::uint64_t>(fileSize)) {
        return failImport("The GLB exporter produced an invalid binary glTF "
                          "file.");
    }

    Assimp::Importer validationImporter;
    const aiScene* exportedScene = validationImporter.ReadFile(
        stagedModel.string(), aiProcess_ValidateDataStructure);
    if (!exportedScene || !exportedScene->mRootNode ||
        (exportedScene->mFlags & AI_SCENE_FLAGS_INCOMPLETE) != 0 ||
        exportedScene->mNumMeshes == 0) {
        return failImport("The exported GLB could not be reopened: " +
                          std::string(validationImporter.GetErrorString()));
    }
    for (unsigned int materialIndex = 0;
         materialIndex < exportedScene->mNumMaterials; ++materialIndex) {
        const aiMaterial* exportedMaterial =
            exportedScene->mMaterials[materialIndex];
        for (int textureType = aiTextureType_NONE;
             textureType <= aiTextureType_UNKNOWN; ++textureType) {
            const auto type = static_cast<aiTextureType>(textureType);
            for (unsigned int textureIndex = 0;
                 textureIndex < exportedMaterial->GetTextureCount(type);
                 ++textureIndex) {
                aiString textureReference;
                if (exportedMaterial->GetTexture(type, textureIndex,
                                                 &textureReference) !=
                    AI_SUCCESS) {
                    return failImport(
                        "Could not verify a texture in the exported GLB.");
                }
                if (!exportedScene->GetEmbeddedTexture(
                        textureReference.C_Str())) {
                    std::string referenceText = textureReference.C_Str();
                    std::replace(referenceText.begin(), referenceText.end(),
                                 '\\', '/');
                    const std::filesystem::path relativeTexture(referenceText);
                    if (relativeTexture.is_absolute() ||
                        std::find(relativeTexture.begin(), relativeTexture.end(),
                                  std::filesystem::path("..")) !=
                            relativeTexture.end()) {
                        return failImport(
                            "The exported GLB references a texture outside "
                            "its import folder.");
                    }
                    const std::filesystem::path packagedTexture =
                        stagedModel.parent_path() / relativeTexture;
                    std::error_code textureError;
                    if (!std::filesystem::is_regular_file(packagedTexture,
                                                          textureError) ||
                        textureError) {
                        return failImport(
                            "The exported GLB texture is not present in its "
                            "import folder: " + packagedTexture.string());
                    }
                }
            }
        }
    }

    std::filesystem::rename(stagingDirectory, finalDirectory, filesystemError);
    if (filesystemError) {
        return failImport("Could not publish imported model folder: " +
                          filesystemError.message());
    }
    error.clear();
    return true;
}

bool LoadAqenMaterial(const std::filesystem::path& file,
                      ModelMaterial& material, std::string& error) {
    return ReadMaterialDescriptor(file, material, error);
}

bool SaveAqenMaterial(const std::filesystem::path& file,
                      const ModelMaterial& material, std::string& error,
                      bool overwrite) {
    error.clear();
    if (material.name.empty()) {
        error = "Material name cannot be empty.";
        return false;
    }
    if (!std::isfinite(material.textureScale.x) ||
        !std::isfinite(material.textureScale.y) ||
        !std::isfinite(material.textureScale.z) ||
        material.textureScale.x <= 0.0f ||
        material.textureScale.y <= 0.0f ||
        material.textureScale.z <= 0.0f ||
        material.textureScale.x > 1000.0f ||
        material.textureScale.y > 1000.0f ||
        material.textureScale.z > 1000.0f) {
        error = "Material texture scale values must be between 0 and 1000.";
        return false;
    }
    if (!std::isfinite(material.heightScale) || material.heightScale < 0.0f ||
        material.heightScale > 10.0f) {
        error = "Material height displacement must be between 0 and 10.";
        return false;
    }
    std::error_code filesystemError;
    if ((!overwrite && std::filesystem::exists(file, filesystemError)) ||
        filesystemError) {
        error = "Material file already exists or could not be checked: '" +
                file.string() + "'" +
                (filesystemError ? ": " + filesystemError.message() : ".");
        return false;
    }

    const auto storedPath = [&](const std::filesystem::path& texturePath,
                                std::string& result) {
        if (texturePath.empty()) {
            result.clear();
            return true;
        }
        const std::filesystem::path resolvedTexture =
            std::filesystem::weakly_canonical(texturePath, filesystemError);
        if (filesystemError ||
            !std::filesystem::is_regular_file(resolvedTexture,
                                              filesystemError) ||
            filesystemError) {
            error = MissingMaterialTexturePrefix + texturePath.string() +
                    "' is missing or unreadable." +
                    (filesystemError ? " " + filesystemError.message() : "");
            return false;
        }
        const std::filesystem::path relative =
            resolvedTexture.lexically_relative(file.parent_path());
        const bool safeRelative =
            !relative.empty() && !relative.is_absolute() &&
            std::find(relative.begin(), relative.end(),
                      std::filesystem::path("..")) == relative.end();
        result = safeRelative ? relative.generic_string()
                              : resolvedTexture.generic_string();
        return true;
    };
    std::array<std::string, 6> paths;
    if (!storedPath(material.diffuse, paths[0]) ||
        !storedPath(material.normal, paths[1]) ||
        !storedPath(material.roughness, paths[2]) ||
        !storedPath(material.metallic, paths[3]) ||
        !storedPath(material.ambientOcclusion, paths[4]) ||
        !storedPath(material.height, paths[5])) {
        return false;
    }

    const std::filesystem::path temporaryFile = file.string() + ".tmp";
    std::ofstream output(temporaryFile, std::ios::trunc);
    if (!output) {
        error = "Could not write temporary material file '" +
                temporaryFile.string() + "'.";
        return false;
    }
    output << "AQENMAT 5\n"
           << std::quoted(material.name) << '\n'
           << std::setprecision(9) << material.textureScale.x << ' '
           << material.textureScale.y << ' ' << material.textureScale.z << ' '
           << material.heightScale << ' ' << material.twoSided << '\n'
           << std::quoted(paths[0]) << ' ' << std::quoted(paths[1]) << ' '
           << std::quoted(paths[2]) << ' ' << std::quoted(paths[3]) << ' '
           << std::quoted(paths[4]) << ' ' << std::quoted(paths[5]) << '\n';
    output.close();
    if (!output) {
        std::filesystem::remove(temporaryFile, filesystemError);
        error = "Could not finish writing material file '" + file.string() +
                "'.";
        return false;
    }

    std::filesystem::rename(temporaryFile, file, filesystemError);
    if (filesystemError) {
        std::error_code cleanupError;
        std::filesystem::remove(temporaryFile, cleanupError);
        error = "Could not publish material file '" + file.string() +
                "': " + filesystemError.message();
        if (cleanupError) {
            error += " Temporary file cleanup failed: " +
                     cleanupError.message();
        }
        return false;
    }
    return true;
}

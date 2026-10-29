#pragma once

#include "Model.hpp"

#include <filesystem>
#include <string>

inline constexpr const char* MissingMaterialTexturePrefix =
    "Material texture '";
inline bool IsMissingMaterialTextureError(const std::string& error) {
    return error.rfind(MissingMaterialTexturePrefix, 0) == 0;
}

bool ConvertModelToGlb(const std::filesystem::path& source,
                       const std::filesystem::path& destination,
                       std::string& error);
bool LoadAqenMaterial(const std::filesystem::path& file,
                      ModelMaterial& material, std::string& error);
bool SaveAqenMaterial(const std::filesystem::path& file,
                      const ModelMaterial& material, std::string& error,
                      bool overwrite = false);

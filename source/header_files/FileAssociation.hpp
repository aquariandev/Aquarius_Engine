#pragma once

#include <filesystem>
#include <string>

bool RegisterProjectFileAssociation(
    const std::filesystem::path& executable, std::string& error);

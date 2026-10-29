#pragma once

#include <filesystem>
#include <string>

struct ProjectDescriptor {
    std::string name;
    std::filesystem::path root;
    std::filesystem::path assets;
};

bool LoadProjectDescriptor(const std::filesystem::path& file,
                           ProjectDescriptor& project, std::string& error);

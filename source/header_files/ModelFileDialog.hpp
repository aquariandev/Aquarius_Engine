#pragma once

#include <filesystem>
#include <string>

bool OpenModelFileDialog(std::filesystem::path& selectedFile,
                         const std::filesystem::path& initialDirectory,
                         bool& cancelled, std::string& error);
bool OpenTextureFileDialog(std::filesystem::path& selectedFile,
                           const std::filesystem::path& initialDirectory,
                           bool& cancelled, std::string& error);
bool OpenDirectoryDialog(std::filesystem::path& selectedDirectory,
                         const std::filesystem::path& initialDirectory,
                         bool& cancelled, std::string& error);

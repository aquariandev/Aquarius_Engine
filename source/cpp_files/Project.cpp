#include "Project.hpp"

#include <fstream>
#include <map>
#include <system_error>

namespace {

bool IsStrictlyWithinDirectory(const std::filesystem::path& path,
                               const std::filesystem::path& directory) {
    const std::filesystem::path relative = path.lexically_relative(directory);
    return !relative.empty() && relative != "." &&
           *relative.begin() != std::filesystem::path("..");
}

} // namespace

bool LoadProjectDescriptor(const std::filesystem::path& file,
                           ProjectDescriptor& project, std::string& error) {
    project = {};
    error.clear();
    if (file.extension() != ".aqenproj") {
        error = "Project files must use the .aqenproj extension.";
        return false;
    }

    std::ifstream input(file);
    if (!input) {
        error = "Could not open project file '" + file.string() + "'.";
        return false;
    }

    std::map<std::string, std::string> values;
    std::string line;
    std::size_t lineNumber = 0;
    while (std::getline(input, line)) {
        ++lineNumber;
        if (line.empty()) {
            continue;
        }
        const std::size_t separator = line.find('=');
        if (separator == std::string::npos || separator == 0) {
            error = "Invalid project metadata at line " +
                    std::to_string(lineNumber) + ".";
            return false;
        }
        const std::string key = line.substr(0, separator);
        if (!values.emplace(key, line.substr(separator + 1)).second) {
            error = "Duplicate project metadata field '" + key + "'.";
            return false;
        }
    }
    if (input.bad()) {
        error = "Could not read project file '" + file.string() + "'.";
        return false;
    }

    if (!values.contains("name") || values["name"].empty() ||
        !values.contains("version") || values["version"] != "1" ||
        !values.contains("assets") || values["assets"].empty()) {
        error = "Project file is missing a required field or uses an "
                "unsupported format.";
        return false;
    }

    std::error_code filesystemError;
    const std::filesystem::path canonicalProjectFile =
        std::filesystem::canonical(file, filesystemError);
    if (filesystemError) {
        error = "Could not resolve the project folder: " +
                filesystemError.message();
        return false;
    }
    const std::filesystem::path root = canonicalProjectFile.parent_path();
    const std::filesystem::path assetsRelative(values["assets"]);
    if (assetsRelative.is_absolute()) {
        error = "The project assets path must be relative to the project.";
        return false;
    }
    const std::filesystem::path assetsPath =
        (root / assetsRelative).lexically_normal();
    if (!IsStrictlyWithinDirectory(assetsPath, root)) {
        error =
            "The project assets path must be a subdirectory of the project.";
        return false;
    }

    const std::filesystem::file_status assetsStatus =
        std::filesystem::symlink_status(assetsPath, filesystemError);
    if (filesystemError || std::filesystem::is_symlink(assetsStatus)) {
        error = "The project assets directory is unavailable or is a "
                "symbolic link.";
        if (filesystemError) {
            error += " " + filesystemError.message();
        }
        return false;
    }
    if (!std::filesystem::is_directory(assetsStatus)) {
        error = "The project assets folder is missing or unavailable: " +
                assetsPath.string();
        return false;
    }

    const std::filesystem::path assets =
        std::filesystem::canonical(assetsPath, filesystemError);
    if (filesystemError) {
        error = "Could not resolve the project assets folder: " +
                filesystemError.message();
        return false;
    }
    if (!IsStrictlyWithinDirectory(assets, root)) {
        error = "The project assets folder must resolve inside the project.";
        return false;
    }

    project.name = values["name"];
    project.root = root;
    project.assets = assets;
    return true;
}

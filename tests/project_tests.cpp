#include "Project.hpp"

#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>
#include <system_error>

namespace {

class TemporaryDirectory {
public:
    TemporaryDirectory() {
        const auto timestamp =
            std::chrono::steady_clock::now().time_since_epoch().count();
        m_Path = std::filesystem::temp_directory_path() /
                 ("aquarius-project-tests-" + std::to_string(timestamp));
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

std::filesystem::path WriteProjectDescriptor(
    const std::filesystem::path& root, const std::string& assets) {
    std::filesystem::create_directories(root);
    const std::filesystem::path descriptor = root / "Test.aqenproj";
    std::ofstream output(descriptor);
    output << "name=Test\n"
              "version=1\n"
           << "assets=" << assets << '\n';
    Require(static_cast<bool>(output),
            "Could not write the project descriptor fixture.");
    return descriptor;
}

void TestDefaultAssetsPath(const std::filesystem::path& temporaryRoot) {
    const std::filesystem::path root = temporaryRoot / "DefaultProject";
    std::filesystem::create_directories(root / "assets");
    const std::filesystem::path descriptor =
        WriteProjectDescriptor(root, "assets");

    ProjectDescriptor project;
    std::string error;
    Require(LoadProjectDescriptor(descriptor, project, error),
            "Default assets path was rejected: " + error);
    Require(project.root == std::filesystem::canonical(root) &&
                project.assets ==
                    std::filesystem::canonical(root / "assets"),
            "Default project paths were not resolved explicitly.");
}

void TestNestedAssetsPath(const std::filesystem::path& temporaryRoot) {
    const std::filesystem::path root = temporaryRoot / "NestedProject";
    const std::filesystem::path assets = root / "content" / "packed-assets";
    std::filesystem::create_directories(assets);
    const std::filesystem::path descriptor =
        WriteProjectDescriptor(root, "content/packed-assets");

    ProjectDescriptor project;
    std::string error;
    Require(LoadProjectDescriptor(descriptor, project, error),
            "Nested assets path was rejected: " + error);
    Require(project.root == std::filesystem::canonical(root) &&
                project.assets == std::filesystem::canonical(assets),
            "Nested assets path was not resolved relative to the project.");
}

void TestMissingAssetsPath(const std::filesystem::path& temporaryRoot) {
    const std::filesystem::path root = temporaryRoot / "MissingProject";
    const std::filesystem::path descriptor =
        WriteProjectDescriptor(root, "missing-assets");

    ProjectDescriptor project;
    std::string error;
    Require(!LoadProjectDescriptor(descriptor, project, error),
            "A missing assets directory was accepted.");
    Require(!error.empty(), "Missing assets directory returned no error.");
}

void TestAssetsPathCannotBeProjectRoot(
    const std::filesystem::path& temporaryRoot) {
    const std::filesystem::path root = temporaryRoot / "RootAssetsProject";
    const std::filesystem::path descriptor =
        WriteProjectDescriptor(root, ".");

    ProjectDescriptor project;
    std::string error;
    Require(!LoadProjectDescriptor(descriptor, project, error),
            "The project root was accepted as the assets directory.");
}

void TestSymlinkedAssetsPaths(const std::filesystem::path& temporaryRoot) {
    const std::filesystem::path outsideAssets =
        temporaryRoot / "OutsideAssets";
    const std::filesystem::path outsideProject =
        temporaryRoot / "OutsideLinkProject";
    std::filesystem::create_directories(outsideAssets);
    std::filesystem::create_directories(outsideProject);

    std::error_code symlinkError;
    std::filesystem::create_directory_symlink(
        outsideAssets, outsideProject / "assets", symlinkError);
    if (symlinkError) {
        std::cout << "Skipped symlink path checks: "
                  << symlinkError.message() << '\n';
        return;
    }

    ProjectDescriptor project;
    std::string error;
    const std::filesystem::path outsideDescriptor =
        WriteProjectDescriptor(outsideProject, "assets");
    Require(!LoadProjectDescriptor(outsideDescriptor, project, error),
            "An assets directory symlink to an external target was accepted.");

    const std::filesystem::path insideProject =
        temporaryRoot / "InsideLinkProject";
    const std::filesystem::path realAssets = insideProject / "real-assets";
    std::filesystem::create_directories(realAssets);
    std::filesystem::create_directory_symlink(
        realAssets, insideProject / "assets", symlinkError);
    Require(!symlinkError, "Could not create the in-project symlink fixture: " +
                               symlinkError.message());
    const std::filesystem::path insideDescriptor =
        WriteProjectDescriptor(insideProject, "assets");
    Require(!LoadProjectDescriptor(insideDescriptor, project, error),
            "A symlinked assets directory inside the project was accepted.");

    const std::filesystem::path linkedParentProject =
        temporaryRoot / "LinkedParentProject";
    const std::filesystem::path externalParent =
        temporaryRoot / "ExternalParent";
    std::filesystem::create_directories(externalParent / "assets");
    std::filesystem::create_directories(linkedParentProject);
    std::filesystem::create_directory_symlink(
        externalParent, linkedParentProject / "redirect", symlinkError);
    Require(!symlinkError, "Could not create the symlinked parent fixture: " +
                               symlinkError.message());
    const std::filesystem::path linkedParentDescriptor =
        WriteProjectDescriptor(linkedParentProject, "redirect/assets");
    Require(!LoadProjectDescriptor(linkedParentDescriptor, project, error),
            "An assets path resolving outside the project was accepted.");
}

} // namespace

int main() {
    try {
        TemporaryDirectory temporary;
        TestDefaultAssetsPath(temporary.Path());
        TestNestedAssetsPath(temporary.Path());
        TestMissingAssetsPath(temporary.Path());
        TestAssetsPathCannotBeProjectRoot(temporary.Path());
        TestSymlinkedAssetsPaths(temporary.Path());
    } catch (const std::exception& exception) {
        std::cerr << "Project path test failed: " << exception.what() << '\n';
        return 1;
    }
    std::cout << "Project path tests passed.\n";
    return 0;
}

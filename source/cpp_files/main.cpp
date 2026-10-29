#include "Application.hpp"
#include "FileAssociation.hpp"
#include "Project.hpp"
#include "ProjectLauncher.hpp"
#include "SplashScreen.hpp"

#include <iostream>

int main(int argc, char* argv[]) {
    if (argc == 3 &&
        std::string(argv[1]) == "--register-file-association") {
        std::string error;
        if (!RegisterProjectFileAssociation(argv[2], error)) {
            std::cerr << "Project-file association failed: " << error << '\n';
            return 1;
        }
        std::cout << "Registered .aqenproj files to open with Aquarius Engine "
                     "for this user.\n";
        return 0;
    }

    if (argc > 2 ||
        (argc == 2 && std::string(argv[1]).starts_with("--"))) {
        std::cerr << "Usage: AquariusEngine [project.aqenproj]\n"
                     "       AquariusEngine --register-file-association "
                     "<executable>\n";
        return 1;
    }

    std::optional<std::filesystem::path> projectFile;
    bool startMaximized = false;
    std::string splashError;
    if (!ShowSplashScreen(splashError)) {
        std::cerr << "Splash screen failed: " << splashError << '\n';
        return 1;
    }
    if (argc == 2) {
        projectFile = std::filesystem::path(argv[1]);
    } else {
        ProjectLauncher launcher;
        std::string error;
        if (!launcher.Initialize(error)) {
            std::cerr << "Project launcher initialization failed: " << error
                      << '\n';
            return 1;
        }
        projectFile = launcher.Run();
        startMaximized = launcher.IsMaximized();
    }

    if (!projectFile) {
        return 0;
    }

    ProjectDescriptor project;
    std::string error;
    if (!LoadProjectDescriptor(*projectFile, project, error)) {
        std::cerr << "Project loading failed: " << error << '\n';
        return 1;
    }

    Application app(project.root, project.assets, startMaximized);
    app.Run();

    return 0;
}
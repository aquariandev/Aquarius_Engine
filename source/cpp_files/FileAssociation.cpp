#include "FileAssociation.hpp"

#include <cerrno>
#include <cstdlib>
#include <fstream>
#include <string>
#include <vector>

#ifdef __linux__
#include <sys/wait.h>
#include <unistd.h>
#endif

namespace {

#ifdef __linux__
bool RunCommand(const std::vector<std::string>& arguments) {
    const pid_t child = fork();
    if (child < 0) {
        return false;
    }
    if (child == 0) {
        std::vector<char*> argumentPointers;
        argumentPointers.reserve(arguments.size() + 1);
        for (const std::string& argument : arguments) {
            argumentPointers.push_back(const_cast<char*>(argument.c_str()));
        }
        argumentPointers.push_back(nullptr);
        execvp(argumentPointers.front(), argumentPointers.data());
        _exit(127);
    }

    int status = 0;
    pid_t result = 0;
    do {
        result = waitpid(child, &status, 0);
    } while (result < 0 && errno == EINTR);
    if (result < 0) {
        return false;
    }
    return WIFEXITED(status) && WEXITSTATUS(status) == 0;
}

std::string EscapeDesktopExecArgument(const std::string& argument) {
    std::string escaped;
    escaped.reserve(argument.size());
    for (const char character : argument) {
        if (character == '\\' || character == '"' || character == '`' ||
            character == '$') {
            escaped.push_back('\\');
        }
        escaped.push_back(character);
    }
    return escaped;
}
#endif

} // namespace

bool RegisterProjectFileAssociation(const std::filesystem::path& executable,
                                    std::string& error) {
#ifndef __linux__
    (void)executable;
    error = "Automatic .aqenproj file association is currently supported on Linux only.";
    return false;
#else
    const char* home = std::getenv("HOME");
    if (!home || *home == '\0') {
        error = "Could not locate the current user's home directory.";
        return false;
    }

    std::error_code filesystemError;
    const std::filesystem::path absoluteExecutable =
        std::filesystem::canonical(executable, filesystemError);
    if (filesystemError) {
        error = "Could not resolve Aquarius Engine's executable path: " +
                filesystemError.message();
        return false;
    }

    const std::filesystem::path dataHome =
        std::filesystem::path(home) / ".local" / "share";
    const std::filesystem::path applicationsDirectory =
        dataHome / "applications";
    const std::filesystem::path mimeDirectory =
        dataHome / "mime" / "packages";
    std::filesystem::create_directories(applicationsDirectory, filesystemError);
    if (filesystemError) {
        error = "Could not create the user's applications directory: " +
                filesystemError.message();
        return false;
    }
    std::filesystem::create_directories(mimeDirectory, filesystemError);
    if (filesystemError) {
        error = "Could not create the user's MIME directory: " +
                filesystemError.message();
        return false;
    }

    const std::filesystem::path desktopFile =
        applicationsDirectory / "aquarius-engine.desktop";
    std::ofstream desktop(desktopFile, std::ios::trunc);
    if (!desktop) {
        error = "Could not write " + desktopFile.string() + ".";
        return false;
    }
    desktop << "[Desktop Entry]\n"
            << "Type=Application\n"
            << "Name=Aquarius Engine\n"
            << "Exec=\"" << EscapeDesktopExecArgument(
                               absoluteExecutable.string())
            << "\" %f\n"
            << "Terminal=false\n"
            << "Categories=Development;IDE;\n"
            << "MimeType=application/x-aquarius-project;\n";
    desktop.close();
    if (!desktop) {
        error = "Could not finish writing " + desktopFile.string() + ".";
        return false;
    }

    const std::filesystem::path mimePackage =
        mimeDirectory / "aqenproj.xml";
    std::ofstream mime(mimePackage, std::ios::trunc);
    if (!mime) {
        error = "Could not write " + mimePackage.string() + ".";
        return false;
    }
    mime << "<?xml version=\"1.0\"?>\n"
         << "<mime-info xmlns=\"http://www.freedesktop.org/standards/shared-mime-info\">\n"
         << "  <mime-type type=\"application/x-aquarius-project\">\n"
         << "    <comment>Aquarius Engine project</comment>\n"
         << "    <glob pattern=\"*.aqenproj\"/>\n"
         << "  </mime-type>\n"
         << "</mime-info>\n";
    mime.close();
    if (!mime) {
        error = "Could not finish writing " + mimePackage.string() + ".";
        return false;
    }

    if (!RunCommand({"update-mime-database",
                     (dataHome / "mime").string()})) {
        error = "Could not update the user MIME database. Ensure "
                "update-mime-database is installed.";
        return false;
    }
    if (!RunCommand({"xdg-mime", "default", desktopFile.filename().string(),
                     "application/x-aquarius-project"})) {
        error = "Could not register Aquarius Engine as the default .aqenproj "
                "handler. Ensure xdg-mime is installed.";
        return false;
    }
    return true;
#endif
}

#include "ModelFileDialog.hpp"

#include <cerrno>
#include <string>
#include <vector>

#ifdef __linux__
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#endif

namespace {

#ifdef __linux__
enum class DialogResult {
    Selected,
    Cancelled,
    Unavailable,
    Failed
};

DialogResult RunDialog(const std::vector<std::string>& arguments,
                       std::filesystem::path& selectedFile,
                       std::string& error) {
    int outputPipe[2];
    if (pipe(outputPipe) != 0) {
        error = "Could not create a pipe for the file chooser.";
        return DialogResult::Failed;
    }

    const pid_t child = fork();
    if (child < 0) {
        close(outputPipe[0]);
        close(outputPipe[1]);
        error = "Could not start the system file chooser.";
        return DialogResult::Failed;
    }
    if (child == 0) {
        close(outputPipe[0]);
        if (dup2(outputPipe[1], STDOUT_FILENO) < 0) {
            _exit(126);
        }
        close(outputPipe[1]);
        std::vector<char*> argumentPointers;
        argumentPointers.reserve(arguments.size() + 1);
        for (const std::string& argument : arguments) {
            argumentPointers.push_back(const_cast<char*>(argument.c_str()));
        }
        argumentPointers.push_back(nullptr);
        execvp(argumentPointers.front(), argumentPointers.data());
        _exit(errno == ENOENT ? 127 : 126);
    }

    close(outputPipe[1]);
    std::string output;
    char buffer[512];
    ssize_t bytesRead = 0;
    while ((bytesRead = read(outputPipe[0], buffer, sizeof(buffer))) > 0) {
        output.append(buffer, static_cast<std::size_t>(bytesRead));
    }
    const int readError = bytesRead < 0 ? errno : 0;
    close(outputPipe[0]);

    int status = 0;
    pid_t waitResult = 0;
    do {
        waitResult = waitpid(child, &status, 0);
    } while (waitResult < 0 && errno == EINTR);
    if (readError != 0 || waitResult < 0) {
        error = "Could not read the system file chooser result.";
        return DialogResult::Failed;
    }
    if (!WIFEXITED(status)) {
        error = "The system file chooser exited unexpectedly.";
        return DialogResult::Failed;
    }
    const int exitCode = WEXITSTATUS(status);
    if (exitCode == 127) {
        return DialogResult::Unavailable;
    }
    if (exitCode == 1) {
        return DialogResult::Cancelled;
    }
    if (exitCode != 0) {
        error = "The system file chooser failed.";
        return DialogResult::Failed;
    }

    while (!output.empty() &&
           (output.back() == '\n' || output.back() == '\r')) {
        output.pop_back();
    }
    if (output.empty()) {
        error = "The system file chooser returned an empty path.";
        return DialogResult::Failed;
    }
    selectedFile = std::filesystem::path(output);
    return DialogResult::Selected;
}
#endif

} // namespace

bool OpenModelFileDialog(std::filesystem::path& selectedFile,
                         const std::filesystem::path& initialDirectory,
                         bool& cancelled, std::string& error) {
    selectedFile.clear();
    cancelled = false;
    error.clear();
#ifndef __linux__
    error = "The system model file chooser is currently supported on Linux only.";
    return false;
#else
    const std::string initialPath =
        initialDirectory.empty()
            ? std::string(".")
            : (initialDirectory / "").string();
    const DialogResult zenity = RunDialog(
        {"zenity", "--file-selection", "--title=Upload 3D model",
         "--filename=" + initialPath,
         "--file-filter=3D models | *.fbx *.obj *.gltf *.glb",
         "--file-filter=All files | *"},
        selectedFile, error);
    if (zenity == DialogResult::Selected) {
        return true;
    }
    if (zenity == DialogResult::Cancelled) {
        cancelled = true;
        return false;
    }
    if (zenity == DialogResult::Failed) {
        return false;
    }

    const DialogResult kdialog = RunDialog(
        {"kdialog", "--getopenfilename", initialPath,
         "3D models (*.fbx *.obj *.gltf *.glb)"},
        selectedFile, error);
    if (kdialog == DialogResult::Selected) {
        return true;
    }
    if (kdialog == DialogResult::Cancelled) {
        cancelled = true;
        return false;
    }
    if (kdialog == DialogResult::Unavailable) {
        error = "No supported system file chooser was found. Install zenity or kdialog.";
    }
    return false;
#endif
}

bool OpenTextureFileDialog(std::filesystem::path& selectedFile,
                           const std::filesystem::path& initialDirectory,
                           bool& cancelled, std::string& error) {
    selectedFile.clear();
    cancelled = false;
    error.clear();
#ifndef __linux__
    error =
        "The system texture file chooser is currently supported on Linux only.";
    return false;
#else
    const std::string initialPath =
        initialDirectory.empty()
            ? std::string(".")
            : (initialDirectory / "").string();
    const DialogResult zenity = RunDialog(
        {"zenity", "--file-selection", "--title=Choose material texture",
         "--filename=" + initialPath,
         "--file-filter=Images | *.png *.jpg *.jpeg *.bmp *.tga *.hdr *.pic",
         "--file-filter=All files | *"},
        selectedFile, error);
    if (zenity == DialogResult::Selected) {
        return true;
    }
    if (zenity == DialogResult::Cancelled) {
        cancelled = true;
        return false;
    }
    if (zenity == DialogResult::Failed) {
        return false;
    }

    const DialogResult kdialog = RunDialog(
        {"kdialog", "--getopenfilename", initialPath,
         "Images (*.png *.jpg *.jpeg *.bmp *.tga *.hdr *.pic)"},
        selectedFile, error);
    if (kdialog == DialogResult::Selected) {
        return true;
    }
    if (kdialog == DialogResult::Cancelled) {
        cancelled = true;
        return false;
    }
    if (kdialog == DialogResult::Unavailable) {
        error = "No supported system file chooser was found. Install zenity or "
                "kdialog.";
    }
    return false;
#endif
}

bool OpenDirectoryDialog(std::filesystem::path& selectedDirectory,
                         const std::filesystem::path& initialDirectory,
                         bool& cancelled, std::string& error) {
    selectedDirectory.clear();
    cancelled = false;
    error.clear();
#ifndef __linux__
    error = "The system folder chooser is currently supported on Linux only.";
    return false;
#else
    const std::string initialPath =
        initialDirectory.empty() ? std::string(".")
                                 : (initialDirectory / "").string();
    const DialogResult zenity =
        RunDialog({"zenity", "--file-selection", "--directory",
                   "--title=Choose project parent folder",
                   "--filename=" + initialPath},
                  selectedDirectory, error);
    if (zenity == DialogResult::Selected) {
        return true;
    }
    if (zenity == DialogResult::Cancelled) {
        cancelled = true;
        return false;
    }
    if (zenity == DialogResult::Failed) {
        return false;
    }

    const DialogResult kdialog =
        RunDialog({"kdialog", "--getexistingdirectory", initialPath,
                   "Choose project parent folder"},
                  selectedDirectory, error);
    if (kdialog == DialogResult::Selected) {
        return true;
    }
    if (kdialog == DialogResult::Cancelled) {
        cancelled = true;
        return false;
    }
    if (kdialog == DialogResult::Unavailable) {
        error = "No supported system folder chooser was found. Install zenity "
                "or kdialog.";
    }
    return false;
#endif
}

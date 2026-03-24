#pragma once

#include <optional>
#include <string>
#include <vector>

namespace Atlas::Platform {

struct FileDialogFilter {
    // Example: description="Scene", extensions={"scene"}
    std::string description;
    std::vector<std::string> extensions; // without dot
};

std::optional<std::string> openFileDialog(const std::string& title,
                                         const std::string& defaultPath = std::string(),
                                         const std::vector<FileDialogFilter>& filters = {});

std::optional<std::string> saveFileDialog(const std::string& title,
                                         const std::string& defaultPath = std::string(),
                                         const std::vector<FileDialogFilter>& filters = {});

std::optional<std::string> openFolderDialog(const std::string& title,
                                           const std::string& defaultPath = std::string());

} // namespace Atlas::Platform

#pragma once

#include <string>
#include <vector>
#include <filesystem>

namespace fs = std::filesystem;

struct Project {
    std::string name;
    std::string path;
    std::string assetsPath;
    std::vector<std::string> recentFiles;
    
    Project() = default;
    Project(const std::string& name, const std::string& path) 
        : name(name), path(path), assetsPath(path + "/assets") {}
};

class ProjectManager {
public:
    ProjectManager() = default;
    
    void createNewProject(const std::string& name, const std::string& path);
    void openProject(const std::string& projectPath);
    void saveProject();
    void closeProject();
    
    Project& getCurrentProject() { return currentProject; }
    bool hasProject() const { return hasCurrentProject; }
    
    std::vector<std::string> getAssetFiles(const std::string& subfolder = "");
    std::vector<std::string> getModelFiles();
    std::vector<std::string> getTextureFiles();
    
    std::string getAssetFullPath(const std::string& relativePath);
    std::string getProjectPath() const { return currentProject.path; }
    std::string getAssetsPath() const { return currentProject.assetsPath; }
    
    struct FileEntry {
        std::string name;
        std::string fullPath;
        std::string relativePath;
        bool isFolder;
        std::vector<FileEntry> children;
    };

    // Returns a cached snapshot of the assets tree.
    // Call invalidateAssetTreeCache() after filesystem changes.
    FileEntry getAssetTree(const std::string& subfolder = "");
    void invalidateAssetTreeCache();
    
private:
    Project currentProject;
    bool hasCurrentProject = false;

    FileEntry buildAssetTree(const std::string& subfolder);
    FileEntry m_AssetTreeCache{};
    bool m_AssetTreeCacheValid = false;

    void ensureDirectories();
    std::vector<std::string> filterFilesByExtension(const std::vector<fs::path>& files, const std::vector<std::string>& extensions);
};

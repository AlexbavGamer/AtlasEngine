#include "project_manager.h"
#include <iostream>

void ProjectManager::createNewProject(const std::string& name, const std::string& path) {
    currentProject = Project(name, path);
    hasCurrentProject = true;
    ensureDirectories();
    std::cout << "Created new project: " << name << " at " << path << std::endl;
}

void ProjectManager::openProject(const std::string& projectPath) {
    currentProject.path = projectPath;
    currentProject.assetsPath = projectPath + "/assets";
    currentProject.name = fs::path(projectPath).filename().string();
    hasCurrentProject = true;
    
    if (!fs::exists(currentProject.assetsPath)) {
        ensureDirectories();
    }
    
    std::cout << "Opened project: " << currentProject.name << std::endl;
}

void ProjectManager::saveProject() {
    if (!hasCurrentProject) return;
    // Save project metadata
    std::cout << "Project saved: " << currentProject.name << std::endl;
}

void ProjectManager::closeProject() {
    hasCurrentProject = false;
    currentProject = Project();
}

void ProjectManager::ensureDirectories() {
    fs::create_directories(currentProject.assetsPath + "/models");
    fs::create_directories(currentProject.assetsPath + "/textures");
    fs::create_directories(currentProject.assetsPath + "/materials");
    fs::create_directories(currentProject.assetsPath + "/scenes");
}

std::vector<std::string> ProjectManager::getAssetFiles(const std::string& subfolder) {
    std::vector<std::string> files;
    
    if (!hasCurrentProject) return files;
    
    std::string fullPath = currentProject.assetsPath;
    if (!subfolder.empty()) {
        fullPath += "/" + subfolder;
    }
    
    if (!fs::exists(fullPath)) return files;
    
    for (const auto& entry : fs::recursive_directory_iterator(fullPath)) {
        if (entry.is_regular_file()) {
            std::string relativePath = entry.path().string();
            relativePath = relativePath.substr(currentProject.assetsPath.length() + 1);
            files.push_back(relativePath);
        }
    }
    
    return files;
}

std::vector<std::string> ProjectManager::getModelFiles() {
    std::vector<std::string> extensions = {".fbx", ".gltf", ".glb", ".obj", ".dae"};
    std::vector<std::string> allFiles = getAssetFiles("models");
    
    std::vector<std::string> models;
    for (const auto& file : allFiles) {
        std::string ext = fs::path(file).extension().string();
        for (const auto& validExt : extensions) {
            if (ext == validExt) {
                models.push_back(file);
                break;
            }
        }
    }
    return models;
}

std::vector<std::string> ProjectManager::getTextureFiles() {
    std::vector<std::string> extensions = {".png", ".jpg", ".jpeg", ".bmp", ".tga", ".hdr"};
    std::vector<std::string> allFiles = getAssetFiles("textures");
    
    std::vector<std::string> textures;
    for (const auto& file : allFiles) {
        std::string ext = fs::path(file).extension().string();
        for (const auto& validExt : extensions) {
            if (ext == validExt) {
                textures.push_back(file);
                break;
            }
        }
    }
    return textures;
}

std::string ProjectManager::getAssetFullPath(const std::string& relativePath) {
    if (!hasCurrentProject) return "";
    return currentProject.assetsPath + "/" + relativePath;
}

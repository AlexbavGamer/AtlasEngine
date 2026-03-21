#include "project_manager.h"
#include <iostream>
#include <algorithm>

void ProjectManager::createNewProject(const std::string& name, const std::string& path) {
    currentProject = Project(name, path);
    hasCurrentProject = true;
    m_AssetTreeCacheValid = false;
    ensureDirectories();
}

void ProjectManager::openProject(const std::string& projectPath) {
    currentProject.path = projectPath;
    currentProject.assetsPath = projectPath + "/assets";
    currentProject.name = fs::path(projectPath).filename().string();
    hasCurrentProject = true;
    m_AssetTreeCacheValid = false;

    if (!fs::exists(currentProject.assetsPath)) {
        ensureDirectories();
    }
}

void ProjectManager::saveProject() {
    if (!hasCurrentProject) return;
}

void ProjectManager::closeProject() {
    hasCurrentProject = false;
    currentProject = Project();
    m_AssetTreeCacheValid = false;
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
    std::vector<std::string> extensions = {".fbx", ".gltf", ".glb", ".obj", ".dae", ".blend"};
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

void ProjectManager::invalidateAssetTreeCache() {
    m_AssetTreeCacheValid = false;
}

ProjectManager::FileEntry ProjectManager::getAssetTree(const std::string& subfolder) {
    ProjectManager::FileEntry root;
    root.name = subfolder.empty() ? "assets" : subfolder;
    root.fullPath = currentProject.assetsPath + (subfolder.empty() ? "" : "/" + subfolder);
    root.relativePath = subfolder;
    root.isFolder = true;

    if (!hasCurrentProject) {
        return root;
    }

    if (!m_AssetTreeCacheValid) {
        m_AssetTreeCache = buildAssetTree("");
        m_AssetTreeCacheValid = true;
    }

    if (subfolder.empty()) {
        return m_AssetTreeCache;
    }

    // Find requested node in the cached tree.
    ProjectManager::FileEntry current = m_AssetTreeCache;
    if (subfolder.empty()) {
        return current;
    }

    std::string token;
    std::vector<std::string> parts;
    for (char c : subfolder) {
        if (c == '/' || c == '\\') {
            if (!token.empty()) {
                parts.push_back(token);
                token.clear();
            }
        } else {
            token.push_back(c);
        }
    }
    if (!token.empty()) {
        parts.push_back(token);
    }

    for (const auto& folderName : parts) {
        bool found = false;
        for (auto& child : current.children) {
            if (child.name == folderName && child.isFolder) {
                current = child;
                found = true;
                break;
            }
        }
        if (!found) {
            return m_AssetTreeCache;
        }
    }

    return current;
}

ProjectManager::FileEntry ProjectManager::buildAssetTree(const std::string& subfolder) {
    ProjectManager::FileEntry root;
    root.name = subfolder.empty() ? "assets" : subfolder;
    root.fullPath = currentProject.assetsPath + (subfolder.empty() ? "" : "/" + subfolder);
    root.relativePath = subfolder;
    root.isFolder = true;

    if (!hasCurrentProject) {
        return root;
    }
    if (!fs::exists(root.fullPath)) {
        return root;
    }
    if (!fs::is_directory(root.fullPath)) {
        return root;
    }

    std::vector<ProjectManager::FileEntry> children;

    for (const auto& entry : fs::directory_iterator(root.fullPath)) {
        std::string name = entry.path().filename().string();

        bool hidden = name.empty() || name[0] == '.';
        if (hidden) {
            continue;
        }

        ProjectManager::FileEntry child;
        child.name = std::move(name);
        child.fullPath = entry.path().string();
        child.relativePath = subfolder.empty() ? child.name : subfolder + "/" + child.name;
        child.isFolder = entry.is_directory();

        if (child.isFolder) {
            child = buildAssetTree(child.relativePath);
        }

        children.push_back(std::move(child));
    }

    std::sort(children.begin(), children.end(), [](const ProjectManager::FileEntry& a, const ProjectManager::FileEntry& b) {
        if (a.isFolder != b.isFolder) return a.isFolder;
        return a.name < b.name;
    });

    root.children = std::move(children);
    return root;
}
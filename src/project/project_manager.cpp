#include "project_manager.h"
#include <iostream>
#include <algorithm>

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
    
    std::cout << "[openProject] projectPath: " << projectPath << std::endl;
    std::cout << "[openProject] assetsPath: " << currentProject.assetsPath << std::endl;
    
    if (!fs::exists(currentProject.assetsPath)) {
        std::cout << "[openProject] Assets path does not exist, creating..." << std::endl;
        ensureDirectories();
    } else {
        std::cout << "[openProject] Assets path exists" << std::endl;
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

ProjectManager::FileEntry ProjectManager::getAssetTree(const std::string& subfolder) {
    ProjectManager::FileEntry root;
    root.name = subfolder.empty() ? "assets" : subfolder;
    root.fullPath = currentProject.assetsPath + (subfolder.empty() ? "" : "/" + subfolder);
    root.relativePath = subfolder;
    root.isFolder = true;

    // std::cout << "\n[getAssetTree] level = \"" << subfolder << "\"\n";
    // std::cout << "  assetsPath     = " << currentProject.assetsPath << "\n";
    // std::cout << "  fullPath       = " << root.fullPath << "\n";

    if (!hasCurrentProject) {
        // std::cout << "  → sem projeto aberto\n";
        return root;
    }
    if (!fs::exists(root.fullPath)) {
        // std::cout << "  → caminho NÃO existe\n";
        return root;
    }
    if (!fs::is_directory(root.fullPath)) {
        // std::cout << "  → NÃO é diretório\n";
        return root;
    }

    std::vector<ProjectManager::FileEntry> children;

    int count_visible = 0;
    int count_hidden  = 0;

    for (const auto& entry : fs::directory_iterator(root.fullPath)) {
        std::string name = entry.path().filename().string();

        bool hidden = name.empty() || name[0] == '.';
        if (hidden) {
            count_hidden++;
            // std::cout << "  Ignorando oculto: " << name << "\n";
            continue;
        }

        count_visible++;

        ProjectManager::FileEntry child;
        child.name         = std::move(name);
        child.fullPath     = entry.path().string();
        child.relativePath = subfolder.empty() ? child.name : subfolder + "/" + child.name;
        child.isFolder     = entry.is_directory();

        // std::cout << "  +" << (child.isFolder ? "[DIR ]" : "[FILE]") 
        //           << " " << child.name 
        //           << "  → rel: " << child.relativePath << "\n";

        if (child.isFolder) {
            // std::cout << "     ↓ recursão\n";
            child = getAssetTree(child.relativePath);
        }

        children.push_back(std::move(child));
    }

    // std::cout << "  Ocultos ignorados: " << count_hidden << "\n";
    // std::cout << "  Itens visíveis encontrados: " << count_visible << "\n";

    // sort ...
    std::sort(children.begin(), children.end(), [](const ProjectManager::FileEntry& a, const ProjectManager::FileEntry& b) {
        if (a.isFolder != b.isFolder) return a.isFolder;
        return a.name < b.name;
    });

    root.children = std::move(children);
    // std::cout << "[getAssetTree] Retornando " << root.children.size() << " filhos\n\n";

    return root;
}
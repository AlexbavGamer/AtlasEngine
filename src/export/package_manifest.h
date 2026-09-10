#pragma once

#include <string>

namespace Atlas::Export {

struct PackageManifest {
    std::string packageVersion = "ATLAS_PACKAGE_V1";
    std::string startupScene = "assets/scenes/main.scene";
    std::string assetsRoot = "assets";
    bool useEmbeddedShaders = true;
    std::string shadersPath = "shaders";
    // Optional `.atlaspack` archive (relative to the package root).
    // Empty = loose-file mode (old manifests keep working: the key is
    // simply absent, struct defaults apply).
    std::string pakFile;
};

bool savePackageManifest(const PackageManifest& manifest, const std::string& path);
bool loadPackageManifest(const std::string& path, PackageManifest& outManifest);

} // namespace Atlas::Export

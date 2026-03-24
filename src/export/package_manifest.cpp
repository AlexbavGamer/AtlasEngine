#include "package_manifest.h"

#include <fstream>
#include <iomanip>
#include <sstream>

namespace Atlas::Export {
namespace {

std::string quote(const std::string& value) {
    std::ostringstream oss;
    oss << std::quoted(value);
    return oss.str();
}

bool readQuoted(std::istream& is, std::string& out) {
    is >> std::quoted(out);
    return !is.fail();
}

} // namespace

bool savePackageManifest(const PackageManifest& manifest, const std::string& path) {
    std::ofstream out(path, std::ios::trunc);
    if (!out.is_open()) {
        return false;
    }

    out << manifest.packageVersion << '\n';
    out << "startup_scene " << quote(manifest.startupScene) << '\n';
    out << "assets_root " << quote(manifest.assetsRoot) << '\n';
    out << "use_embedded_shaders " << (manifest.useEmbeddedShaders ? 1 : 0) << '\n';
    out << "shaders_path " << quote(manifest.shadersPath) << '\n';
    return true;
}

bool loadPackageManifest(const std::string& path, PackageManifest& outManifest) {
    std::ifstream in(path);
    if (!in.is_open()) {
        return false;
    }

    PackageManifest manifest;
    std::string token;
    if (!(in >> token) || token != manifest.packageVersion) {
        return false;
    }

    while (in >> token) {
        if (token == "startup_scene") {
            if (!readQuoted(in, manifest.startupScene)) return false;
        } else if (token == "assets_root") {
            if (!readQuoted(in, manifest.assetsRoot)) return false;
        } else if (token == "use_embedded_shaders") {
            int value = 0;
            in >> value;
            manifest.useEmbeddedShaders = (value != 0);
        } else if (token == "shaders_path") {
            if (!readQuoted(in, manifest.shadersPath)) return false;
        } else {
            return false;
        }
        if (in.fail()) {
            return false;
        }
    }

    outManifest = std::move(manifest);
    return true;
}

} // namespace Atlas::Export

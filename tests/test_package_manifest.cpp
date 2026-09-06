// Unit tests for Atlas::Export package manifest round-trip.
#include "tests.h"

#include <cstdio>
#include <filesystem>
#include <string>

#include "export/package_manifest.h"

namespace {

std::string tempPath(const std::string& name) {
    return (std::filesystem::temp_directory_path() / name).string();
}

} // namespace

ATLAS_TEST(PackageManifest, RoundTrip) {
    Atlas::Export::PackageManifest in;
    in.startupScene = "assets/scenes/main scene with spaces.scene";
    in.assetsRoot = "assets";
    in.useEmbeddedShaders = false;
    in.shadersPath = "shaders/custom";

    const std::string path = tempPath("atlas_manifest_roundtrip.txt");
    EXPECT_TRUE(Atlas::Export::savePackageManifest(in, path));

    Atlas::Export::PackageManifest out;
    EXPECT_TRUE(Atlas::Export::loadPackageManifest(path, out));
    EXPECT_EQ(out.packageVersion, in.packageVersion);
    EXPECT_EQ(out.startupScene, in.startupScene);
    EXPECT_EQ(out.assetsRoot, in.assetsRoot);
    EXPECT_EQ(out.useEmbeddedShaders, in.useEmbeddedShaders);
    EXPECT_EQ(out.shadersPath, in.shadersPath);

    std::remove(path.c_str());
}

ATLAS_TEST(PackageManifest, MissingFileFails) {
    Atlas::Export::PackageManifest out;
    EXPECT_TRUE(!Atlas::Export::loadPackageManifest(
        tempPath("atlas_manifest_does_not_exist_12345.txt"), out));
}

ATLAS_TEST(PackageManifest, BadVersionFails) {
    const std::string path = tempPath("atlas_manifest_badversion.txt");
    FILE* f = std::fopen(path.c_str(), "w");
    EXPECT_TRUE(f != nullptr);
    std::fputs("WRONG_VERSION\nstartup_scene \"x\"\n", f);
    std::fclose(f);

    Atlas::Export::PackageManifest out;
    EXPECT_TRUE(!Atlas::Export::loadPackageManifest(path, out));
    std::remove(path.c_str());
}

ATLAS_TEST(PackageManifest, UnknownKeyFails) {
    const std::string path = tempPath("atlas_manifest_badkey.txt");
    FILE* f = std::fopen(path.c_str(), "w");
    EXPECT_TRUE(f != nullptr);
    std::fputs("ATLAS_PACKAGE_V1\nbogus_key \"x\"\n", f);
    std::fclose(f);

    Atlas::Export::PackageManifest out;
    EXPECT_TRUE(!Atlas::Export::loadPackageManifest(path, out));
    std::remove(path.c_str());
}

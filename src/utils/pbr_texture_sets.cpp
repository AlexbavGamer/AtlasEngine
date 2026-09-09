// PBR texture-set helpers needing stb (kept out of the header so the
// discovery/matching part stays light and the write implementation lives in
// exactly one translation unit).
#include "pbr_texture_sets.h"

#include <stb_image.h>

#define STB_IMAGE_WRITE_IMPLEMENTATION
#include <stb_image_write.h>

#include <cstdio>
#include <vector>

namespace Atlas {

namespace {

unsigned char channelValue(const unsigned char* px, int comp, int wanted) {
    if (comp <= 0) return 0;
    if (comp == 1) return px[0];
    if (comp == 2) return px[wanted == 0 ? 0 : 1];
    return px[wanted < comp ? wanted : 0];
}

} // namespace

std::string ensureMetallicRoughness(PbrTextureSet& set) {
    if (!set.combined.empty()) return set.combined;
    if (set.metallic.empty() || set.roughness.empty()) return {};

    std::filesystem::path metalPath(set.metallic);
    std::string out = (metalPath.parent_path() / (set.name + "_metallicRoughness.png")).string();
    std::error_code ec;
    if (std::filesystem::exists(out, ec)) {
        set.combined = out;
        return out;
    }

    int mw = 0, mh = 0, mc = 0;
    unsigned char* metal = stbi_load(set.metallic.c_str(), &mw, &mh, &mc, 0);
    if (!metal || mw <= 0 || mh <= 0) {
        stbi_image_free(metal);
        return {};
    }
    int rw = 0, rh = 0, rc = 0;
    unsigned char* rough = stbi_load(set.roughness.c_str(), &rw, &rh, &rc, 0);
    if (!rough || rw <= 0 || rh <= 0) {
        stbi_image_free(metal);
        stbi_image_free(rough);
        return {};
    }

    // Nearest-neighbor resample of roughness into metallic dims when needed.
    std::vector<unsigned char> outPx(static_cast<size_t>(mw) * static_cast<size_t>(mh) * 3u);
    for (int y = 0; y < mh; ++y) {
        const int sy = (y * rh) / mh;
        for (int x = 0; x < mw; ++x) {
            const int sx = (x * rw) / mw;
            const unsigned char* mp = metal + (static_cast<size_t>(y) * static_cast<size_t>(mw) + static_cast<size_t>(x)) * static_cast<size_t>(mc);
            const unsigned char* rp = rough + (static_cast<size_t>(sy) * static_cast<size_t>(rw) + static_cast<size_t>(sx)) * static_cast<size_t>(rc);
            unsigned char* dp = &outPx[(static_cast<size_t>(y) * static_cast<size_t>(mw) + static_cast<size_t>(x)) * 3u];
            dp[0] = 255; // R: occlusion lives in its own slot
            dp[1] = channelValue(rp, rc, 1); // G: roughness (glTF)
            dp[2] = channelValue(mp, mc, 2); // B: metallic (glTF)
        }
    }
    stbi_image_free(metal);
    stbi_image_free(rough);

    if (!stbi_write_png(out.c_str(), mw, mh, 3, outPx.data(), mw * 3)) {
        std::error_code ec2;
        std::filesystem::remove(out, ec2);
        return {};
    }
    set.combined = out;
    return out;
}

bool fillEmptyPbrTexturePaths(const std::string& modelFile, const std::string& setName,
                              std::string& albedo, std::string& normal, std::string& metalRough,
                              std::string& ao, std::string& emissive) {
    if (setName.empty()) return false;
    std::vector<PbrTextureSet> sets = discoverPbrTextureSets(modelFile);
    PbrTextureSet* set = nullptr;
    for (auto& s : sets) {
        if (s.name == setName) { set = &s; break; }
    }
    if (!set) return false;

    bool filled = false;
    if (albedo.empty() && !set->albedo.empty()) { albedo = set->albedo; filled = true; }
    if (normal.empty() && !set->normal.empty()) { normal = set->normal; filled = true; }
    if (ao.empty() && !set->ao.empty()) { ao = set->ao; filled = true; }
    if (emissive.empty() && !set->emissive.empty()) { emissive = set->emissive; filled = true; }
    if (metalRough.empty()) {
        std::string mr = ensureMetallicRoughness(*set);
        if (!mr.empty()) { metalRough = mr; filled = true; }
    }
    // NOTE: opacity maps have no dedicated MaterialComponent slot, so they
    // are intentionally left for manual hookup in the material editor.
    return filled;
}

} // namespace Atlas

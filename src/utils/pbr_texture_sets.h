#pragma once

// PBR texture-set discovery for model imports without usable MTL data.
//
// Sketchfab-style exports often ship bare OBJs (dangling `mtllib`, no
// `usemtl`) plus a sibling `textures/` folder with PBR sets named like
// `<Material>_albedo.png`, `<Material>_normal.png`,
// `<Material>_metallic.png`, `<Material>_roughness.png`, `<Material>_AO.png`.
// assimp finds nothing in that layout, so imports come out gray.
//
// This header discovers those sets (grouped by name prefix before a known
// map suffix), picks a best guess per model, and fills MeshData texture paths
// that assimp left empty. Separate metallic+roughness maps are combined into
// one glTF-style texture (B=metallic, G=roughness, R=255) next to the source.

#include <cctype>
#include <filesystem>
#include <string>
#include <vector>

namespace Atlas {

struct PbrTextureSet {
    std::string name; // prefix as found, e.g. "Cabin_Mat"
    std::string albedo;
    std::string normal;
    std::string metallic;
    std::string roughness;
    std::string combined; // existing *_metallicRoughness / *_orm / ...
    std::string ao;
    std::string opacity;
    std::string emissive;
};

// Groups image files in <modeldir>/textures/ and <modeldir>/ by prefix.
// Files without a recognized map suffix are ignored.
inline std::vector<PbrTextureSet> discoverPbrTextureSets(const std::string& modelFilePath) {
    std::vector<PbrTextureSet> sets;

    std::filesystem::path modelPath(modelFilePath);
    std::filesystem::path modelDir = modelPath.parent_path();
    if (modelDir.empty()) {
        modelDir = std::filesystem::current_path();
    }

    const char* kImageExts[] = {".png", ".jpg", ".jpeg", ".tga", ".bmp"};

    // Longest-first: "_metallicroughness" must win over "_roughness".
    enum class PbrSlot { Combined, Albedo, Normal, Metallic, Roughness, Ao, Opacity, Emissive };
    struct SuffixRule {
        const char* suffix;
        PbrSlot slot;
    };
    static const SuffixRule kRules[] = {
        {"_metallicroughness", PbrSlot::Combined},
        {"_metalrough", PbrSlot::Combined},
        {"_metalnessroughness", PbrSlot::Combined},
        {"_orm", PbrSlot::Combined},
        {"_mr", PbrSlot::Combined},
        {"_basecolor", PbrSlot::Albedo},
        {"_base_color", PbrSlot::Albedo},
        {"_albedo", PbrSlot::Albedo},
        {"_diffuse", PbrSlot::Albedo},
        {"_diff", PbrSlot::Albedo},
        {"_color", PbrSlot::Albedo},
        {"_col", PbrSlot::Albedo},
        {"_normal", PbrSlot::Normal},
        {"_norm", PbrSlot::Normal},
        {"_nrm", PbrSlot::Normal},
        {"_metallic", PbrSlot::Metallic},
        {"_metal", PbrSlot::Metallic},
        {"_met", PbrSlot::Metallic},
        {"_roughness", PbrSlot::Roughness},
        {"_rough", PbrSlot::Roughness},
        {"_rgh", PbrSlot::Roughness},
        {"_ambientocclusion", PbrSlot::Ao},
        {"_occlusion", PbrSlot::Ao},
        {"_ao", PbrSlot::Ao},
        {"_opacity", PbrSlot::Opacity},
        {"_transparent", PbrSlot::Opacity},
        {"_alpha", PbrSlot::Opacity},
        {"_emissive", PbrSlot::Emissive},
        {"_emis", PbrSlot::Emissive},
        {"_n", PbrSlot::Normal},
    };

    auto lowerStr = [](std::string s) {
        for (char& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        return s;
    };

    std::error_code ec;
    std::filesystem::path dirs[2] = {modelDir / "textures", modelDir};
    for (const auto& dir : dirs) {
        if (!std::filesystem::is_directory(dir, ec)) continue;
        for (const auto& entry : std::filesystem::directory_iterator(dir, ec)) {
            if (!entry.is_regular_file(ec)) continue;
            std::filesystem::path p = entry.path();
            std::string ext = lowerStr(p.extension().string());
            bool isImage = false;
            for (const char* e : kImageExts) {
                if (ext == e) { isImage = true; break; }
            }
            if (!isImage) continue;

            std::string rawStem = p.stem().string();
            std::string stem = lowerStr(rawStem);
            // Sketchfab duplicates carry a trailing "_new" (e.g. Glass_Mat_opacity_New).
            bool strippedNew = false;
            const std::string newSuffix("_new");
            if (stem.size() > newSuffix.size() &&
                stem.compare(stem.size() - newSuffix.size(), newSuffix.size(), newSuffix) == 0) {
                stem.resize(stem.size() - newSuffix.size());
                strippedNew = true;
            }
            for (const auto& rule : kRules) {
                const std::string suffix(rule.suffix);
                if (stem.size() > suffix.size() &&
                    stem.compare(stem.size() - suffix.size(), suffix.size(), suffix) == 0) {
                    const size_t prefixLen =
                        rawStem.size() - (strippedNew ? newSuffix.size() : 0) - suffix.size();
                    std::string prefix = rawStem.substr(0, prefixLen);
                    if (prefix.empty()) break;
                    PbrTextureSet* set = nullptr;
                    for (auto& s : sets) {
                        if (lowerStr(s.name) == lowerStr(prefix)) { set = &s; break; }
                    }
                    if (!set) {
                        sets.push_back(PbrTextureSet{});
                        sets.back().name = prefix;
                        set = &sets.back();
                    }
                    std::string* field = nullptr;
                    switch (rule.slot) {
                    case PbrSlot::Combined: field = &set->combined; break;
                    case PbrSlot::Albedo: field = &set->albedo; break;
                    case PbrSlot::Normal: field = &set->normal; break;
                    case PbrSlot::Metallic: field = &set->metallic; break;
                    case PbrSlot::Roughness: field = &set->roughness; break;
                    case PbrSlot::Ao: field = &set->ao; break;
                    case PbrSlot::Opacity: field = &set->opacity; break;
                    case PbrSlot::Emissive: field = &set->emissive; break;
                    }
                    if (field && field->empty()) {
                        *field = p.lexically_normal().string();
                    }
                    break;
                }
            }
        }
    }
    return sets;
}

// Best-guess set for a model/mesh name: single set wins, else fuzzy stem
// match (case/separator-insensitive, trailing mat/material ignored).
// Returns -1 when nothing matches.
inline int bestPbrSetForModel(const std::vector<PbrTextureSet>& sets, const std::string& modelStem) {
    if (sets.empty()) return -1;
    if (sets.size() == 1) return 0;

    auto normalize = [](std::string s) {
        std::string out;
        for (char c : s) {
            const char l = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
            if (l == '_' || l == '-' || l == ' ') continue;
            out += l;
        }
        for (const char* tail : {"material", "mat"}) {
            const std::string t(tail);
            if (out.size() > t.size() + 1 &&
                out.compare(out.size() - t.size(), t.size(), t) == 0) {
                out.resize(out.size() - t.size());
            }
        }
        return out;
    };

    const std::string needle = normalize(modelStem);
    int best = -1;
    int bestScore = 0;
    for (size_t i = 0; i < sets.size(); ++i) {
        const std::string cand = normalize(sets[i].name);
        int score = 0;
        if (!needle.empty() && cand == needle) score = 3;
        else if (!needle.empty() && (cand.find(needle) != std::string::npos || needle.find(cand) != std::string::npos)) score = 2;
        if (score > bestScore) {
            bestScore = score;
            best = static_cast<int>(i);
        }
    }
    return best;
}

// Returns the combined metallicRoughness path for a set: existing combined
// map, or a generated <prefix>_metallicRoughness.png next to the metallic
// source (glTF: B=metallic, G=roughness). Empty when unavailable/failed.
std::string ensureMetallicRoughness(PbrTextureSet& set);

// Fills only empty texture paths from the named set discovered next to
// modelFile. Returns true when at least one path was filled.
bool fillEmptyPbrTexturePaths(const std::string& modelFile, const std::string& setName,
                              std::string& albedo, std::string& normal, std::string& metalRough,
                              std::string& ao, std::string& emissive);

} // namespace Atlas

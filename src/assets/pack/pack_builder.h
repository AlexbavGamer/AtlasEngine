#pragma once

// `.atlaspack` v1 writer: deterministic directory walk → optional deflate →
// 16-byte aligned blobs + (optionally compressed) TOC. Stdlib + zlib only.
// Used by the editor export menu and the AtlasPack CLI (same code path).

#include <cstdint>
#include <functional>
#include <string>

namespace Atlas::Pack {

struct PackOptions {
    int compressLevel = 6;       // zlib 1..9 (0 = store raw always)
    size_t minSavingsPct = 5;    // compress only if it saves at least this %
    bool compressToc = true;     // deflate the table of contents
    bool streamedHintForAll = false; // mark every entry streamed (profiling)
};

struct PackStats {
    uint32_t fileCount = 0;
    uint64_t unpackedBytes = 0;
    uint64_t packedBytes = 0; // blobs only (excludes header + TOC)
    uint32_t deflatedCount = 0;
};

class PackBuilder {
public:
    // Packs assetsDir (recursively) into outPath. Virtual paths are relative
    // to assetsDir with '/' separators. Dotfiles and '*.tmp' are skipped.
    // Returns false with a human-readable reason in err (outPath untouched
    // on failure: writes to outPath + ".tmp" then renames).
    // Log callback receives one line per packed file (may be null).
    static bool build(const std::string& assetsDir, const std::string& outPath,
                      const PackOptions& options, PackStats& stats, std::string& err,
                      const std::function<void(const std::string&)>& log = nullptr);
};

} // namespace Atlas::Pack

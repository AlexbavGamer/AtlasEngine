// `.atlaspack` v1 writer implementation. Stdlib + zlib + <filesystem>.

#include "assets/pack/pack_builder.h"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <filesystem>

#include <zlib.h>

#include "assets/vfs/pack_format.h"

namespace Atlas::Pack {
namespace {

bool readFileBytes(const std::string& path, std::vector<uint8_t>& out, std::string& err) {
    FILE* f = nullptr;
#if defined(_WIN32)
    if (fopen_s(&f, path.c_str(), "rb") != 0) f = nullptr;
#else
    f = std::fopen(path.c_str(), "rb");
#endif
    if (!f) {
        err = "cannot open input file: " + path;
        return false;
    }
    if (std::fseek(f, 0, SEEK_END) != 0) {
        std::fclose(f);
        err = "cannot seek input file: " + path;
        return false;
    }
    const long size = std::ftell(f);
    if (size < 0 || std::fseek(f, 0, SEEK_SET) != 0) {
        std::fclose(f);
        err = "cannot size input file: " + path;
        return false;
    }
    out.resize(static_cast<size_t>(size));
    const bool ok = size == 0 || std::fread(out.data(), 1, out.size(), f) == out.size();
    std::fclose(f);
    if (!ok) {
        out.clear();
        err = "cannot read input file: " + path;
        return false;
    }
    return true;
}

bool deflateBytes(const std::vector<uint8_t>& in, int level, std::vector<uint8_t>& out) {
    uLongf bound = compressBound(static_cast<uLong>(in.size()));
    out.resize(static_cast<size_t>(bound));
    const int rc = compress2(out.data(), &bound, in.data(), static_cast<uLong>(in.size()), level);
    if (rc != Z_OK) return false;
    out.resize(static_cast<size_t>(bound));
    return true;
}

uint32_t crcOf(const std::vector<uint8_t>& data) {
    uLong crc = crc32(0L, Z_NULL, 0);
    if (!data.empty()) {
        crc = crc32(crc, data.data(), static_cast<uInt>(data.size()));
    }
    return static_cast<uint32_t>(crc);
}

bool shouldSkip(const std::filesystem::path& rel) {
    for (const auto& part : rel) {
        const std::string s = part.string();
        if (!s.empty() && s[0] == '.') return true;
    }
    if (rel.extension() == ".tmp") return true;
    return false;
}

} // namespace

bool PackBuilder::build(const std::string& assetsDir, const std::string& outPath, const PackOptions& options,
                        PackStats& stats, std::string& err,
                        const std::function<void(const std::string&)>& log) {
    namespace fs = std::filesystem;
    stats = PackStats{};
    std::error_code ec;

    const fs::path root(assetsDir);
    if (!fs::is_directory(root, ec) || ec) {
        err = "assets dir not found: " + assetsDir;
        return false;
    }

    struct Job {
        std::string vpath;
        std::string full;
        uint64_t size = 0;
    };
    std::vector<Job> jobs;
    for (fs::recursive_directory_iterator it(root, ec), end; it != end; it.increment(ec)) {
        if (ec) {
            err = "directory walk failed: " + ec.message();
            return false;
        }
        if (!it->is_regular_file(ec) || ec) continue;
        const fs::path rel = fs::relative(it->path(), root, ec);
        if (ec || rel.empty()) continue;
        if (shouldSkip(rel)) continue;
        const uint64_t size = it->file_size(ec);
        if (ec) continue;
        if (size > PackFormat::kMaxEntrySize) {
            err = "entry too large (>4GiB): " + rel.generic_string();
            return false;
        }
        jobs.push_back({rel.generic_string(), it->path().generic_string(), size});
    }
    // Deterministic output: sort by virtual path.
    std::sort(jobs.begin(), jobs.end(),
              [](const Job& a, const Job& b) { return a.vpath < b.vpath; });
    {
        // Reject duplicate virtual paths (case-insensitive filesystems can
        // produce two files mapping to one vpath).
        std::string prev;
        for (const auto& j : jobs) {
            if (j.vpath == prev) {
                err = "duplicate virtual path: " + j.vpath;
                return false;
            }
            prev = j.vpath;
        }
    }

    struct Blob {
        std::string vpath;
        std::vector<uint8_t> packed;
        uint64_t unpackedSize = 0;
        uint32_t flags = 0;
        uint32_t crc = 0;
        uint64_t offset = 0;
    };
    std::vector<Blob> blobs;
    blobs.reserve(jobs.size());

    const int level = std::clamp(options.compressLevel, 0, 9);
    for (const auto& job : jobs) {
        std::vector<uint8_t> raw;
        if (!readFileBytes(job.full, raw, err)) return false;
        Blob b;
        b.vpath = job.vpath;
        b.unpackedSize = raw.size();
        b.crc = crcOf(raw);
        b.flags = options.streamedHintForAll ? PackFormat::kEntryFlagStreamedHint : 0u;
        bool storedRaw = true;
        if (level > 0 && !raw.empty()) {
            std::vector<uint8_t> deflated;
            if (!deflateBytes(raw, level, deflated)) {
                err = "deflate failed: " + job.vpath;
                return false;
            }
            const size_t minPacked =
                b.unpackedSize - b.unpackedSize * options.minSavingsPct / 100;
            if (deflated.size() < minPacked) {
                b.packed = std::move(deflated);
                b.flags |= PackFormat::kEntryFlagDeflate;
                storedRaw = false;
            }
        }
        if (storedRaw) b.packed = std::move(raw);
        if (b.packed.size() > PackFormat::kMaxEntrySize) {
            err = "packed entry too large: " + job.vpath;
            return false;
        }
        stats.unpackedBytes += b.unpackedSize;
        stats.packedBytes += b.packed.size();
        if (!storedRaw) ++stats.deflatedCount;
        blobs.push_back(std::move(b));
        if (log) log(job.vpath);
    }

    // Layout: header (32) + blobs (16-aligned) + TOC.
    uint64_t cursor = 32;
    for (auto& b : blobs) {
        cursor = PackFormat::alignUp(cursor, PackFormat::kBlobAlign);
        b.offset = cursor;
        cursor += b.packed.size();
    }

    std::vector<uint8_t> toc;
    PackFormat::writeU32LE(toc, static_cast<uint32_t>(blobs.size()));
    for (const auto& b : blobs) {
        if (b.vpath.size() > 0xFFFFu) {
            err = "path too long: " + b.vpath;
            return false;
        }
        PackFormat::writeU16LE(toc, static_cast<uint16_t>(b.vpath.size()));
        toc.insert(toc.end(), b.vpath.begin(), b.vpath.end());
        PackFormat::writeU64LE(toc, b.offset);
        PackFormat::writeU64LE(toc, b.packed.size());
        PackFormat::writeU64LE(toc, b.unpackedSize);
        PackFormat::writeU32LE(toc, b.flags);
        PackFormat::writeU32LE(toc, b.crc);
    }
    uint32_t headerFlags = 0;
    std::vector<uint8_t> tocStored;
    if (options.compressToc && !toc.empty()) {
        std::vector<uint8_t> deflated;
        if (!deflateBytes(toc, level > 0 ? level : 6, deflated) || deflated.size() >= toc.size()) {
            tocStored = toc; // compression not worth it: store raw
        } else {
            tocStored = std::move(deflated);
            headerFlags |= PackFormat::kFlagTocCompressed;
        }
    } else {
        tocStored = toc;
    }
    const uint64_t tocOffset = PackFormat::alignUp(cursor, PackFormat::kBlobAlign);

    // Write to a temp file first; rename on success (outPath untouched on failure).
    const std::string tmpPath = outPath + ".tmp";
    FILE* f = nullptr;
#if defined(_WIN32)
    if (fopen_s(&f, tmpPath.c_str(), "wb") != 0) f = nullptr;
#else
    f = std::fopen(tmpPath.c_str(), "wb");
#endif
    if (!f) {
        err = "cannot open output file: " + tmpPath;
        return false;
    }
    bool ok = true;
    const auto writeAll = [&](const void* data, size_t size) {
        if (ok && size > 0) ok = std::fwrite(data, 1, size, f) == size;
    };
    const auto writePad = [&](uint64_t from, uint64_t to) {
        static const uint8_t zeros[16] = {};
        while (ok && from < to) {
            const size_t chunk = static_cast<size_t>(std::min<uint64_t>(to - from, sizeof(zeros)));
            writeAll(zeros, chunk);
            from += chunk;
        }
    };

    uint8_t header[32];
    std::memcpy(header, PackFormat::kMagic, 12);
    {
        std::vector<uint8_t> h;
        PackFormat::writeU32LE(h, headerFlags);
        PackFormat::writeU64LE(h, tocOffset);
        PackFormat::writeU64LE(h, tocStored.size());
        std::memcpy(header + 12, h.data(), h.size());
    }
    writeAll(header, sizeof(header));
    uint64_t pos = 32;
    for (const auto& b : blobs) {
        writePad(pos, b.offset);
        pos = b.offset;
        writeAll(b.packed.data(), b.packed.size());
        pos += b.packed.size();
    }
    writePad(pos, tocOffset);
    writeAll(tocStored.data(), tocStored.size());
    std::fclose(f);

    if (!ok) {
        std::error_code rmEc;
        fs::remove(tmpPath, rmEc);
        err = "write failed: " + tmpPath;
        return false;
    }
    fs::rename(tmpPath, outPath, ec);
    if (ec) {
        fs::remove(tmpPath, ec);
        err = "rename failed: " + ec.message();
        return false;
    }

    stats.fileCount = static_cast<uint32_t>(blobs.size());
    err.clear();
    return true;
}

} // namespace Atlas::Pack

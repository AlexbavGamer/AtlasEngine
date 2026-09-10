#pragma once

// Read-only `.atlaspack` v1 reader: header + TOC validation, entry lookup,
// decompress (zlib) + CRC32 verification. Thread-safe for concurrent reads
// (one mutex guards the file handle). Stdlib + zlib only.

#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

namespace Atlas::VFS {

struct PackEntry {
    std::string path; // virtual path, '/' separators, relative
    uint64_t offset = 0;
    uint64_t packedSize = 0;
    uint64_t unpackedSize = 0;
    uint32_t flags = 0;
    uint32_t crc32 = 0;
};

class PackReader {
public:
    PackReader(const PackReader&) = delete;
    PackReader& operator=(const PackReader&) = delete;

    // Opens + validates (magic, header bounds, TOC parse). On failure
    // returns nullptr with a human-readable reason in err.
    static std::unique_ptr<PackReader> open(const std::string& path, std::string& err);

    // Entry lookup by virtual path (exact, case-sensitive). False = absent.
    bool find(std::string_view vpath, PackEntry& out) const;

    // Full entry read: seek + packed bytes + inflate (if flagged) + CRC32
    // check over UNPACKED bytes. False = IO / format / checksum failure.
    bool read(std::string_view vpath, std::vector<uint8_t>& out) const;
    bool read(const PackEntry& entry, std::vector<uint8_t>& out) const;

    size_t entryCount() const { return m_entries.size(); }
    const std::string& sourcePath() const { return m_path; }

private:
    PackReader() = default;

    std::string m_path;
    std::unordered_map<std::string, PackEntry> m_entries;
    mutable std::mutex m_mutex;
};

} // namespace Atlas::VFS

#pragma once

// `.atlaspack` v1 shared format constants + little-endian helpers.
// Full spec: docs/Asset_Packaging_TDD.md §3. Stdlib-only.

#include <cstdint>
#include <vector>

namespace Atlas::PackFormat {

inline constexpr char kMagic[12] = {
    'A', 'T', 'L', 'A', 'S', 'P', 'A', 'C', 'K', '_', 'V', '1'};
inline constexpr uint32_t kFlagTocCompressed = 1u << 0;

inline constexpr uint32_t kEntryFlagDeflate = 1u << 0;
inline constexpr uint32_t kEntryFlagStreamedHint = 1u << 1;

// Entry blobs are padded to this alignment (cheap `pread`, future O_DIRECT).
inline constexpr uint64_t kBlobAlign = 16u;

// v1 range guard (offsets are u64 on disk, but v1 clamps to u32 range).
inline constexpr uint64_t kMaxEntrySize = 0xFFFFFFFFu;

inline void writeU16LE(std::vector<uint8_t>& out, uint16_t v) {
    out.push_back(static_cast<uint8_t>(v & 0xFFu));
    out.push_back(static_cast<uint8_t>((v >> 8) & 0xFFu));
}

inline void writeU32LE(std::vector<uint8_t>& out, uint32_t v) {
    for (int i = 0; i < 4; ++i) {
        out.push_back(static_cast<uint8_t>((v >> (8 * i)) & 0xFFu));
    }
}

inline void writeU64LE(std::vector<uint8_t>& out, uint64_t v) {
    for (int i = 0; i < 8; ++i) {
        out.push_back(static_cast<uint8_t>((v >> (8 * i)) & 0xFFu));
    }
}

inline bool readU16LE(const uint8_t*& p, const uint8_t* end, uint16_t& v) {
    if (static_cast<size_t>(end - p) < 2) return false;
    v = static_cast<uint16_t>(p[0] | (static_cast<uint16_t>(p[1]) << 8));
    p += 2;
    return true;
}

inline bool readU32LE(const uint8_t*& p, const uint8_t* end, uint32_t& v) {
    if (static_cast<size_t>(end - p) < 4) return false;
    v = 0;
    for (int i = 0; i < 4; ++i) {
        v |= static_cast<uint32_t>(p[i]) << (8 * i);
    }
    p += 4;
    return true;
}

inline bool readU64LE(const uint8_t*& p, const uint8_t* end, uint64_t& v) {
    if (static_cast<size_t>(end - p) < 8) return false;
    v = 0;
    for (int i = 0; i < 8; ++i) {
        v |= static_cast<uint64_t>(p[i]) << (8 * i);
    }
    p += 8;
    return true;
}

inline uint64_t alignUp(uint64_t v, uint64_t align) {
    return (v + align - 1u) & ~(align - 1u);
}

} // namespace Atlas::PackFormat

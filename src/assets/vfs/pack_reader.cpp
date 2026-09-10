// `.atlaspack` v1 reader implementation. Stdlib + zlib only.

#include "assets/vfs/pack_reader.h"

#include <cstdio>
#include <cstring>

#include <zlib.h>

#include "assets/vfs/pack_format.h"

namespace Atlas::VFS {
namespace {

uint32_t crcOf(const std::vector<uint8_t>& data) {
    uLong crc = crc32(0L, Z_NULL, 0);
    if (!data.empty()) {
        crc = crc32(crc, data.data(), static_cast<uInt>(data.size()));
    }
    return static_cast<uint32_t>(crc);
}

bool inflateRaw(const std::vector<uint8_t>& packed, uint64_t unpackedSize, std::vector<uint8_t>& out,
                std::string& err) {
    out.resize(static_cast<size_t>(unpackedSize));
    uLongf destLen = static_cast<uLongf>(out.size());
    const int rc = uncompress(out.data(), &destLen, packed.data(), static_cast<uLong>(packed.size()));
    if (rc != Z_OK) {
        err = "zlib inflate failed";
        out.clear();
        return false;
    }
    if (destLen != static_cast<uLongf>(out.size())) {
        err = "inflate size mismatch";
        out.clear();
        return false;
    }
    return true;
}

bool readAt(FILE* f, uint64_t offset, void* dst, size_t size) {
#if defined(_WIN32)
    if (_fseeki64(f, static_cast<__int64>(offset), SEEK_SET) != 0) return false;
#else
    if (fseeko(f, static_cast<off_t>(offset), SEEK_SET) != 0) return false;
#endif
    return size == 0 || std::fread(dst, 1, size, f) == size;
}

} // namespace

std::unique_ptr<PackReader> PackReader::open(const std::string& path, std::string& err) {
    auto reader = std::unique_ptr<PackReader>(new PackReader());
    reader->m_path = path;

    FILE* f = nullptr;
#if defined(_WIN32)
    if (fopen_s(&f, path.c_str(), "rb") != 0) f = nullptr;
#else
    f = std::fopen(path.c_str(), "rb");
#endif
    if (!f) {
        err = "cannot open file";
        return nullptr;
    }

    // Whole-file size for bounds checks.
    uint64_t fileSize = 0;
#if defined(_WIN32)
    if (_fseeki64(f, 0, SEEK_END) == 0) {
        const __int64 end = _ftelli64(f);
        if (end >= 0) fileSize = static_cast<uint64_t>(end);
    }
#else
    if (fseeko(f, 0, SEEK_END) == 0) {
        const off_t end = ftello(f);
        if (end >= 0) fileSize = static_cast<uint64_t>(end);
    }
#endif
    if (fileSize < 32) {
        std::fclose(f);
        err = "file too small for header";
        return nullptr;
    }

    uint8_t header[32];
    if (!readAt(f, 0, header, sizeof(header))) {
        std::fclose(f);
        err = "cannot read header";
        return nullptr;
    }
    if (std::memcmp(header, PackFormat::kMagic, sizeof(PackFormat::kMagic)) != 0) {
        std::fclose(f);
        err = "bad magic (not an .atlaspack v1 file)";
        return nullptr;
    }

    const uint8_t* p = header + 12;
    const uint8_t* hend = header + sizeof(header);
    uint32_t flags = 0;
    uint64_t tocOffset = 0;
    uint64_t tocSize = 0;
    if (!PackFormat::readU32LE(p, hend, flags) || !PackFormat::readU64LE(p, hend, tocOffset) ||
        !PackFormat::readU64LE(p, hend, tocSize)) {
        std::fclose(f);
        err = "truncated header";
        return nullptr;
    }
    if (flags & ~PackFormat::kFlagTocCompressed) {
        std::fclose(f);
        err = "unknown header flags";
        return nullptr;
    }
    if (tocOffset >= fileSize || tocSize == 0 || tocOffset + tocSize > fileSize) {
        std::fclose(f);
        err = "TOC out of file bounds";
        return nullptr;
    }

    std::vector<uint8_t> tocRaw(static_cast<size_t>(tocSize));
    if (!readAt(f, tocOffset, tocRaw.data(), tocRaw.size())) {
        std::fclose(f);
        err = "cannot read TOC";
        return nullptr;
    }

    std::vector<uint8_t> toc;
    if (flags & PackFormat::kFlagTocCompressed) {
        // TOC layout has no stored unpacked size: try-grow inflate.
        // TOCs are small (typical < 1 MiB); grow geometrically to 64 MiB cap.
        std::string zerr;
        bool ok = false;
        for (size_t guess = 1u << 20; guess <= (64u << 20); guess *= 2) {
            std::vector<uint8_t> trial(guess);
            uLongf destLen = static_cast<uLongf>(trial.size());
            const int rc =
                uncompress(trial.data(), &destLen, tocRaw.data(), static_cast<uLong>(tocRaw.size()));
            if (rc == Z_OK) {
                trial.resize(static_cast<size_t>(destLen));
                toc = std::move(trial);
                ok = true;
                break;
            }
            if (rc != Z_BUF_ERROR) break;
        }
        if (!ok) {
            std::fclose(f);
            err = "TOC inflate failed: " + zerr;
            return nullptr;
        }
        (void)zerr;
    } else {
        toc = std::move(tocRaw);
    }

    const uint8_t* tp = toc.data();
    const uint8_t* tend = toc.data() + toc.size();
    uint32_t entryCount = 0;
    if (!PackFormat::readU32LE(tp, tend, entryCount)) {
        std::fclose(f);
        err = "truncated TOC";
        return nullptr;
    }
    for (uint32_t i = 0; i < entryCount; ++i) {
        uint16_t pathLen = 0;
        if (!PackFormat::readU16LE(tp, tend, pathLen) || pathLen == 0) {
            std::fclose(f);
            err = "truncated TOC entry path";
            return nullptr;
        }
        if (static_cast<size_t>(tend - tp) < pathLen) {
            std::fclose(f);
            err = "truncated TOC entry path bytes";
            return nullptr;
        }
        std::string vpath(reinterpret_cast<const char*>(tp), pathLen);
        tp += pathLen;
        PackEntry e;
        e.path = vpath;
        if (!PackFormat::readU64LE(tp, tend, e.offset) || !PackFormat::readU64LE(tp, tend, e.packedSize) ||
            !PackFormat::readU64LE(tp, tend, e.unpackedSize) || !PackFormat::readU32LE(tp, tend, e.flags) ||
            !PackFormat::readU32LE(tp, tend, e.crc32)) {
            std::fclose(f);
            err = "truncated TOC entry fields";
            return nullptr;
        }
        if (vpath.empty() || vpath.front() == '/' || vpath.find("..") != std::string::npos) {
            std::fclose(f);
            err = "unsafe entry path: " + vpath;
            return nullptr;
        }
        if (e.unpackedSize > PackFormat::kMaxEntrySize || e.packedSize > PackFormat::kMaxEntrySize) {
            std::fclose(f);
            err = "entry too large: " + vpath;
            return nullptr;
        }
        if (e.flags & ~(PackFormat::kEntryFlagDeflate | PackFormat::kEntryFlagStreamedHint)) {
            std::fclose(f);
            err = "unknown entry flags: " + vpath;
            return nullptr;
        }
        if (e.offset >= fileSize || e.packedSize > fileSize || e.offset + e.packedSize > fileSize) {
            std::fclose(f);
            err = "entry blob out of bounds: " + vpath;
            return nullptr;
        }
        if (!reader->m_entries.emplace(vpath, e).second) {
            std::fclose(f);
            err = "duplicate entry path: " + vpath;
            return nullptr;
        }
    }

    std::fclose(f);
    err.clear();
    return reader;
}

bool PackReader::find(std::string_view vpath, PackEntry& out) const {
    const auto it = m_entries.find(std::string(vpath));
    if (it == m_entries.end()) return false;
    out = it->second;
    return true;
}

bool PackReader::read(std::string_view vpath, std::vector<uint8_t>& out) const {
    PackEntry entry;
    if (!find(vpath, entry)) return false;
    return read(entry, out);
}

bool PackReader::read(const PackEntry& entry, std::vector<uint8_t>& out) const {
    std::lock_guard<std::mutex> lock(m_mutex);
    FILE* f = nullptr;
#if defined(_WIN32)
    if (fopen_s(&f, m_path.c_str(), "rb") != 0) f = nullptr;
#else
    f = std::fopen(m_path.c_str(), "rb");
#endif
    if (!f) return false;

    std::vector<uint8_t> packed(static_cast<size_t>(entry.packedSize));
    const bool ioOk = entry.packedSize == 0 || readAt(f, entry.offset, packed.data(), packed.size());
    std::fclose(f);
    if (!ioOk) return false;

    std::vector<uint8_t> unpacked;
    if (entry.flags & PackFormat::kEntryFlagDeflate) {
        std::string zerr;
        if (!inflateRaw(packed, entry.unpackedSize, unpacked, zerr)) return false;
    } else {
        unpacked = std::move(packed);
        if (unpacked.size() != static_cast<size_t>(entry.unpackedSize)) return false;
    }
    return crcOf(unpacked) == entry.crc32 ? (out = std::move(unpacked), true) : false;
}

} // namespace Atlas::VFS

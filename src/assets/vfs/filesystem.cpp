// Virtual filesystem implementation. Stdlib-only (PackReader pulls zlib).

#include "assets/vfs/filesystem.h"

#include <algorithm>
#include <atomic>
#include <cstdio>
#include <filesystem>

#include "assets/vfs/pack_reader.h"

namespace Atlas::VFS {

FileSystem& FileSystem::instance() {
    static FileSystem fs;
    return fs;
}

void FileSystem::mountPack(std::unique_ptr<PackReader> pack, int priority) {
    if (!pack) return;
    std::unique_lock<std::shared_mutex> lock(m_mutex);
    m_packs.push_back(PackMount{std::move(pack), priority, m_nextOrder++});
}

void FileSystem::mountLoose(const std::string& root, int priority) {
    std::unique_lock<std::shared_mutex> lock(m_mutex);
    m_loose.push_back(LooseMount{root, priority, m_nextOrder++});
}

void FileSystem::clear() {
    std::unique_lock<std::shared_mutex> lock(m_mutex);
    m_packs.clear();
    m_loose.clear();
}

bool FileSystem::readDirect(const std::string& path, std::vector<uint8_t>& out) {
    FILE* f = nullptr;
#if defined(_WIN32)
    if (fopen_s(&f, path.c_str(), "rb") != 0) f = nullptr;
#else
    f = std::fopen(path.c_str(), "rb");
#endif
    if (!f) return false;
    if (std::fseek(f, 0, SEEK_END) != 0) {
        std::fclose(f);
        return false;
    }
    const long size = std::ftell(f);
    if (size < 0) {
        std::fclose(f);
        return false;
    }
    if (std::fseek(f, 0, SEEK_SET) != 0) {
        std::fclose(f);
        return false;
    }
    out.resize(static_cast<size_t>(size));
    const bool ok = size == 0 || std::fread(out.data(), 1, out.size(), f) == out.size();
    std::fclose(f);
    if (!ok) out.clear();
    return ok;
}

std::string FileSystem::joinLoose(const std::string& root, std::string_view vpath) {
    namespace fs = std::filesystem;
    fs::path p(root);
    p /= fs::path(std::string(vpath));
    return p.generic_string();
}

bool FileSystem::exists(std::string_view vpath) const {
    std::vector<uint8_t> scratch;
    return readAll(vpath, scratch);
}

bool FileSystem::readAll(std::string_view vpath, std::vector<uint8_t>& out) const {
    // Snapshot mount order under a shared lock, then read unlocked (PackReader
    // is internally synchronized; disk reads need no VFS lock).
    struct PackHit {
        const PackReader* reader;
        int priority;
        uint64_t order;
    };
    struct LooseHit {
        std::string root;
        int priority;
        uint64_t order;
    };
    std::vector<PackHit> packs;
    std::vector<LooseHit> loose;
    {
        std::shared_lock<std::shared_mutex> lock(m_mutex);
        packs.reserve(m_packs.size());
        for (const auto& m : m_packs) packs.push_back({m.reader.get(), m.priority, m.order});
        loose.reserve(m_loose.size());
        for (const auto& m : m_loose) loose.push_back({m.root, m.priority, m.order});
    }
    const auto rank = [](int priority, uint64_t order) {
        return std::pair<int, uint64_t>(-priority, order);
    };
    std::sort(packs.begin(), packs.end(), [&](const PackHit& a, const PackHit& b) {
        return rank(a.priority, a.order) < rank(b.priority, b.order);
    });
    std::sort(loose.begin(), loose.end(), [&](const LooseHit& a, const LooseHit& b) {
        return rank(a.priority, a.order) < rank(b.priority, b.order);
    });

    // Merge both mount lists by (priority, order) for first-hit-wins.
    size_t pi = 0;
    size_t li = 0;
    while (pi < packs.size() || li < loose.size()) {
        const bool takePack = li >= loose.size() ||
            (pi < packs.size() && rank(packs[pi].priority, packs[pi].order) <
                                      rank(loose[li].priority, loose[li].order));
        if (takePack) {
            if (packs[pi].reader->read(vpath, out)) return true;
            ++pi;
        } else {
            if (readDirect(joinLoose(loose[li].root, vpath), out)) return true;
            ++li;
        }
    }
    // Final fallback: the path itself (absolute or CWD-relative). Keeps
    // unmigrated/editor absolute-path flows working through one API.
    return readDirect(std::string(vpath), out);
}

bool FileSystem::extractTemp(std::string_view vpath, std::string& outTempPath) const {
    std::vector<uint8_t> bytes;
    if (!readAll(vpath, bytes)) return false;
    namespace fs = std::filesystem;
    std::error_code ec;
    const fs::path dir = fs::temp_directory_path(ec);
    if (ec) return false;
    // Unique name: vpath sanitized + counter (no clock dependency).
    static std::atomic<uint64_t> s_counter{0};
    std::string name = "atlas_pak_";
    name += std::to_string(s_counter.fetch_add(1, std::memory_order_relaxed));
    name += "_";
    for (char c : vpath) {
        name += (c == '/' || c == '\\' || c == ':') ? '_' : c;
    }
    const fs::path full = dir / name;
    FILE* f = nullptr;
#if defined(_WIN32)
    if (fopen_s(&f, full.string().c_str(), "wb") != 0) f = nullptr;
#else
    f = std::fopen(full.string().c_str(), "wb");
#endif
    if (!f) return false;
    bool ok = true;
    if (!bytes.empty() && std::fwrite(bytes.data(), 1, bytes.size(), f) != bytes.size()) ok = false;
    std::fclose(f);
    if (!ok) {
        std::error_code rmEc;
        fs::remove(full, rmEc);
        return false;
    }
    outTempPath = full.generic_string();
    return true;
}

} // namespace Atlas::VFS

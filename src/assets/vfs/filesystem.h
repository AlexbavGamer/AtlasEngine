#pragma once

// Virtual filesystem: ordered mounts (packs + loose roots) with transparent
// fallback to the real path. The editor mounts loose roots and keeps working
// on source assets; the game mounts `game.pak` first. With no mounts at all,
// `readAll` degrades to a direct disk read, so migrating readers is
// behavior-preserving until mounts are added. Thread-safe.

#include <cstdint>
#include <memory>
#include <mutex>
#include <shared_mutex>
#include <string>
#include <vector>

namespace Atlas::VFS {

class PackReader;

class FileSystem {
public:
    FileSystem(const FileSystem&) = delete;
    FileSystem& operator=(const FileSystem&) = delete;

    static FileSystem& instance();

    // Higher priority wins; ties break by mount order (earlier first).
    void mountPack(std::unique_ptr<PackReader> pack, int priority = 0);
    void mountLoose(const std::string& root, int priority = -1);
    void clear();

    bool exists(std::string_view vpath) const;
    // Resolution: packs (priority order) -> loose mounts (root + vpath) ->
    // direct path as given (absolute or CWD-relative). False = not found /
    // unreadable anywhere.
    bool readAll(std::string_view vpath, std::vector<uint8_t>& out) const;

    // Materialize a virtual file at a real temp path (for APIs that demand
    // filesystem paths, e.g. exotic assimp importers). Caller deletes it.
    bool extractTemp(std::string_view vpath, std::string& outTempPath) const;

private:
    FileSystem() = default;

    struct PackMount {
        std::unique_ptr<PackReader> reader;
        int priority = 0;
        uint64_t order = 0;
    };
    struct LooseMount {
        std::string root;
        int priority = 0;
        uint64_t order = 0;
    };

    static bool readDirect(const std::string& path, std::vector<uint8_t>& out);
    static std::string joinLoose(const std::string& root, std::string_view vpath);

    mutable std::shared_mutex m_mutex;
    std::vector<PackMount> m_packs;
    std::vector<LooseMount> m_loose;
    uint64_t m_nextOrder = 0;
};

} // namespace Atlas::VFS

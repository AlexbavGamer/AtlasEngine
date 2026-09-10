# Asset Packaging (`.atlaspack`) — TDD

Single-file asset distribution for exported games: pack `assets/` into one
`.pak`-style archive with a virtual filesystem (VFS) at runtime.

## 1. Goal / non-goals

Goal: `Export Game` produces `game.pak` (+ tiny `package.manifest`); the
standalone game mounts it and never touches loose files. Faster installs
(one file), no asset tampering by accident, shorter load stutters (one
sequential read + async decompress).

Non-goals (v1): patching/DLC overlays beyond mount order, encryption (only
checksum), cooking/transcoding assets (store as-authored bytes), network
streaming, console platforms.

## 2. Current state (ground truth)

- Readers are scattered, all direct-filesystem: `scene_serializer`
  (`ifstream`), `script_engine` (`luaL_loadfile`), `model_loader` (assimp
  `ReadFile`), textures (stb from file), `hlod` chunk cache (`ifstream` POD),
  `world_partition`, `renderer::readFile` (shaders — already embedded, skip).
- `AssetManager` caches GPU objects by `StringID` but loads from disk paths.
- Export (`EditorApp::exportGamePackage*`) copies loose trees + writes
  `package.manifest` (text, `ATLAS_PACKAGE_V1`, **strict reader**: unknown
  token fails; missing keys keep struct defaults).
- zlib is already vendored (`zlib` premake project via assimp) → deflate +
  crc32 available in-house, no new dependency.

## 3. Format `.atlaspack` v1 (little-endian, versioned)

```text
offset  size  field
0       12    magic "ATLASPACK_V1" (12 bytes, no NUL)
12      4     u32 flags (bit0 = TOC compressed; reserved rest = 0)
16      8     u64 tocOffset (from file start)
24      8     u64 tocSize (bytes)
32      ...   entry blobs (each 16-byte aligned)
```

TOC (at `tocOffset`, zlib-compressed iff flags bit0, else raw):

```text
u32 entryCount
per entry:
  u16 pathLen | path bytes (UTF-8, '/' separators, relative, e.g.
    "assets/models/hero.fbx"; NO leading slash, NO '..')
  u64 offset, u64 packedSize, u64 unpackedSize
  u32 flags (bit0 = deflate; bit1 = streamed-hint for HLOD chunks)
  u32 crc32 (of UNPACKED bytes)
```

Rules: paths case-preserved, lookup case-sensitive; duplicates rejected at
pack time; max 4 GiB per entry (u64 future-proof, v1 enforces u32 range);
whole-file CRC optional (v2). No per-entry encryption in v1.

## 4. VirtualFileSystem design

New module `src/assets/vfs/` (stdlib-only + zlib, unit-testable like
`render_resources`):

```cpp
namespace Atlas::VFS {
struct FileView { const uint8_t* data; size_t size; }; // borrowed, pak-backed
class PackReader { // one open .pak, mmap-style (plain pread into cache)
  public: static std::unique_ptr<PackReader> open(const std::string& path, std::string& err);
    bool find(std::string_view vpath, Entry& out) const; // hash map
    bool read(std::string_view vpath, std::vector<uint8_t>& out) const; // verify crc32
};
class FileSystem {
  public: void mountPack(std::unique_ptr<PackReader>, int priority = 0);
    void mountLoose(const std::string& root, int priority = -1); // dev mode
    // First hit wins by priority, then mount order.
    bool exists(std::string_view vpath) const;
    bool readAll(std::string_view vpath, std::vector<uint8_t>& out) const;
    // Escape hatch for APIs that demand real paths (assimp fallback):
    bool extractTemp(std::string_view vpath, std::filesystem::path& out) const;
};
} // namespace Atlas::VFS
```

- Thread-safety: `PackReader::read` is const + internally synchronized
  (mutex around file handle) — asset workers already load on threads.
- Reader migration (each keeps loose fallback via `mountLoose`, so the
  editor keeps working on source assets):

| reader | today | with VFS |
| --- | --- | --- |
| scenes | `ifstream` path | `readAll` → memory parse (serializer gains `loadFromMemory`) |
| scripts | `luaL_loadfile` | `readAll` + `luaL_loadbuffer` (chunkname = vpath for errors) |
| models | assimp `ReadFile` | `readAll` + `aiImportFileFromMemory` (+ `extractTemp` fallback for exotic importers) |
| textures | stb from file | `readAll` + `stbi_load_from_memory` |
| HLOD chunks | `ifstream` POD | `readAll` + existing POD readers over a memory cursor |
| shaders | embedded already | untouched |

## 5. Pack tool

- Editor menu `Project > Build Asset Pack` + headless `--pack <assetsDir> <out.pak>`
  (same code path, `src/assets/pack/`).
- Walk `assetsDir` (sorted for determinism), skip dotfiles/`*.tmp`, per-file:
  compress with zlib level 6 iff savings > 5 % (else store raw — avoids
  wasting CPU on PNG/FBX that don't compress), CRC32 unpacked, 16-byte
  align blobs.
- Writes `game.pak` + prints manifest snippet (`pak_file "game.pak"`).
- v1: full rebuild every time (typical projects < 2 GiB, seconds). No
  incremental yet (see §8.5).

## 6. Manifest delta (backward compatible)

`package.manifest` gains one optional line: `pak_file "game.pak"`.
Reader: missing key → empty = loose-file mode (old manifests keep working).
`ATLAS_PACKAGE_V1` magic unchanged (additive key, not a format break).
Game boot: if `pak_file` present → `mountPack`; always `mountLoose` last as
dev override (or `--loose` flag forces loose-first for modding).

## 7. Phases + acceptance

- **P0 — VFS core (no behavior change)**: `PackReader` + `FileSystem` +
  `AtlasTests` (round-trip, missing file, bad CRC fails, overlay priority,
  10 MiB fuzz). Acceptance: 25+ tests green, zero engine wiring.
- **P1 — pack tool + manifest**: CLI + editor menu, `pak_file` key, game
  boots pack with a scene containing only primitives (no model migration
  yet). Acceptance: exported `game.pak` runs identical to loose.
- **P2 — readers: scenes + scripts + textures** (all memory-native APIs).
  Acceptance: demo scene with scripts + textured primitives identical.
- **P3 — readers: models** (`aiImportFileFromMemory`, embedded textures
  already memory-side). Acceptance: FBX/glTF game from pack, pixel-compare
  vs loose (screenshot diff).
- **P4 — readers: HLOD/world chunks + compression on**. Acceptance: city
  scene streams from pack; load-time ≤ 1.2× loose.
- **P5 — hardening**: `--verify` mode (CRC walk), corrupt-pack error paths
  in-game (dialog, not crash), docs. (Patching/DLC = v2 proposal.)

## 8. Risks & mitigations

- Assimp memory import of giant FBX (2× RAM spike: packed + unpacked +
  assimp DOM) → cap entry size 512 MiB v1; `extractTemp` fallback per file.
- Loose-vs-packed divergence while debugging → editor always loose-first;
  pack only on export; `--verify` diffs listing vs `assets/`.
- Seek-heavy HLOD streaming → entries 16-byte aligned, one `pread` per
  chunk; `streamed-hint` flag reserves placement grouping (v2: reorder by
  cell).
- Load-time regression from decompress → worker-thread inflate (zlib is
  thread-safe with separate streams); textures/models already async.

## 9. Tests (`AtlasTests`, stdlib-only)

`tests/test_pack.cpp`: empty pack, 1-file round-trip, 10k-file TOC,
deflate/raw auto-choice boundary, CRC-mismatch rejection, truncated file
rejection, overlay priority (pack beats loose, higher beats lower),
multithreaded reads (8 threads × 500 iters, mirrors render_resources test).

## 10. Alternatives considered

- **ZIP + miniz**: standard tooling, but central-directory parsing +
  new dep; ours is 300 lines with zlib already vendored.
- **PhysFS**: mature VFS, but C library + build integration for 3
  platforms; overkill for "one pack + loose fallback".
- **Loose-only + installer zip**: zero engine work, but keeps slow
  per-file IO, no integrity, amateur distribution.

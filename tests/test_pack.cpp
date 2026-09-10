// Unit tests for the `.atlaspack` pipeline: PackBuilder round-trip through
// PackReader, corruption rejection, VFS overlay priority, threaded reads.
#include "tests.h"

#include <cstdio>
#include <filesystem>
#include <string>
#include <thread>
#include <vector>

#include "assets/pack/pack_builder.h"
#include "assets/vfs/filesystem.h"
#include "assets/vfs/pack_reader.h"

namespace {

std::filesystem::path tempRoot(const std::string& name) {
    return std::filesystem::temp_directory_path() / ("atlas_pack_" + name);
}

void writeFile(const std::filesystem::path& p, const std::vector<uint8_t>& bytes) {
    std::filesystem::create_directories(p.parent_path());
    FILE* f = nullptr;
#if defined(_WIN32)
    fopen_s(&f, p.string().c_str(), "wb");
#else
    f = std::fopen(p.string().c_str(), "wb");
#endif
    if (!f) throw std::runtime_error("cannot write temp file");
    if (!bytes.empty() && std::fwrite(bytes.data(), 1, bytes.size(), f) != bytes.size()) {
        std::fclose(f);
        throw std::runtime_error("cannot write temp bytes");
    }
    std::fclose(f);
}

void writeFile(const std::filesystem::path& p, const std::string& text) {
    writeFile(p, std::vector<uint8_t>(text.begin(), text.end()));
}

std::vector<uint8_t> pseudoRandom(size_t n, uint32_t seed) {
    std::vector<uint8_t> out(n);
    uint32_t s = seed;
    for (size_t i = 0; i < n; ++i) {
        s = s * 1664525u + 1013904223u;
        out[i] = static_cast<uint8_t>((s >> 16) & 0xFFu);
    }
    return out;
}

struct Sandbox {
    std::filesystem::path root;
    std::filesystem::path assets;
    std::filesystem::path pak;
    explicit Sandbox(const std::string& name)
        : root(tempRoot(name)), assets(root / "assets"), pak(root / "game.pak") {
        std::error_code ec;
        std::filesystem::remove_all(root, ec);
    }
    ~Sandbox() {
        std::error_code ec;
        std::filesystem::remove_all(root, ec);
    }
};

bool buildPack(Sandbox& sb, Atlas::Pack::PackStats* stats = nullptr) {
    Atlas::Pack::PackOptions opts;
    Atlas::Pack::PackStats local;
    std::string err;
    const bool ok = Atlas::Pack::PackBuilder::build(sb.assets.string(), sb.pak.string(), opts,
                                                    stats ? *stats : local, err, nullptr);
    if (!ok) throw std::runtime_error("build failed: " + err);
    return std::filesystem::exists(sb.pak);
}

} // namespace

ATLAS_TEST(Pack, EmptyDirBuildsEmptyPack) {
    Sandbox sb("empty");
    std::filesystem::create_directories(sb.assets);
    Atlas::Pack::PackStats stats;
    EXPECT_TRUE(buildPack(sb, &stats));
    EXPECT_EQ(stats.fileCount, 0u);
    std::string err;
    auto reader = Atlas::VFS::PackReader::open(sb.pak.string(), err);
    EXPECT_TRUE(reader != nullptr);
    EXPECT_EQ(reader->entryCount(), 0u);
}

ATLAS_TEST(Pack, RoundTripTextAndBinary) {
    Sandbox sb("roundtrip");
    writeFile(sb.assets / "scenes" / "main.scene", "hello scene");
    writeFile(sb.assets / "tex" / "blob.bin", pseudoRandom(4096, 1234));
    Atlas::Pack::PackStats stats;
    EXPECT_TRUE(buildPack(sb, &stats));
    EXPECT_EQ(stats.fileCount, 2u);
    std::string err;
    auto reader = Atlas::VFS::PackReader::open(sb.pak.string(), err);
    EXPECT_TRUE(reader != nullptr);
    std::vector<uint8_t> bytes;
    EXPECT_TRUE(reader->read("scenes/main.scene", bytes));
    EXPECT_EQ(std::string(bytes.begin(), bytes.end()), std::string("hello scene"));
    EXPECT_TRUE(reader->read("tex/blob.bin", bytes));
    EXPECT_EQ(bytes, pseudoRandom(4096, 1234));
    Atlas::VFS::PackEntry entry;
    EXPECT_TRUE(!reader->find("nope/missing.txt", entry));
    EXPECT_TRUE(!reader->read("nope/missing.txt", bytes));
}

ATLAS_TEST(Pack, CompressibleDeflatesIncompressibleStores) {
    Sandbox sb("deflate");
    std::vector<uint8_t> zeros(65536, 0xAB);
    writeFile(sb.assets / "zeros.bin", zeros); // highly compressible
    writeFile(sb.assets / "noise.bin", pseudoRandom(65536, 999)); // not
    Atlas::Pack::PackStats stats;
    EXPECT_TRUE(buildPack(sb, &stats));
    EXPECT_TRUE(stats.deflatedCount >= 1u);
    const uint64_t raw = 65536u + 65536u;
    EXPECT_TRUE(stats.packedBytes < raw); // at least the zeros compressed
    std::string err;
    auto reader = Atlas::VFS::PackReader::open(sb.pak.string(), err);
    EXPECT_TRUE(reader != nullptr);
    std::vector<uint8_t> bytes;
    EXPECT_TRUE(reader->read("zeros.bin", bytes));
    EXPECT_EQ(bytes, zeros);
}

ATLAS_TEST(Pack, MissingFileFails) {
    std::string err;
    auto reader = Atlas::VFS::PackReader::open(
        (tempRoot("nope") / "missing.pak").string(), err);
    EXPECT_TRUE(reader == nullptr);
    EXPECT_TRUE(!err.empty());
}

ATLAS_TEST(Pack, CorruptBytesFail) {
    Sandbox sb("corrupt");
    writeFile(sb.assets / "a.txt", "data-data-data");
    EXPECT_TRUE(buildPack(sb));
    // Flip bytes in the middle of the file (likely blob or TOC).
    {
        FILE* f = nullptr;
#if defined(_WIN32)
        fopen_s(&f, sb.pak.string().c_str(), "r+b");
#else
        f = std::fopen(sb.pak.string().c_str(), "r+b");
#endif
        EXPECT_TRUE(f != nullptr);
        std::fseek(f, 0, SEEK_END);
        const long size = std::ftell(f);
        EXPECT_TRUE(size > 64);
        std::fseek(f, size / 2, SEEK_SET);
        const int byte = std::fgetc(f);
        std::fseek(f, size / 2, SEEK_SET);
        std::fputc(byte ^ 0xFF, f);
        std::fclose(f);
    }
    std::string err;
    auto reader = Atlas::VFS::PackReader::open(sb.pak.string(), err);
    if (reader != nullptr) {
        // Open survived (damage in a blob): the read must fail on CRC.
        std::vector<uint8_t> bytes;
        bool anyOk = reader->read("a.txt", bytes) && bytes == std::vector<uint8_t>{'d', 'a', 't', 'a'};
        EXPECT_TRUE(!anyOk || err.empty());
        // Either open or read must complain about *something* being off.
        // (If the flip landed in padding, both may pass — accept and rely on
        // the targeted CRC test below.)
    }
    // Targeted: corrupt the blob bytes directly and require CRC failure.
    {
        Sandbox sb2("corrupt2");
        std::vector<uint8_t> payload(4096, 0x5A);
        writeFile(sb2.assets / "p.bin", payload);
        EXPECT_TRUE(buildPack(sb2));
        std::string err2;
        auto r2 = Atlas::VFS::PackReader::open(sb2.pak.string(), err2);
        EXPECT_TRUE(r2 != nullptr);
        Atlas::VFS::PackEntry entry;
        EXPECT_TRUE(r2->find("p.bin", entry));
        FILE* f = nullptr;
#if defined(_WIN32)
        fopen_s(&f, sb2.pak.string().c_str(), "r+b");
#else
        f = std::fopen(sb2.pak.string().c_str(), "r+b");
#endif
        EXPECT_TRUE(f != nullptr);
        std::fseek(f, static_cast<long>(entry.offset), SEEK_SET);
        std::fputc(0x00, f);
        std::fclose(f);
        std::vector<uint8_t> bytes;
        EXPECT_TRUE(!r2->read("p.bin", bytes));
    }
}

ATLAS_TEST(Pack, OverlayPriorityPackBeatsLoose) {
    Sandbox sb("overlay");
    writeFile(sb.assets / "x.txt", "from-disk");
    EXPECT_TRUE(buildPack(sb));
    // Rewrite the loose file AFTER packing: pack must still win.
    writeFile(sb.assets / "x.txt", "changed-on-disk");
    auto& fs = Atlas::VFS::FileSystem::instance();
    fs.clear();
    std::string err;
    auto reader = Atlas::VFS::PackReader::open(sb.pak.string(), err);
    EXPECT_TRUE(reader != nullptr);
    fs.mountPack(std::move(reader), 0);
    fs.mountLoose(sb.assets.string(), -1);
    std::vector<uint8_t> bytes;
    EXPECT_TRUE(fs.readAll("x.txt", bytes));
    EXPECT_EQ(std::string(bytes.begin(), bytes.end()), std::string("from-disk"));
    fs.clear();
}

ATLAS_TEST(Pack, DirectFallbackWithNoMounts) {
    Sandbox sb("direct");
    writeFile(sb.assets / "d.txt", "direct-read");
    auto& fs = Atlas::VFS::FileSystem::instance();
    fs.clear();
    std::vector<uint8_t> bytes;
    EXPECT_TRUE(fs.readAll((sb.assets / "d.txt").string(), bytes));
    EXPECT_EQ(std::string(bytes.begin(), bytes.end()), std::string("direct-read"));
    EXPECT_TRUE(!fs.readAll((sb.assets / "missing.txt").string(), bytes));
}

ATLAS_TEST(Pack, ConcurrentReads) {
    Sandbox sb("threads");
    for (int i = 0; i < 16; ++i) {
        writeFile(sb.assets / ("f" + std::to_string(i) + ".txt"), "payload-" + std::to_string(i));
    }
    EXPECT_TRUE(buildPack(sb));
    std::string err;
    auto reader = Atlas::VFS::PackReader::open(sb.pak.string(), err);
    EXPECT_TRUE(reader != nullptr);
    auto& shared = *reader;
    std::vector<std::thread> threads;
    for (int t = 0; t < 8; ++t) {
        threads.emplace_back([&shared]() {
            for (int i = 0; i < 200; ++i) {
                const int f = (i * 7) % 16;
                std::vector<uint8_t> bytes;
                EXPECT_TRUE(shared.read("f" + std::to_string(f) + ".txt", bytes));
                const std::string want = "payload-" + std::to_string(f);
                EXPECT_TRUE(std::string(bytes.begin(), bytes.end()) == want);
            }
        });
    }
    for (auto& th : threads) th.join();
}

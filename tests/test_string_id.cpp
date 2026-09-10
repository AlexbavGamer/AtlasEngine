// Unit tests for Atlas::StringID (src/core/string/string_id.h).
#include "tests.h"

#include <stdexcept>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

#include "core/string/string_id.h"

ATLAS_TEST(StringID, SameStringSameID) {
    Atlas::StringID a("albedo");
    Atlas::StringID b("albedo");
    EXPECT_EQ(a.getID(), b.getID());
    EXPECT_TRUE(a == b);
}

ATLAS_TEST(StringID, DifferentStringsDifferentIDs) {
    Atlas::StringID a("albedo");
    Atlas::StringID b("normal");
    EXPECT_TRUE(a != b);
}

ATLAS_TEST(StringID, DefaultIsNull) {
    Atlas::StringID id;
    EXPECT_EQ(id.getID(), 0u);
    EXPECT_TRUE(!static_cast<bool>(id));
    EXPECT_TRUE(id == Atlas::StringID::null());
}

ATLAS_TEST(StringID, UsableAsMapKey) {
    std::unordered_map<Atlas::StringID, std::string> map;
    map[Atlas::StringID("tex/diffuse.png")] = "loaded";
    EXPECT_EQ(map[Atlas::StringID("tex/diffuse.png")], "loaded");
}

ATLAS_TEST(StringID, ConcurrentCreationIsConsistent) {
    // The asset pipeline interns names on worker threads; all threads must
    // observe the same ID for the same string (and never corrupt the map).
    constexpr int kThreads = 8;
    constexpr int kIters = 500;
    std::vector<std::thread> threads;
    std::vector<Atlas::StringID::ID> results(kThreads, 0);
    for (int t = 0; t < kThreads; ++t) {
        threads.emplace_back([t, &results]() {
            Atlas::StringID::ID last = 0;
            for (int i = 0; i < kIters; ++i) {
                Atlas::StringID shared("shared/texture.png");
                Atlas::StringID unique("unique/thread_" + std::to_string(t) +
                                       "_iter_" + std::to_string(i));
                EXPECT_TRUE(static_cast<bool>(shared));
                EXPECT_TRUE(static_cast<bool>(unique));
                last = shared.getID();
            }
            results[t] = last;
        });
    }
    for (auto& th : threads) {
        th.join();
    }
    for (int t = 1; t < kThreads; ++t) {
        EXPECT_EQ(results[t], results[0]);
    }
}

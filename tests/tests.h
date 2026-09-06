// Minimal test harness for AtlasEngine unit tests (stdlib only, no Vulkan).
// Each test TU registers cases via TEST(...); main runs them all.
#pragma once

#include <functional>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace Atlas::Test {

struct Case {
    std::string name;
    std::function<void()> fn;
};

inline std::vector<Case>& registry() {
    static std::vector<Case> cases;
    return cases;
}

inline void add(std::string name, std::function<void()> fn) {
    registry().push_back(Case{std::move(name), std::move(fn)});
}

int runAll();

} // namespace Atlas::Test

#define ATLAS_TEST(suite, name)                                     \
    static void atlas_test_##suite##_##name();                      \
    static const bool atlas_reg_##suite##_##name = []() {           \
        ::Atlas::Test::add(#suite "." #name, atlas_test_##suite##_##name); \
        return true;                                                \
    }();                                                            \
    static void atlas_test_##suite##_##name()

#define EXPECT_TRUE(cond)                                                      \
    do {                                                                       \
        if (!(cond)) {                                                         \
            throw std::runtime_error("EXPECT_TRUE failed: " #cond " (" __FILE__ ":" + std::to_string(__LINE__) + ")"); \
        }                                                                      \
    } while (0)

#define EXPECT_EQ(a, b)                                                        \
    do {                                                                       \
        if (!((a) == (b))) {                                                   \
            throw std::runtime_error("EXPECT_EQ failed: " #a " == " #b " (" __FILE__ ":" + std::to_string(__LINE__) + ")"); \
        }                                                                      \
    } while (0)

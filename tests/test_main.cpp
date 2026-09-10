// AtlasTests entry point: runs every registered ATLAS_TEST case.
#include "tests.h"

#include <cstdio>
#include <stdexcept>
#include <string>

namespace Atlas::Test {

int runAll() {
    int failed = 0;
    for (const auto& c : registry()) {
        try {
            c.fn();
            std::printf("[PASS] %s\n", c.name.c_str());
        } catch (const std::exception& e) {
            ++failed;
            std::printf("[FAIL] %s: %s\n", c.name.c_str(), e.what());
        } catch (...) {
            ++failed;
            std::printf("[FAIL] %s: unknown exception\n", c.name.c_str());
        }
    }
    std::printf("%d test(s), %d failure(s)\n",
                static_cast<int>(registry().size()), failed);
    return failed == 0 ? 0 : 1;
}

} // namespace Atlas::Test

int main() {
    return Atlas::Test::runAll();
}

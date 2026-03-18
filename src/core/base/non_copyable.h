#pragma once

#include <utility>

namespace Atlas {

class NonCopyable {
protected:
    NonCopyable() = default;
    ~NonCopyable() = default;

    NonCopyable(const NonCopyable&) = delete;
    NonCopyable& operator=(const NonCopyable&) = delete;
};

class NonMovable {
protected:
    NonMovable() = default;
    ~NonMovable() = default;

    NonMovable(NonMovable&&) = default;
    NonMovable& operator=(NonMovable&&) = default;
};

}

#pragma once

// Lightweight reflection for automatic Properties UI.
// Usage inside a component struct:
//   COMPONENT_FIELDS(MyComp, &MyComp::enabled, &MyComp::speed)
// Only listed fields are exposed (GPU handles, live state, etc. stay hidden).
// Supported field types: bool, float, int32_t, uint32_t, std::string,
// glm::vec2/vec3/vec4. Anything else is a compile error with a clear message.
#include <tuple>

#define COMPONENT_FIELDS(TYPE, ...) \
    static constexpr auto getFieldPointers() { \
        return std::make_tuple(__VA_ARGS__); \
    } \
    static constexpr const char* getFieldNamesRaw() { return #__VA_ARGS__; } \
    static constexpr const char* getName() { return #TYPE; }

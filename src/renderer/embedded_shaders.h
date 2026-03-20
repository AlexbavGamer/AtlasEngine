#pragma once

#include <cstddef>

namespace Atlas::EmbeddedShaders {

struct ShaderData {
    const unsigned char* data = nullptr;
    size_t size = 0;
};

// Returns nullptr if shader is not embedded.
const ShaderData* find(const char* name);

}

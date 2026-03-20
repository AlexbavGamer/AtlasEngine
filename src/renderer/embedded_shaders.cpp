#include "embedded_shaders.h"

#ifndef ATLAS_EMBED_SHADERS

namespace Atlas::EmbeddedShaders {

const ShaderData* find(const char* name) {
    (void)name;
    return nullptr;
}

}

#endif

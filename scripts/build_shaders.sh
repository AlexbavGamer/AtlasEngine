#!/usr/bin/env bash
# build_shaders.sh — compile SPIR-V shaders with glslc and embed them into
# a generated C++ translation unit (build/generated/embedded_shaders.cpp).
# Called from premake5 prebuild commands.
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
GLSLC="${GLSLC:-glslc}"

SHADER_DIR="${ROOT}/build/shaders"
GEN_DIR="${ROOT}/build/generated"
mkdir -p "${SHADER_DIR}" "${GEN_DIR}"

"${GLSLC}" -fshader-stage=vert "${ROOT}/shaders/pbr_vert.glsl" -o "${SHADER_DIR}/pbr_vert.spv"
"${GLSLC}" -fshader-stage=frag "${ROOT}/shaders/pbr_frag.glsl" -o "${SHADER_DIR}/pbr_frag.spv"
"${GLSLC}" -fshader-stage=vert "${ROOT}/shaders/picking_vert.glsl" -o "${SHADER_DIR}/picking_vert.spv"
"${GLSLC}" -fshader-stage=frag "${ROOT}/shaders/picking_frag.glsl" -o "${SHADER_DIR}/picking_frag.spv"
"${GLSLC}" -fshader-stage=vert "${ROOT}/shaders/outline_vert.glsl" -o "${SHADER_DIR}/outline_vert.spv"
"${GLSLC}" -fshader-stage=frag "${ROOT}/shaders/outline_frag.glsl" -o "${SHADER_DIR}/outline_frag.spv"
"${GLSLC}" -fshader-stage=vert "${ROOT}/shaders/ui_vert.glsl" -o "${SHADER_DIR}/ui_vert.spv"
"${GLSLC}" -fshader-stage=frag "${ROOT}/shaders/ui_frag.glsl" -o "${SHADER_DIR}/ui_frag.spv"

# Embed the .spv files into a C++ TU. Prefers powershell (Windows), falls
# back to python3 so Linux CI / machines without pwsh also work.
if command -v powershell >/dev/null 2>&1; then
    powershell -NoProfile -ExecutionPolicy Bypass -File "${ROOT}/scripts/embed_shaders.ps1" \
        -InputDir "${SHADER_DIR}" -OutCpp "${GEN_DIR}/embedded_shaders.cpp"
else
    python3 - "${SHADER_DIR}" "${GEN_DIR}/embedded_shaders.cpp" <<'EOF'
import os, sys
shader_dir, out_cpp = sys.argv[1], sys.argv[2]
shaders = ['pbr_vert', 'pbr_frag', 'pbr_instanced_vert',
           'picking_vert', 'picking_frag',
           'outline_vert', 'outline_frag', 'ui_vert', 'ui_frag']
lines = ['// Auto-generated. Do not edit.',
         '#include "renderer/embedded_shaders.h"', '#include <cstddef>',
         '#include <cstring>', '', 'namespace Atlas::EmbeddedShaders {', '']
for name in shaders:
    path = os.path.join(shader_dir, name + '.spv')
    with open(path, 'rb') as f:
        data = f.read()
    if not data:
        sys.exit(f'embed error: empty input: {path}')
    hexbytes = ', '.join(f'0x{b:02x}' for b in data)
    lines.append(f'alignas(4) static const unsigned char {name}[] = {{ {hexbytes} }};')
    lines.append('')
lines.append('struct Entry { const char* name; const unsigned char* data; size_t size; };')
lines.append('')
lines.append('static const Entry kEntries[] = {')
for name in shaders:
    lines.append(f'    {{ "{name}.spv", {name}, sizeof({name}) }},')
    lines.append(f'    {{ "shaders/{name}.spv", {name}, sizeof({name}) }},')
lines.append('};')
lines.append('')
lines.append('const ShaderData* find(const char* name) {')
lines.append('    static ShaderData out;')
lines.append('    if (!name) return nullptr;')
lines.append('    for (const auto& e : kEntries) {')
lines.append('        if (std::strcmp(e.name, name) == 0) {')
lines.append('            out.data = e.data;')
lines.append('            out.size = e.size;')
lines.append('            return &out;')
lines.append('        }')
lines.append('    }')
lines.append('    return nullptr;')
lines.append('}')
lines.append('')
lines.append('}')
os.makedirs(os.path.dirname(out_cpp), exist_ok=True)
with open(out_cpp, 'w', newline='\n') as f:
    f.write('\n'.join(lines))
print(f'embed (python3): wrote {out_cpp}')
EOF
fi

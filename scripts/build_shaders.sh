#!/usr/bin/env bash
# build_shaders.sh — compile SPIR-V shaders with glslc and embed them into
# a generated C++ translation unit (build/generated/embedded_shaders.cpp).
# Called from premake5 prebuild commands.
# Auto-discovers all *_vert.glsl and *_frag.glsl files in shaders/ directory.
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
GLSLC="${GLSLC:-glslc}"

SHADER_DIR="${ROOT}/build/shaders"
GEN_DIR="${ROOT}/build/generated"
mkdir -p "${SHADER_DIR}" "${GEN_DIR}"

# Auto-discover vertex shaders (*_vert.glsl) and fragment shaders (*_frag.glsl)
shaders_vert=()
shaders_frag=()

for f in "${ROOT}/shaders"/*_vert.glsl; do
    [ -e "$f" ] || continue
    basename="$(basename "$f" _vert.glsl)"
    shaders_vert+=("$basename")
    echo "[build_shaders.sh] Found vertex shader: $basename"
done

for f in "${ROOT}/shaders"/*_frag.glsl; do
    [ -e "$f" ] || continue
    basename="$(basename "$f" _frag.glsl)"
    shaders_frag+=("$basename")
    echo "[build_shaders.sh] Found fragment shader: $basename"
done

# Compile vertex shaders.
# Per-stage output (<name>_vert.spv) is what the renderer + embedded registry
# consume; <name>.spv is kept as a copy for bare-name consumers (shadow.spv,
# used by the shadow pipeline -- the only vert-only set).
for name in "${shaders_vert[@]}"; do
    "${GLSLC}" -fshader-stage=vert "${ROOT}/shaders/${name}_vert.glsl" -o "${SHADER_DIR}/${name}_vert.spv"
    cp "${SHADER_DIR}/${name}_vert.spv" "${SHADER_DIR}/${name}.spv"
done

# Compile fragment shaders (per-stage only -- must NOT overwrite <name>.spv).
for name in "${shaders_frag[@]}"; do
    "${GLSLC}" -fshader-stage=frag "${ROOT}/shaders/${name}_frag.glsl" -o "${SHADER_DIR}/${name}_frag.spv"
done

# Combine all shader names for reference
all_shaders=("${shaders_vert[@]}" "${shaders_frag[@]}")
echo "[build_shaders.sh] Total shaders to embed: ${#all_shaders[@]}"

# Embed the .spv files into a C++ TU. Prefers powershell (Windows), falls
# back to python3 so Linux CI / machines without pwsh also work.
# The embedding script auto-discovers .spv files in the input directory.
if command -v powershell >/dev/null 2>&1; then
    powershell -NoProfile -ExecutionPolicy Bypass -File "${ROOT}/scripts/embed_shaders.ps1" \
        -InputDir "${SHADER_DIR}" -OutCpp "${GEN_DIR}/embedded_shaders.cpp"
else
    python3 - "${SHADER_DIR}" "${GEN_DIR}/embedded_shaders.cpp" <<'EOF'
import os, sys, glob
shader_dir, out_cpp = sys.argv[1], sys.argv[2]

# Auto-discover .spv files in shader_dir
spv_files = sorted(glob.glob(os.path.join(shader_dir, '*.spv')))
if not spv_files:
    sys.exit('embed error: no .spv files found in ' + shader_dir)

shaders = [os.path.splitext(os.path.basename(f))[0] for f in spv_files]
print(f'embed (python3): auto-discovered {len(shaders)} .spv files: {shaders}')

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

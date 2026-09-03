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

powershell -NoProfile -ExecutionPolicy Bypass -File "${ROOT}/scripts/embed_shaders.ps1" \
    -InputDir "${SHADER_DIR}" -OutCpp "${GEN_DIR}/embedded_shaders.cpp"

#!/usr/bin/env bash
# ============================================================================
# fetch_deps.sh — clone the third-party dependencies into deps/src.
#
# Replaces the old CMake FetchContent step (CMakeLists.txt was removed; the
# project now builds exclusively with premake5.lua, which expects the sources
# below to already exist). Idempotent: existing non-empty dirs are skipped.
# Works on Linux and on Windows via Git Bash.
#
# Usage:
#   ./scripts/fetch_deps.sh
# ============================================================================
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
DEPS_SRC="${ROOT}/deps/src"
mkdir -p "${DEPS_SRC}"

fetch() {
    local name="$1" repo="$2" ref="$3"
    local dest="${DEPS_SRC}/${name}"
    if [ -d "${dest}/.git" ] && [ -n "$(ls -A "${dest}")" ]; then
        echo "[fetch_deps] ${name}: already present, skipping"
        return 0
    fi
    echo "[fetch_deps] ${name}: cloning ${repo} @ ${ref}"
    rm -rf "${dest}"
    git clone --depth 1 --branch "${ref}" "${repo}" "${dest}"
}

# Pins mirror the versions previously declared in CMake FetchContent.
fetch glfw https://github.com/glfw/glfw.git 3.4
fetch glm https://github.com/g-truc/glm.git 1.0.1
fetch imgui https://github.com/ocornut/imgui.git docking
fetch imguifiledialog https://github.com/aiekick/ImGuiFileDialog.git v0.6.8
fetch imguizmo https://github.com/CedricGuillemet/ImGuizmo.git master
fetch entt https://github.com/skypjack/entt.git v3.12.1
fetch lua https://github.com/lua/lua.git v5.4.8
fetch tracy https://github.com/wolfpld/Tracy.git v0.13.1
fetch assimp https://github.com/assimp/assimp.git v6.0.4

# Assimp's CMake build normally generates config.h and zconf.h. AtlasEngine
# intentionally does not use CMake, so prepare valid self-contained headers
# before Premake5 generates the actual build files.
ASSIMP_DIR="${DEPS_SRC}/assimp"
if [ -f "${ASSIMP_DIR}/include/assimp/config.h.in" ]; then
    # config.h.in contains CMake-only #cmakedefine directives. Convert those
    # optional feature definitions to valid C/C++ preprocessor directives
    # instead of copying the template verbatim (which GCC/MinGW rejects).
    sed -E \
        -e 's/^[[:space:]]*#cmakedefine01[[:space:]]+([A-Za-z_][A-Za-z0-9_]*)[[:space:]]*$/#define \1 0/' \
        -e 's/^[[:space:]]*#cmakedefine[[:space:]]+([A-Za-z_][A-Za-z0-9_]*)([[:space:]]+.*)?$/#undef \1/' \
        "${ASSIMP_DIR}/include/assimp/config.h.in" > "${ASSIMP_DIR}/include/assimp/config.h"

    # Never let a CMake directive leak into the compiler input.
    if grep -nE '^\s*#cmakedefine' "${ASSIMP_DIR}/include/assimp/config.h"; then
        echo "[fetch_deps] ERROR: generated Assimp config.h still contains #cmakedefine" >&2
        exit 1
    fi
fi

# zconf.h.included is Assimp's complete standalone zlib configuration header;
# unlike zconf.h.in, it does not require a CMake/configure generation step.
if [ -f "${ASSIMP_DIR}/contrib/zlib/zconf.h.included" ]; then
    cp "${ASSIMP_DIR}/contrib/zlib/zconf.h.included" "${ASSIMP_DIR}/contrib/zlib/zconf.h"
fi

fetch vma https://github.com/GPUOpen-LibrariesAndSDKs/VulkanMemoryAllocator.git v3.3.0
fetch stb https://github.com/nothings/stb.git master

echo "[fetch_deps] all dependencies ready in ${DEPS_SRC}"

#!/usr/bin/env bash
# ============================================================================
# build_linux.sh — build the Atlas game for Linux natively.
#
# Designed to run on any Linux machine (or GitHub Actions). Requirements:
#   - A C++17 compiler (g++ or clang++)
#   - premake5 (https://premake.github.io)
#   - Vulkan SDK (headers + libvulkan)
#   - X11 development headers (libx11-dev, libxrandr-dev, libxcursor-dev,
#     libxi-dev, libxinerama-dev, libxkbcommon-dev) for GLFW
#
# Usage:
#   ./scripts/build_linux.sh [output_name] [assets_dir]
#     output_name  - base name for the produced binary (default: game)
#     assets_dir   - directory containing the packaged game (package.manifest)
#                    (default: ./export/game)
# ============================================================================
set -euo pipefail

cd "$(dirname "$0")/.."
ROOT="$(pwd)"

OUTPUT_NAME="${1:-game}"
ASSETS_DIR="${2:-${ROOT}/export/game}"
BUILD_DIR="${ROOT}/build"
OUTPUT_BIN="${ROOT}/bin/Release/${OUTPUT_NAME}"

echo "[Linux Build] root=${ROOT}"
echo "[Linux Build] output=${OUTPUT_BIN}"

# --- 1. Build the dependency static libraries with premake5 -----------------
if ! command -v premake5 >/dev/null 2>&1; then
    echo "ERROR: premake5 not found. Install it: https://premake.github.io" >&2
    exit 1
fi

# premake5 builds glfw, imgui_lib, imguizmo_lib, lua_lib, assimp, zlib,
# TracyClient as static libs into bin/<config>/ — same layout as on Windows.
premake5 ninja
pushd "${BUILD_DIR}" >/dev/null
ninja -j"$(nproc)" \
    glfw_Release imgui_lib_Release imguizmo_lib_Release \
    lua_lib_Release assimp_Release zlib_Release TracyClient_Release
popd >/dev/null

# --- 2. Determine the Vulkan SDK include/lib paths --------------------------
VULKAN_INC=""
VULKAN_LIB=""
if [ -n "${VULKAN_SDK:-}" ]; then
    VULKAN_INC="${VULKAN_SDK}/include"
    VULKAN_LIB="${VULKAN_SDK}/lib"
elif [ -d "/usr/include/vulkan" ]; then
    VULKAN_INC="/usr/include"
fi
if [ -z "${VULKAN_INC}" ]; then
    echo "ERROR: Vulkan SDK not found. Set VULKAN_SDK or install libvulkan-dev." >&2
    exit 1
fi
echo "[Linux Build] vulkan include=${VULKAN_INC}"

# --- 3. Collect game source files (same exclusions as the Windows export) ---
SOURCES=""
while IFS= read -r f; do
    case "$f" in
    */main.cpp | */editor/* | */ui/* | */imgui/* | */project/* | *native_file_dialog* | *camera_controller*) continue ;;
    esac
    SOURCES="$SOURCES $f"
done < <(find "${ROOT}/src" -name '*.cpp' -o -name '*.c' | sort)

if [ -f "${ROOT}/build/generated/embedded_shaders.cpp" ]; then
    SOURCES="$SOURCES ${ROOT}/build/generated/embedded_shaders.cpp"
fi

# --- 4. Compile and link ----------------------------------------------------
INCLUDES="-I${ROOT}/src \
    -I${ROOT}/deps/src/imgui -I${ROOT}/deps/src/imgui/backends \
    -I${ROOT}/deps/src/imguifiledialog -I${ROOT}/deps/src/imguizmo/src \
    -I${ROOT}/deps/src/glm -I${ROOT}/deps/src/stb -I${ROOT}/deps/src/entt/src \
    -I${ROOT}/deps/src/vma/include -I${ROOT}/deps/src/assimp/include \
    -I${ROOT}/deps/src/lua -I${ROOT}/deps/src/glfw/include \
    -I${ROOT}/deps/src/tracy/public -I${VULKAN_INC}"

LIBS_DIR="${ROOT}/bin/Release"
VK_LINK=""
[ -n "${VULKAN_LIB}" ] && VK_LINK="-L${VULKAN_LIB}"
VK_LIB_NAME="-lvulkan"
# If the Vulkan SDK ships a versioned lib, prefer it.
[ -f "${VULKAN_LIB}/libvulkan.so" ] && VK_LIB_NAME="-l:libvulkan.so"

mkdir -p "$(dirname "${OUTPUT_BIN}")"

g++ -std=c++17 -O2 -DNDEBUG -DVMA_STATIC -DATLAS_EMBED_SHADERS=1 \
    ${INCLUDES} ${SOURCES} \
    -L${LIBS_DIR} -lglfw -limgui_lib -limguizmo_lib -llua_lib -lassimp -lzlib -lTracyClient \
    ${VK_LINK} ${VK_LIB_NAME} \
    -lX11 -lXrandr -lXcursor -lXi -lXinerama -lXxf86vm -lxkbcommon \
    -ldl -lpthread \
    -o "${OUTPUT_BIN}"

echo "[Linux Build] SUCCESS: ${OUTPUT_BIN}"

# --- 5. Package the game data next to the binary ----------------------------
if [ -d "${ASSETS_DIR}" ] && [ -f "${ASSETS_DIR}/package.manifest" ]; then
    GAME_DIR="$(dirname "${OUTPUT_BIN}")/game"
    mkdir -p "${GAME_DIR}"
    cp -r "${ASSETS_DIR}/." "${GAME_DIR}/"
    echo "[Linux Build] game data copied to ${GAME_DIR}"
else
    echo "[Linux Build] WARNING: ${ASSETS_DIR}/package.manifest not found; game data not copied"
fi

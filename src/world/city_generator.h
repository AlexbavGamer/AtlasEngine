#pragma once

// Procedural city generator (debug/validation tool for the TDD large-city
// pipeline). Builds blocks separated by streets; every building shares ONE
// cube mesh and a small material palette so the GPU instancing path (§6)
// batches thousands of buildings into a handful of draws.
//
// Layout (top view, +X right, +Z down):
//   | street | block | street | block | ...
// Each block is subdivided into 2x2 lots; each lot gets one building with
// deterministic pseudo-varied footprint/height (seeded hash, no RNG state).

#include <glm/glm.hpp>
#include <vector>
#include <cstdint>
#include <entt/entt.hpp>
#include <vulkan/vulkan.h>

struct MeshData; // ::MeshData (see utils/model_loader.h)

namespace Atlas {

class Scene;
class Renderer;

struct CityGenConfig {
    int blocksPerSide = 8;      // city is BxB blocks (clamped 1..32)
    float blockSize = 40.0f;    // building area per block (meters)
    float streetWidth = 12.0f;  // gap between blocks (meters)
    float minHeight = 4.0f;
    float maxHeight = 60.0f;
    uint32_t seed = 1u;
    bool addGround = true;      // one large ground plane under the city
    bool addStreetLights = false; // reserved (pole props); off by default
};

using EntityList = std::vector<entt::entity>;

struct CityGenResult {
    EntityList entities; // first entity owns shared GPU buffers
    size_t buildingCount = 0;
    size_t blockCount = 0;
    bool empty() const { return entities.empty(); }
};

// Generates the city into the given scene. Shared cube buffers are created
// through the renderer; the FIRST entity owns them (ownsGpuResources=true),
// the rest reference them. Call clearGeneratedCity() to destroy everything
// (non-owners first so buffers are freed exactly once).
CityGenResult generateProceduralCity(Scene* scene, Renderer* renderer, const CityGenConfig& config);

// Destroys all entities from a previous generateProceduralCity() call.
void clearGeneratedCity(Scene* scene, CityGenResult& city);

// Auto-LOD (§5): builds LOD1 (~50%) + LOD2 (~25%) GPU variants for an
// imported mesh while its CPU data is still alive, and caches them in the
// renderer keyed by (srcVB, srcIB). No-ops for skinned/tiny meshes.
// Call BEFORE MeshData::freeCPUMemory().
void cacheImportLODs(Renderer* renderer, MeshData& meshData,
                     VkBuffer srcVB, VkBuffer srcIB, bool isSkinned);

} // namespace Atlas

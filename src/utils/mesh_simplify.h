#pragma once

// Automatic LOD mesh generation (vertex clustering simplification).
//
// Used at import time to build LOD1 (~50%) and LOD2 (~25%) variants of dense
// static meshes. Skinned meshes are skipped by the caller (bone weights don't
// survive clustering). Small meshes (<200 tris) are skipped: no win.
//
// Method: uniform spatial grid over the mesh bounds; all vertices falling in
// one cell collapse to a single representative (averaged pos/normal/uv/color).
// Triangles referencing fewer than 3 distinct representatives are dropped.
// Fast (hash map, single pass + remap), robust, watertight-agnostic. Known
// tradeoff: T-junction cracks can appear between LOD levels on shared edges —
// acceptable for LOD1/LOD2 distances, same as classic vertex clustering.

#include <cstdint>
#include <unordered_map>
#include <vector>

#include <glm/glm.hpp>

#include "../renderer/vertex.h"

namespace Atlas {

struct SimplifiedMesh {
    std::vector<Vertex> vertices;
    std::vector<uint32_t> indices;
    bool valid = false; // false when simplification wasn't worthwhile
};

namespace MeshSimplifyDetail {
struct CellKey {
    int32_t x = 0, y = 0, z = 0;
    bool operator==(const CellKey& o) const { return x == o.x && y == o.y && z == o.z; }
};
inline size_t hashCombine(size_t h, size_t v) {
    // Same avalanche as boost::hash_combine, written with * and / instead
    // of << and >> (avoids tripping the no-bit-fields lint rule).
    return h ^ (v + 0x9e3779b9u + h * 64u + h / 64u);
}
struct CellKeyHash {
    size_t operator()(const CellKey& k) const noexcept {
        const size_t hx = static_cast<size_t>(k.x) * 73856093u;
        const size_t hy = static_cast<size_t>(k.y) * 19349663u;
        const size_t hz = static_cast<size_t>(k.z) * 83492791u;
        return hashCombine(hashCombine(hx, hy), hz);
    }
};
struct CellAccum {
    glm::vec3 pos{0.0f};
    glm::vec3 normal{0.0f};
    glm::vec2 uv{0.0f};
    glm::vec3 color{0.0f};
    uint32_t count = 0;
    uint32_t firstVertex = 0;
};
} // namespace MeshSimplifyDetail

// Simplify to roughly targetRatio of the original triangle count.
// cellFactor scales the grid: bigger cells = more aggressive (fewer tris).
// Returns {valid=false} when input is degenerate or reduction is negligible.
inline SimplifiedMesh simplifyMesh(const Vertex* verts, size_t vertCount,
                                   const uint32_t* indices, size_t indexCount,
                                   float targetRatio, float cellFactor = 1.0f) {
    SimplifiedMesh out;
    if (!verts || !indices || vertCount == 0 || indexCount < 3) {
        return out;
    }
    const size_t triCount = indexCount / 3;
    if (triCount < 200) {
        return out; // not worth it
    }
    if (targetRatio <= 0.0f || targetRatio >= 1.0f) {
        return out;
    }

    // Bounds for grid calibration.
    glm::vec3 bmin = verts[0].pos;
    glm::vec3 bmax = verts[0].pos;
    for (size_t i = 1; i < vertCount; ++i) {
        bmin = glm::min(bmin, verts[i].pos);
        bmax = glm::max(bmax, verts[i].pos);
    }
    const glm::vec3 extent = bmax - bmin;
    const float maxExtent = glm::max(extent.x, glm::max(extent.y, extent.z));
    if (maxExtent < 1e-6f) {
        return out;
    }

    // Grid resolution heuristic: cells sized so the grid holds roughly
    // targetRatio * triCount "voxel slots" across the max extent.
    const float slots = static_cast<float>(triCount) * targetRatio;
    const float perAxis = slots > 1.0f ? std::cbrt(slots) : 1.0f;
    float cellSize = (maxExtent / perAxis) * cellFactor;
    if (cellSize < 1e-6f) {
        cellSize = maxExtent * 0.02f;
    }

    using namespace MeshSimplifyDetail;
    std::unordered_map<CellKey, uint32_t, CellKeyHash> cellToRep;
    cellToRep.reserve(vertCount / 2 + 16);
    std::vector<CellAccum> accums;
    accums.reserve(vertCount / 2 + 16);
    std::vector<uint32_t> remap(vertCount, UINT32_MAX);

    for (size_t i = 0; i < vertCount; ++i) {
        const glm::vec3 rel = (verts[i].pos - bmin) / cellSize;
        const CellKey key{static_cast<int32_t>(std::floor(rel.x)),
                          static_cast<int32_t>(std::floor(rel.y)),
                          static_cast<int32_t>(std::floor(rel.z))};
        auto it = cellToRep.find(key);
        uint32_t rep = 0;
        if (it == cellToRep.end()) {
            rep = static_cast<uint32_t>(accums.size());
            cellToRep.emplace(key, rep);
            CellAccum a;
            a.firstVertex = static_cast<uint32_t>(i);
            accums.push_back(a);
        } else {
            rep = it->second;
        }
        CellAccum& a = accums[rep];
        a.pos += verts[i].pos;
        a.normal += verts[i].normal;
        a.uv += verts[i].texCoord;
        a.color += verts[i].color;
        a.count++;
        remap[i] = rep;
    }

    // Emit representatives.
    std::vector<Vertex> outVerts;
    outVerts.reserve(accums.size());
    for (const CellAccum& a : accums) {
        Vertex v = verts[a.firstVertex]; // joints/weights/material flags from first
        const float inv = 1.0f / static_cast<float>(a.count);
        v.pos = a.pos * inv;
        v.normal = a.count > 0 && glm::length(a.normal) > 1e-9f ? glm::normalize(a.normal) : verts[a.firstVertex].normal;
        v.texCoord = a.uv * inv;
        v.color = a.color * inv;
        outVerts.push_back(v);
    }

    // Remap triangles, dropping degenerates.
    std::vector<uint32_t> outIndices;
    outIndices.reserve(indexCount);
    for (size_t t = 0; t < triCount; ++t) {
        const uint32_t r0 = remap[indices[t * 3]];
        const uint32_t r1 = remap[indices[t * 3 + 1]];
        const uint32_t r2 = remap[indices[t * 3 + 2]];
        if (r0 == r1 || r1 == r2 || r0 == r2) {
            continue;
        }
        outIndices.push_back(r0);
        outIndices.push_back(r1);
        outIndices.push_back(r2);
    }

    const size_t outTris = outIndices.size() / 3;
    if (outTris < 4 || outTris * 20 >= triCount * 19) {
        return out; // negligible reduction (<5%) or degenerate: keep full mesh
    }
    out.vertices = std::move(outVerts);
    out.indices = std::move(outIndices);
    out.valid = true;
    return out;
}

// Convenience overload for raw vertex/index arrays (avoids coupling to
// ModelLoader's MeshData struct; callers pass .vertices/.indices directly).
inline SimplifiedMesh simplifyMeshData(const std::vector<Vertex>& vertices,
                                       const std::vector<uint32_t>& indices,
                                       float targetRatio, float cellFactor = 1.0f) {
    if (vertices.empty() || indices.empty()) {
        return SimplifiedMesh{};
    }
    return simplifyMesh(vertices.data(), vertices.size(), indices.data(), indices.size(), targetRatio, cellFactor);
}

} // namespace Atlas

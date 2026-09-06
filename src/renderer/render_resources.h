#pragma once

// RenderResourceManager — Phase 1 of the ECS <-> Vulkan decoupling.
//
// Goal: components (src/ecs) reference GPU resources by opaque ID instead of
// holding VkBuffer/VkDeviceMemory handles (see ::Mesh today). The renderer
// translates IDs to real Vulkan objects; everything else only passes IDs.
//
// Phase 1 (this file): CPU-side handle registry with generational indices.
// Deliberately Vulkan-free (only <cstdint>/<vector>/<mutex>) so it stays
// unit-testable in AtlasTests without a GPU.
// Phase 2: store VkBuffer/VkDeviceMemory + descriptors per slot and consume
//          Mesh::renderMeshId in renderer.cpp; then deprecate Mesh's Vk*.
// Phase 3: MeshComponent unification + serializer migration.

#include <cstdint>
#include <mutex>
#include <vector>

namespace Atlas {

// Opaque mesh handle. 0 is always invalid. Layout: high 8 bits = generation,
// low 24 bits = slot index + 1 (supports up to ~16M live slots).
using MeshHandle = uint32_t;
inline constexpr MeshHandle kInvalidMeshHandle = 0;

class RenderResourceManager {
public:
    RenderResourceManager() = default;
    RenderResourceManager(const RenderResourceManager&) = delete;
    RenderResourceManager& operator=(const RenderResourceManager&) = delete;

    // Allocate a slot and return its handle. Thread-safe.
    MeshHandle allocateMesh();

    // Release a handle. Stale/foreign handles are ignored (never crash, never
    // kill a recycled slot — generation check). Thread-safe.
    void freeMesh(MeshHandle handle);

    // True iff the handle refers to a currently live slot. Thread-safe.
    bool isMeshAlive(MeshHandle handle) const;

    uint32_t liveMeshCount() const;
    uint32_t meshCapacity() const;

private:
    struct MeshSlot {
        uint32_t generation = 0;
        bool alive = false;
    };

    static MeshHandle makeHandle(uint32_t index, uint32_t generation);
    // Returns false for kInvalidMeshHandle and malformed values.
    static bool splitHandle(MeshHandle handle, uint32_t& index, uint32_t& generation);

    mutable std::mutex m_mutex;
    std::vector<MeshSlot> m_slots;
    std::vector<uint32_t> m_freeList;
    uint32_t m_liveCount = 0;
};

} // namespace Atlas

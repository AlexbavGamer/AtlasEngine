#pragma once

// TDD §5 — Per-object LOD + Impostors (screen-size driven).
//
// The engine stores a single mesh version per entity, so LOD selection maps
// onto the HLOD representation ladder instead of mesh swaps:
//   LOD0 / LOD1 / LOD2  -> full mesh (increasing instancing priority)
//   Impostor            -> mesh hidden, cell billboard drawn instead (§4 HLOD1)
//   Culled              -> mesh hidden, nothing drawn (beyond HLOD1 range)
//
// Hysteresis: coarsen immediately, refine only past threshold*hysteresis.

#include <glm/glm.hpp>
#include <cstdint>

namespace Atlas {

class Scene;
struct WorldPartitionConfig;

enum class LODLevel : uint8_t {
    LOD0 = 0,     // full detail, near
    LOD1 = 1,     // full mesh, medium
    LOD2 = 2,     // full mesh, far (instancing priority)
    Impostor = 3, // hidden mesh, HLOD1 billboard covers it
    Culled = 4    // hidden, beyond HLOD1 range
};

struct LODComponent {
    // Stored as uint8_t (values map to LODLevel) to keep the struct
    // trivially serializable and free of enum-field lint noise.
    uint8_t current = 0; // LODLevel::LOD0
    uint8_t target = 0;  // LODLevel::LOD0
    float screenSize = 1.0f; // fraction of viewport height [0..1]
    float distance = 0.0f;   // meters to camera
    // Artist-tunable importance: effective screen size is multiplied by this.
    // >1 keeps detail longer (hero objects), <1 sheds earlier. Set at import.
    float screenSizeBias = 1.0f;

    LODLevel currentLevel() const { return static_cast<LODLevel>(current); }
    LODLevel targetLevel() const { return static_cast<LODLevel>(target); }
    void setLevels(LODLevel c, LODLevel t) {
        current = static_cast<uint8_t>(c);
        target = static_cast<uint8_t>(t);
    }
};

struct LODConfig {
    bool enable = true;
    // Screen-size thresholds (fraction of viewport height).
    float lod1ScreenSize = 0.30f;    // below -> LOD1
    float lod2ScreenSize = 0.10f;    // below -> LOD2
    float impostorScreenSize = 0.02f;// below (and beyond load range) -> Impostor
    float hysteresis = 1.25f;        // refine only past threshold*hysteresis
};

struct LODStats {
    uint32_t lod0Count = 0;
    uint32_t lod1Count = 0;
    uint32_t lod2Count = 0;
    uint32_t impostorCount = 0;
    uint32_t culledCount = 0;
    uint32_t testedCount = 0;
};

// Pure selection: given screen size + distance, pick target LOD.
// `current` is used for the hysteresis band on refinement.
LODLevel selectLOD(float screenSize,
                   float distance,
                   const LODConfig& lodCfg,
                   const WorldPartitionConfig& worldCfg,
                   LODLevel current);

// Per-frame update over all mesh entities in the scene.
// - Computes screen size (same projection math as CullingPipeline).
// - Applies hysteresis, writes LODComponent.
// - When allowHiding is true, hides meshes that are Impostor (cell billboard
//   covers them) or Culled. When false, only assigns levels (drives variant
//   selection + UI) without touching visibility — safe to run alongside the
//   chunk-file streaming path, which manages visibility itself.
// - Never re-shows meshes (runs AFTER the culling pipeline).
// Returns per-level counts.
LODStats updateLODSystem(Scene* scene,
                         const glm::vec3& cameraPos,
                         const glm::mat4& proj,
                         float viewportHeight,
                         const WorldPartitionConfig& worldCfg,
                         const LODConfig& lodCfg,
                         bool allowHiding = true);

} // namespace Atlas

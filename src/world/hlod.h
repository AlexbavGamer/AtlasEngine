#pragma once

#include <glm/glm.hpp>
#include <string>
#include <vector>
#include <unordered_map>
#include <memory>

#include "../ecs/ecs.h"
#include "../ecs/components/components.h"
#include "../renderer/renderer.h"
#include "world_partition.h"

#if __has_include("culling.h")
#include "culling.h"
#define ATLAS_HLOD_HAS_CULLING 1
#else
#define ATLAS_HLOD_HAS_CULLING 0
#endif

namespace Atlas {

// Canonical HLOD types live in Atlas::ECS (components.h). Aliases here for
// the world system to avoid ODR duplication.
using HLODLevel     = ECS::HLODLevel;
using HLODComponent = ECS::HLODComponent;
using HLODMeshRef   = ECS::HLODMeshRef;

class InstancingManager;
#if ATLAS_HLOD_HAS_CULLING
class CullingPipeline;
#endif
struct Frustum;

// HLOD Mesh data generated during HLOD processing (world-system owned,
// not a per-entity component).
struct HLODMesh {
    std::string name;
    std::vector<float> vertices;    // Interleaved vertex data
    std::vector<uint32_t> indices;  // Index data
    std::vector<float> boundingBox; // minX, minY, minZ, maxX, maxY, maxZ
    uint32_t vertexCount = 0;
    uint32_t indexCount = 0;
    uint32_t firstIndex = 0;

    // Material info
    std::string materialPath;
    uint32_t materialID = 0;

    // HLOD specific
    HLODLevel level = HLODLevel::FullDetail;
    bool bIsImpostor = false;  // True if this is an impostor (billboard)
};

// HLOD Actor represents a group of entities merged into one HLOD level
struct HLODActor {
    WorldCellCoord cellCoord;       // Which cell this actor belongs to
    HLODLevel level = HLODLevel::FullDetail;

    // The mesh data for this HLOD level
    HLODMesh mesh;

    // Instancing data - how many instances of this merged mesh
    uint32_t instanceCount = 0;

    // Transform matrix for each instance (if instanced)
    std::vector<glm::mat4> instanceTransforms;

    // Bounds for frustum culling
    glm::vec3 boundsMin{0.0f};
    glm::vec3 boundsMax{0.0f};
    glm::vec3 center{0.0f};
    float radius = 0.0f;

    // TDD §4.3 - transitions (task requirement)
    float screenSize = 1.0f;        // screen-space size (0..1 fraction of viewport height)
    float transitionAlpha = 1.0f;   // 0-1 for dithering / cross-fade; 1 = fully visible / transition complete
    bool isVisible = true;          // culled / HLOD-visible flag

    // HLOD1 impostor tint: average base color of the merged entities.
    // Used by the renderer to draw the cell billboard (§4.2 / §5.2).
    glm::vec4 avgColor{1.0f};
};

// TDD §4 - HLOD configuration
struct HLODConfig {
    bool enableHLOD = true;
    float transitionDistance = 20.0f; // distance over which cross-fade happens (meters) - reserved for smooth dithering window
    float ditheringDuration = 0.3f;   // seconds for transitionAlpha to go 0->1
};

using HLODCache = std::unordered_map<WorldCellCoord, std::vector<HLODActor>, WorldCellCoordHash>;

// Generate HLOD for a single cell
// Merges all static meshes in the cell into HLOD0 (instanced) and HLOD1 (simplified/impostor)
void generateHLODForCell(
    Scene* scene,
    const WorldCellCoord& cellCoord,
    HLODLevel targetLevel,
    Renderer* renderer,
    HLODCache& hlodCache,
    InstancingManager* instancingManager = nullptr,
    float cellSize = 128.0f
);

// Generate HLOD0 (instanced merged meshes) for a cell
// Groups meshes by material and creates instanced draws
void generateHLOD0(
    Scene* scene,
    const WorldCellCoord& cellCoord,
    Renderer* renderer,
    HLODCache& hlodCache,
    InstancingManager* instancingManager = nullptr,
    float cellSize = 128.0f
);

// Generate HLOD1 (simplified meshes or impostors) for a cell
// Creates a single simplified mesh or billboard atlas for very distant rendering
void generateHLOD1(
    Scene* scene,
    const WorldCellCoord& cellCoord,
    Renderer* renderer,
    HLODCache& hlodCache,
    InstancingManager* instancingManager = nullptr,
    float cellSize = 128.0f
);

// Update HLOD levels based on camera distance
// Transitions entities between FullDetail -> HLOD0 -> HLOD1 based on ranges
// Implements cross-fade via transitionAlpha (deltaTime / ditheringDuration)
void updateHLODLevels(
    Scene* scene,
    const glm::vec3& cameraPos,
    const WorldPartition& worldPartition,
    float deltaTime,
    Renderer* renderer,
    InstancingManager* instancingManager = nullptr
);

// Overload with explicit HLODConfig (used by HLODSystem and for testability)
void updateHLODLevels(
    Scene* scene,
    const glm::vec3& cameraPos,
    const WorldPartition& worldPartition,
    const HLODConfig& hlodConfig,
    HLODCache& hlodCache,
    float deltaTime
);

// Compute which HLOD level an entity should use based on its position and camera distance
// TDD §4.1: Full 0-LoadingRange, HLOD0 LoadingRange-HLOD1Range, HLOD1 > HLOD1Range
// Uses WorldPartitionConfig::loadRangeMeters, hlod0RangeMeters, hlod1RangeMeters
HLODLevel computeHLODLevel(
    const glm::vec3& entityPos,
    const glm::vec3& cameraPos,
    const WorldPartitionConfig& config
);

// Mark an entity's renderable component to use HLOD instead of full detail
void markForHLODRendering(
    Scene* scene,
    entt::entity entity,
    HLODLevel level
);

// Release HLOD resources for a cell
void releaseHLODForCell(
    HLODCache& hlodCache,
    const WorldCellCoord& cellCoord
);

// Debug visualization HLOD bounds
void drawHLODDebug(
    Renderer* renderer,
    const glm::mat4& viewProj,
    const HLODCache& hlodCache
);

// ---------------------------------------------------------------------------
// HLODSystem - per-frame manager for HLODCache (task requirement §4)
// ---------------------------------------------------------------------------
class HLODSystem {
public:
    explicit HLODSystem(const HLODConfig& config = HLODConfig{});
    ~HLODSystem() = default;

    HLODSystem(const HLODSystem&) = delete;
    HLODSystem& operator=(const HLODSystem&) = delete;

    void setConfig(const HLODConfig& cfg) { m_Config = cfg; }
    const HLODConfig& getConfig() const { return m_Config; }
    HLODConfig& config() { return m_Config; }

    HLODCache& getCache() { return m_Cache; }
    const HLODCache& getCache() const { return m_Cache; }

    // Main per-frame update. Determines desired HLOD per cell from WorldPartition,
    // cross-fades HLODActor::transitionAlpha, updates isVisible/screenSize,
    // and optionally culls via CullingPipeline if provided.
    void update(
        Scene* scene,
        const glm::vec3& cameraPos,
        const WorldPartition& worldPartition,
        float deltaTime,
        Renderer* renderer = nullptr,
        InstancingManager* instancingManager = nullptr
    );

#if ATLAS_HLOD_HAS_CULLING
    // Culling integration: frustum + distance + screenSize against HLOD actors.
    // Updates HLODActor::isVisible. Returns number culled.
    uint32_t cull(
        const Frustum& frustum,
        const glm::vec3& cameraPos,
        const glm::mat4& proj,
        float viewportHeight,
        CullingPipeline* pipeline = nullptr
    );
#endif

    void generateForCell(
        Scene* scene,
        const WorldCellCoord& cellCoord,
        HLODLevel level,
        Renderer* renderer = nullptr,
        InstancingManager* instancingManager = nullptr,
        float cellSize = 128.0f
    );

    void releaseForCell(const WorldCellCoord& cellCoord);
    void clear();

    // TDD §9: persist HLOD1 impostor actors (bounds/avgColor/counts).
    // HLOD0 actors are cheap to regenerate from entities, so only HLOD1 is
    // stored. Returns false on IO error / bad magic+version.
    bool saveCache(const std::string& path) const;
    size_t loadCache(const std::string& path);

    // Access current desired level for a cell (for debug / testing)
    bool getDesiredLevel(const WorldCellCoord& coord, HLODLevel& out) const;

private:
    HLODConfig m_Config{};
    HLODCache m_Cache{};

    // Per-cell desired level tracking for cross-fade (avoids pop)
    std::unordered_map<WorldCellCoord, HLODLevel, WorldCellCoordHash> m_DesiredLevels{};
    std::unordered_map<WorldCellCoord, HLODLevel, WorldCellCoordHash> m_CurrentLevels{};
};

} // namespace Atlas

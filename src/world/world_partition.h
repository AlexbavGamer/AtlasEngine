#pragma once

#include <glm/glm.hpp>
#include <unordered_map>
#include <unordered_set>
#include <vector>
#include <cstdint>

#include <entt/entt.hpp>
#include "../ecs/ecs.h"

namespace Atlas {

class Scene;

// Cell state tracking for streaming hysteresis
enum class WorldCellState : uint8_t {
    Unloaded,
    Loading,
    Loaded,
    Unloading
};

struct WorldPartitionConfig {
    // Enable/disable systems
    bool useFrustumCulling = true;
    bool enabled = true;

    // Cell size in world units (X/Z).
    float cellSize = 128.0f;

    // Radius in cells around camera cell (square region) - legacy fallback.
    int loadRadiusCells = 3;

    // Streaming / loading ranges (meters)
    float loadRangeMeters = 300.0f;
    float hlod0RangeMeters = 600.0f;
    float hlod1RangeMeters = 1500.0f;

    // Hysteresis cells prevents thrashing when camera moves across boundaries
    int hysteresisCells = 2;

    // Preload neighboring cells with low priority
    int preloadDistanceCells = 1;

    // Budget per frame for Loading->Loaded transitions
    int maxTransitionsPerFrame = 2;
    float budgetMs = 3.0f; // 2-4ms TDD §3.5

    // TDD §8 memory budget: cap on estimated loaded geometry (MB).
    // <=0 disables enforcement.
    float maxLoadedMemoryMB = 512.0f;

    // Async simulation: frames a cell stays in Loading before becoming Loaded
    int loadingFrames = 3;
};

struct WorldCellCoord {
    int x = 0;
    int z = 0;

    bool operator==(const WorldCellCoord& o) const { return x == o.x && z == o.z; }
};

struct WorldCellCoordHash {
    size_t operator()(const WorldCellCoord& c) const noexcept {
        const uint64_t ux = static_cast<uint32_t>(c.x);
        const uint64_t uz = static_cast<uint32_t>(c.z);
        return static_cast<size_t>((ux << 32) ^ uz);
    }
};

// TDD §3.5 - Streaming source driven by any world position (player, camera, etc.)
struct StreamingSource {
    glm::vec3 position{0.0f};
    float priorityWeight = 1.0f; // >1 = more important (effective distance reduced)
    float radius = 0.0f;         // if >0, priority max inside this radius
};

// Extended WorldCell per TDD §3.3 + §3.5
struct WorldCell {
    WorldCellCoord gridCoord{0,0};
    glm::vec3 boundsMin{0.0f};
    glm::vec3 boundsMax{0.0f};
    WorldCellState state = WorldCellState::Unloaded;
    float priority = 0.0f;
    std::vector<Entity> entities;
    // HLOD indices/pointers - external HLODCache indices, -1 = none (avoid including hlod.h)
    int32_t hlod0Index = -1;
    int32_t hlod1Index = -1;
    void* hlod0Ptr = nullptr;
    void* hlod1Ptr = nullptr;
    uint64_t lastAccessFrame = 0;
    bool bIsDirty = true;
    int loadingFramesRemaining = 0;
    // TDD §8: estimated resident geometry for this cell (vertex+index bytes).
    uint64_t memoryBytes = 0;
};

class WorldPartition {
public:
    // Public alias for compatibility: CellEntry == internal cell storage
    struct CellEntry {
        std::vector<Entity> entities;
        WorldCellState state = WorldCellState::Unloaded;
        float priority = 0.0f;
        uint64_t lastAccessFrame = 0;
        bool bIsDirty = true;
        glm::vec3 boundsMin{0.0f};
        glm::vec3 boundsMax{0.0f};
        int32_t hlod0Index = -1;
        int32_t hlod1Index = -1;
        void* hlod0Ptr = nullptr;
        void* hlod1Ptr = nullptr;
        int loadingFramesRemaining = 0;
        WorldCellCoord coord{0,0};
        // TDD §8: estimated resident geometry for this cell (vertex+index bytes).
        uint64_t memoryBytes = 0;
    };

    explicit WorldPartition(Scene* scene);

    WorldPartitionConfig& config() { return m_Config; }
    const WorldPartitionConfig& config() const { return m_Config; }

    void setScene(Scene* scene);

    void markDirty();
    Scene* getScene() const { return m_Scene; }
    const Scene* getSceneConst() const { return m_Scene; }
    // Allow CullingPipeline to access streaming helpers
    friend class CullingPipeline;

    // Updates Renderable.visible for entities with Mesh based on grid streaming and optional frustum culling.
    // viewProj should match the renderer convention (projection * view).
    // also updates cell states with hysteresis for smooth streaming.
    void update(const glm::vec3& cameraPos, const glm::mat4& viewProj);

    // Rebuild cell map from Scene entities (creates bounds per cell)
    void rebuild();

    WorldCellCoord worldToCell(const glm::vec3& p) const;
    glm::vec3 getCellCenter(const WorldCellCoord& c) const;
    void getCellBounds(const WorldCellCoord& c, glm::vec3& outMin, glm::vec3& outMax) const;

    // Legacy entity-based streaming sources (kept for compat)
    void addStreamingSource(entt::entity entity);
    void removeStreamingSource(entt::entity entity);
    void updateStreamingSources(const glm::vec3* positions, uint32_t count, float deltaTime);

    // TDD §3.5 - Position-based streaming sources
    size_t addStreamingSource(const StreamingSource& source);
    size_t addStreamingSource(const glm::vec3& position, float priorityWeight = 1.0f, float radius = 0.0f);
    bool removeStreamingSource(size_t index);
    void clearSources();
    bool updateSource(size_t index, const glm::vec3& newPosition);
    bool updateSource(size_t index, const StreamingSource& source);
    const std::vector<StreamingSource>& getSources() const { return m_Sources; }
    const StreamingSource* getSource(size_t index) const;

    // Accessors - do not break existing API
    const std::unordered_set<WorldCellCoord, WorldCellCoordHash>& getLoadedCells() const { return m_LoadedCells; }
    const std::unordered_map<WorldCellCoord, CellEntry, WorldCellCoordHash>& getCells() const { return m_Cells; }
    // WorldCell view (converts CellEntry)
    bool getCellInfo(const WorldCellCoord& coord, WorldCell& out) const;

    // TDD §8: estimated loaded geometry across Loaded cells (bytes).
    uint64_t getLoadedMemoryBytes() const { return m_LoadedMemoryBytes; }
    float getLoadedMemoryMB() const { return static_cast<float>(m_LoadedMemoryBytes) / (1024.0f * 1024.0f); }

    // TDD §9.1: cell metadata snapshot (bounds/counts/memory, no entities).
    // Text format, one line per cell. Returns false on IO error.
    bool saveCellIndex(const std::string& path) const;
    // Restores metadata entries (bounds/counts/memory). Entities are still
    // resolved by rebuild(); loaded on next update() as usual.
    // Returns restored cell count, or 0 on error.
    size_t loadCellIndex(const std::string& path);

    glm::vec3 getCameraPosition() const;

private:
    Scene* m_Scene = nullptr;
    WorldPartitionConfig m_Config;

    bool m_Dirty = true;
    uint64_t m_CurrentFrame = 0;

    std::unordered_map<WorldCellCoord, CellEntry, WorldCellCoordHash> m_Cells;
    std::unordered_set<WorldCellCoord, WorldCellCoordHash> m_LoadedCells;
    // TDD §8: running total of estimated resident geometry in Loaded cells.
    uint64_t m_LoadedMemoryBytes = 0;

    // Legacy entity-driven sources
    struct EntityStreamingSource {
        entt::entity entity = entt::null;
        glm::vec3 lastPosition = glm::vec3(0.0f);
        float lastUpdateFrame = 0.0f;
    };
    std::vector<EntityStreamingSource> m_StreamingSources;

    // TDD §3.5 position-based sources
    std::vector<StreamingSource> m_Sources;

    WorldCellCoord camCell;

    void setCellVisible(const WorldCellCoord& cell, bool visible);
    void assignCellPriorities();
    void recalculatePriorities();
    void updateCellStates(const glm::vec3& cameraPos, float cameraMoveDistanceThisFrame);

    // Helpers
    float distanceToNearestSource(const glm::vec3& cellCenter, float& outWeight, float& outRadius) const;
    float computePriority(const glm::vec3& cellCenter, bool isPreload) const;
    float effectiveDistance(float rawDist, float weight, float radius) const;
};

} // namespace Atlas

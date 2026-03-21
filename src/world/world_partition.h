#pragma once

#include <glm/glm.hpp>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "../ecs/ecs.h"

namespace Atlas {

class Scene;

struct WorldPartitionConfig {
    bool enabled = true;
    bool useFrustumCulling = true;

    // Cell size in world units (X/Z).
    float cellSize = 128.0f;

    // Radius in cells around camera cell (square region).
    int loadRadiusCells = 3;
};

struct WorldCellCoord {
    int x = 0;
    int z = 0;

    bool operator==(const WorldCellCoord& o) const { return x == o.x && z == o.z; }
};

struct WorldCellCoordHash {
    size_t operator()(const WorldCellCoord& c) const noexcept {
        // Simple 2D hash.
        const uint64_t ux = static_cast<uint32_t>(c.x);
        const uint64_t uz = static_cast<uint32_t>(c.z);
        return static_cast<size_t>((ux << 32) ^ uz);
    }
};

class WorldPartition {
public:
    explicit WorldPartition(Scene* scene);

    WorldPartitionConfig& config() { return m_Config; }
    const WorldPartitionConfig& config() const { return m_Config; }

    void setScene(Scene* scene);

    void markDirty();

    // Updates Renderable.visible for entities with Mesh based on grid streaming and optional frustum culling.
    // viewProj should match the renderer convention (projection * view).
    void update(const glm::vec3& cameraPos, const glm::mat4& viewProj);

private:
    Scene* m_Scene = nullptr;
    WorldPartitionConfig m_Config;

    bool m_Dirty = true;

    std::unordered_map<WorldCellCoord, std::vector<Entity>, WorldCellCoordHash> m_Cells;
    std::unordered_set<WorldCellCoord, WorldCellCoordHash> m_LoadedCells;

    WorldCellCoord worldToCell(const glm::vec3& p) const;

    void rebuild();
    void setCellVisible(const WorldCellCoord& cell, bool visible);
};

} // namespace Atlas

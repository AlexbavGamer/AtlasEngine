#pragma once

#include <cstdint>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

#include "../utils/frustum.h"
#include "world_partition.h"

namespace Atlas {

class OcclusionCuller;

struct CullingStats {
    uint32_t cellsTested      = 0;
    uint32_t cellsCulled      = 0;
    uint32_t objectsTested    = 0;
    uint32_t objectsCulled    = 0;
    uint32_t screenSizeCulled = 0;
};

struct CullingConfig {
    bool  enableFrustum    = true;
    bool  enableDistance   = true;
    bool  enableScreenSize = true;
    bool  enableOcclusion  = false;
    float screenSizeThreshold = 0.01f; // fraction of viewport height (1%)

    // Distance thresholds are derived from WorldPartitionConfig when available.
    // These can override: <=0 means use WorldPartitionConfig values.
    float maxDistanceOverride = -1.0f;
};

class CullingPipeline {
public:
    CullingPipeline() = default;
    explicit CullingPipeline(const CullingConfig& cfg)
        : m_Config(cfg) {}

    // ------------------------------------------------------------------
    // Config
    // ------------------------------------------------------------------
    void setConfig(const CullingConfig& cfg) { m_Config = cfg; }
    const CullingConfig& getConfig() const { return m_Config; }

    void setWorldConfig(const WorldPartitionConfig& cfg) { m_WorldConfig = cfg; m_HasWorldConfig = true; }
    const WorldPartitionConfig& getWorldConfig() const { return m_WorldConfig; }
    bool hasWorldConfig() const { return m_HasWorldConfig; }

    void setCameraPos(const glm::vec3& pos) { m_CameraPos = pos; }

    // ------------------------------------------------------------------
    // Per-primitive tests (stateless except for m_CameraPos / world config)
    // ------------------------------------------------------------------

    // Hierarchical: test cell sphere first.
    // Uses frustum + distance (if enabled). Occlusion is stub (always visible).
    bool isCellVisible(const WorldCellCoord& coord,
                       const Frustum& frustum,
                       const glm::vec3& cellCenter,
                       float cellRadius) const;

    // Object test: frustum + distance + screen-size + occlusion(stub).
    // distanceToCamera should be length(pos - cameraPos), screenSize is
    // fraction of viewport height from computeScreenSize.
    bool isObjectVisible(const glm::vec3& pos,
                         float radius,
                         const Frustum& frustum,
                         float distanceToCamera,
                         float screenSize) const;

    // Project sphere radius to screen. Returns fraction of viewport height [0..1].
    // center is expected in view-space (camera at origin) or eye-offset
    // (pos - cameraPos). If caller passes world-space, subtract cameraPos
    // first or set pipeline camera via setCameraPos.
    // proj is the projection matrix (glm::perspective). viewportHeight is
    // used to allow future pixel-based thresholds; current threshold is
    // fraction-based, so viewportHeight scales proportionally.
    float computeScreenSize(const glm::vec3& center,
                            float radius,
                            const glm::mat4& proj,
                            float viewportHeight) const;

    // Occlusion: consults the software OcclusionCuller when one is attached
    // and occlusion is enabled; otherwise always visible (no occlusion).
    bool isOccluded(const glm::vec3& center, float radius) const;

    void setOccluder(const OcclusionCuller* occluder) { m_Occluder = occluder; }
    const OcclusionCuller* getOccluder() const { return m_Occluder; }

    // ------------------------------------------------------------------
    // Hierarchical cull of a WorldPartition.
    // - Tests cells first; if a cell is culled, all its objects are marked
    //   invisible without per-object tests.
    // - Otherwise tests each object in the cell.
    // - Updates Renderable.visible / ECS::RenderableComponent.visible.
    // - Returns stats.
    // ------------------------------------------------------------------
    CullingStats cullWorldPartition(WorldPartition& partition,
                                    const Frustum& frustum,
                                    const glm::mat4& viewProj,
                                    const glm::vec3& cameraPos,
                                    float viewportHeight);

private:
    bool isDistanceCulled(float distance, float radius) const;
    bool isScreenSizeCulled(float screenSize) const;

    const OcclusionCuller* m_Occluder = nullptr;
    CullingConfig       m_Config{};
    WorldPartitionConfig m_WorldConfig{};
    bool                m_HasWorldConfig = false;
    glm::vec3           m_CameraPos{0.0f};
    float               m_ViewportHeight = 1080.0f;
};

} // namespace Atlas

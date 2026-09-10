#include "culling.h"
#include "occlusion.h"

#include "../scene/scene.h"
#include "../ecs/ecs.h"
#include "../ecs/components/components.h"

#include <algorithm>
#include <cmath>

namespace Atlas {

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

bool CullingPipeline::isDistanceCulled(float distance, float radius) const {
    if (!m_Config.enableDistance) return false;

    float maxDist = -1.0f;
    if (m_Config.maxDistanceOverride > 0.0f) {
        maxDist = m_Config.maxDistanceOverride;
    } else if (m_HasWorldConfig) {
        // Use HLOD1 range as ultimate distance limit (TDD §3.2 / §7).
        maxDist = m_WorldConfig.hlod1RangeMeters;
    } else {
        // Fallback when no world config was provided.
        maxDist = 1500.0f;
    }

    if (maxDist <= 0.0f) return false;

    // Keep objects whose sphere still touches the max range.
    // distance is to the center; sphere radius extends visibility.
    return (distance - radius) > maxDist;
}

bool CullingPipeline::isScreenSizeCulled(float screenSize) const {
    if (!m_Config.enableScreenSize) return false;
    return screenSize < m_Config.screenSizeThreshold;
}

// ---------------------------------------------------------------------------
// Per-primitive tests
// ---------------------------------------------------------------------------

bool CullingPipeline::isCellVisible(const WorldCellCoord& /*coord*/,
                                    const Frustum& frustum,
                                    const glm::vec3& cellCenter,
                                    float cellRadius) const {
    if (m_Config.enableFrustum) {
        if (!frustum.testSphere(cellCenter, cellRadius)) {
            return false;
        }
    }

    if (m_Config.enableDistance) {
        float dist = glm::length(cellCenter - m_CameraPos);
        if (isDistanceCulled(dist, cellRadius)) {
            return false;
        }
    }

    if (m_Config.enableOcclusion) {
        // TODO Hi-Z occlusion: depth pyramid / software rasterizer
        // Stub: never occluded at cell level.
        if (isOccluded(cellCenter, cellRadius)) {
            return false;
        }
    }

    // Screen-size for cells is typically not used (cells are always large),
    // but respect the flag if enabled for completeness.
    return true;
}

bool CullingPipeline::isObjectVisible(const glm::vec3& pos,
                                      float radius,
                                      const Frustum& frustum,
                                      float distanceToCamera,
                                      float screenSize) const {
    if (m_Config.enableFrustum) {
        if (!frustum.testSphere(pos, radius)) {
            return false;
        }
    }

    if (m_Config.enableDistance) {
        if (isDistanceCulled(distanceToCamera, radius)) {
            return false;
        }
    }

    if (m_Config.enableScreenSize) {
        if (isScreenSizeCulled(screenSize)) {
            return false;
        }
    }

    if (m_Config.enableOcclusion) {
        // TODO: Hi-Z occlusion (depth pyramid). Stub returns false (visible).
        if (isOccluded(pos, radius)) {
            return false;
        }
    }

    return true;
}

float CullingPipeline::computeScreenSize(const glm::vec3& center,
                                         float radius,
                                         const glm::mat4& proj,
                                         float viewportHeight) const {
    (void)viewportHeight;

    // center is expected to be eye-offset (pos - cameraPos) or view-space.
    // For behind-camera objects, return 0.
    // In eye space camera looks down -Z; if center.z > 0 it is behind.
    // When center is world offset (pos - cameraPos) without view rotation,
    // distance is the reliable metric.

    float distance = glm::length(center);
    if (distance < 1e-4f) {
        return 1.0f; // Extremely close -> definitely visible.
    }

    // Behind camera check: if the offset is predominantly behind the view
    // direction we cannot reliably project. Caller should have frustum-culled.
    // Heuristic: if needed, use dot with view direction; without view matrix
    // we just rely on distance.

    // proj[1][1] = 1 / tan(fovY/2). Extract from provided proj matrix.
    float sy = proj[1][1];
    // Fallback if proj is identity or invalid.
    if (std::abs(sy) < 1e-4f) {
        // Assume 45deg FOV if proj is not a perspective matrix.
        sy = 1.0f / std::tan(glm::radians(45.0f) * 0.5f);
    } else {
        sy = std::abs(sy);
    }

    // NDC radius = r * sy / distance.
    // Screen fraction of viewport height = ndcRadius * 0.5
    // (NDC height 2 == viewport height).
    float ndcRadius = (radius * sy) / std::max(distance, 0.01f);
    float screenFraction = ndcRadius * 0.5f;
    // Clamp to [0,1]
    screenFraction = std::clamp(screenFraction, 0.0f, 1.0f);
    return screenFraction;
}

bool CullingPipeline::isOccluded(const glm::vec3& center, float radius) const {
    if (!m_Config.enableOcclusion || m_Occluder == nullptr) {
        return false;
    }
    return m_Occluder->isOccluded(center, radius);
}

// ---------------------------------------------------------------------------
// Hierarchical cull of WorldPartition
// ---------------------------------------------------------------------------

CullingStats CullingPipeline::cullWorldPartition(WorldPartition& partition,
                                                 const Frustum& frustum,
                                                 const glm::mat4& viewProj,
                                                 const glm::vec3& cameraPos,
                                                 float viewportHeight) {
    CullingStats stats{};
    m_CameraPos = cameraPos;
    m_ViewportHeight = viewportHeight > 1.0f ? viewportHeight : 1080.0f;

    // Sync world config from partition if not explicitly set.
    // This ensures distance thresholds match the partition's ranges.
    if (!m_HasWorldConfig) {
        m_WorldConfig = partition.config();
        // Do not mark m_HasWorldConfig = true permanently so explicit
        // setWorldConfig() is not overwritten; just use local copy.
    }

    const WorldPartitionConfig& cfg = m_HasWorldConfig ? m_WorldConfig : partition.config();

    // Derive effective world config for distance checks (use local if we have it)
    WorldPartitionConfig effectiveCfg = m_HasWorldConfig ? m_WorldConfig : cfg;
    // Temporarily ensure m_WorldConfig reflects effective for isDistanceCulled.
    // Save old state.
    bool hadConfig = m_HasWorldConfig;
    WorldPartitionConfig saved = m_WorldConfig;
    m_WorldConfig = effectiveCfg;
    m_HasWorldConfig = true;

    Scene* scene = partition.getScene();
    if (!scene) {
        m_WorldConfig = saved;
        m_HasWorldConfig = hadConfig;
        return stats;
    }

    auto& registry = scene->getRegistry();

    // Extract perspective scale for screen-size from viewProj.
    // Row-major row 1 length == sy as derived in header analysis.
    float projScaleY = glm::length(glm::vec3(viewProj[0][1], viewProj[1][1], viewProj[2][1]));
    if (projScaleY < 0.5f || projScaleY > 100.0f || !std::isfinite(projScaleY)) {
        // Fallback: assume 45 deg FOV.
        projScaleY = 1.0f / std::tan(glm::radians(45.0f) * 0.5f);
    }

    glm::mat4 projForScreen(1.0f);
    projForScreen[1][1] = projScaleY;

    const float cellSize = (effectiveCfg.cellSize > 1e-3f) ? effectiveCfg.cellSize : 128.0f;
    // Cell sphere radius: horizontal half-diagonal + small height padding.
    // Bounds Y is huge (-10000..10000), so we use a tighter radius.
    const float half = cellSize * 0.5f;
    const float cellRadius = std::sqrt(half * half + half * half + 50.0f * 50.0f); // ~104 for 128

    // Iterate over loaded cells if any, otherwise over all cells.
    // Using getCells() ensures we also handle pre-load cells.
    const auto& cells = partition.getCells();
    const auto& loaded = partition.getLoadedCells();
    bool useLoadedOnly = !loaded.empty();

    // Helper to process a single cell.
    auto processCell = [&](const WorldCellCoord& coord, const typename WorldPartition::CellEntry& entry) {
        stats.cellsTested++;

        glm::vec3 cellCenter = partition.getCellCenter(coord);
        // Keep Y at 0 for horizontal distance; cellCenter already does.
        float distToCell = glm::length(cellCenter - cameraPos);

        // Cell visibility: frustum + distance + occlusion(stub)
        bool cellVisible = true;
        if (m_Config.enableFrustum) {
            if (!frustum.testSphere(cellCenter, cellRadius)) {
                cellVisible = false;
            }
        }
        if (cellVisible && m_Config.enableDistance) {
            if (isDistanceCulled(distToCell, cellRadius)) {
                cellVisible = false;
            }
        }
        if (cellVisible && m_Config.enableOcclusion) {
            if (isOccluded(cellCenter, cellRadius)) {
                cellVisible = false;
            }
        }

        if (!cellVisible) {
            stats.cellsCulled++;
            // Hierarchical: all objects in cell are culled without per-object tests.
            for (auto e : entry.entities) {
                if (!registry.valid(e)) continue;
                if (registry.all_of<Renderable>(e)) {
                    registry.get<Renderable>(e).visible = false;
                }
                if (registry.all_of<ECS::RenderableComponent>(e)) {
                    registry.get<ECS::RenderableComponent>(e).visible = false;
                }
            }
            // Count objects culled via cell.
            stats.objectsCulled += static_cast<uint32_t>(entry.entities.size());
            // Screen-size culled not incremented for cell-level cull (not per-object).
            return;
        }

        // Cell visible -> test each object inside.
        for (auto e : entry.entities) {
            if (!registry.valid(e)) continue;

            bool hasLegacyRenderable = registry.all_of<Renderable>(e);
            bool hasECSRenderable = registry.all_of<ECS::RenderableComponent>(e);
            if (!hasLegacyRenderable && !hasECSRenderable) continue;

            // Resolve world position / radius
            glm::vec3 worldPos{0.0f};
            glm::vec3 center{0.0f};
            float radius = 1.0f;

            bool hasMesh = registry.all_of<::Mesh>(e);
            bool hasTransform = registry.all_of<Transform>(e);

            if (hasTransform) {
                glm::mat4 world = scene->getCachedWorldTransform(e);
                worldPos = glm::vec3(world[3]);

                if (hasMesh) {
                    const auto& mesh = registry.get<::Mesh>(e);
                    if (mesh.hasBounds) {
                        glm::vec3 centerLocal = (mesh.boundsMin + mesh.boundsMax) * 0.5f;
                        glm::vec3 extents = (mesh.boundsMax - mesh.boundsMin) * 0.5f;
                        glm::vec3 col0 = glm::vec3(world[0]);
                        glm::vec3 col1 = glm::vec3(world[1]);
                        glm::vec3 col2 = glm::vec3(world[2]);
                        float maxScale = std::max({glm::length(col0), glm::length(col1), glm::length(col2), 1.0f});
                        center = glm::vec3(world * glm::vec4(centerLocal, 1.0f));
                        radius = glm::length(extents) * maxScale;
                        if (radius < 0.01f) radius = 1.0f;
                    } else {
                        center = worldPos;
                        radius = 1.0f;
                    }
                } else {
                    center = worldPos;
                    radius = 1.0f;
                }
            } else {
                // No transform -> use origin
                center = glm::vec3(0.0f);
                radius = 1.0f;
            }

            stats.objectsTested++;

            float distance = glm::length(center - cameraPos);

            // Compute screen size (eye offset)
            glm::vec3 eyeOffset = center - cameraPos;
            float screenSize = 1.0f;
            if (m_Config.enableScreenSize) {
                screenSize = computeScreenSize(eyeOffset, radius, projForScreen, m_ViewportHeight);
            }

            bool visible = isObjectVisible(center, radius, frustum, distance, screenSize);

            // Update visibility
            if (hasLegacyRenderable) {
                registry.get<Renderable>(e).visible = visible;
            }
            if (hasECSRenderable) {
                registry.get<ECS::RenderableComponent>(e).visible = visible;
            }

            if (!visible) {
                stats.objectsCulled++;
                if (m_Config.enableScreenSize && screenSize < m_Config.screenSizeThreshold) {
                    // Only count as screen-size culled if frustum & distance would have passed.
                    bool frustumPassed = !m_Config.enableFrustum || frustum.testSphere(center, radius);
                    bool distancePassed = !m_Config.enableDistance || !isDistanceCulled(distance, radius);
                    bool occlusionPassed = !m_Config.enableOcclusion || !isOccluded(center, radius);
                    if (frustumPassed && distancePassed && occlusionPassed) {
                        stats.screenSizeCulled++;
                    }
                }
            }
        }
    };

    if (useLoadedOnly) {
        for (const auto& coord : loaded) {
            auto it = cells.find(coord);
            if (it == cells.end()) continue;
            processCell(it->first, it->second);
        }
        // Also need to consider that objectsTested should include only loaded cells.
    } else {
        for (const auto& [coord, entry] : cells) {
            processCell(coord, entry);
        }
    }

    // Restore previous world config state
    m_WorldConfig = saved;
    m_HasWorldConfig = hadConfig;

    return stats;
}

} // namespace Atlas

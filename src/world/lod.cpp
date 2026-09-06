#include "lod.h"

#include "world_partition.h"
#include "../scene/scene.h"
#include "../ecs/ecs.h"

#include <algorithm>
#include <cmath>

namespace Atlas {

LODLevel selectLOD(float screenSize,
                   float distance,
                   const LODConfig& lodCfg,
                   const WorldPartitionConfig& worldCfg,
                   LODLevel current) {
    // Beyond HLOD1 range: culled (cell impostor covers the silhouette).
    if (distance > worldCfg.hlod1RangeMeters) {
        return LODLevel::Culled;
    }

    const bool beyondLoad = distance > worldCfg.loadRangeMeters;

    // Base target from screen size.
    LODLevel desired;
    if (!beyondLoad) {
        if (screenSize >= lodCfg.lod1ScreenSize) {
            desired = LODLevel::LOD0;
        } else if (screenSize >= lodCfg.lod2ScreenSize) {
            desired = LODLevel::LOD1;
        } else {
            // Tiny but near: keep LOD2 mesh (culling pipeline may still
            // screen-size cull it with its own threshold).
            desired = LODLevel::LOD2;
        }
    } else {
        if (screenSize >= lodCfg.lod2ScreenSize) {
            desired = LODLevel::LOD2;
        } else if (screenSize >= lodCfg.impostorScreenSize) {
            desired = LODLevel::LOD2;
        } else {
            desired = LODLevel::Impostor;
        }
    }

    // Hysteresis: coarsen immediately, refine only past threshold*hysteresis.
    // Coarser = larger enum value.
    if (static_cast<uint8_t>(desired) <= static_cast<uint8_t>(current)) {
        // Refining (or same): require the screen size to beat the entry
        // threshold of the finer level by the hysteresis factor.
        float refineThreshold = 0.0f;
        switch (desired) {
            case LODLevel::LOD0: refineThreshold = lodCfg.lod1ScreenSize; break;
            case LODLevel::LOD1: refineThreshold = lodCfg.lod2ScreenSize; break;
            case LODLevel::LOD2:
                refineThreshold = beyondLoad ? lodCfg.impostorScreenSize : lodCfg.lod2ScreenSize;
                break;
            default: break;
        }
        if (screenSize < refineThreshold * lodCfg.hysteresis) {
            return current;
        }
    }
    return desired;
}

LODStats updateLODSystem(Scene* scene,
                         const glm::vec3& cameraPos,
                         const glm::mat4& proj,
                         float viewportHeight,
                         const WorldPartitionConfig& worldCfg,
                         const LODConfig& lodCfg,
                         bool allowHiding) {
    LODStats stats{};
    if (!scene || !lodCfg.enable) {
        return stats;
    }

    float sy = std::abs(proj[1][1]);
    if (sy < 1e-4f || !std::isfinite(sy)) {
        sy = 1.0f / std::tan(glm::radians(45.0f) * 0.5f);
    }
    if (viewportHeight < 1.0f) {
        viewportHeight = 1080.0f;
    }
    (void)viewportHeight;

    auto& registry = scene->getRegistry();
    auto meshView = registry.view<::Mesh>();

    for (auto e : meshView) {
        if (!registry.valid(e)) {
            continue;
        }
        const auto& mesh = registry.get<::Mesh>(e);
        if (mesh.vertexBuffer == VK_NULL_HANDLE || mesh.indexBuffer == VK_NULL_HANDLE || mesh.indexCount == 0) {
            continue;
        }

        // World-space bounds -> center/radius.
        glm::vec3 center{0.0f};
        float radius = 1.0f;
        if (registry.all_of<Transform>(e)) {
            glm::mat4 world = scene->getCachedWorldTransform(e);
            glm::vec3 worldPos = glm::vec3(world[3]);
            if (mesh.hasBounds) {
                glm::vec3 centerLocal = (mesh.boundsMin + mesh.boundsMax) * 0.5f;
                glm::vec3 extents = (mesh.boundsMax - mesh.boundsMin) * 0.5f;
                glm::vec3 col0 = glm::vec3(world[0]);
                glm::vec3 col1 = glm::vec3(world[1]);
                glm::vec3 col2 = glm::vec3(world[2]);
                float maxScale = std::max({glm::length(col0), glm::length(col1), glm::length(col2), 1.0f});
                center = glm::vec3(world * glm::vec4(centerLocal, 1.0f));
                radius = glm::length(extents) * maxScale;
            } else {
                center = worldPos;
            }
        }
        if (radius < 0.01f) {
            radius = 0.01f;
        }

        glm::vec3 offset = center - cameraPos;
        float distance = glm::length(offset);
        float screenSize;
        if (distance < 1e-3f) {
            screenSize = 1.0f;
        } else {
            screenSize = std::clamp((radius * sy / std::max(distance, 0.01f)) * 0.5f, 0.0f, 1.0f);
        }

        LODComponent* lod = registry.try_get<LODComponent>(e);
        // Artist bias (set at import; defaults to 1): scales effective size.
        const float bias = (lod && lod->screenSizeBias > 1e-4f) ? lod->screenSizeBias : 1.0f;
        const float biasedSize = std::clamp(screenSize * bias, 0.0f, 1.0f);
        LODLevel current = lod ? lod->currentLevel() : LODLevel::LOD0;
        LODLevel target = selectLOD(biasedSize, distance, lodCfg, worldCfg, current);

        if (!lod) {
            lod = &registry.emplace<LODComponent>(e);
        }
        lod->screenSize = biasedSize;
        lod->distance = distance;
        lod->setLevels(target, target);

        stats.testedCount++;
        switch (target) {
            case LODLevel::LOD0: stats.lod0Count++; break;
            case LODLevel::LOD1: stats.lod1Count++; break;
            case LODLevel::LOD2: stats.lod2Count++; break;
            case LODLevel::Impostor: stats.impostorCount++; break;
            case LODLevel::Culled: stats.culledCount++; break;
        }

        // Hide-only policy (runs after culling): impostor-covered and
        // beyond-range meshes are hidden here, never re-shown.
        // Skipped when running alongside chunk-file streaming (it owns
        // visibility); level assignment above still drives variants + UI.
        if (allowHiding && (target == LODLevel::Impostor || target == LODLevel::Culled)) {
            if (registry.all_of<Renderable>(e)) {
                registry.get<Renderable>(e).visible = false;
            }
            if (registry.all_of<ECS::RenderableComponent>(e)) {
                registry.get<ECS::RenderableComponent>(e).visible = false;
            }
        }
    }

    return stats;
}

} // namespace Atlas

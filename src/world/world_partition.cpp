#include "world_partition.h"

#include "../scene/scene.h"
#include "../utils/frustum.h"

#include <cmath>
#include <algorithm>

namespace Atlas {

WorldPartition::WorldPartition(Scene* scene) : m_Scene(scene) {}

void WorldPartition::setScene(Scene* scene) {
    if (m_Scene == scene) return;
    m_Scene = scene;
    markDirty();
}

void WorldPartition::markDirty() {
    m_Dirty = true;
}

WorldCellCoord WorldPartition::worldToCell(const glm::vec3& p) const {
    const float cs = (m_Config.cellSize > 1e-3f) ? m_Config.cellSize : 1.0f;
    WorldCellCoord c;
    c.x = static_cast<int>(std::floor(p.x / cs));
    c.z = static_cast<int>(std::floor(p.z / cs));
    return c;
}

void WorldPartition::rebuild() {
    m_Cells.clear();
    m_LoadedCells.clear();

    if (!m_Scene) {
        m_Dirty = false;
        return;
    }

    auto& registry = m_Scene->getRegistry();

    auto view = registry.view<::Mesh>();
    for (auto e : view) {
        if (!registry.valid(e)) continue;
        if (!registry.all_of<Transform>(e)) continue;

        glm::mat4 world = m_Scene->getCachedWorldTransform(e);
        glm::vec3 pos = glm::vec3(world[3]);
        WorldCellCoord cell = worldToCell(pos);
        m_Cells[cell].push_back(e);
    }

    m_Dirty = false;
}

void WorldPartition::setCellVisible(const WorldCellCoord& cell, bool visible) {
    if (!m_Scene) return;

    auto it = m_Cells.find(cell);
    if (it == m_Cells.end()) return;

    auto& registry = m_Scene->getRegistry();
    for (auto e : it->second) {
        if (!registry.valid(e)) continue;
        if (registry.all_of<Renderable>(e)) {
            registry.get<Renderable>(e).visible = visible;
        }
    }
}

void WorldPartition::update(const glm::vec3& cameraPos, const glm::mat4& viewProj) {
    if (!m_Scene) return;
    if (!m_Config.enabled) {
        return;
    }

    if (m_Dirty) {
        rebuild();
    }

    WorldCellCoord camCell = worldToCell(cameraPos);

    std::unordered_set<WorldCellCoord, WorldCellCoordHash> desired;
    const int r = (m_Config.loadRadiusCells < 0) ? 0 : m_Config.loadRadiusCells;

    desired.reserve(static_cast<size_t>((2 * r + 1) * (2 * r + 1)));

    for (int dz = -r; dz <= r; ++dz) {
        for (int dx = -r; dx <= r; ++dx) {
            desired.insert(WorldCellCoord{camCell.x + dx, camCell.z + dz});
        }
    }

    // Activate newly desired cells.
    for (const auto& cell : desired) {
        if (m_LoadedCells.find(cell) != m_LoadedCells.end()) continue;
        setCellVisible(cell, true);
        m_LoadedCells.insert(cell);
    }

    // Deactivate cells that are no longer desired.
    for (auto it = m_LoadedCells.begin(); it != m_LoadedCells.end(); ) {
        if (desired.find(*it) == desired.end()) {
            setCellVisible(*it, false);
            it = m_LoadedCells.erase(it);
        } else {
            ++it;
        }
    }

    if (!m_Config.useFrustumCulling) {
        return;
    }

    // Frustum culling inside loaded cells only.
    Frustum fr = Frustum::fromViewProj(viewProj);

    auto& registry = m_Scene->getRegistry();

    for (const auto& cell : m_LoadedCells) {
        auto cit = m_Cells.find(cell);
        if (cit == m_Cells.end()) continue;

        for (auto e : cit->second) {
            if (!registry.valid(e)) continue;
            if (!registry.all_of<::Mesh, Renderable>(e)) continue;
            auto& rend = registry.get<Renderable>(e);
            if (!rend.visible) continue;

            // If we have bounds, use them, else do a cheap distance test that always passes.
            const auto& mesh = registry.get<::Mesh>(e);

            glm::mat4 world = m_Scene->getCachedWorldTransform(e);
            glm::vec3 worldPos = glm::vec3(world[3]);

            glm::vec3 center = worldPos;
            float radius = 1.0f;

            if (mesh.hasBounds) {
                glm::vec3 centerLocal = (mesh.boundsMin + mesh.boundsMax) * 0.5f;
                glm::vec3 extents = (mesh.boundsMax - mesh.boundsMin) * 0.5f;

                glm::vec3 col0 = glm::vec3(world[0]);
                glm::vec3 col1 = glm::vec3(world[1]);
                glm::vec3 col2 = glm::vec3(world[2]);
                float maxScale = std::max(std::max(glm::length(col0), glm::length(col1)), glm::length(col2));

                center = glm::vec3(world * glm::vec4(centerLocal, 1.0f));
                radius = glm::length(extents) * maxScale;
            }

            if (!fr.testSphere(center, radius)) {
                rend.visible = false;
            }
        }
    }
}

} // namespace Atlas

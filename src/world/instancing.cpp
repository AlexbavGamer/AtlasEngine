#include "instancing.h"

#include "../scene/scene.h"
#include "../renderer/renderer.h"
#include "../utils/frustum.h"

#include <cmath>
#include <algorithm>

namespace Atlas {

InstancingManager::InstancingManager(Renderer* renderer) : m_Renderer(renderer) {}

InstancingManager::~InstancingManager() {
    cleanup();
}

std::shared_ptr<InstancedMesh> InstancingManager::createInstancedMesh(
    uint32_t meshID,
    uint32_t materialID,
    uint32_t initialCapacity
) {
    auto mesh = std::make_shared<InstancedMesh>();
    mesh->meshID = meshID;
    mesh->materialID = materialID;
    mesh->transforms.reserve(initialCapacity);
    mesh->instanceFlags.reserve(initialCapacity);
    return mesh;
}

void InstancingManager::addInstance(
    std::shared_ptr<InstancedMesh> mesh,
    const glm::mat4& transform
) {
    if (!mesh) return;
    mesh->transforms.push_back(transform);
    mesh->instanceCount = static_cast<uint32_t>(mesh->transforms.size());
}

void InstancingManager::removeInstance(
    std::shared_ptr<InstancedMesh> mesh,
    uint32_t index
) {
    if (!mesh || index >= mesh->transforms.size()) return;
    mesh->transforms.erase(mesh->transforms.begin() + index);
    mesh->instanceCount = static_cast<uint32_t>(mesh->transforms.size());
}

void InstancingManager::updateInstance(
    std::shared_ptr<InstancedMesh> mesh,
    uint32_t index,
    const glm::mat4& transform
) {
    if (!mesh || index >= mesh->transforms.size()) return;
    mesh->transforms[index] = transform;
}

std::vector<InstancingBatch> InstancingManager::buildBatches(
    const std::unordered_map<uint32_t, std::shared_ptr<InstancedMesh>>& meshes,
    const glm::mat4& /*viewProj*/,
    float /*cameraDistance*/
) {
    std::vector<InstancingBatch> batches;

    for (const auto& [meshID, mesh] : meshes) {
        (void)meshID;
        if (mesh->instanceCount == 0) continue;

        InstancingBatch batch;
        batch.mesh = mesh;
        batch.instanceCount = mesh->instanceCount;

        batch.drawParams.vertexCountPerInstance = 0;
        batch.drawParams.instanceCount = mesh->instanceCount;
        batch.drawParams.startInstanceLocation = 0;
        batch.drawParams.baseVertexLocation = 0;

        batches.push_back(std::move(batch));
    }

    return batches;
}

void InstancingManager::generateDrawIndirectCommands(
    Renderer* /*renderer*/,
    const std::vector<InstancingBatch>& /*batches*/
) {
}

void InstancingManager::cleanup() {
    if (m_IndirectCommandBuffer) {
        m_IndirectCommandBuffer = 0;
    }
    if (m_InstanceBuffer) {
        m_InstanceBuffer = 0;
    }
    if (m_DrawDescriptorSet) {
        m_DrawDescriptorSet = 0;
    }
}

std::vector<InstancingBatch> generateInstancingBatchesFromCell(
    Scene* scene,
    const WorldCellCoord& cellCoord,
    const InstancingConfig& /*config*/,
    Renderer* /*renderer*/
) {
    std::vector<InstancingBatch> batches;

    if (!scene) return batches;

    auto& registry = scene->getRegistry();

    // Group by materialID from Renderable (legacy) — ECS::MaterialComponent has no numeric ID.
    auto view = registry.view<Renderable, Mesh, Transform>();

    std::unordered_map<uint32_t, std::vector<std::pair<entt::entity, glm::mat4>>> entitiesByMaterial;

    for (auto e : view) {
        if (!registry.valid(e)) continue;

        glm::mat4 world = scene->getCachedWorldTransform(e);
        glm::vec3 pos = glm::vec3(world[3]);

        int cellX = static_cast<int>(std::floor(pos.x / 128.0f));
        int cellZ = static_cast<int>(std::floor(pos.z / 128.0f));

        if (cellX == cellCoord.x && cellZ == cellCoord.z) {
            uint32_t matID = 0;
            if (registry.all_of<Renderable>(e)) {
                matID = registry.get<Renderable>(e).materialID;
            }
            entitiesByMaterial[matID].emplace_back(e, world);
        }
    }

    for (auto& [matID, entities] : entitiesByMaterial) {
        auto mesh = std::make_shared<InstancedMesh>();
        mesh->meshID = 0;
        mesh->materialID = matID;
        mesh->transforms.reserve(entities.size());
        for (auto& [entity, transform] : entities) {
            (void)entity;
            mesh->transforms.push_back(transform);
        }
        mesh->instanceCount = static_cast<uint32_t>(mesh->transforms.size());

        InstancingBatch batch;
        batch.mesh = mesh;
        batch.instanceCount = mesh->instanceCount;
        batch.drawParams.vertexCountPerInstance = 0;
        batch.drawParams.instanceCount = mesh->instanceCount;
        batch.drawParams.startInstanceLocation = 0;
        batch.drawParams.baseVertexLocation = 0;
        batches.push_back(std::move(batch));
    }

    return batches;
}

void updateInstancingTransforms(
    Scene* /*scene*/,
    const WorldPartition& /*worldPartition*/,
    float /*deltaTime*/
) {
}

float computeScreenSizeForInstancing(
    const glm::vec3& entityCenter,
    const glm::vec3& /*entityExtents*/,
    const glm::mat4& viewProj
) {
    glm::vec3 cameraPos(
        viewProj[3][0],
        viewProj[3][1],
        viewProj[3][2]
    );

    float dist = glm::length(entityCenter - cameraPos);
    float screenSize = 1.0f / std::max(dist, 0.1f);
    return screenSize;
}

} // namespace Atlas

#include "city_generator.h"

#include "../scene/scene.h"
#include "../renderer/renderer.h"
#include "../ecs/ecs.h"
#include "../ecs/components/components.h"
#include "../utils/model_loader.h"
#include "../utils/primitive_helpers.h"
#include "../utils/mesh_simplify.h"
#include "lod.h"

#include <algorithm>

namespace Atlas {

namespace {
// Deterministic 0..1 hash (no RNG state, reproducible cities).
float hash01(uint32_t x, uint32_t y, uint32_t seed) {
    uint32_t h = x * 374761393u + y * 668265263u + seed * 1440662683u;
    h = (h ^ (h >> 13)) * 1274126177u;
    h ^= (h >> 16);
    return static_cast<float>(h & 0xFFFFFFu) / static_cast<float>(0x1000000u);
}

// Facade palette (shared across all buildings => few instancing batches).
glm::vec4 facadeColor(int variant) {
    static const glm::vec4 kPalette[] = {
        {0.62f, 0.65f, 0.72f, 1.0f}, // concrete
        {0.72f, 0.68f, 0.62f, 1.0f}, // sandstone
        {0.55f, 0.58f, 0.66f, 1.0f}, // blue glass-ish
        {0.68f, 0.55f, 0.48f, 1.0f}, // brick
        {0.60f, 0.62f, 0.60f, 1.0f}, // sage
        {0.50f, 0.53f, 0.60f, 1.0f}, // slate
    };
    constexpr int kCount = static_cast<int>(sizeof(kPalette) / sizeof(kPalette[0]));
    return kPalette[variant < 0 ? 0 : (variant % kCount)];
}
} // namespace

CityGenResult generateProceduralCity(Scene* scene, Renderer* renderer, const CityGenConfig& config) {
    CityGenResult result;
    if (!scene || !renderer) {
        return result;
    }

    int blocks = std::clamp(config.blocksPerSide, 1, 32);
    const float blockSize = std::max(config.blockSize, 4.0f);
    const float street = std::max(config.streetWidth, 2.0f);
    const float pitch = blockSize + street;
    const float origin = -static_cast<float>(blocks - 1) * pitch * 0.5f;

    // One shared cube mesh for every building (unit cube, scaled per entity).
    MeshData boxData = ModelLoader::createCube(1.0f);
    boxData.name = "CityBox";
    if (boxData.vertexBuffer == VK_NULL_HANDLE || boxData.indexBuffer == VK_NULL_HANDLE) {
        ModelLoader::createBuffers(boxData, renderer->getDevice(), renderer->getPhysicalDevice(),
                                   PrimitiveHelpers::findMemoryType);
        boxData.freeCPUMemory();
    }
    // One GPU allocation shared by every box entity below → single handle,
    // freed by the first (owning) entity's on-destroy path.
    const Atlas::MeshHandle sharedMeshHandle = (boxData.vertexBuffer != VK_NULL_HANDLE)
        ? renderer->getMeshRegistry().allocateMesh()
        : Atlas::kInvalidMeshHandle;
    const glm::vec3 boxMin(-0.5f), boxMax(0.5f);

    auto& registry = scene->getRegistry();
    result.entities.reserve(static_cast<size_t>(blocks) * blocks * 4 + 1);

    auto addBox = [&](const std::string& name, const glm::vec3& pos, const glm::vec3& scale,
                      const glm::vec4& color, float roughness) {
        auto entity = scene->createEntity(name);
        if (registry.all_of<Transform>(entity)) {
            auto& t = registry.get<Transform>(entity);
            t.position = pos;
            t.scale = scale;
        }
        auto& mesh = registry.emplace<::Mesh>(entity);
        mesh.meshPath = "primitive://CityBox";
        mesh.vertexBuffer = boxData.vertexBuffer;
        mesh.indexBuffer = boxData.indexBuffer;
        mesh.vertexMemory = boxData.vertexMemory;
        mesh.indexMemory = boxData.indexMemory;
        mesh.renderMeshId = sharedMeshHandle;
        mesh.vertexCount = boxData.vertexCount;
        mesh.indexCount = boxData.indexCount;
        mesh.hasBounds = true;
        mesh.boundsMin = boxMin;
        mesh.boundsMax = boxMax;
        mesh.ownsGpuResources = result.entities.empty(); // first owns
        // Auto-LOD variants for the shared box mesh (once: first entity).
        // Cubes are tiny so the simplifier declines them; harmless uniform call.
        if (result.entities.empty()) {
            cacheImportLODs(renderer, boxData, reinterpret_cast<uint64_t>(mesh.vertexBuffer),
                              reinterpret_cast<uint64_t>(mesh.indexBuffer), false);
        }
        ECS::MaterialComponent material;
        material.baseColor = color;
        material.roughness = roughness;
        material.metallic = 0.0f;
        registry.emplace_or_replace<ECS::MaterialComponent>(entity, material);
        registry.emplace_or_replace<LODComponent>(entity, LODComponent{});
        result.entities.push_back(entity);
        return entity;
    };

    // Ground plane (shared quad via thin box, dark asphalt).
    if (config.addGround) {
        const float citySpan = blocks * pitch + street;
        addBox("CityGround", glm::vec3(0.0f, -0.55f, 0.0f), glm::vec3(citySpan, 0.1f, citySpan),
               glm::vec4(0.16f, 0.17f, 0.19f, 1.0f), 0.95f);
    }

    // Blocks -> 2x2 lots -> one building each.
    for (int bz = 0; bz < blocks; ++bz) {
        for (int bx = 0; bx < blocks; ++bx) {
            result.blockCount++;
            const float baseX = origin + bx * pitch;
            const float baseZ = origin + bz * pitch;
            for (int lz = 0; lz < 2; ++lz) {
                for (int lx = 0; lx < 2; ++lx) {
                    const float lot = blockSize * 0.5f;
                    const float cx = baseX + (lx - 0.5f) * lot;
                    const float cz = baseZ + (lz - 0.5f) * lot;
                    const float h01 = hash01(static_cast<uint32_t>(bx * 2 + lx),
                                             static_cast<uint32_t>(bz * 2 + lz), config.seed);
                    const float w01 = hash01(static_cast<uint32_t>(bz * 2 + lz) + 101u,
                                             static_cast<uint32_t>(bx * 2 + lx) + 57u, config.seed);
                    const float footprint = lot * (0.55f + 0.3f * w01);
                    const float height = config.minHeight + h01 * (config.maxHeight - config.minHeight);
                    const int variant = static_cast<int>(h01 * 6.0f + w01 * 3.0f);
                    // Quantized roughness: shared materials batch (§6); geometry
                    // variety still comes from per-instance footprint/height.
                    const float roughness = 0.7f + 0.1f * static_cast<float>(variant % 3);
                    addBox("Building", glm::vec3(cx, height * 0.5f, cz),
                           glm::vec3(footprint, height, footprint),
                           facadeColor(variant), roughness);
                    result.buildingCount++;
                }
            }
        }
    }

    // Buffers now owned by the first entity; forget local handles.
    boxData.vertexBuffer = VK_NULL_HANDLE;
    boxData.indexBuffer = VK_NULL_HANDLE;
    boxData.vertexMemory = VK_NULL_HANDLE;
    boxData.indexMemory = VK_NULL_HANDLE;
    boxData.ownerDevice = VK_NULL_HANDLE;
    return result;
}

void cacheImportLODs(Renderer* renderer, MeshData& meshData,
                     uint64_t srcVB, uint64_t srcIB, bool isSkinned) {
    if (!renderer || isSkinned) {
        return;
    }
    if (meshData.vertices.empty() || meshData.indices.empty()) {
        return;
    }
    // Bit-cast the opaque keys back to handles at the renderer boundary.
    const VkBuffer srcVBHandle = reinterpret_cast<VkBuffer>(srcVB);
    const VkBuffer srcIBHandle = reinterpret_cast<VkBuffer>(srcIB);
    // LOD1 (~50%) then LOD2 (~25%). simplifyMeshData declines tiny meshes.
    SimplifiedMesh lod1 = simplifyMeshData(meshData.vertices, meshData.indices, 0.5f);
    if (lod1.valid) {
        Renderer::StaticMeshBuffers up = renderer->uploadStaticMesh(
            lod1.vertices.data(), sizeof(Vertex), lod1.vertices.size(),
            lod1.indices.data(), lod1.indices.size());
        if (up.vertexBuffer != VK_NULL_HANDLE) {
            renderer->cacheSimplifiedVariant(srcVBHandle, srcIBHandle, 1, up);
        }
    }
    SimplifiedMesh lod2 = simplifyMeshData(meshData.vertices, meshData.indices, 0.25f);
    if (lod2.valid) {
        Renderer::StaticMeshBuffers up = renderer->uploadStaticMesh(
            lod2.vertices.data(), sizeof(Vertex), lod2.vertices.size(),
            lod2.indices.data(), lod2.indices.size());
        if (up.vertexBuffer != VK_NULL_HANDLE) {
            renderer->cacheSimplifiedVariant(srcVBHandle, srcIBHandle, 2, up);
        }
    }
}

void clearGeneratedCity(Scene* scene, CityGenResult& city) {
    if (!scene || city.entities.empty()) {
        city.entities.clear();
        city.buildingCount = 0;
        city.blockCount = 0;
        return;
    }
    auto& registry = scene->getRegistry();
    // Non-owners first so shared GPU buffers are freed exactly once.
    for (size_t i = 1; i < city.entities.size(); ++i) {
        Entity e = city.entities[i];
        if (e != entt::null && registry.valid(e)) {
            scene->destroyEntity(e);
        }
    }
    Entity owner = city.entities.front();
    if (owner != entt::null && registry.valid(owner)) {
        scene->destroyEntity(owner);
    }
    city.entities.clear();
    city.buildingCount = 0;
    city.blockCount = 0;
}

} // namespace Atlas

#include "runtime_scene_loader.h"

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <unordered_map>

#include <glm/gtc/constants.hpp>

#include "../assets/asset_manager.h"
#include "../core/string/string_id.h"
#include "../ecs/components/components.h"
#include "../renderer/renderer.h"
#include "../scene/scene.h"
#include "../scene/scene_serializer.h"
#include "../utils/model_loader.h"

namespace Atlas::Runtime {
namespace {

uint32_t runtimeFindMemoryType(uint32_t typeFilter, VkMemoryPropertyFlags properties, VkPhysicalDeviceMemoryProperties* memProperties) {
    for (uint32_t i = 0; i < memProperties->memoryTypeCount; ++i) {
        if ((typeFilter & (1u << i)) && (memProperties->memoryTypes[i].propertyFlags & properties) == properties) {
            return i;
        }
    }
    return uint32_t(~0);
}

MeshData createPlanePrimitive(float size = 1.0f) {
    MeshData mesh;
    mesh.name = "Plane";
    const float h = size * 0.5f;
    mesh.vertices = {
        {{-h, 0.0f, -h}, {1.0f, 1.0f, 1.0f}, {0.0f, 0.0f}, {0.0f, 1.0f, 0.0f}},
        {{ h, 0.0f, -h}, {1.0f, 1.0f, 1.0f}, {1.0f, 0.0f}, {0.0f, 1.0f, 0.0f}},
        {{ h, 0.0f,  h}, {1.0f, 1.0f, 1.0f}, {1.0f, 1.0f}, {0.0f, 1.0f, 0.0f}},
        {{-h, 0.0f,  h}, {1.0f, 1.0f, 1.0f}, {0.0f, 1.0f}, {0.0f, 1.0f, 0.0f}},
    };
    mesh.indices = {0, 1, 2, 2, 3, 0};
    mesh.vertexCount = static_cast<uint32_t>(mesh.vertices.size());
    mesh.indexCount = static_cast<uint32_t>(mesh.indices.size());
    return mesh;
}

MeshData createSpherePrimitive(float radius = 0.5f, int segments = 24, int rings = 16) {
    MeshData mesh;
    mesh.name = "Sphere";
    for (int y = 0; y <= rings; ++y) {
        const float v = static_cast<float>(y) / static_cast<float>(rings);
        const float phi = v * glm::pi<float>();
        for (int x = 0; x <= segments; ++x) {
            const float u = static_cast<float>(x) / static_cast<float>(segments);
            const float theta = u * glm::two_pi<float>();
            glm::vec3 n(
                std::sin(phi) * std::cos(theta),
                std::cos(phi),
                std::sin(phi) * std::sin(theta)
            );
            Vertex vert{};
            vert.pos = n * radius;
            vert.normal = glm::normalize(n);
            vert.texCoord = glm::vec2(u, v);
            vert.color = glm::vec3(1.0f);
            mesh.vertices.push_back(vert);
        }
    }
    const int stride = segments + 1;
    for (int y = 0; y < rings; ++y) {
        for (int x = 0; x < segments; ++x) {
            const uint32_t i0 = static_cast<uint32_t>(y * stride + x);
            const uint32_t i1 = i0 + 1;
            const uint32_t i2 = i0 + static_cast<uint32_t>(stride);
            const uint32_t i3 = i2 + 1;
            mesh.indices.insert(mesh.indices.end(), {i0, i2, i1, i1, i2, i3});
        }
    }
    mesh.vertexCount = static_cast<uint32_t>(mesh.vertices.size());
    mesh.indexCount = static_cast<uint32_t>(mesh.indices.size());
    return mesh;
}

MeshData createCylinderPrimitive(float radius = 0.5f, float height = 1.0f, int segments = 24) {
    MeshData mesh;
    mesh.name = "Cylinder";
    const float halfH = height * 0.5f;

    for (int i = 0; i <= segments; ++i) {
        const float u = static_cast<float>(i) / static_cast<float>(segments);
        const float theta = u * glm::two_pi<float>();
        const float c = std::cos(theta);
        const float s = std::sin(theta);
        glm::vec3 normal(c, 0.0f, s);

        Vertex vb{};
        vb.pos = glm::vec3(c * radius, -halfH, s * radius);
        vb.normal = normal;
        vb.texCoord = glm::vec2(u, 1.0f);
        vb.color = glm::vec3(1.0f);
        mesh.vertices.push_back(vb);

        Vertex vt{};
        vt.pos = glm::vec3(c * radius, halfH, s * radius);
        vt.normal = normal;
        vt.texCoord = glm::vec2(u, 0.0f);
        vt.color = glm::vec3(1.0f);
        mesh.vertices.push_back(vt);
    }

    for (int i = 0; i < segments; ++i) {
        const uint32_t b0 = static_cast<uint32_t>(i * 2);
        const uint32_t t0 = b0 + 1;
        const uint32_t b1 = b0 + 2;
        const uint32_t t1 = b0 + 3;
        mesh.indices.insert(mesh.indices.end(), {b0, t0, b1, b1, t0, t1});
    }

    const uint32_t bottomCenter = static_cast<uint32_t>(mesh.vertices.size());
    Vertex cb{};
    cb.pos = glm::vec3(0.0f, -halfH, 0.0f);
    cb.normal = glm::vec3(0.0f, -1.0f, 0.0f);
    cb.texCoord = glm::vec2(0.5f);
    cb.color = glm::vec3(1.0f);
    mesh.vertices.push_back(cb);

    const uint32_t topCenter = static_cast<uint32_t>(mesh.vertices.size());
    Vertex ct{};
    ct.pos = glm::vec3(0.0f, halfH, 0.0f);
    ct.normal = glm::vec3(0.0f, 1.0f, 0.0f);
    ct.texCoord = glm::vec2(0.5f);
    ct.color = glm::vec3(1.0f);
    mesh.vertices.push_back(ct);

    const uint32_t bottomStart = static_cast<uint32_t>(mesh.vertices.size());
    for (int i = 0; i <= segments; ++i) {
        const float u = static_cast<float>(i) / static_cast<float>(segments);
        const float theta = u * glm::two_pi<float>();
        const float c = std::cos(theta);
        const float s = std::sin(theta);
        Vertex v{};
        v.pos = glm::vec3(c * radius, -halfH, s * radius);
        v.normal = glm::vec3(0.0f, -1.0f, 0.0f);
        v.texCoord = glm::vec2(c * 0.5f + 0.5f, s * 0.5f + 0.5f);
        v.color = glm::vec3(1.0f);
        mesh.vertices.push_back(v);
    }

    const uint32_t topStart = static_cast<uint32_t>(mesh.vertices.size());
    for (int i = 0; i <= segments; ++i) {
        const float u = static_cast<float>(i) / static_cast<float>(segments);
        const float theta = u * glm::two_pi<float>();
        const float c = std::cos(theta);
        const float s = std::sin(theta);
        Vertex v{};
        v.pos = glm::vec3(c * radius, halfH, s * radius);
        v.normal = glm::vec3(0.0f, 1.0f, 0.0f);
        v.texCoord = glm::vec2(c * 0.5f + 0.5f, s * 0.5f + 0.5f);
        v.color = glm::vec3(1.0f);
        mesh.vertices.push_back(v);
    }

    for (int i = 0; i < segments; ++i) {
        mesh.indices.insert(mesh.indices.end(), {bottomCenter, bottomStart + static_cast<uint32_t>(i + 1), bottomStart + static_cast<uint32_t>(i)});
        mesh.indices.insert(mesh.indices.end(), {topCenter, topStart + static_cast<uint32_t>(i), topStart + static_cast<uint32_t>(i + 1)});
    }

    mesh.vertexCount = static_cast<uint32_t>(mesh.vertices.size());
    mesh.indexCount = static_cast<uint32_t>(mesh.indices.size());
    return mesh;
}

MeshData createCapsulePrimitive(float radius = 0.5f, float height = 2.0f, int segments = 24, int hemiRings = 8) {
    MeshData mesh;
    mesh.name = "Capsule";
    const float bodyHalf = std::max(0.0f, height * 0.5f - radius);

    for (int y = 0; y <= hemiRings * 2 + 1; ++y) {
        const float t = static_cast<float>(y) / static_cast<float>(hemiRings * 2 + 1);
        const float phi = t * glm::pi<float>();
        float yPos = std::cos(phi) * radius;
        const float ringRadius = std::sin(phi) * radius;
        if (yPos > 0.0f) {
            yPos += bodyHalf;
        } else {
            yPos -= bodyHalf;
        }
        for (int x = 0; x <= segments; ++x) {
            const float u = static_cast<float>(x) / static_cast<float>(segments);
            const float theta = u * glm::two_pi<float>();
            const float c = std::cos(theta);
            const float s = std::sin(theta);
            glm::vec3 pos(ringRadius * c, yPos, ringRadius * s);
            glm::vec3 sphereCenter(0.0f, yPos > 0.0f ? bodyHalf : -bodyHalf, 0.0f);
            Vertex v{};
            v.pos = pos;
            v.normal = glm::normalize(pos - sphereCenter);
            v.texCoord = glm::vec2(u, t);
            v.color = glm::vec3(1.0f);
            mesh.vertices.push_back(v);
        }
    }

    const int stride = segments + 1;
    const int rows = hemiRings * 2 + 1;
    for (int y = 0; y < rows; ++y) {
        for (int x = 0; x < segments; ++x) {
            const uint32_t i0 = static_cast<uint32_t>(y * stride + x);
            const uint32_t i1 = i0 + 1;
            const uint32_t i2 = i0 + static_cast<uint32_t>(stride);
            const uint32_t i3 = i2 + 1;
            mesh.indices.insert(mesh.indices.end(), {i0, i2, i1, i1, i2, i3});
        }
    }

    mesh.vertexCount = static_cast<uint32_t>(mesh.vertices.size());
    mesh.indexCount = static_cast<uint32_t>(mesh.indices.size());
    return mesh;
}

MeshData createPrimitiveMeshData(const std::string& primitiveType) {
    if (primitiveType == "Cube") {
        MeshData mesh = ModelLoader::createCube(1.0f);
        mesh.name = "Cube";
        return mesh;
    }
    if (primitiveType == "Plane") {
        return createPlanePrimitive(2.0f);
    }
    if (primitiveType == "Sphere") {
        return createSpherePrimitive(0.5f);
    }
    if (primitiveType == "Cylinder") {
        return createCylinderPrimitive(0.5f, 1.5f);
    }
    if (primitiveType == "Capsule") {
        return createCapsulePrimitive(0.45f, 1.8f);
    }
    return MeshData();
}

void applyPrimitiveMesh(Scene& scene, Renderer& renderer, entt::entity entity, const std::string& primitiveType) {
    MeshData meshData = createPrimitiveMeshData(primitiveType);
    if (meshData.vertices.empty() || meshData.indices.empty()) {
        return;
    }

    glm::vec3 boundsMin = meshData.vertices[0].pos;
    glm::vec3 boundsMax = meshData.vertices[0].pos;
    for (const auto& v : meshData.vertices) {
        boundsMin = glm::min(boundsMin, v.pos);
        boundsMax = glm::max(boundsMax, v.pos);
    }

    ModelLoader::createBuffers(meshData, renderer.getDevice(), renderer.getPhysicalDevice(), runtimeFindMemoryType);
    meshData.freeCPUMemory();

    auto& mesh = scene.getRegistry().emplace_or_replace<::Mesh>(entity);
    mesh.meshPath = "primitive://" + primitiveType;
    mesh.vertexBuffer = meshData.vertexBuffer;
    mesh.indexBuffer = meshData.indexBuffer;
    mesh.vertexMemory = meshData.vertexMemory;
    mesh.indexMemory = meshData.indexMemory;
    mesh.vertexCount = meshData.vertexCount;
    mesh.indexCount = meshData.indexCount;
    mesh.hasBounds = true;
    mesh.boundsMin = boundsMin;
    mesh.boundsMax = boundsMax;

    meshData.vertexBuffer = VK_NULL_HANDLE;
    meshData.indexBuffer = VK_NULL_HANDLE;
    meshData.vertexMemory = VK_NULL_HANDLE;
    meshData.indexMemory = VK_NULL_HANDLE;
    meshData.ownerDevice = VK_NULL_HANDLE;
}

std::string resolveAssetPath(const std::string& path, const std::string& assetsRoot) {
    if (path.empty()) {
        return std::string();
    }
    std::filesystem::path p(path);
    if (p.is_absolute()) {
        return p.lexically_normal().string();
    }
    return (std::filesystem::path(assetsRoot) / p).lexically_normal().string();
}

void bindMaterialTexture(AssetManager& assetManager,
                         Renderer& renderer,
                         std::unordered_map<std::string, uint32_t>& textureSlots,
                         const std::string& assetsRoot,
                         const std::string& inputPath,
                         AssetManager::TextureColorSpace colorSpace,
                         bool useFlag,
                         int32_t& outIndex,
                         StringID& outId,
                         std::string& outPath) {
    outIndex = -1;
    outId = StringID();
    outPath.clear();
    if (!useFlag || inputPath.empty()) {
        return;
    }

    const std::string fullPath = resolveAssetPath(inputPath, assetsRoot);
    if (fullPath.empty() || !std::filesystem::exists(fullPath)) {
        return;
    }

    auto it = textureSlots.find(fullPath);
    uint32_t slot = 0;
    if (it != textureSlots.end()) {
        slot = it->second;
    } else {
        auto tex = assetManager.loadTexture(StringID(fullPath), fullPath, colorSpace);
        if (tex && tex->isValid()) {
            slot = renderer.bindTexture(tex->getImageView(), tex->getSampler());
            if (slot != 0) {
                textureSlots[fullPath] = slot;
            }
        }
    }

    if (slot != 0) {
        outIndex = static_cast<int32_t>(slot);
        outId = StringID(fullPath);
        outPath = inputPath;
    }
}

} // namespace

bool loadRuntimeSceneFromFile(Scene& scene,
                              Renderer& renderer,
                              AssetManager& assetManager,
                              const std::string& scenePath,
                              const std::string& assetsRoot) {
    SerializedScene data;
    if (!SceneSerializer::loadFromFile(scenePath, data)) {
        return false;
    }

    auto& registry = scene.getRegistry();
    std::unordered_map<uint32_t, entt::entity> entityMap;
    entityMap.reserve(data.entities.size());
    std::unordered_map<std::string, uint32_t> textureSlots;

    for (const auto& src : data.entities) {
        entt::entity entity = scene.createEntity(src.name.empty() ? "Entity" : src.name);
        entityMap[src.id] = entity;

        registry.emplace_or_replace<ECS::TagComponent>(entity, src.name.empty() ? "Entity" : src.name);
        if (src.hasTransform) {
            registry.emplace_or_replace<Transform>(entity, src.transform);
        }
        if (src.hasRenderable) {
            registry.emplace_or_replace<Renderable>(entity, src.renderable);
        }
        if (src.hasCamera) {
            registry.emplace_or_replace<Camera>(entity, src.camera);
        }
        if (src.hasGameCamera) {
            ECS::GameCameraComponent gameCamera;
            gameCamera.primary = src.gameCameraPrimary;
            registry.emplace_or_replace<ECS::GameCameraComponent>(entity, gameCamera);
        }
        if (src.hidden) {
            registry.emplace_or_replace<ECS::EditorHiddenComponent>(entity, ECS::EditorHiddenComponent{true});
        }
        if (src.hasRigidBody) {
            registry.emplace_or_replace<ECS::RigidBodyComponent>(entity, src.rigidBody);
        }
        if (src.hasBoxCollider) {
            registry.emplace_or_replace<ECS::BoxColliderComponent>(entity, src.boxCollider);
        }
        if (src.hasSphereCollider) {
            registry.emplace_or_replace<ECS::SphereColliderComponent>(entity, src.sphereCollider);
        }
        if (src.hasCapsuleCollider) {
            registry.emplace_or_replace<ECS::CapsuleColliderComponent>(entity, src.capsuleCollider);
        }
        if (src.hasMaterial) {
            ECS::MaterialComponent material;
            material.baseColor = src.material.baseColor;
            material.metallic = src.material.metallic;
            material.roughness = src.material.roughness;
            material.ambientOcclusion = src.material.ambientOcclusion;
            material.emissiveFactor = src.material.emissiveFactor;
            material.alphaMode = static_cast<ECS::MaterialComponent::AlphaMode>(src.material.alphaMode);
            material.alphaCutoff = src.material.alphaCutoff;
            material.doubleSided = src.material.doubleSided;
            material.invertCulling = src.material.invertCulling;
            material.useAlbedoTexture = src.material.useAlbedoTexture;
            material.albedoTexturePath = src.material.albedoTexturePath;
            material.useNormalTexture = src.material.useNormalTexture;
            material.normalTexturePath = src.material.normalTexturePath;
            material.useMetallicRoughnessTexture = src.material.useMetallicRoughnessTexture;
            material.metallicRoughnessTexturePath = src.material.metallicRoughnessTexturePath;
            material.useAOTexture = src.material.useAOTexture;
            material.aoTexturePath = src.material.aoTexturePath;
            material.useEmissiveTexture = src.material.useEmissiveTexture;
            material.emissiveTexturePath = src.material.emissiveTexturePath;

            bindMaterialTexture(assetManager, renderer, textureSlots, assetsRoot, material.albedoTexturePath,
                AssetManager::TextureColorSpace::SRGB, material.useAlbedoTexture,
                material.albedoTextureIndex, material.albedoTextureId, material.albedoTexturePath);
            bindMaterialTexture(assetManager, renderer, textureSlots, assetsRoot, material.normalTexturePath,
                AssetManager::TextureColorSpace::Linear, material.useNormalTexture,
                material.normalTextureIndex, material.normalTextureId, material.normalTexturePath);
            bindMaterialTexture(assetManager, renderer, textureSlots, assetsRoot, material.metallicRoughnessTexturePath,
                AssetManager::TextureColorSpace::Linear, material.useMetallicRoughnessTexture,
                material.metallicRoughnessTextureIndex, material.metallicRoughnessTextureId, material.metallicRoughnessTexturePath);
            bindMaterialTexture(assetManager, renderer, textureSlots, assetsRoot, material.aoTexturePath,
                AssetManager::TextureColorSpace::Linear, material.useAOTexture,
                material.aoTextureIndex, material.aoTextureId, material.aoTexturePath);
            bindMaterialTexture(assetManager, renderer, textureSlots, assetsRoot, material.emissiveTexturePath,
                AssetManager::TextureColorSpace::SRGB, material.useEmissiveTexture,
                material.emissiveTextureIndex, material.emissiveTextureId, material.emissiveTexturePath);
            registry.emplace_or_replace<ECS::MaterialComponent>(entity, material);
        }
        if (!src.scripts.empty()) {
            ECS::ScriptComponent scripts;
            scripts.scripts.reserve(src.scripts.size());
            for (const auto& srcScript : src.scripts) {
                ECS::ScriptEntry entry;
                entry.enabled = srcScript.enabled;
                entry.scriptPath = srcScript.scriptPath;
                for (const auto& field : srcScript.fields) {
                    entry.fields[field.name] = field.value;
                }
                scripts.scripts.push_back(std::move(entry));
            }
            registry.emplace_or_replace<ECS::ScriptComponent>(entity, std::move(scripts));
        }

        if (!src.primitiveType.empty()) {
            applyPrimitiveMesh(scene, renderer, entity, src.primitiveType);
        }
    }

    for (const auto& src : data.entities) {
        auto it = entityMap.find(src.id);
        if (it == entityMap.end()) {
            continue;
        }
        entt::entity entity = it->second;

        if (src.parentId > 0) {
            auto parentIt = entityMap.find(static_cast<uint32_t>(src.parentId));
            if (parentIt != entityMap.end()) {
                scene.setParent(entity, parentIt->second);
            }
        }

        if (src.hasFollowCamera) {
            ECS::FollowCameraComponent follow;
            follow.offset = src.followOffset;
            follow.smoothness = src.followSmoothness;
            follow.lookAtTarget = src.followLookAtTarget;
            if (src.followTargetId > 0) {
                auto targetIt = entityMap.find(static_cast<uint32_t>(src.followTargetId));
                if (targetIt != entityMap.end()) {
                    follow.target = targetIt->second;
                }
            }
            registry.emplace_or_replace<ECS::FollowCameraComponent>(entity, follow);
        }
    }

    scene.setName(data.name.empty() ? "Runtime Scene" : data.name);
    scene.updateWorldTransforms();
    scene.setDirty(false);
    return true;
}

} // namespace Atlas::Runtime

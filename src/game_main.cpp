// Game main — entry point for the standalone game executable.
// Created by the AtlasEngine editor's "Build Game" action.
// Expects a `package.manifest` + `.scene` file next to the exe.

#include <chrono>
#include <cstdlib>
#include <exception>
#include <filesystem>
#include <iostream>
#include <string>
#include <unordered_map>
#include <vector>

#ifdef _WIN32
#include <windows.h>
#endif

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

#include <entt/entt.hpp>

#include "scene/scene.h"
#include "scene/scene_serializer.h"
#include "assets/asset_manager.h"
#include "export/package_manifest.h"
#include "physics/physics_system.h"
#include "platform/window.h"
#include "renderer/renderer.h"
#include "scripting/script_engine.h"
#include "ecs/ecs.h"
#include "ecs/components/components.h"
#include "utils/model_loader.h"
#include "utils/primitive_helpers.h"

namespace {

constexpr int kWindowWidth = 1600;
constexpr int kWindowHeight = 900;
constexpr const char* kWindowTitle = "Atlas Game";

std::string getExecutableDir() {
#ifdef _WIN32
    char buf[MAX_PATH] = {};
    DWORD len = GetModuleFileNameA(nullptr, buf, MAX_PATH);
    if (len == 0 || len == MAX_PATH) return std::filesystem::current_path().string();
    return std::filesystem::path(std::string(buf, len)).parent_path().string();
#else
    std::error_code ec;
    auto exe = std::filesystem::read_symlink("/proc/self/exe", ec);
    if (ec) return std::filesystem::current_path().string();
    return exe.parent_path().string();
#endif
}

// Resolve the package root directory.
std::string resolvePackageRoot(int argc, char** argv) {
    if (argc > 1 && argv[1]) {
        return std::filesystem::path(argv[1]).lexically_normal().string();
    }
    return (std::filesystem::path(getExecutableDir()) / "game").lexically_normal().string();
}

// Create a primitive mesh entity (standalone, no editor dependency).
entt::entity createPrimitiveEntity(Atlas::Scene& scene, Atlas::Renderer& renderer, const std::string& type) {
    MeshData meshData;
    std::string name = type;
    if (type == "Cube") {
        meshData = ModelLoader::createCube(1.0f);
        meshData.name = "Cube";
    } else if (type == "Plane") {
        meshData = Atlas::PrimitiveHelpers::createPlane(2.0f);
    } else if (type == "Sphere") {
        meshData = Atlas::PrimitiveHelpers::createSphere(0.5f);
    } else if (type == "Cylinder") {
        meshData = Atlas::PrimitiveHelpers::createCylinder(0.5f, 1.5f);
    } else if (type == "Capsule") {
        meshData = Atlas::PrimitiveHelpers::createCapsule(0.45f, 1.8f);
    } else {
        std::cerr << "[Game] Unknown primitive: " << type << std::endl;
        return entt::null;
    }

    const bool createdMeshBuffers =
        (meshData.vertexBuffer == VK_NULL_HANDLE || meshData.indexBuffer == VK_NULL_HANDLE);
    if (createdMeshBuffers) {
        ModelLoader::createBuffers(meshData, renderer.getDevice(), renderer.getPhysicalDevice(),
                                   Atlas::PrimitiveHelpers::findMemoryType);
        meshData.freeCPUMemory();
    }

    auto entity = scene.createEntity(name);
    auto& registry = scene.getRegistry();

    // Compute bounds
    glm::vec3 boundsMin(0.0f), boundsMax(0.0f);
    bool hasBounds = false;
    if (!meshData.vertices.empty()) {
        boundsMin = boundsMax = meshData.vertices[0].pos;
        for (const auto& v : meshData.vertices) {
            boundsMin = glm::min(boundsMin, v.pos);
            boundsMax = glm::max(boundsMax, v.pos);
        }
        hasBounds = true;
    }

    registry.emplace<Transform>(entity, Transform());

    auto& mesh = registry.emplace<Mesh>(entity);
    mesh.meshPath = "primitive://" + type;
    // Phase 3b: ::Mesh holds no Vk* fields (ecs.h is Vulkan-free). GPU
    // handles transfer straight from MeshData into the registry binding.
    if (createdMeshBuffers) {
        mesh.renderMeshId = renderer.getMeshRegistry().allocateMesh();
        Atlas::MeshBinding binding{};
        binding.vertexBuffer = reinterpret_cast<uint64_t>(meshData.vertexBuffer);
        binding.indexBuffer = reinterpret_cast<uint64_t>(meshData.indexBuffer);
        binding.vertexMemory = reinterpret_cast<uint64_t>(meshData.vertexMemory);
        binding.indexMemory = reinterpret_cast<uint64_t>(meshData.indexMemory);
        binding.vertexCount = meshData.vertexCount;
        binding.indexCount = meshData.indexCount;
        renderer.getMeshRegistry().setMeshData(mesh.renderMeshId, binding);
    }
    mesh.vertexCount = meshData.vertexCount;
    mesh.indexCount = meshData.indexCount;
    mesh.hasBounds = hasBounds;
    mesh.boundsMin = boundsMin;
    mesh.boundsMax = boundsMax;

    // Transfer GPU buffer ownership to the ECS Mesh component.
    meshData.vertexBuffer = VK_NULL_HANDLE;
    meshData.indexBuffer = VK_NULL_HANDLE;
    meshData.vertexMemory = VK_NULL_HANDLE;
    meshData.indexMemory = VK_NULL_HANDLE;
    meshData.ownerDevice = VK_NULL_HANDLE;

    registry.emplace<Renderable>(entity, Renderable{true, 0});

    Atlas::ECS::MaterialComponent& mat = registry.emplace<Atlas::ECS::MaterialComponent>(entity);
    mat.baseColor = glm::vec4(1.0f);
    mat.metallic = 0.0f;
    mat.roughness = 0.5f;
    mat.ambientOcclusion = 1.0f;
    mat.emissiveFactor = glm::vec3(0.0f);

    return entity;
}

// Deserialize a SerializedScene into an existing Scene (game variant:
// primitives are created via createPrimitiveEntity above; no editor-only
// components such as EditorCamera / EditorHidden are applied).
std::unordered_map<uint32_t, entt::entity> applyGameScene(
    Atlas::Scene& scene, Atlas::Renderer& renderer, const Atlas::SerializedScene& data) {

    std::unordered_map<uint32_t, entt::entity> entityMap;
    entityMap.reserve(data.entities.size());

    auto& registry = scene.getRegistry();

    for (const auto& src : data.entities) {
        entt::entity entity = entt::null;

        if (!src.primitiveType.empty()) {
            entity = createPrimitiveEntity(scene, renderer, src.primitiveType);
        } else {
            entity = scene.createEntity(src.name.empty() ? "Entity" : src.name);
        }
        if (entity == entt::null) continue;

        entityMap[src.id] = entity;

        registry.emplace_or_replace<Atlas::ECS::TagComponent>(entity, src.name.empty() ? "Entity" : src.name);
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
            Atlas::ECS::GameCameraComponent gcc;
            gcc.primary = src.gameCameraPrimary;
            registry.emplace_or_replace<Atlas::ECS::GameCameraComponent>(entity, gcc);
        }
        if (src.hasRigidBody) {
            registry.emplace_or_replace<Atlas::ECS::RigidBodyComponent>(entity, src.rigidBody);
        }
        if (src.hasBoxCollider) {
            registry.emplace_or_replace<Atlas::ECS::BoxColliderComponent>(entity, src.boxCollider);
        }
        if (src.hasSphereCollider) {
            registry.emplace_or_replace<Atlas::ECS::SphereColliderComponent>(entity, src.sphereCollider);
        }
        if (src.hasCapsuleCollider) {
            registry.emplace_or_replace<Atlas::ECS::CapsuleColliderComponent>(entity, src.capsuleCollider);
        }
        // Sun/Sky task: procedural sun + sky backdrop (data-only; the
        // renderer resolves the first of each per frame).
        if (src.hasSun) {
            Atlas::ECS::SunComponent sun;
            sun.azimuthDeg = src.sunAzimuthDeg;
            sun.elevationDeg = src.sunElevationDeg;
            sun.color = src.sunColor;
            sun.intensity = src.sunIntensity;
            sun.castShadows = src.sunCastShadows;
            sun.shadowRange = src.sunShadowRange;
            registry.emplace_or_replace<Atlas::ECS::SunComponent>(entity, sun);
        }
        if (src.hasSky) {
            Atlas::ECS::SkyComponent sky;
            sky.enabled = src.skyEnabled;
            sky.horizonColor = src.skyHorizon;
            sky.zenithColor = src.skyZenith;
            sky.groundColor = src.skyGround;
            sky.sunColor = src.skySunColor;
            sky.sunDiskSizeDeg = src.skySunDiskSizeDeg;
            sky.sunGlow = src.skySunGlow;
            registry.emplace_or_replace<Atlas::ECS::SkyComponent>(entity, sky);
        }
        if (src.hasMaterial) {
            Atlas::ECS::MaterialComponent mat;
            mat.baseColor = src.material.baseColor;
            mat.metallic = src.material.metallic;
            mat.roughness = src.material.roughness;
            mat.ambientOcclusion = src.material.ambientOcclusion;
            mat.emissiveFactor = src.material.emissiveFactor;
            mat.alphaMode = static_cast<Atlas::ECS::MaterialComponent::AlphaMode>(src.material.alphaMode);
            mat.alphaCutoff = src.material.alphaCutoff;
            mat.doubleSided = src.material.doubleSided;
            mat.invertCulling = src.material.invertCulling;
            mat.useAlbedoTexture = src.material.useAlbedoTexture;
            mat.albedoTexturePath = src.material.albedoTexturePath;
            mat.useNormalTexture = src.material.useNormalTexture;
            mat.normalTexturePath = src.material.normalTexturePath;
            mat.useMetallicRoughnessTexture = src.material.useMetallicRoughnessTexture;
            mat.metallicRoughnessTexturePath = src.material.metallicRoughnessTexturePath;
            mat.useAOTexture = src.material.useAOTexture;
            mat.aoTexturePath = src.material.aoTexturePath;
            mat.useEmissiveTexture = src.material.useEmissiveTexture;
            mat.emissiveTexturePath = src.material.emissiveTexturePath;
            registry.emplace_or_replace<Atlas::ECS::MaterialComponent>(entity, mat);
        }
        if (!src.scripts.empty()) {
            Atlas::ECS::ScriptComponent sc;
            sc.scripts.reserve(src.scripts.size());
            for (const auto& s : src.scripts) {
                Atlas::ECS::ScriptEntry entry;
                entry.enabled = s.enabled;
                entry.scriptPath = s.scriptPath;
                for (const auto& f : s.fields) {
                    entry.fields[f.name] = f.value;
                }
                sc.scripts.push_back(std::move(entry));
            }
            registry.emplace_or_replace<Atlas::ECS::ScriptComponent>(entity, std::move(sc));
        }
    }

    // Second pass: parent-child relationships.
    for (const auto& src : data.entities) {
        auto it = entityMap.find(src.id);
        if (it == entityMap.end()) continue;
        entt::entity entity = it->second;

        if (src.parentId > 0) {
            auto pit = entityMap.find(static_cast<uint32_t>(src.parentId));
            if (pit != entityMap.end()) {
                scene.setParent(entity, pit->second);
            }
        }

        if (src.hasFollowCamera) {
            Atlas::ECS::FollowCameraComponent follow;
            follow.offset = src.followOffset;
            follow.smoothness = src.followSmoothness;
            follow.lookAtTarget = src.followLookAtTarget;
            if (src.followTargetId > 0) {
                auto tit = entityMap.find(static_cast<uint32_t>(src.followTargetId));
                if (tit != entityMap.end()) follow.target = tit->second;
            }
            registry.emplace_or_replace<Atlas::ECS::FollowCameraComponent>(entity, follow);
        }
    }

    scene.setName(data.name.empty() ? "Untitled" : data.name);
    return entityMap;
}

// ---------------------------------------------------------------------------
// Camera update helpers
// ---------------------------------------------------------------------------

void updateGameCameras(Atlas::Scene& scene) {
    auto& registry = scene.getRegistry();
    auto view = registry.view<Transform, Camera, Atlas::ECS::GameCameraComponent>(entt::exclude<Atlas::ECS::FollowCameraComponent>);
    for (auto entity : view) {
        auto& camera = view.get<Camera>(entity);
        const glm::mat4 world = scene.getWorldTransform(entity);
        const glm::vec3 pos = glm::vec3(world[3]);
        glm::vec3 forward = glm::normalize(glm::vec3(world * glm::vec4(0.0f, 0.0f, -1.0f, 0.0f)));
        glm::vec3 up = glm::normalize(glm::vec3(world * glm::vec4(0.0f, 1.0f, 0.0f, 0.0f)));
        if (glm::length(forward) < 1e-5f) forward = glm::vec3(0.0f, 0.0f, -1.0f);
        if (glm::length(up) < 1e-5f) up = glm::vec3(0.0f, 1.0f, 0.0f);
        camera.position = pos;
        camera.target = pos + forward;
        camera.up = up;
    }
}

void updateFollowCameras(Atlas::Scene& scene, float dt) {
    auto& registry = scene.getRegistry();
    auto view = registry.view<Camera, Atlas::ECS::FollowCameraComponent>();
    for (auto entity : view) {
        auto& camera = view.get<Camera>(entity);
        auto& follow = view.get<Atlas::ECS::FollowCameraComponent>(entity);
        if (follow.target == entt::null || !registry.valid(follow.target) || !registry.all_of<Transform>(follow.target)) continue;
        const glm::mat4 tw = scene.getWorldTransform(follow.target);
        const glm::vec3 targetPos = glm::vec3(tw[3]);
        const glm::vec3 desired = targetPos + follow.offset;
        const float alpha = 1.0f - std::exp(-std::max(0.0f, follow.smoothness) * dt);
        camera.position = glm::mix(camera.position, desired, alpha);
        if (follow.lookAtTarget) camera.target = targetPos;
    }
}

} // anonymous namespace

// ---------------------------------------------------------------------------
// main
// ---------------------------------------------------------------------------

int main(int argc, char** argv) {
    try {
        const std::string packageRoot = resolvePackageRoot(argc, argv);
        const std::filesystem::path manifestPath = std::filesystem::path(packageRoot) / "package.manifest";

        Atlas::Export::PackageManifest manifest;
        if (!Atlas::Export::loadPackageManifest(manifestPath.string(), manifest)) {
            std::cerr << "[Game] Failed to load package manifest: " << manifestPath << std::endl;
            return EXIT_FAILURE;
        }

        const std::string assetsRoot = (std::filesystem::path(packageRoot) / manifest.assetsRoot).lexically_normal().string();
        const std::string scenePath = (std::filesystem::path(packageRoot) / manifest.startupScene).lexically_normal().string();

        Atlas::Window window(kWindowWidth, kWindowHeight, kWindowTitle);
        Atlas::Renderer renderer(&window);
        renderer.init();
        // Use the scene's GameCameraComponent camera, not the editor camera.
        renderer.setPreferGameCamera(true);
        // Render the scene directly to the swapchain (no offscreen + ImGui path).
        renderer.setGameMode(true);

        Atlas::AssetManager assetManager;
        assetManager.setRenderer(&renderer);

        Atlas::Scene scene;

        Atlas::Physics::PhysicsSystem physics;
        physics.initialize();

        Atlas::Scripting::ScriptEngine scriptEngine;
        scriptEngine.setWindow(window.getGLFWWindow());
        scriptEngine.initialize();
        scriptEngine.setAssetsRoot(assetsRoot);
        scriptEngine.setInputEnabled(true);

        Atlas::SerializedScene serializedScene;
        if (!Atlas::SceneSerializer::loadFromFile(scenePath, serializedScene)) {
            std::cerr << "[Game] Failed to load scene: " << scenePath << std::endl;
            return EXIT_FAILURE;
        }
        applyGameScene(scene, renderer, serializedScene);

        physics.rebuild(&scene);
        scriptEngine.instantiateScene(&scene);
        scriptEngine.callStart();

        auto lastFrame = std::chrono::high_resolution_clock::now();
        while (!window.shouldClose()) {
            window.update();
            if (window.isMinimized()) continue;

            const auto now = std::chrono::high_resolution_clock::now();
            const float dt = std::chrono::duration<float>(now - lastFrame).count();
            lastFrame = now;

            updateGameCameras(scene);
            scriptEngine.update(dt);
            physics.step(&scene, dt);
            updateFollowCameras(scene, dt);
            scene.updateWorldTransforms();

            renderer.beginFrame();
            renderer.renderScene(&scene);
            renderer.endFrame();
        }

        scriptEngine.destroyScene();
        scriptEngine.shutdown();
        physics.shutdown();
        assetManager.shutdown();
        renderer.shutdown();

    } catch (const std::exception& e) {
        std::cerr << "[Game] Fatal error: " << e.what() << std::endl;
        return EXIT_FAILURE;
    }
    return EXIT_SUCCESS;
}
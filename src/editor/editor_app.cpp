#include "editor_app.h"

#include <filesystem>
#include <iostream>
#include <chrono>
#include <thread>
#include <unordered_map>
#include <unordered_set>
#include <vector>
#include <cmath>
#include <algorithm>
#include <cctype>

#include <ImGuizmo.h>
#include <imgui.h>

#include "../assets/asset_manager.h"
#include "../core/profiler.h"
#include "../core/threading/async_loader.h"
#include "../imgui/imgui_manager.h"
#include "../platform/window.h"
#include "../project/project_manager.h"
#include "../renderer/renderer.h"
#include "../scene/scene.h"
#include "../scene/scene_serializer.h"
#include "../scripting/script_engine.h"
#include "../ui/ui_manager.h"
#include "../utils/camera_controller.h"
#include "../utils/model_loader.h"
#include "../ecs/ecs.h"
#include "../world/world_partition.h"
#include "../utils/frustum.h"

namespace Atlas {
using namespace ecs;

namespace {
uint32_t primitiveFindMemoryType(uint32_t typeFilter, VkMemoryPropertyFlags properties, VkPhysicalDeviceMemoryProperties* memProperties) {
    for (uint32_t i = 0; i < memProperties->memoryTypeCount; ++i) {
        if ((typeFilter & (1u << i)) && (memProperties->memoryTypes[i].propertyFlags & properties) == properties) {
            return i;
        }
    }
    return uint32_t(~0u);
}

MeshData createPlanePrimitive(float size = 1.0f) {
    MeshData mesh;
    const float h = size * 0.5f;
    mesh.name = "Plane";
    mesh.vertices = {
        Vertex{{-h, 0.0f, -h}, {0.85f, 0.85f, 0.85f}, {0.0f, 0.0f}, {0.0f, 1.0f, 0.0f}},
        Vertex{{ h, 0.0f, -h}, {0.85f, 0.85f, 0.85f}, {1.0f, 0.0f}, {0.0f, 1.0f, 0.0f}},
        Vertex{{ h, 0.0f,  h}, {0.85f, 0.85f, 0.85f}, {1.0f, 1.0f}, {0.0f, 1.0f, 0.0f}},
        Vertex{{-h, 0.0f,  h}, {0.85f, 0.85f, 0.85f}, {0.0f, 1.0f}, {0.0f, 1.0f, 0.0f}},
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

            glm::vec3 normal(
                std::sin(phi) * std::cos(theta),
                std::cos(phi),
                std::sin(phi) * std::sin(theta));
            glm::vec3 pos = normal * radius;
            glm::vec3 color(0.92f, 0.92f, 0.92f);
            mesh.vertices.push_back(Vertex{pos, color, glm::vec2(u, v), glm::normalize(normal)});
        }
    }

    for (int y = 0; y < rings; ++y) {
        for (int x = 0; x < segments; ++x) {
            const uint32_t i0 = static_cast<uint32_t>(y * (segments + 1) + x);
            const uint32_t i1 = i0 + 1;
            const uint32_t i2 = i0 + static_cast<uint32_t>(segments + 1);
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
    const float halfHeight = height * 0.5f;
    const glm::vec3 color(0.9f, 0.9f, 0.92f);

    for (int i = 0; i <= segments; ++i) {
        const float u = static_cast<float>(i) / static_cast<float>(segments);
        const float theta = u * glm::two_pi<float>();
        const float x = std::cos(theta);
        const float z = std::sin(theta);
        const glm::vec3 normal = glm::normalize(glm::vec3(x, 0.0f, z));

        mesh.vertices.push_back(Vertex{{radius * x, -halfHeight, radius * z}, color, {u, 0.0f}, normal});
        mesh.vertices.push_back(Vertex{{radius * x,  halfHeight, radius * z}, color, {u, 1.0f}, normal});
    }

    for (int i = 0; i < segments; ++i) {
        const uint32_t i0 = static_cast<uint32_t>(i * 2);
        const uint32_t i1 = i0 + 1;
        const uint32_t i2 = i0 + 2;
        const uint32_t i3 = i0 + 3;
        mesh.indices.insert(mesh.indices.end(), {i0, i1, i2, i2, i1, i3});
    }

    const uint32_t topCenter = static_cast<uint32_t>(mesh.vertices.size());
    mesh.vertices.push_back(Vertex{{0.0f, halfHeight, 0.0f}, color, {0.5f, 0.5f}, {0.0f, 1.0f, 0.0f}});
    const uint32_t topStart = static_cast<uint32_t>(mesh.vertices.size());
    for (int i = 0; i <= segments; ++i) {
        const float u = static_cast<float>(i) / static_cast<float>(segments);
        const float theta = u * glm::two_pi<float>();
        const float x = std::cos(theta);
        const float z = std::sin(theta);
        mesh.vertices.push_back(Vertex{{radius * x, halfHeight, radius * z}, color, {0.5f + 0.5f * x, 0.5f + 0.5f * z}, {0.0f, 1.0f, 0.0f}});
    }
    for (int i = 0; i < segments; ++i) {
        mesh.indices.insert(mesh.indices.end(), {topCenter, topStart + static_cast<uint32_t>(i + 1), topStart + static_cast<uint32_t>(i)});
    }

    const uint32_t bottomCenter = static_cast<uint32_t>(mesh.vertices.size());
    mesh.vertices.push_back(Vertex{{0.0f, -halfHeight, 0.0f}, color, {0.5f, 0.5f}, {0.0f, -1.0f, 0.0f}});
    const uint32_t bottomStart = static_cast<uint32_t>(mesh.vertices.size());
    for (int i = 0; i <= segments; ++i) {
        const float u = static_cast<float>(i) / static_cast<float>(segments);
        const float theta = u * glm::two_pi<float>();
        const float x = std::cos(theta);
        const float z = std::sin(theta);
        mesh.vertices.push_back(Vertex{{radius * x, -halfHeight, radius * z}, color, {0.5f + 0.5f * x, 0.5f + 0.5f * z}, {0.0f, -1.0f, 0.0f}});
    }
    for (int i = 0; i < segments; ++i) {
        mesh.indices.insert(mesh.indices.end(), {bottomCenter, bottomStart + static_cast<uint32_t>(i), bottomStart + static_cast<uint32_t>(i + 1)});
    }

    mesh.vertexCount = static_cast<uint32_t>(mesh.vertices.size());
    mesh.indexCount = static_cast<uint32_t>(mesh.indices.size());
    return mesh;
}

MeshData createCapsulePrimitive(float radius = 0.5f, float height = 2.0f, int segments = 24, int hemiRings = 8) {
    MeshData mesh;
    mesh.name = "Capsule";
    const glm::vec3 color(0.91f, 0.91f, 0.93f);
    const float cylinderHalf = std::max(0.0f, (height * 0.5f) - radius);
    const uint32_t stride = static_cast<uint32_t>(segments + 1);
    const uint32_t rows = static_cast<uint32_t>(hemiRings * 2 + 2);

    for (uint32_t y = 0; y < rows; ++y) {
        const float t = static_cast<float>(y) / static_cast<float>(rows - 1);
        const float phi = t * glm::pi<float>();
        const float sinPhi = std::sin(phi);
        const float cosPhi = std::cos(phi);
        const float centerOffset = (cosPhi >= 0.0f) ? cylinderHalf : -cylinderHalf;

        for (int x = 0; x <= segments; ++x) {
            const float u = static_cast<float>(x) / static_cast<float>(segments);
            const float theta = u * glm::two_pi<float>();
            const float cosTheta = std::cos(theta);
            const float sinTheta = std::sin(theta);

            glm::vec3 normal(cosTheta * sinPhi, cosPhi, sinTheta * sinPhi);
            glm::vec3 pos = normal * radius;
            pos.y += centerOffset;
            mesh.vertices.push_back(Vertex{pos, color, glm::vec2(u, t), glm::normalize(normal)});
        }
    }

    for (uint32_t y = 0; y < rows - 1; ++y) {
        for (int x = 0; x < segments; ++x) {
            const uint32_t i0 = y * stride + static_cast<uint32_t>(x);
            const uint32_t i1 = i0 + 1;
            const uint32_t i2 = i0 + stride;
            const uint32_t i3 = i2 + 1;
            mesh.indices.insert(mesh.indices.end(), {i0, i2, i1, i1, i2, i3});
        }
    }

    mesh.vertexCount = static_cast<uint32_t>(mesh.vertices.size());
    mesh.indexCount = static_cast<uint32_t>(mesh.indices.size());
    return mesh;
}
} // namespace

uint64_t EditorApp::makeCellKey(int x, int z) {
    const uint64_t ux = static_cast<uint32_t>(x);
    const uint64_t uz = static_cast<uint32_t>(z);
    return (ux << 32) | uz;
}

void EditorApp::decodeCellKey(uint64_t key, int& outX, int& outZ) {
    outX = static_cast<int>(static_cast<uint32_t>(key >> 32));
    outZ = static_cast<int>(static_cast<uint32_t>(key & 0xFFFFFFFFu));
}

EditorApp::EditorApp() {
    m_Window = std::make_unique<Window>(1280, 720, "Atlas Engine");
    m_Renderer = std::make_unique<Renderer>(m_Window.get());
    m_Renderer->init();

    m_AssetManager = std::make_unique<AssetManager>();
    m_AssetManager->setRenderer(m_Renderer.get());

    m_Scene = std::make_unique<Scene>();
    m_Scene->getRegistry().on_destroy<::Mesh>().connect<&EditorApp::onMeshDestroyed>(this);
    m_WorldPartition = std::make_unique<WorldPartition>(m_Scene.get());

    m_ImGuiManager = std::make_unique<::ImGuiManager>();
    m_ImGuiManager->init(
        m_Renderer->getInstance(),
        m_Renderer->getPhysicalDevice(),
        m_Renderer->getDevice(),
        m_Renderer->getGraphicsQueue(),
        m_Renderer->getGraphicsQueueFamily(),
        m_Renderer->getRenderPass(),
        m_Window->getGLFWWindow(),
        m_Renderer->getSwapChainImageCount());

    m_Viewport.setRenderer(m_Renderer.get());
    m_Viewport.refreshTexture();

    m_UIManager = std::make_unique<::UIManager>(m_Scene.get());
    m_UIManager->setWindow(m_Window->getGLFWWindow());
    m_UIManager->setRenderer(m_Renderer.get());
    m_UIManager->setAssetManager(m_AssetManager.get());

    m_ProjectManager = std::make_unique<::ProjectManager>();
    m_UIManager->setProjectManager(m_ProjectManager.get());

    m_ScriptEngine = std::make_unique<Atlas::Scripting::ScriptEngine>();
    m_ScriptEngine->setWindow(m_Window->getGLFWWindow());
    m_ScriptEngine->initialize();

    auto cameraEntity = m_Scene->createEntity("Editor Camera");
    m_Scene->getRegistry().emplace<EditorCamera>(cameraEntity);
    auto& camera = m_Scene->getRegistry().get<EditorCamera>(cameraEntity);
    camera.position = glm::vec3(0.0f, 2.0f, 5.0f);
    camera.target = glm::vec3(0.0f, 0.0f, 0.0f);

    rebindEditorCameraController();
    m_UIManager->setOnNewProject([this]() { loadProjectScene(); });
    m_UIManager->setOnOpenProject([this]() { loadProjectScene(); });
    m_UIManager->setOnSaveProject([this]() { saveProjectScene(); });
    m_UIManager->setOnPlay([this]() { startPlayMode(); });
    m_UIManager->setOnPause([this]() { togglePausePlayMode(); });
    m_UIManager->setOnStop([this]() { stopPlayMode(); });
    m_UIManager->setOnReleaseGameFocus([this]() {
        if (m_ScriptEngine) {
            m_ScriptEngine->setMouseCaptured(false);
        }
    });
    m_UIManager->setOnCreatePrimitive([this](const std::string& primitiveType, Entity parent) { createPrimitiveEntity(primitiveType, parent); });
    m_UIManager->setOnCreateGameCamera([this](Entity parent) { createGameCameraEntity(parent); });

    AsyncLoader::getInstance().init();

    m_UIManager->setOnAssetDropped([this](const std::string& assetPath) { queueModelImport(assetPath); });
    m_Renderer->setResizeCallback([this](int, int) { m_Viewport.refreshTexture(); });
    m_Window->setResizeCallback([this](int, int) { m_Renderer->recreateSwapChain(); });
    m_Renderer->setRenderCallback([this](VkCommandBuffer commandBuffer) { m_ImGuiManager->render(commandBuffer); });

    m_Window->setFileDropCallback([this](const std::vector<std::string>& paths) {
        onExternalFileDrop(paths);
    });
}

EditorApp::~EditorApp() {
    AsyncLoader::getInstance().shutdown();

    // UI owns ImGui-created descriptor sets (e.g. thumbnails). Destroy it while ImGui backend is still alive.
    m_UIManager.reset();

    m_Viewport.releaseTexture();

    if (m_ImGuiManager && m_Renderer) {
        m_ImGuiManager->cleanup(m_Renderer->getDevice());
    }

    if (m_AssetManager) {
        m_AssetManager->shutdown();
    }

    if (m_Renderer) {
        m_Renderer->shutdown();
    }
}

void EditorApp::resetEditorScene() {
    stopPlayMode();

    m_Scene = std::make_unique<Scene>();
    m_Scene->getRegistry().on_destroy<::Mesh>().connect<&EditorApp::onMeshDestroyed>(this);

    if (m_UIManager) {
        m_UIManager->setScene(m_Scene.get());
        m_UIManager->clearSelection();
    }

    if (m_WorldPartition) {
        m_WorldPartition->setScene(m_Scene.get());
        m_WorldPartition->markDirty();
    }

    ensureEditorCamera();
    rebindEditorCameraController();
}

void EditorApp::ensureEditorCamera() {
    if (!m_Scene) {
        return;
    }

    auto& registry = m_Scene->getRegistry();
    auto camView = registry.view<EditorCamera>();
    if (camView.begin() != camView.end()) {
        return;
    }

    auto cameraEntity = m_Scene->createEntity("Editor Camera");
    registry.emplace<EditorCamera>(cameraEntity);
    auto& camera = registry.get<EditorCamera>(cameraEntity);
    camera.position = glm::vec3(0.0f, 2.0f, 5.0f);
    camera.target = glm::vec3(0.0f, 0.0f, 0.0f);
}

void EditorApp::rebindEditorCameraController() {
    if (!m_Scene || !m_Window) {
        return;
    }

    auto& registry = m_Scene->getRegistry();
    auto camView = registry.view<EditorCamera>();
    if (camView.begin() == camView.end()) {
        return;
    }

    auto cameraEntity = *camView.begin();
    auto& camera = registry.get<EditorCamera>(cameraEntity);
    m_CameraController = std::make_unique<::CameraController>(
        m_Window->getGLFWWindow(),
        camera.position,
        camera.target,
        camera.up);
    if (m_UIManager) {
        m_UIManager->setCameraController(m_CameraController.get());
    }
}

bool EditorApp::saveProjectScene() {
    if (!m_ProjectManager || !m_ProjectManager->hasProject() || !m_Scene) {
        return false;
    }

    m_ProjectManager->ensureProjectDirectories();
    const std::string scenePath = m_ProjectManager->getDefaultScenePath();
    if (scenePath.empty()) {
        return false;
    }

    const bool ok = SceneSerializer::saveToFile(*m_Scene, scenePath);
    if (ok) {
        m_Scene->setDirty(false);
        if (m_ProjectManager) {
            m_ProjectManager->invalidateAssetTreeCache();
        }
    }
    return ok;
}

bool EditorApp::loadProjectScene() {
    if (!m_ProjectManager || !m_ProjectManager->hasProject()) {
        return false;
    }

    resetEditorScene();

    const std::string scenePath = m_ProjectManager->getDefaultScenePath();
    SerializedScene data;
    if (!scenePath.empty() && std::filesystem::exists(scenePath) && SceneSerializer::loadFromFile(scenePath, data)) {
        std::unordered_map<uint32_t, entt::entity> entityMap;
        entityMap.reserve(data.entities.size());

        auto& registry = m_Scene->getRegistry();
        for (const auto& src : data.entities) {
            entt::entity entity = entt::null;
            if (!src.primitiveType.empty()) {
                entity = createPrimitiveEntity(src.primitiveType, entt::null);
            } else {
                entity = m_Scene->createEntity(src.name.empty() ? "Entity" : src.name);
            }

            if (entity == entt::null) {
                continue;
            }

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
            } else if (registry.all_of<Camera>(entity) && src.primitiveType.empty()) {
                registry.remove<Camera>(entity);
            }
            if (src.hasGameCamera) {
                ECS::GameCameraComponent gcc;
                gcc.primary = src.gameCameraPrimary;
                registry.emplace_or_replace<ECS::GameCameraComponent>(entity, gcc);
            }
            if (src.hidden) {
                registry.emplace_or_replace<ECS::EditorHiddenComponent>(entity, ECS::EditorHiddenComponent{true});
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
                registry.emplace_or_replace<ECS::MaterialComponent>(entity, material);
            }
            if (!src.scripts.empty()) {
                ECS::ScriptComponent scriptComponent;
                scriptComponent.scripts.reserve(src.scripts.size());
                for (const auto& srcScript : src.scripts) {
                    ECS::ScriptEntry entry;
                    entry.enabled = srcScript.enabled;
                    entry.scriptPath = srcScript.scriptPath;
                    for (const auto& field : srcScript.fields) {
                        entry.fields[field.name] = field.value;
                    }
                    scriptComponent.scripts.push_back(std::move(entry));
                }
                registry.emplace_or_replace<ECS::ScriptComponent>(entity, std::move(scriptComponent));
            }
        }

        for (const auto& src : data.entities) {
            auto it = entityMap.find(src.id);
            if (it == entityMap.end()) {
                continue;
            }
            entt::entity entity = it->second;

            if (src.parentId > 0) {
                auto pit = entityMap.find(static_cast<uint32_t>(src.parentId));
                if (pit != entityMap.end()) {
                    m_Scene->setParent(entity, pit->second);
                }
            }

            if (src.hasFollowCamera) {
                ECS::FollowCameraComponent follow;
                follow.offset = src.followOffset;
                follow.smoothness = src.followSmoothness;
                follow.lookAtTarget = src.followLookAtTarget;
                if (src.followTargetId > 0) {
                    auto tit = entityMap.find(static_cast<uint32_t>(src.followTargetId));
                    if (tit != entityMap.end()) {
                        follow.target = tit->second;
                    }
                }
                registry.emplace_or_replace<ECS::FollowCameraComponent>(entity, follow);
            }
        }

        m_Scene->setName(data.name.empty() ? "Untitled" : data.name);
    }

    ensureEditorCamera();
    rebindEditorCameraController();
    m_Scene->updateWorldTransforms();
    m_Scene->setDirty(false);
    return true;
}

void EditorApp::cloneSceneToRuntime() {
    if (!m_Scene) {
        m_RuntimeScene.reset();
        return;
    }

    m_RuntimeScene = std::make_unique<Scene>();
    m_RuntimeScene->setName(m_Scene->getName() + " [Runtime]");

    auto& src = m_Scene->getRegistry();
    auto& dst = m_RuntimeScene->getRegistry();
    std::unordered_map<entt::entity, entt::entity> remap;
    const auto sourceEntities = m_Scene->getAllEntities();
    remap.reserve(sourceEntities.size());

    for (auto entity : sourceEntities) {
        entt::entity cloned = dst.create();
        remap[entity] = cloned;
    }

    auto copyIfPresent = [&](auto typeTag) {
        using T = decltype(typeTag);
        for (const auto& pair : remap) {
            if (src.all_of<T>(pair.first)) {
                dst.emplace_or_replace<T>(pair.second, src.get<T>(pair.first));
            }
        }
    };

    copyIfPresent(Transform{});
    copyIfPresent(Renderable{});
    copyIfPresent(Camera{});
    copyIfPresent(WorldChunk{});
    copyIfPresent(WorldTransform{});
    copyIfPresent(::Mesh{});
    copyIfPresent(Atlas::ECS::TagComponent{});
    copyIfPresent(Atlas::ECS::MaterialComponent{});
    copyIfPresent(Atlas::ECS::EditorHiddenComponent{});
    copyIfPresent(Atlas::ECS::ScriptComponent{});
    copyIfPresent(Atlas::ECS::FollowCameraComponent{});
    copyIfPresent(Atlas::ECS::GameCameraComponent{});
    copyIfPresent(Atlas::ECS::SkeletonComponent{});
    copyIfPresent(Atlas::ECS::AnimationPlayerComponent{});
    copyIfPresent(Atlas::ECS::BonePoseOverrideComponent{});
    copyIfPresent(Atlas::ECS::SkinnedMeshComponent{});

    for (const auto& pair : remap) {
        if (src.all_of<Atlas::ECS::ParentComponent>(pair.first)) {
            auto parent = src.get<Atlas::ECS::ParentComponent>(pair.first);
            if (parent.parent != entt::null) {
                auto it = remap.find(parent.parent);
                parent.parent = (it != remap.end()) ? it->second : entt::null;
            }
            dst.emplace_or_replace<Atlas::ECS::ParentComponent>(pair.second, parent);
        }

        if (src.all_of<Atlas::ECS::ChildrenComponent>(pair.first)) {
            auto children = src.get<Atlas::ECS::ChildrenComponent>(pair.first);
            for (auto& child : children.children) {
                auto it = remap.find(child);
                child = (it != remap.end()) ? it->second : entt::null;
            }
            children.children.erase(std::remove(children.children.begin(), children.children.end(), entt::null), children.children.end());
            dst.emplace_or_replace<Atlas::ECS::ChildrenComponent>(pair.second, std::move(children));
        }

        if (src.all_of<Atlas::ECS::SkinnedMeshComponent>(pair.first)) {
            auto& smc = dst.get<Atlas::ECS::SkinnedMeshComponent>(pair.second);
            if (smc.skeletonEntity != entt::null) {
                auto it = remap.find(smc.skeletonEntity);
                smc.skeletonEntity = (it != remap.end()) ? it->second : entt::null;
            }
        }

        if (src.all_of<Atlas::ECS::FollowCameraComponent>(pair.first)) {
            auto& fcc = dst.get<Atlas::ECS::FollowCameraComponent>(pair.second);
            if (fcc.target != entt::null) {
                auto it = remap.find(fcc.target);
                fcc.target = (it != remap.end()) ? it->second : entt::null;
            }
        }
    }

    bool hasGameCamera = false;
    entt::entity preferredGameCamera = entt::null;
    auto runtimeGameCameras = dst.view<Camera, Atlas::ECS::GameCameraComponent>();
    for (auto e : runtimeGameCameras) {
        hasGameCamera = true;
        const auto& gcc = runtimeGameCameras.get<Atlas::ECS::GameCameraComponent>(e);
        if (preferredGameCamera == entt::null || gcc.primary) {
            preferredGameCamera = e;
            if (gcc.primary) {
                break;
            }
        }
    }

    if (hasGameCamera) {
        auto runtimeCameras = dst.view<Camera>();
        std::vector<entt::entity> camerasToRemove;
        for (auto e : runtimeCameras) {
            if (e != preferredGameCamera) {
                camerasToRemove.push_back(e);
            }
        }
        for (auto e : camerasToRemove) {
            dst.remove<Camera>(e);
        }
    }

    m_RuntimeScene->updateWorldTransforms();
}

void EditorApp::startPlayMode() {
    const bool focusGameOnPlay = (m_UIManager && m_UIManager->wasViewportFocused());

    cloneSceneToRuntime();
    m_GameModePlaying = (m_RuntimeScene != nullptr);
    m_GameModePaused = false;

    if (m_GameModePlaying && focusGameOnPlay && m_UIManager) {
        m_UIManager->requestFocusGameViewport();
    }

    if (m_GameModePlaying && m_ScriptEngine) {
        if (m_ProjectManager && m_ProjectManager->hasProject()) {
            m_ScriptEngine->setAssetsRoot(m_ProjectManager->getAssetsPath());
        } else {
            m_ScriptEngine->setAssetsRoot(std::string());
        }
        m_ScriptEngine->instantiateScene(m_RuntimeScene.get());
        m_ScriptEngine->callStart();
    }
}

void EditorApp::togglePausePlayMode() {
    if (!m_GameModePlaying) {
        return;
    }
    m_GameModePaused = !m_GameModePaused;
}

void EditorApp::stopPlayMode() {
    if (m_ScriptEngine) {
        m_ScriptEngine->destroyScene();
    }
    m_GameModePaused = false;
    m_GameModePlaying = false;
    m_RuntimeScene.reset();
}

void EditorApp::updateGameCameras(Scene* scene) {
    if (!scene) {
        return;
    }

    auto& registry = scene->getRegistry();
    auto view = registry.view<Transform, Camera, Atlas::ECS::GameCameraComponent>(entt::exclude<Atlas::ECS::FollowCameraComponent>);
    for (auto entity : view) {
        auto& camera = view.get<Camera>(entity);

        glm::mat4 world = scene->getWorldTransform(entity);
        glm::vec3 position = glm::vec3(world[3]);
        glm::vec3 forward = glm::normalize(glm::vec3(world * glm::vec4(0.0f, 0.0f, -1.0f, 0.0f)));
        glm::vec3 up = glm::normalize(glm::vec3(world * glm::vec4(0.0f, 1.0f, 0.0f, 0.0f)));

        if (glm::length(forward) < 1e-5f) {
            forward = glm::vec3(0.0f, 0.0f, -1.0f);
        }
        if (glm::length(up) < 1e-5f) {
            up = glm::vec3(0.0f, 1.0f, 0.0f);
        }

        camera.position = position;
        camera.target = position + forward;
        camera.up = up;
    }
}

Entity EditorApp::createGameCameraEntity(Entity parent) {
    if (!m_Scene) {
        return entt::null;
    }

    auto& registry = m_Scene->getRegistry();
    auto entity = m_Scene->createEntity("Game Camera");

    if (parent != entt::null && registry.valid(parent)) {
        m_Scene->setParent(entity, parent);
    }

    glm::vec3 spawnPos = getDefaultSpawnPosition();
    glm::vec3 spawnTarget = spawnPos + glm::vec3(0.0f, 0.0f, -1.0f);
    glm::vec3 spawnUp(0.0f, 1.0f, 0.0f);

    auto editorCamView = registry.view<EditorCamera>();
    if (editorCamView.begin() != editorCamView.end()) {
        auto camEntity = *editorCamView.begin();
        const auto& srcCam = registry.get<EditorCamera>(camEntity);
        spawnPos = srcCam.position;
        spawnTarget = srcCam.target;
        spawnUp = srcCam.up;
    } else {
        auto camView = registry.view<Camera>();
        if (camView.begin() != camView.end()) {
            auto camEntity = *camView.begin();
            const auto& srcCam = registry.get<Camera>(camEntity);
            spawnPos = srcCam.position;
            spawnTarget = srcCam.target;
            spawnUp = srcCam.up;
        }
    }

    auto& transform = registry.get<Transform>(entity);
    transform.position = (parent != entt::null && registry.valid(parent)) ? glm::vec3(0.0f) : spawnPos;

    glm::vec3 forward = spawnTarget - spawnPos;
    if (glm::length(forward) < 1e-5f) {
        forward = glm::vec3(0.0f, 0.0f, -1.0f);
    } else {
        forward = glm::normalize(forward);
    }
    glm::quat rot = glm::quatLookAtRH(forward, spawnUp);
    transform.rotation = glm::degrees(glm::eulerAngles(rot));

    Camera camera;
    camera.position = spawnPos;
    camera.target = spawnTarget;
    camera.up = spawnUp;
    registry.emplace_or_replace<Camera>(entity, camera);

    Atlas::ECS::GameCameraComponent gcc;
    auto gameCameraView = registry.view<Camera, Atlas::ECS::GameCameraComponent>();
    bool hasAnyGameCamera = (gameCameraView.begin() != gameCameraView.end());
    gcc.primary = !hasAnyGameCamera;
    registry.emplace_or_replace<Atlas::ECS::GameCameraComponent>(entity, gcc);

    if (m_UIManager) {
        m_UIManager->setSelectedEntity(entity);
    }

    return entity;
}

Entity EditorApp::createPrimitiveEntity(const std::string& primitiveType, Entity parent) {
    if (!m_Scene || !m_Renderer) {
        return entt::null;
    }

    MeshData meshData;
    std::string entityName = primitiveType;
    if (primitiveType == "Cube") {
        meshData = ModelLoader::createCube(1.0f);
        meshData.name = "Cube";
    } else if (primitiveType == "Plane") {
        meshData = createPlanePrimitive(2.0f);
    } else if (primitiveType == "Sphere") {
        meshData = createSpherePrimitive(0.5f);
    } else if (primitiveType == "Cylinder") {
        meshData = createCylinderPrimitive(0.5f, 1.5f);
    } else if (primitiveType == "Capsule") {
        meshData = createCapsulePrimitive(0.45f, 1.8f);
    } else {
        return entt::null;
    }

    glm::vec3 boundsMin(0.0f);
    glm::vec3 boundsMax(0.0f);
    bool hasBounds = false;
    if (!meshData.vertices.empty()) {
        boundsMin = meshData.vertices[0].pos;
        boundsMax = meshData.vertices[0].pos;
        for (const auto& v : meshData.vertices) {
            boundsMin = glm::min(boundsMin, v.pos);
            boundsMax = glm::max(boundsMax, v.pos);
        }
        hasBounds = true;
    }

    if (meshData.vertexBuffer == VK_NULL_HANDLE || meshData.indexBuffer == VK_NULL_HANDLE) {
        ModelLoader::createBuffers(meshData, m_Renderer->getDevice(), m_Renderer->getPhysicalDevice(), primitiveFindMemoryType);
        meshData.freeCPUMemory();
    }

    auto entity = m_Scene->createEntity(entityName);
    auto& registry = m_Scene->getRegistry();

    if (parent != entt::null && registry.valid(parent)) {
        m_Scene->setParent(entity, parent);
    }

    if (registry.all_of<Transform>(entity)) {
        auto& transform = registry.get<Transform>(entity);
        transform.position = (parent != entt::null && registry.valid(parent)) ? glm::vec3(0.0f) : getDefaultSpawnPosition();
    }

    auto& mesh = registry.emplace<::Mesh>(entity);
    mesh.meshPath = "primitive://" + entityName;
    mesh.vertexBuffer = meshData.vertexBuffer;
    mesh.indexBuffer = meshData.indexBuffer;
    mesh.vertexMemory = meshData.vertexMemory;
    mesh.indexMemory = meshData.indexMemory;
    mesh.vertexCount = meshData.vertexCount;
    mesh.indexCount = meshData.indexCount;
    mesh.hasBounds = hasBounds;
    mesh.boundsMin = boundsMin;
    mesh.boundsMax = boundsMax;

    meshData.vertexBuffer = VK_NULL_HANDLE;
    meshData.indexBuffer = VK_NULL_HANDLE;
    meshData.vertexMemory = VK_NULL_HANDLE;
    meshData.indexMemory = VK_NULL_HANDLE;
    meshData.ownerDevice = VK_NULL_HANDLE;

    ECS::MaterialComponent material;
    if (primitiveType == "Plane") {
        material.baseColor = glm::vec4(0.75f, 0.78f, 0.82f, 1.0f);
        material.roughness = 0.95f;
        material.metallic = 0.0f;
        material.doubleSided = true;
    } else if (primitiveType == "Sphere") {
        material.baseColor = glm::vec4(0.85f, 0.86f, 0.9f, 1.0f);
        material.roughness = 0.35f;
        material.metallic = 0.15f;
    } else if (primitiveType == "Cylinder") {
        material.baseColor = glm::vec4(0.87f, 0.88f, 0.9f, 1.0f);
        material.roughness = 0.45f;
        material.metallic = 0.08f;
    } else if (primitiveType == "Capsule") {
        material.baseColor = glm::vec4(0.9f, 0.9f, 0.92f, 1.0f);
        material.roughness = 0.4f;
        material.metallic = 0.06f;
    } else {
        material.baseColor = glm::vec4(0.88f, 0.88f, 0.9f, 1.0f);
        material.roughness = 0.55f;
        material.metallic = 0.05f;
    }
    registry.emplace_or_replace<ECS::MaterialComponent>(entity, material);

    if (m_UIManager) {
        m_UIManager->setSelectedEntity(entity);
    }

    return entity;
}

void EditorApp::updateFollowCameras(Scene* scene, float deltaTime) {
    if (!scene) {
        return;
    }

    auto& registry = scene->getRegistry();
    auto view = registry.view<Camera, Atlas::ECS::FollowCameraComponent>();
    for (auto e : view) {
        auto& cam = view.get<Camera>(e);
        auto& follow = view.get<Atlas::ECS::FollowCameraComponent>(e);
        if (follow.target == entt::null || !registry.valid(follow.target) || !registry.all_of<Transform>(follow.target)) {
            continue;
        }

        const glm::mat4 targetWorld = scene->getWorldTransform(follow.target);
        const glm::vec3 targetPos = glm::vec3(targetWorld[3]);
        const glm::vec3 desiredPos = targetPos + follow.offset;

        const float smooth = std::max(0.0f, follow.smoothness);
        const float alpha = (smooth <= 0.0f) ? 1.0f : (1.0f - std::exp(-smooth * deltaTime));
        cam.position = glm::mix(cam.position, desiredPos, alpha);

        if (follow.lookAtTarget) {
            cam.target = targetPos;
        }

        if (registry.all_of<Transform>(e)) {
            registry.get<Transform>(e).position = cam.position;
        }
    }
}

void EditorApp::updateAnimationRuntime(Scene* scene, float deltaTime) {
    if (!scene) {
        return;
    }

    auto& registry = scene->getRegistry();
    auto view = registry.view<ECS::AnimationPlayerComponent, ECS::SkeletonComponent, Transform>();
    for (auto e : view) {
        auto& ap = view.get<ECS::AnimationPlayerComponent>(e).player;
        auto& skc = view.get<ECS::SkeletonComponent>(e);
        auto& xform = view.get<Transform>(e);
        if (!ap.playing) {
            continue;
        }

        const Atlas::Anim::Skeleton* skel = skc.skeleton.get();
        const Atlas::Anim::AnimationClip* clip = nullptr;
        if (ap.clipIndex >= 0 && static_cast<size_t>(ap.clipIndex) < skc.clips.size()) {
            clip = &skc.clips[static_cast<size_t>(ap.clipIndex)];
        }

        const float prevTimeRaw = ap.timeSeconds;
        const float deltaSeconds = deltaTime * ap.speed;
        float nextTimeRaw = prevTimeRaw + deltaSeconds;
        float prevSampleTime = prevTimeRaw;
        float nextSampleTime = nextTimeRaw;
        bool wrapped = false;

        if (clip && clip->durationSeconds > 0.0f) {
            prevSampleTime = Atlas::Anim::wrapTime(prevTimeRaw, clip->durationSeconds);
            if (ap.loop) {
                nextSampleTime = Atlas::Anim::wrapTime(nextTimeRaw, clip->durationSeconds);
                wrapped = (deltaSeconds >= 0.0f) ? (nextSampleTime < prevSampleTime) : (nextSampleTime > prevSampleTime);
                ap.timeSeconds = nextSampleTime;
            } else {
                nextSampleTime = std::clamp(nextTimeRaw, 0.0f, clip->durationSeconds);
                ap.timeSeconds = nextSampleTime;
                if ((deltaSeconds >= 0.0f && nextTimeRaw >= clip->durationSeconds) ||
                    (deltaSeconds < 0.0f && nextTimeRaw <= 0.0f)) {
                    ap.playing = false;
                }
            }
        } else {
            ap.timeSeconds = nextTimeRaw;
        }

        if (!skel || !clip || clip->durationSeconds <= 0.0f) {
            continue;
        }

        bool canApplyRootMotion = ap.enableRootMotion && skel->rootMotionBoneIndex >= 0;
        if (canApplyRootMotion) {
            if (auto* ov = registry.try_get<ECS::BonePoseOverrideComponent>(e); ov && ov->enabled) {
                canApplyRootMotion = false;
            }
        }

        if (canApplyRootMotion) {
            auto applyRootDelta = [&](float fromTime, float toTime) {
                const int32_t rootBone = skel->rootMotionBoneIndex;
                if (rootBone < 0) {
                    return;
                }

                Atlas::Anim::PoseOverrides overrides;
                const Atlas::Anim::TRS prevRoot = Atlas::Anim::sampleLocalTRS(*skel, clip, fromTime, overrides, static_cast<uint32_t>(rootBone));
                const Atlas::Anim::TRS nextRoot = Atlas::Anim::sampleLocalTRS(*skel, clip, toTime, overrides, static_cast<uint32_t>(rootBone));

                glm::vec3 localDelta = nextRoot.translation - prevRoot.translation;
                if (!ap.rootMotionApplyY) {
                    localDelta.y = 0.0f;
                }
                glm::quat entityRot = glm::quat(glm::radians(xform.rotation));
                xform.position += entityRot * localDelta;

                if (ap.rootMotionApplyRotation) {
                    glm::quat deltaRot = glm::normalize(nextRoot.rotation * glm::inverse(prevRoot.rotation));
                    glm::quat nextEntityRot = glm::normalize(deltaRot * entityRot);
                    xform.rotation = glm::degrees(glm::eulerAngles(nextEntityRot));
                }
            };

            if (ap.loop && wrapped) {
                if (deltaSeconds >= 0.0f) {
                    applyRootDelta(prevSampleTime, clip->durationSeconds);
                    applyRootDelta(0.0f, nextSampleTime);
                } else {
                    applyRootDelta(prevSampleTime, 0.0f);
                    applyRootDelta(clip->durationSeconds, nextSampleTime);
                }
            } else {
                applyRootDelta(prevSampleTime, nextSampleTime);
            }
        }
    }
}


void EditorApp::onMeshDestroyed(entt::registry& registry, entt::entity entity) {
    if (!m_Renderer) return;
    if (!registry.valid(entity)) return;
    if (!registry.all_of<::Mesh>(entity)) return;

    VkDevice device = m_Renderer->getDevice();
    if (device == VK_NULL_HANDLE) return;

    auto& mesh = registry.get<::Mesh>(entity);

    VkBuffer vb = mesh.vertexBuffer;
    VkDeviceMemory vm = mesh.vertexMemory;
    VkBuffer ib = mesh.indexBuffer;
    VkDeviceMemory im = mesh.indexMemory;

    mesh.vertexBuffer = VK_NULL_HANDLE;
    mesh.vertexMemory = VK_NULL_HANDLE;
    mesh.indexBuffer = VK_NULL_HANDLE;
    mesh.indexMemory = VK_NULL_HANDLE;

    m_Renderer->defer([device, vb, vm, ib, im]() {
        if (vb) vkDestroyBuffer(device, vb, nullptr);
        if (ib) vkDestroyBuffer(device, ib, nullptr);
        if (vm) vkFreeMemory(device, vm, nullptr);
        if (im) vkFreeMemory(device, im, nullptr);
    });
}

void EditorApp::queueModelImport(const std::string& assetPath) {
    queueModelImportAt(assetPath, glm::vec3(0.0f), false, 0);
}

static bool resolveModelImportPath(::ProjectManager* projectManager, const std::string& assetPath, std::string& outFullPath, std::string& outModelName) {
    outFullPath = assetPath;

    // Normalize "@"-prefixed paths (Content Explorer style) and handle Windows slashes.
    if (!outFullPath.empty() && outFullPath[0] == '@') {
        outFullPath.erase(outFullPath.begin());
    }
    std::replace(outFullPath.begin(), outFullPath.end(), '\\', '/');

    // If this already resolves to an existing file, keep it.
    {
        std::error_code ec;
        std::filesystem::path p(outFullPath);
        if (!p.empty() && std::filesystem::exists(p, ec)) {
            outFullPath = std::filesystem::absolute(p, ec).lexically_normal().string();
        } else if (projectManager && projectManager->hasProject()) {
            // Accept "MyProject/assets/..." and "assets/..." as input and map to project assets.
            const std::string assetsPrefix = projectManager->getAssetsPath() + "/";

            std::string rel = outFullPath;
            if (rel.rfind(assetsPrefix, 0) == 0) {
                rel = rel.substr(assetsPrefix.size());
            } else {
                const std::string mpPrefix = "MyProject/assets/";
                const std::string assetsPrefix2 = "assets/";
                if (rel.rfind(mpPrefix, 0) == 0) {
                    rel = rel.substr(mpPrefix.size());
                } else if (rel.rfind(assetsPrefix2, 0) == 0) {
                    rel = rel.substr(assetsPrefix2.size());
                }
            }

            std::filesystem::path rp(rel);
            if (!rp.is_absolute()) {
                outFullPath = projectManager->getAssetFullPath(rel);
            }
        }
    }

    std::filesystem::path fsPath(outFullPath);
    std::string ext = fsPath.extension().string();
    for (char& c : ext) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));

    if (ext != ".fbx" && ext != ".gltf" && ext != ".glb" && ext != ".obj" && ext != ".dae") {
        return false;
    }

    outModelName = fsPath.stem().string();
    return true;
}

void EditorApp::enqueueImportRequest(const ImportRequest& req) {
    // World chunk imports should be silent.
    if (req.isWorldChunk) {
        ImportOptions opts;
        startModelImportAt(req.assetPath, req.rootPosition, req.isWorldChunk, req.cellKey, opts);
        return;
    }

    m_ImportQueue.push_back(req);

    if (!m_ShowImportOptionsPopup) {
        m_ActiveImport = m_ImportQueue.front();
        m_ImportQueue.pop_front();

        m_ActiveImportOptions = m_LastImportOptions;
        if (m_ActiveImportOptions.uniformScale <= 0.0f) {
            m_ActiveImportOptions.uniformScale = 1.0f;
        }

        m_ActiveImportFullPath.clear();
        m_ActiveImportModelName.clear();
        if (!resolveModelImportPath(m_ProjectManager.get(), m_ActiveImport.assetPath, m_ActiveImportFullPath, m_ActiveImportModelName)) {
            // Drop invalid requests silently.
            m_ActiveImport = ImportRequest{};
            return;
        }

        m_ShowImportOptionsPopup = true;
    }
}

void EditorApp::renderImportOptionsPopup() {
    if (!m_ShowImportOptionsPopup) {
        return;
    }

    ImGui::OpenPopup("Import Options");

    bool open = true;
    if (ImGui::BeginPopupModal("Import Options", &open, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::TextUnformatted("Asset");
        ImGui::Separator();
        ImGui::TextWrapped("%s", m_ActiveImportFullPath.c_str());

        ImGui::Spacing();
        ImGui::SeparatorText("Options");

        ImGui::DragFloat("Uniform Scale", &m_ActiveImportOptions.uniformScale, 0.01f, 0.001f, 1000.0f, "%.3f");
        ImGui::Checkbox("Import Animations", &m_ActiveImportOptions.importAnimations);

        ImGui::BeginDisabled(!m_ActiveImportOptions.importAnimations);
        ImGui::Checkbox("Start Playing", &m_ActiveImportOptions.startPlaying);
        ImGui::EndDisabled();

        ImGui::Spacing();
        ImGui::Separator();

        bool doImport = ImGui::Button("Import");
        ImGui::SameLine();
        bool doCancel = ImGui::Button("Cancel");

        if (!open) {
            doCancel = true;
        }

        if (doImport) {
            startModelImportAt(m_ActiveImport.assetPath, m_ActiveImport.rootPosition, m_ActiveImport.isWorldChunk, m_ActiveImport.cellKey, m_ActiveImportOptions);
            m_LastImportOptions = m_ActiveImportOptions;
            doCancel = true;
        }

        if (doCancel) {
            ImGui::CloseCurrentPopup();
            m_ShowImportOptionsPopup = false;
            m_ActiveImport = ImportRequest{};
            m_ActiveImportFullPath.clear();
            m_ActiveImportModelName.clear();

            if (!m_ImportQueue.empty()) {
                m_ActiveImport = m_ImportQueue.front();
                m_ImportQueue.pop_front();

                m_ActiveImportOptions = m_LastImportOptions;
                if (m_ActiveImportOptions.uniformScale <= 0.0f) {
                    m_ActiveImportOptions.uniformScale = 1.0f;
                }

                if (resolveModelImportPath(m_ProjectManager.get(), m_ActiveImport.assetPath, m_ActiveImportFullPath, m_ActiveImportModelName)) {
                    m_ShowImportOptionsPopup = true;
                } else {
                    m_ActiveImport = ImportRequest{};
                    m_ActiveImportFullPath.clear();
                    m_ActiveImportModelName.clear();
                }
            }
        }

        ImGui::EndPopup();
    }
}

void EditorApp::queueModelImportAt(const std::string& assetPath, const glm::vec3& rootPosition, bool isWorldChunk, uint64_t cellKey) {
    ImportRequest req;
    req.assetPath = assetPath;
    req.rootPosition = rootPosition;
    req.isWorldChunk = isWorldChunk;
    req.cellKey = cellKey;

    // Avoid spamming chunk loads.
    if (isWorldChunk) {
        if (m_WorldLoadingCells.find(cellKey) != m_WorldLoadingCells.end()) {
            return;
        }
        if (m_WorldCellRoots.find(cellKey) != m_WorldCellRoots.end()) {
            return;
        }
    }

    enqueueImportRequest(req);
}

void EditorApp::startModelImportAt(const std::string& assetPath, const glm::vec3& rootPosition, bool isWorldChunk, uint64_t cellKey, const ImportOptions& options) {
    std::string fullPath;
    std::string modelName;
    if (!resolveModelImportPath(m_ProjectManager.get(), assetPath, fullPath, modelName)) {
        return;
    }

    if (isWorldChunk) {
        if (m_WorldLoadingCells.find(cellKey) != m_WorldLoadingCells.end()) {
            return;
        }
        if (m_WorldCellRoots.find(cellKey) != m_WorldCellRoots.end()) {
            return;
        }
    }

    auto tempEntity = m_Scene->createEntity(modelName + " [Loading...]");
    if (m_Scene->getRegistry().all_of<Transform>(tempEntity)) {
        m_Scene->getRegistry().get<Transform>(tempEntity).position = rootPosition;
    }

    if (isWorldChunk) {
        m_WorldCellRoots[cellKey] = tempEntity;
        m_Scene->getRegistry().emplace_or_replace<WorldChunk>(tempEntity, WorldChunk{cellKey, true});
        m_WorldLoadingCells.insert(cellKey);
    }

    auto modelDataPtr = std::make_shared<::ModelData>();
    AsyncLoader::getInstance().loadModelAsync<::ModelData>(
        fullPath,
        [fullPath, modelDataPtr]() -> std::shared_ptr<::ModelData> {
            PROFILE_SCOPE("ModelLoad");
            ModelLoader::loadModelMultiMesh(fullPath, VK_NULL_HANDLE, VK_NULL_HANDLE, nullptr, modelDataPtr.get(), false);
            return modelDataPtr;
        },
        [this, modelName, fullPath, tempEntity, rootPosition, isWorldChunk, cellKey, options](AsyncLoader::LoadResult<::ModelData> result) {
            std::lock_guard<std::mutex> lock(m_PendingModelsMutex);
            PendingModel p;
            p.modelData = result.data;
            p.modelName = modelName;
            p.basePath = fullPath;
            p.placeholderEntity = tempEntity;
            p.importOptions = options;
            p.hasRootPosition = true;
            p.rootPosition = rootPosition;
            p.isWorldChunk = isWorldChunk;
            p.worldCellKey = cellKey;

            if (!result.success || !result.data) {
                p.loadFailed = true;
                p.error = result.error;
                p.modelData.reset();
            }

            m_PendingModels.push_back(std::move(p));
        });
}

glm::vec3 EditorApp::getDefaultSpawnPosition() const {
    if (!m_Scene) return glm::vec3(0.0f);

    auto& registry = m_Scene->getRegistry();
    auto editorCamView = registry.view<EditorCamera>();
    if (editorCamView.begin() != editorCamView.end()) {
        auto camEnt = *editorCamView.begin();
        auto& cam = registry.get<EditorCamera>(camEnt);
        return cam.target;
    }

    auto camView = registry.view<Camera>();
    if (camView.begin() != camView.end()) {
        auto camEnt = *camView.begin();
        auto& cam = registry.get<Camera>(camEnt);
        return cam.target;
    }

    return glm::vec3(0.0f);
}

static bool isModelExt(const std::string& ext) {
    return ext == ".fbx" || ext == ".gltf" || ext == ".glb" || ext == ".obj" || ext == ".dae";
}

static void copyIfExists(const std::filesystem::path& src, const std::filesystem::path& dst, bool recursive) {
    std::error_code ec;
    if (!std::filesystem::exists(src, ec)) return;

    if (recursive && std::filesystem::is_directory(src, ec)) {
        std::filesystem::create_directories(dst, ec);
        std::filesystem::copy(src, dst, std::filesystem::copy_options::recursive | std::filesystem::copy_options::overwrite_existing, ec);
        return;
    }

    if (std::filesystem::is_regular_file(src, ec)) {
        std::filesystem::create_directories(dst.parent_path(), ec);
        std::filesystem::copy_file(src, dst, std::filesystem::copy_options::overwrite_existing, ec);
    }
}

std::string EditorApp::importExternalModelToProjectAssets(const std::string& srcPathStr) {
    if (!m_ProjectManager || !m_ProjectManager->hasProject()) {
        return srcPathStr;
    }

    std::error_code ec;
    std::filesystem::path src(srcPathStr);
    if (!std::filesystem::exists(src, ec) || !std::filesystem::is_regular_file(src, ec)) {
        return {};
    }

    std::string ext = src.extension().string();
    for (char& c : ext) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    if (!isModelExt(ext)) {
        return {};
    }

    // Place under assets/models/Imported/<stem>/
    std::filesystem::path assetsRoot(m_ProjectManager->getAssetsPath());
    std::filesystem::path importRoot = assetsRoot / "models" / "Imported";

    std::filesystem::path dstFolder = importRoot / src.stem();
    std::filesystem::create_directories(dstFolder, ec);

    auto uniquePath = [&](std::filesystem::path p) -> std::filesystem::path {
        if (!std::filesystem::exists(p, ec)) return p;
        std::filesystem::path dir = p.parent_path();
        std::string stem = p.stem().string();
        std::string e = p.extension().string();
        for (int v = 2; v < 1000; ++v) {
            std::filesystem::path cand = dir / (stem + "_v" + std::to_string(v) + e);
            if (!std::filesystem::exists(cand, ec)) return cand;
        }
        return p;
    };

    std::filesystem::path dstModel = uniquePath(dstFolder / src.filename());
    copyIfExists(src, dstModel, false);

    // Best-effort dependency copy
    std::filesystem::path srcDir = src.parent_path();

    auto copySidecarsInDir = [&](const std::vector<std::string>& exts) {
        for (auto& it : std::filesystem::directory_iterator(srcDir, ec)) {
            if (ec) break;
            if (!it.is_regular_file(ec)) continue;
            std::string e = it.path().extension().string();
            for (char& c : e) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
            for (const auto& want : exts) {
                if (e == want) {
                    copyIfExists(it.path(), dstFolder / it.path().filename(), false);
                    break;
                }
            }
        }
    };

    if (ext == ".gltf") {
        copySidecarsInDir({".bin", ".png", ".jpg", ".jpeg", ".tga", ".bmp", ".hdr"});
        copyIfExists(srcDir / "textures", dstFolder / "textures", true);
    } else if (ext == ".obj") {
        copySidecarsInDir({".mtl", ".png", ".jpg", ".jpeg", ".tga", ".bmp", ".hdr"});
        copyIfExists(srcDir / "textures", dstFolder / "textures", true);
    } else if (ext == ".fbx" || ext == ".dae") {
        copySidecarsInDir({".png", ".jpg", ".jpeg", ".tga", ".bmp", ".hdr"});
        copyIfExists(srcDir / "textures", dstFolder / "textures", true);
    }

    if (m_ProjectManager) {
        m_ProjectManager->invalidateAssetTreeCache();
    }

    return dstModel.lexically_normal().string();
}

void EditorApp::onExternalFileDrop(const std::vector<std::string>& paths) {
    if (paths.empty()) return;

    const glm::vec3 spawnPos = getDefaultSpawnPosition();

    for (const auto& p : paths) {
        std::filesystem::path fp(p);
        std::string ext = fp.extension().string();
        for (char& c : ext) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));

        if (!isModelExt(ext)) {
            continue;
        }

        std::string imported = importExternalModelToProjectAssets(p);
        if (imported.empty()) {
            continue;
        }

        queueModelImportAt(imported, spawnPos, false, 0);
    }
}

void EditorApp::updateWorldStreaming() {
    if (!m_WorldStreamingEnabled) return;
    if (!m_ProjectManager || !m_ProjectManager->hasProject()) return;
    if (!m_Scene || !m_Renderer) return;

    const std::string chunksDir = m_ProjectManager->getAssetsPath() + "/" + m_WorldChunksSubdir;

    glm::vec3 camPos(0.0f);
    glm::mat4 view(1.0f);
    glm::mat4 proj(1.0f);

    // Use editor camera if present.
    {
        auto& registry = m_Scene->getRegistry();
        auto editorCamView = registry.view<EditorCamera>();
        if (editorCamView.begin() != editorCamView.end()) {
            auto camEnt = *editorCamView.begin();
            auto& cam = registry.get<EditorCamera>(camEnt);
            camPos = cam.position;

            VkExtent2D extent = m_Renderer->getSwapChainExtent();
            if (extent.width > 0 && extent.height > 0) {
                cam.aspectRatio = static_cast<float>(extent.width) / static_cast<float>(extent.height);
            }

            view = cam.getViewMatrix();
            proj = cam.getProjectionMatrix();
            proj[1][1] = -proj[1][1];
        } else if (m_CameraController) {
            view = m_CameraController->getViewMatrix();
            proj = m_CameraController->getProjMatrix();
            proj[1][1] = -proj[1][1];
        }
    }

    const float cs = (m_WorldCellSize > 1e-3f) ? m_WorldCellSize : 1.0f;
    const int camCellX = static_cast<int>(std::floor(camPos.x / cs));
    const int camCellZ = static_cast<int>(std::floor(camPos.z / cs));

    std::unordered_set<uint64_t> desired;
    const int r = (m_WorldLoadRadius < 0) ? 0 : m_WorldLoadRadius;
    desired.reserve(static_cast<size_t>((2 * r + 1) * (2 * r + 1)));

    for (int dz = -r; dz <= r; ++dz) {
        for (int dx = -r; dx <= r; ++dx) {
            desired.insert(makeCellKey(camCellX + dx, camCellZ + dz));
        }
    }

    // Unload cells not desired.
    for (auto it = m_WorldActiveCells.begin(); it != m_WorldActiveCells.end(); ) {
        uint64_t key = *it;
        if (desired.find(key) == desired.end()) {
            if (auto rit = m_WorldCellRoots.find(key); rit != m_WorldCellRoots.end()) {
                if (m_Scene->getRegistry().valid(rit->second)) {
                    m_Scene->destroyEntity(rit->second);
                }
                m_WorldCellRoots.erase(rit);
            }
            m_WorldLoadingCells.erase(key);
            m_WorldFailedCells.erase(key);
            it = m_WorldActiveCells.erase(it);
            continue;
        }
        ++it;
    }

    // Load desired cells.
    const double nowTime = glfwGetTime();

    // Prune negative-cache entries outside desired area.
    for (auto it = m_WorldFailedCells.begin(); it != m_WorldFailedCells.end(); ) {
        if (desired.find(it->first) == desired.end()) {
            it = m_WorldFailedCells.erase(it);
        } else {
            ++it;
        }
    }

    for (uint64_t key : desired) {
        if (m_WorldActiveCells.find(key) != m_WorldActiveCells.end()) {
            continue;
        }
        if (m_WorldLoadingCells.find(key) != m_WorldLoadingCells.end()) {
            continue;
        }
        if (auto fit = m_WorldFailedCells.find(key); fit != m_WorldFailedCells.end()) {
            if ((nowTime - fit->second) < m_WorldFailRetrySeconds) {
                continue;
            }
        }

        int cx = 0;
        int cz = 0;
        decodeCellKey(key, cx, cz);

        std::string found;
        for (const auto& ext : m_WorldChunkExtensions) {
            std::string candidate = chunksDir + "/cell_" + std::to_string(cx) + "_" + std::to_string(cz) + ext;
            std::error_code ec;
            if (std::filesystem::exists(candidate, ec) && std::filesystem::is_regular_file(candidate, ec)) {
                found = candidate;
                break;
            }
        }

        if (!found.empty()) {
            glm::vec3 origin(static_cast<float>(cx) * cs, 0.0f, static_cast<float>(cz) * cs);
            m_WorldActiveCells.insert(key);
            queueModelImportAt(found, origin, true, key);
        } else {
            // Avoid hammering the filesystem for missing chunks.
            m_WorldFailedCells[key] = nowTime;
        }
    }

    // Frustum culling (post-stream) for Mesh+Renderable.
    Frustum fr = Frustum::fromViewProj(proj * view);

    auto& registry = m_Scene->getRegistry();
    auto viewMeshes = registry.view<::Mesh>();
    for (auto e : viewMeshes) {
        if (!registry.valid(e)) continue;
        if (!registry.all_of<Renderable>(e)) continue;

        // Only frustum-cull streamed chunks for now.
        if (!registry.all_of<WorldChunk>(e)) {
            continue;
        }

        auto& mesh = registry.get<::Mesh>(e);
        auto& rend = registry.get<Renderable>(e);

        // Default to visible, then cull.
        rend.visible = true;

        glm::mat4 world = m_Scene->getCachedWorldTransform(e);
        glm::vec3 center = glm::vec3(world[3]);
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

void EditorApp::run() {
    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;

    double lastTime = glfwGetTime();
    while (!m_Window->shouldClose()) {
        auto frameStart = std::chrono::steady_clock::now();

        double currentTime = glfwGetTime();
        float deltaTime = static_cast<float>(currentTime - lastTime);
        lastTime = currentTime;

        m_Window->update();
        m_UIManager->updateProfiler(deltaTime);

        if (m_Renderer) {
            m_Renderer->beginFrame();
        }

        if (!m_GameModePlaying && m_CameraController && !m_UIManager->isGizmoUsing()) {
            bool uiAllow = (m_UIManager && m_UIManager->allowViewportCameraInput());
            bool allow = uiAllow || m_CameraController->isCapturing();
            m_CameraController->update(deltaTime, allow);
        }

        m_ImGuiManager->newFrame();
        ImGuizmo::BeginFrame();

        Scene* cameraScene = m_GameModePlaying ? m_RuntimeScene.get() : m_Scene.get();
        if (m_UIManager && cameraScene) {
            auto& registry = cameraScene->getRegistry();

            auto chooseRuntimeCameraEntity = [&]() -> entt::entity {
                if (m_GameModePlaying) {
                    auto gameCams = registry.view<Camera, Atlas::ECS::GameCameraComponent>();
                    entt::entity fallback = entt::null;
                    for (auto e : gameCams) {
                        const auto& gcc = gameCams.get<Atlas::ECS::GameCameraComponent>(e);
                        if (fallback == entt::null) {
                            fallback = e;
                        }
                        if (gcc.primary) {
                            return e;
                        }
                    }
                    if (fallback != entt::null) {
                        return fallback;
                    }
                }

                auto camView = registry.view<Camera>();
                return (camView.begin() == camView.end()) ? entt::null : *camView.begin();
            };

            entt::entity camEnt = m_GameModePlaying ? chooseRuntimeCameraEntity() : entt::null;
            if (camEnt == entt::null && !m_GameModePlaying) {
                auto editorCamView = registry.view<EditorCamera>();
                if (editorCamView.begin() != editorCamView.end()) {
                    camEnt = *editorCamView.begin();
                    auto& cam = registry.get<EditorCamera>(camEnt);
                    VkExtent2D extent = m_Renderer ? m_Renderer->getSwapChainExtent() : VkExtent2D{1280, 720};
                    if (extent.width > 0 && extent.height > 0) {
                        cam.aspectRatio = static_cast<float>(extent.width) / static_cast<float>(extent.height);
                    }
                    m_UIManager->setCameraMatrices(cam.getViewMatrix(), cam.getProjectionMatrix());
                }
            } else if (camEnt != entt::null) {
                auto& cam = registry.get<Camera>(camEnt);
                VkExtent2D extent = m_Renderer ? m_Renderer->getSwapChainExtent() : VkExtent2D{1280, 720};
                if (extent.width > 0 && extent.height > 0) {
                    cam.aspectRatio = static_cast<float>(extent.width) / static_cast<float>(extent.height);
                }
                m_UIManager->setCameraMatrices(cam.getViewMatrix(), cam.getProjectionMatrix());
            } else if (!m_GameModePlaying && m_CameraController) {
                m_UIManager->setCameraMatrices(m_CameraController->getViewMatrix(), m_CameraController->getProjMatrix());
            }
        }

        m_UIManager->render(m_Viewport.getSceneTextureId(), m_Viewport.getGameTextureId(), m_GameModePlaying, m_GameModePaused);

        // World streaming controls (debug)
        if(m_UIManager->m_ShowWorldStreamingWindow) {
            ImGui::Begin("World Streaming", &m_UIManager->m_ShowWorldStreamingWindow);
            ImGui::Checkbox("Enabled", &m_WorldStreamingEnabled);
            ImGui::DragFloat("Cell Size", &m_WorldCellSize, 1.0f, 1.0f, 8192.0f, "%.1f");
            ImGui::SliderInt("Load Radius (cells)", &m_WorldLoadRadius, 0, 16);
            ImGui::Text("Active: %zu", m_WorldActiveCells.size());
            ImGui::Text("Loading: %zu", m_WorldLoadingCells.size());
            ImGui::DragFloat("Fail retry (sec)", &m_WorldFailRetrySeconds, 0.1f, 0.0f, 30.0f, "%.1f");
            ImGui::Text("Failed: %zu", m_WorldFailedCells.size());
            if (ImGui::Button("Clear Failed")) {
                m_WorldFailedCells.clear();
            }

            if (m_WorldPartition) {
                auto& cfg = m_WorldPartition->config();
                ImGui::SeparatorText("Partition Culling");
                ImGui::Checkbox("Enabled##partition", &cfg.enabled);
                ImGui::Checkbox("Frustum Culling##partition", &cfg.useFrustumCulling);
                ImGui::DragFloat("Cell Size##partition", &cfg.cellSize, 1.0f, 1.0f, 8192.0f, "%.1f");
                ImGui::SliderInt("Load Radius##partition", &cfg.loadRadiusCells, 0, 16);
                if (ImGui::Button("Rebuild##partition")) {
                    m_WorldPartition->markDirty();
                }
            }

            ImGui::End();
        }

        renderImportOptionsPopup();
        processPendingModels();

        if (m_GameModePlaying) {
            updateGameCameras(m_RuntimeScene.get());
            if (!m_GameModePaused) {
                updateAnimationRuntime(m_RuntimeScene.get(), deltaTime);
                if (m_ScriptEngine) {
                    m_ScriptEngine->update(deltaTime);
                }
            }
            updateFollowCameras(m_RuntimeScene.get(), deltaTime);
        } else {
            updateAnimationRuntime(m_Scene.get(), deltaTime);
            updateGameCameras(m_Scene.get());
        }

        bool sceneXformsChanged = false;
        Scene* activeScene = m_GameModePlaying ? m_RuntimeScene.get() : m_Scene.get();
        if (activeScene) {
            sceneXformsChanged = activeScene->updateWorldTransforms();
        }
        if (!m_GameModePlaying && sceneXformsChanged && m_WorldPartition) {
            m_WorldPartition->markDirty();
        }

        // Optional: WorldPartition culling (disabled while chunk streaming is enabled).
        if (!m_GameModePlaying && m_WorldPartition && !m_WorldStreamingEnabled && m_WorldPartition->config().enabled) {
            glm::vec3 camPos(0.0f);
            glm::mat4 view(1.0f);
            glm::mat4 proj(1.0f);

            auto& registry = m_Scene->getRegistry();
            auto editorCamView = registry.view<EditorCamera>();
            if (editorCamView.begin() != editorCamView.end()) {
                auto camEnt = *editorCamView.begin();
                auto& cam = registry.get<EditorCamera>(camEnt);
                camPos = cam.position;

                VkExtent2D extent = m_Renderer ? m_Renderer->getSwapChainExtent() : VkExtent2D{1280, 720};
                if (extent.width > 0 && extent.height > 0) {
                    cam.aspectRatio = static_cast<float>(extent.width) / static_cast<float>(extent.height);
                }

                view = cam.getViewMatrix();
                proj = cam.getProjectionMatrix();
                proj[1][1] = -proj[1][1];
            } else if (m_CameraController) {
                view = m_CameraController->getViewMatrix();
                proj = m_CameraController->getProjMatrix();
                proj[1][1] = -proj[1][1];

                // Derive camera world position from the view matrix.
                glm::mat4 invView = glm::inverse(view);
                camPos = glm::vec3(invView[3]);
            }

            m_WorldPartition->update(camPos, proj * view);
        }

        if (m_UIManager && m_Renderer) {
            std::vector<uint32_t> selectedIds;
            if (!m_GameModePlaying) {
                const auto& selected = m_UIManager->getSelectedEntities();
                selectedIds.reserve(selected.size());
                for (auto e : selected) {
                    if (e != entt::null) {
                        selectedIds.push_back(static_cast<uint32_t>(e));
                    }
                }
            }
            m_Renderer->setSelectedEntityIds(selectedIds);
        }

        if (!m_GameModePlaying) {
            updateWorldStreaming();
        }

        m_Renderer->setPreferGameCamera(m_GameModePlaying);
        m_Renderer->renderScene(activeScene);

        uint32_t pickX = 0;
        uint32_t pickY = 0;
        bool pickAdditive = false;
        bool deselectOnMiss = false;
        if (!m_GameModePlaying && m_UIManager && m_Renderer && m_UIManager->popViewportPickRequest(pickX, pickY, pickAdditive, deselectOnMiss)) {
            uint32_t pickedId = m_Renderer->pickEntityId(pickX, pickY);
            if (pickedId == UINT32_MAX) {
                if (!pickAdditive && deselectOnMiss) {
                    m_UIManager->clearSelection();
                }
            } else {
                Entity pickedEntity = static_cast<Entity>(pickedId);
                if (m_Scene && m_Scene->getRegistry().valid(pickedEntity)) {
                    if (pickAdditive) {
                        m_UIManager->toggleSelectedEntity(pickedEntity);
                    } else {
                        m_UIManager->setSelectedEntity(pickedEntity);
                    }
                } else if (!pickAdditive) {
                    m_UIManager->clearSelection();
                }
            }
        }

        m_Renderer->endFrame();

        int maxFps = m_UIManager ? m_UIManager->getMaxFps() : 0;
        if (maxFps > 0) {
            using namespace std::chrono;
            duration<double> targetFrameTime(1.0 / static_cast<double>(maxFps));
            auto frameEnd = steady_clock::now();
            duration<double> frameTime = frameEnd - frameStart;
            if (frameTime < targetFrameTime) {
                auto sleepDuration = duration_cast<microseconds>(targetFrameTime - frameTime);
                if (sleepDuration.count() > 0) {
                    std::this_thread::sleep_for(sleepDuration);
                }
            }
        }
    }

    vkDeviceWaitIdle(m_Renderer->getDevice());
}

void EditorApp::processPendingModels() {
    std::vector<PendingModel> items;
    {
        std::lock_guard<std::mutex> lock(m_PendingModelsMutex);
        items.swap(m_PendingModels);
    }

    for (auto& item : items) {
        if (!m_Scene || !m_Renderer) {
            continue;
        }

        // If this was a chunk request but the cell is no longer active, treat it as cancelled.
        if (item.isWorldChunk) {
            if (m_WorldActiveCells.find(item.worldCellKey) == m_WorldActiveCells.end()) {
                if (m_Scene->getRegistry().valid(item.placeholderEntity)) {
                    m_Scene->destroyEntity(item.placeholderEntity);
                }

                m_WorldLoadingCells.erase(item.worldCellKey);

                if (auto it = m_WorldCellRoots.find(item.worldCellKey); it != m_WorldCellRoots.end()) {
                    if (it->second == item.placeholderEntity) {
                        m_WorldCellRoots.erase(it);
                    }
                }

                continue;
            }
        }

        // Handle failed loads.
        if (item.loadFailed || !item.modelData) {
            std::cerr << "Async model load failed: " << item.error << std::endl;

            if (m_Scene->getRegistry().valid(item.placeholderEntity)) {
                m_Scene->destroyEntity(item.placeholderEntity);
            }

            if (item.isWorldChunk) {
                m_WorldLoadingCells.erase(item.worldCellKey);
                m_WorldCellRoots.erase(item.worldCellKey);
                m_WorldActiveCells.erase(item.worldCellKey);
                m_WorldFailedCells[item.worldCellKey] = glfwGetTime();
            }

            continue;
        }

        // Remove placeholder entity.
        if (m_Scene->getRegistry().valid(item.placeholderEntity)) {
            m_Scene->destroyEntity(item.placeholderEntity);
        }

        std::string rootName = item.modelName;
        if (item.modelData && !item.modelData->rootName.empty()) {
            rootName = item.modelData->rootName;
        }

        auto rootEntity = m_Scene->createEntity(rootName);
        if (item.hasRootPosition && m_Scene->getRegistry().all_of<Transform>(rootEntity)) {
            m_Scene->getRegistry().get<Transform>(rootEntity).position = item.rootPosition;
        }

        // Import options: root scale.
        if (m_Scene->getRegistry().all_of<Transform>(rootEntity)) {
            float s = item.importOptions.uniformScale;
            if (s <= 0.0f) s = 1.0f;
            m_Scene->getRegistry().get<Transform>(rootEntity).scale *= glm::vec3(s);
        }

        // Skeletal animation: attach skeleton + clips + player to the root.
        if (item.modelData && item.modelData->skeleton) {
            if (!item.importOptions.importAnimations) {
                item.modelData->clips.clear();
            }

            ECS::SkeletonComponent skc;
            skc.skeleton = item.modelData->skeleton;
            skc.clips = std::move(item.modelData->clips);
            if (skc.skeleton) {
                skc.skeleton->rootMotionBoneIndex = Atlas::Anim::chooseRootMotionBone(*skc.skeleton, skc.clips);
            }
            m_Scene->getRegistry().emplace_or_replace<ECS::SkeletonComponent>(rootEntity, std::move(skc));

            ECS::AnimationPlayerComponent apc;
            const auto& clips = m_Scene->getRegistry().get<ECS::SkeletonComponent>(rootEntity).clips;
            apc.player.clipIndex = clips.empty() ? -1 : 0;
            apc.player.timeSeconds = 0.0f;
            apc.player.speed = 1.0f;
            apc.player.loop = true;
            apc.player.playing = item.importOptions.startPlaying && !clips.empty();
            m_Scene->getRegistry().emplace_or_replace<ECS::AnimationPlayerComponent>(rootEntity, apc);
        }

        if (item.isWorldChunk) {
            m_WorldLoadingCells.erase(item.worldCellKey);
            m_WorldFailedCells.erase(item.worldCellKey);
            m_WorldCellRoots[item.worldCellKey] = rootEntity;
            m_Scene->getRegistry().emplace_or_replace<WorldChunk>(rootEntity, WorldChunk{item.worldCellKey, true});
        }

        auto bindMaterialTexture = [&](const std::string& relPath, AssetManager::TextureColorSpace colorSpace,
            bool& useFlag, int32_t& outIndex, StringID& outId, std::string& outPath) {
            useFlag = false;
            outIndex = -1;
            outPath.clear();

            if (relPath.empty() || !m_AssetManager || !m_Renderer) {
                return;
            }

            std::filesystem::path modelDir = std::filesystem::path(item.basePath).parent_path();
            std::filesystem::path candidate(relPath);
            std::filesystem::path texPath;
            if (candidate.is_absolute() || std::filesystem::exists(candidate)) {
                texPath = candidate;
            } else {
                texPath = (modelDir / candidate).lexically_normal();
            }
            std::string texFullPath = texPath.lexically_normal().string();

            // If the texture comes from MyProject/assets/models/textures, copy it into MyProject/assets/textures
            // and load from there.
            {
                std::error_code fsEc;
                std::filesystem::path srcPath(texFullPath);
                if (std::filesystem::exists(srcPath, fsEc) && std::filesystem::is_regular_file(srcPath, fsEc)) {
                    auto p = srcPath.parent_path();
                    if (p.filename() == "textures") {
                        auto models = p.parent_path();
                        if (models.filename() == "models") {
                            auto assets = models.parent_path();
                            if (assets.filename() == "assets") {
                                std::filesystem::path dstDir = assets / "textures";
                                std::filesystem::create_directories(dstDir, fsEc);

                                std::string modelStem = std::filesystem::path(item.basePath).stem().string();
                                std::filesystem::path dstBase = dstDir / (modelStem + "_" + srcPath.filename().string());

                                auto sameSize = [&](const std::filesystem::path& a, const std::filesystem::path& b) -> bool {
                                    std::error_code ecA;
                                    std::error_code ecB;
                                    auto sa = std::filesystem::file_size(a, ecA);
                                    auto sb = std::filesystem::file_size(b, ecB);
                                    return !ecA && !ecB && sa == sb;
                                };

                                std::filesystem::path dst = dstBase;
                                if (std::filesystem::exists(dst) && !sameSize(srcPath, dst)) {
                                    for (int v = 2; v < 1000; ++v) {
                                        std::filesystem::path cand = dstDir / (modelStem + "_" + srcPath.stem().string() + "_v" + std::to_string(v) + srcPath.extension().string());
                                        if (!std::filesystem::exists(cand) || sameSize(srcPath, cand)) {
                                            dst = cand;
                                            break;
                                        }
                                    }
                                }

                                if (!std::filesystem::exists(dst)) {
                                    std::error_code copyEc;
                                    std::filesystem::copy_file(srcPath, dst, std::filesystem::copy_options::skip_existing, copyEc);
                                }

                                if (std::filesystem::exists(dst)) {
                                    texFullPath = dst.lexically_normal().string();
                                }
                            }
                        }
                    }
                }
            }

            std::string cacheKey = texFullPath + ((colorSpace == AssetManager::TextureColorSpace::Linear) ? "#linear" : "#srgb");

            uint32_t slot = 0;
            if (auto it = m_TextureSlots.find(cacheKey); it != m_TextureSlots.end()) {
                slot = it->second;
            } else {
                auto tex = m_AssetManager->loadTexture(StringID(cacheKey), texFullPath, colorSpace);
                if (tex && tex->isValid()) {
                    slot = m_Renderer->bindTexture(tex->getImageView(), tex->getSampler());
                    if (slot != 0) {
                        m_TextureSlots[cacheKey] = slot;
                    }
                }
            }

            if (slot != 0) {
                useFlag = true;
                outIndex = static_cast<int32_t>(slot);
                outId = StringID(texFullPath);
                outPath = texFullPath;
            }
        };

        auto applyMeshToEntity = [&](entt::entity entity, MeshData& meshData) {
            if (meshData.vertexBuffer == VK_NULL_HANDLE || meshData.indexBuffer == VK_NULL_HANDLE) {
                ModelLoader::createBuffers(meshData, m_Renderer->getDevice(), m_Renderer->getPhysicalDevice(),
                    [](uint32_t typeFilter, VkMemoryPropertyFlags properties, VkPhysicalDeviceMemoryProperties* memProperties) -> uint32_t {
                        for (uint32_t i = 0; i < memProperties->memoryTypeCount; ++i) {
                            if ((typeFilter & (1 << i)) && (memProperties->memoryTypes[i].propertyFlags & properties) == properties) {
                                return i;
                            }
                        }
                        return uint32_t(~0);
                    });
            }

            auto& mesh = m_Scene->getRegistry().emplace<::Mesh>(entity);
            mesh.meshPath = item.basePath + "#" + meshData.name;
            mesh.vertexBuffer = meshData.vertexBuffer;
            mesh.indexBuffer = meshData.indexBuffer;
            mesh.vertexMemory = meshData.vertexMemory;
            mesh.indexMemory = meshData.indexMemory;
            mesh.vertexCount = meshData.vertexCount;
            mesh.indexCount = meshData.indexCount;

            // Compute local bounds while CPU vertices still exist.
            if (!meshData.vertices.empty()) {
                glm::vec3 bmin = meshData.vertices[0].pos;
                glm::vec3 bmax = meshData.vertices[0].pos;
                for (const auto& v : meshData.vertices) {
                    bmin = glm::min(bmin, v.pos);
                    bmax = glm::max(bmax, v.pos);
                }
                mesh.hasBounds = true;
                mesh.boundsMin = bmin;
                mesh.boundsMax = bmax;
            }

            meshData.vertexBuffer = VK_NULL_HANDLE;
            meshData.indexBuffer = VK_NULL_HANDLE;
            meshData.vertexMemory = VK_NULL_HANDLE;
            meshData.indexMemory = VK_NULL_HANDLE;
            meshData.ownerDevice = VK_NULL_HANDLE;

            ECS::MaterialComponent material;
            material.baseColor = meshData.baseColor;
            material.metallic = meshData.metallic;
            material.roughness = meshData.roughness;
            material.emissiveFactor = meshData.emissiveFactor;
            material.alphaCutoff = meshData.alphaCutoff;
            material.doubleSided = meshData.doubleSided;

            if (meshData.alphaMode == 1) material.alphaMode = ECS::MaterialComponent::AlphaMode::Mask;
            else if (meshData.alphaMode == 2) material.alphaMode = ECS::MaterialComponent::AlphaMode::Blend;
            else material.alphaMode = ECS::MaterialComponent::AlphaMode::Opaque;

            bindMaterialTexture(meshData.baseColorTexturePath, AssetManager::TextureColorSpace::SRGB,
                material.useAlbedoTexture, material.albedoTextureIndex, material.albedoTextureId, material.albedoTexturePath);
            bindMaterialTexture(meshData.normalTexturePath, AssetManager::TextureColorSpace::Linear,
                material.useNormalTexture, material.normalTextureIndex, material.normalTextureId, material.normalTexturePath);
            bindMaterialTexture(meshData.metallicRoughnessTexturePath, AssetManager::TextureColorSpace::Linear,
                material.useMetallicRoughnessTexture, material.metallicRoughnessTextureIndex, material.metallicRoughnessTextureId, material.metallicRoughnessTexturePath);
            bindMaterialTexture(meshData.aoTexturePath, AssetManager::TextureColorSpace::Linear,
                material.useAOTexture, material.aoTextureIndex, material.aoTextureId, material.aoTexturePath);
            bindMaterialTexture(meshData.emissiveTexturePath, AssetManager::TextureColorSpace::SRGB,
                material.useEmissiveTexture, material.emissiveTextureIndex, material.emissiveTextureId, material.emissiveTexturePath);

            m_Scene->getRegistry().emplace<ECS::MaterialComponent>(entity, material);
            meshData.freeCPUMemory();
        };

                std::unordered_map<std::string, int> childNameCounts;

        for (auto& meshData : item.modelData->meshes) {
            std::string baseName = meshData.name.empty() ? std::string("Mesh") : meshData.name;
            int& nameCount = childNameCounts[baseName];
            std::string entityName = baseName;
            if (nameCount > 0) {
                entityName = baseName + "_" + std::to_string(nameCount + 1);
            }
            nameCount++;

            auto entity = m_Scene->createEntity(entityName);

            if (item.isWorldChunk) {
                m_Scene->getRegistry().emplace_or_replace<WorldChunk>(entity, WorldChunk{item.worldCellKey, false});
            }

            if (m_Scene->getRegistry().all_of<ECS::SkeletonComponent>(rootEntity)) {
                ECS::SkinnedMeshComponent smc;
                smc.skeletonEntity = rootEntity;
                m_Scene->getRegistry().emplace_or_replace<ECS::SkinnedMeshComponent>(entity, smc);
            }

            // Skinned meshes stay at the model root transform; static meshes keep legacy pivot recenter.
            if (!m_Scene->getRegistry().all_of<ECS::SkinnedMeshComponent>(entity) &&
                m_Scene->getRegistry().all_of<Transform>(entity)) {
                m_Scene->getRegistry().get<Transform>(entity).position = meshData.pivotPosition;
            }

            applyMeshToEntity(entity, meshData);
            m_Scene->setParent(entity, rootEntity);
        }
    }
}

} // namespace Atlas

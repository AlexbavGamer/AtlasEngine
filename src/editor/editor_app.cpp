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
#include "../ui/ui_manager.h"
#include "../utils/camera_controller.h"
#include "../utils/model_loader.h"
#include "../ecs/ecs.h"
#include "../world/world_partition.h"
#include "../utils/frustum.h"

namespace Atlas {
using namespace ecs;

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

    auto cameraEntity = m_Scene->createEntity("Camera");
    m_Scene->getRegistry().emplace<Camera>(cameraEntity);
    auto& camera = m_Scene->getRegistry().get<Camera>(cameraEntity);
    camera.position = glm::vec3(0.0f, 2.0f, 5.0f);
    camera.target = glm::vec3(0.0f, 0.0f, 0.0f);

    m_CameraController = std::make_unique<::CameraController>(
        m_Window->getGLFWWindow(),
        camera.position,
        camera.target,
        camera.up);
    m_UIManager->setCameraController(m_CameraController.get());

    AsyncLoader::getInstance().init();

    m_UIManager->setOnAssetDropped([this](const std::string& assetPath) { queueModelImport(assetPath); });
    m_Renderer->setResizeCallback([this](int, int) { m_Viewport.refreshTexture(); });
    m_Window->setResizeCallback([this](int, int) { m_Renderer->recreateSwapChain(); });
    m_Renderer->setRenderCallback([this](VkCommandBuffer commandBuffer) { m_ImGuiManager->render(commandBuffer); });
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

void EditorApp::queueModelImportAt(const std::string& assetPath, const glm::vec3& rootPosition, bool isWorldChunk, uint64_t cellKey) {
    std::string fullPath = assetPath;

    // Normalize "@"-prefixed paths (Content Explorer style) and handle Windows slashes.
    if (!fullPath.empty() && fullPath[0] == '@') {
        fullPath.erase(fullPath.begin());
    }
    std::replace(fullPath.begin(), fullPath.end(), '\\', '/');

    // If this already resolves to an existing file, keep it.
    {
        std::error_code ec;
        std::filesystem::path p(fullPath);
        if (!p.empty() && std::filesystem::exists(p, ec)) {
            fullPath = std::filesystem::absolute(p, ec).lexically_normal().string();
        } else if (m_ProjectManager && m_ProjectManager->hasProject()) {
            // Accept "MyProject/assets/..." and "assets/..." as input and map to project assets.
            const std::string assetsPrefix = m_ProjectManager->getAssetsPath() + "/";

            std::string rel = fullPath;
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
                fullPath = m_ProjectManager->getAssetFullPath(rel);
            }
        }
    }
    std::filesystem::path fsPath(fullPath);
    std::string ext = fsPath.extension().string();
    if (ext != ".fbx" && ext != ".gltf" && ext != ".glb" && ext != ".obj" && ext != ".dae") {
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

    std::string modelName = fsPath.stem().string();
    auto tempEntity = m_Scene->createEntity(modelName + " [Loading...]");
    if (m_Scene->getRegistry().all_of<Transform>(tempEntity)) {
        m_Scene->getRegistry().get<Transform>(tempEntity).position = rootPosition;
    }

    if (isWorldChunk) {
        m_WorldCellRoots[cellKey] = tempEntity;
        m_Scene->getRegistry().emplace_or_replace<WorldChunk>(tempEntity, WorldChunk{cellKey, true});
    }

    if (isWorldChunk) {
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
        [this, modelName, fullPath, tempEntity, rootPosition, isWorldChunk, cellKey](AsyncLoader::LoadResult<::ModelData> result) {
            std::lock_guard<std::mutex> lock(m_PendingModelsMutex);
            PendingModel p;
            p.modelData = result.data;
            p.modelName = modelName;
            p.basePath = fullPath;
            p.placeholderEntity = tempEntity;
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

void EditorApp::updateWorldStreaming() {
    if (!m_WorldStreamingEnabled) return;
    if (!m_ProjectManager || !m_ProjectManager->hasProject()) return;
    if (!m_Scene || !m_Renderer) return;

    const std::string chunksDir = m_ProjectManager->getAssetsPath() + "/" + m_WorldChunksSubdir;

    glm::vec3 camPos(0.0f);
    glm::mat4 view(1.0f);
    glm::mat4 proj(1.0f);

    // Use scene camera if present.
    {
        auto& registry = m_Scene->getRegistry();
        auto camView = registry.view<Camera>();
        if (!camView.empty()) {
            auto camEnt = camView[0];
            auto& cam = registry.get<Camera>(camEnt);
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

        glm::mat4 world = m_Scene->getWorldTransform(e);
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

        if (m_CameraController && !m_UIManager->isGizmoUsing() && m_UIManager->allowViewportCameraInput()) {
            m_CameraController->update(deltaTime);
        }

        m_ImGuiManager->newFrame();
        ImGuizmo::BeginFrame();

        if (m_UIManager && m_Scene) {
            auto& registry = m_Scene->getRegistry();
            auto camView = registry.view<Camera>();
            if (!camView.empty()) {
                auto camEnt = camView[0];
                auto& cam = registry.get<Camera>(camEnt);
                VkExtent2D extent = m_Renderer ? m_Renderer->getSwapChainExtent() : VkExtent2D{1280, 720};
                if (extent.width > 0 && extent.height > 0) {
                    cam.aspectRatio = static_cast<float>(extent.width) / static_cast<float>(extent.height);
                }
                m_UIManager->setCameraMatrices(cam.getViewMatrix(), cam.getProjectionMatrix());
            } else if (m_CameraController) {
                m_UIManager->setCameraMatrices(m_CameraController->getViewMatrix(), m_CameraController->getProjMatrix());
            }
        }

        m_UIManager->render(m_Viewport.getTextureId());

        // World streaming controls (debug)
        ImGui::Begin("World Streaming");
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
        ImGui::End();

        processPendingModels();

        if (m_UIManager && m_Renderer) {
            const auto& selected = m_UIManager->getSelectedEntities();
            std::vector<uint32_t> selectedIds;
            selectedIds.reserve(selected.size());
            for (auto e : selected) {
                if (e != entt::null) {
                    selectedIds.push_back(static_cast<uint32_t>(e));
                }
            }
            m_Renderer->setSelectedEntityIds(selectedIds);
        }

        updateWorldStreaming();

        m_Renderer->renderScene(m_Scene.get());

        uint32_t pickX = 0;
        uint32_t pickY = 0;
        bool pickAdditive = false;
        if (m_UIManager && m_Renderer && m_UIManager->popViewportPickRequest(pickX, pickY, pickAdditive)) {
            uint32_t pickedId = m_Renderer->pickEntityId(pickX, pickY);
            if (pickedId == UINT32_MAX) {
                if (!pickAdditive) {
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

            // Restore legacy import behavior: mesh is pretransformed and recentered around a pivot.
            if (m_Scene->getRegistry().all_of<Transform>(entity)) {
                m_Scene->getRegistry().get<Transform>(entity).position = meshData.pivotPosition;
            }

            applyMeshToEntity(entity, meshData);
            m_Scene->setParent(entity, rootEntity);
        }
    }
}

} // namespace Atlas

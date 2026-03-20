#include "editor_app.h"

#include <filesystem>
#include <iostream>
#include <chrono>
#include <thread>

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

namespace Atlas {
using namespace ecs;

EditorApp::EditorApp() {
    m_Window = std::make_unique<Window>(1280, 720, "Atlas Engine");
    m_Renderer = std::make_unique<Renderer>(m_Window.get());
    m_Renderer->init();

    m_AssetManager = std::make_unique<AssetManager>();
    m_AssetManager->setRenderer(m_Renderer.get());

    m_Scene = std::make_unique<Scene>();
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
    m_Viewport.releaseTexture();
    if (m_ImGuiManager) {
        m_ImGuiManager->cleanup(m_Renderer->getDevice());
    }
    if (m_AssetManager) {
        m_AssetManager->shutdown();
    }
    if (m_Renderer) {
        m_Renderer->shutdown();
    }
    if (m_Window) {
        glfwDestroyWindow(static_cast<GLFWwindow*>(m_Window->getNativeWindow()));
    }
}

void EditorApp::queueModelImport(const std::string& assetPath) {
    std::string fullPath = m_ProjectManager ? m_ProjectManager->getAssetFullPath(assetPath) : assetPath;
    std::filesystem::path fsPath(fullPath);
    std::string ext = fsPath.extension().string();
    if (ext != ".fbx" && ext != ".gltf" && ext != ".glb" && ext != ".obj" && ext != ".dae") {
        return;
    }

    std::string modelName = fsPath.filename().string();
    MeshData placeholderData = ModelLoader::createCube(0.1f, m_Renderer->getDevice(), m_Renderer->getPhysicalDevice(),
        [](uint32_t typeFilter, VkMemoryPropertyFlags properties, VkPhysicalDeviceMemoryProperties* memProperties) -> uint32_t {
            for (uint32_t i = 0; i < memProperties->memoryTypeCount; ++i) {
                if ((typeFilter & (1 << i)) && (memProperties->memoryTypes[i].propertyFlags & properties) == properties) {
                    return i;
                }
            }
            return uint32_t(~0);
        });

    auto tempEntity = m_Scene->createEntity(modelName + " [Loading...]");
    auto& tempMesh = m_Scene->getRegistry().emplace<::Mesh>(tempEntity);
    tempMesh.vertexBuffer = placeholderData.vertexBuffer;
    tempMesh.indexBuffer = placeholderData.indexBuffer;
    tempMesh.vertexMemory = placeholderData.vertexMemory;
    tempMesh.indexMemory = placeholderData.indexMemory;
    tempMesh.vertexCount = placeholderData.vertexCount;
    tempMesh.indexCount = placeholderData.indexCount;
    tempMesh.meshPath = fullPath;

    placeholderData.vertexBuffer = VK_NULL_HANDLE;
    placeholderData.indexBuffer = VK_NULL_HANDLE;
    placeholderData.vertexMemory = VK_NULL_HANDLE;
    placeholderData.indexMemory = VK_NULL_HANDLE;
    placeholderData.ownerDevice = VK_NULL_HANDLE;

    auto modelDataPtr = std::make_shared<::ModelData>();
    AsyncLoader::getInstance().loadModelAsync<::ModelData>(
        fullPath,
        [fullPath, modelDataPtr]() -> std::shared_ptr<::ModelData> {
            PROFILE_SCOPE("ModelLoad");
            ModelLoader::loadModelMultiMesh(fullPath, VK_NULL_HANDLE, VK_NULL_HANDLE, nullptr, modelDataPtr.get(), false);
            return modelDataPtr;
        },
        [this, modelName, fullPath, tempEntity](AsyncLoader::LoadResult<::ModelData> result) {
            if (!result.success || !result.data) {
                std::cerr << "Async model load failed: " << result.error << std::endl;
                return;
            }
            std::lock_guard<std::mutex> lock(m_PendingModelsMutex);
            m_PendingModels.push_back(PendingModel{result.data, modelName, fullPath, tempEntity});
        });
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

        if (m_CameraController && !m_UIManager->isGizmoUsing()) {
            m_CameraController->update(deltaTime);
        }

        m_ImGuiManager->newFrame();
        ImGuizmo::BeginFrame();

        if (m_CameraController) {
            m_UIManager->setCameraMatrices(m_CameraController->getViewMatrix(), m_CameraController->getProjMatrix());
        }

        m_UIManager->render(m_Viewport.getTextureId());
        processPendingModels();
        m_Renderer->renderScene(m_Scene.get());
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
    std::lock_guard<std::mutex> lock(m_PendingModelsMutex);
    for (auto& item : m_PendingModels) {
        if (!item.modelData || !m_Scene) {
            continue;
        }

        if (m_Scene->getRegistry().valid(item.placeholderEntity)) {
            auto& placeholderMesh = m_Scene->getRegistry().get<::Mesh>(item.placeholderEntity);
            if (placeholderMesh.vertexBuffer) vkDestroyBuffer(m_Renderer->getDevice(), placeholderMesh.vertexBuffer, nullptr);
            if (placeholderMesh.indexBuffer) vkDestroyBuffer(m_Renderer->getDevice(), placeholderMesh.indexBuffer, nullptr);
            if (placeholderMesh.vertexMemory) vkFreeMemory(m_Renderer->getDevice(), placeholderMesh.vertexMemory, nullptr);
            if (placeholderMesh.indexMemory) vkFreeMemory(m_Renderer->getDevice(), placeholderMesh.indexMemory, nullptr);
            m_Scene->getRegistry().destroy(item.placeholderEntity);
        }

        auto rootEntity = m_Scene->createEntity(item.modelName);
        if (!m_Scene->hasTransform(rootEntity)) {
            m_Scene->getRegistry().emplace<Transform>(rootEntity);
        }

        for (auto& meshData : item.modelData->meshes) {
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

            auto entity = m_Scene->createEntity(item.modelName + "_" + meshData.name);
            auto& mesh = m_Scene->getRegistry().emplace<::Mesh>(entity);
            mesh.meshPath = item.basePath + "#" + meshData.name;
            mesh.vertexBuffer = meshData.vertexBuffer;
            mesh.indexBuffer = meshData.indexBuffer;
            mesh.vertexMemory = meshData.vertexMemory;
            mesh.indexMemory = meshData.indexMemory;
            mesh.vertexCount = meshData.vertexCount;
            mesh.indexCount = meshData.indexCount;

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
                std::string texFullPath = texPath.string();

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
            m_Scene->setParent(entity, rootEntity);
        }
    }
    m_PendingModels.clear();
}

}

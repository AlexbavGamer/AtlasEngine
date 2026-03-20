#include <iostream>
#include <stdexcept>

#include "platform/window.h"
#include "renderer/renderer.h"
#include "assets/asset_manager.h"
#include "scene/scene.h"
#include "imgui/imgui_manager.h"
#include <imgui.h>
#include <imgui_impl_vulkan.h>
#include <ImGuizmo.h>
#include "ui/ui_manager.h"
#include "project/project_manager.h"
#include "utils/camera_controller.h"
#include "ecs/ecs.h"
#include "core/threading/async_loader.h"
#include "core/profiler.h"
#include <mutex>
#include <unordered_map>
#include <filesystem>

using namespace ecs;
#include "ecs/vertex.h"
#include "utils/model_loader.h"

#ifdef NDEBUG
const bool enableValidationLayers = false;
#else
const bool enableValidationLayers = true;
#endif

namespace Atlas
{

    class Editor
    {
    public:
        Editor()
        {
            m_Window = std::make_unique<Window>(1280, 720, "Atlas Engine");
            m_Renderer = std::make_unique<Renderer>(m_Window.get());
            m_Renderer->init();

            m_AssetManager = std::make_unique<AssetManager>();
            m_AssetManager->setRenderer(m_Renderer.get());

            m_Scene = std::make_unique<Scene>();

            m_ImGuiManager.init(
                m_Renderer->getInstance(),
                m_Renderer->getPhysicalDevice(),
                m_Renderer->getDevice(),
                m_Renderer->getGraphicsQueue(),
                m_Renderer->getGraphicsQueueFamily(),
                m_Renderer->getRenderPass(),
                m_Window->getGLFWWindow(),
                m_Renderer->getSwapChainImageCount());

            // Add viewport texture (required for ImGui)
            m_ViewportTexture = (ImTextureID)ImGui_ImplVulkan_AddTexture(
                m_Renderer->getOffscreenSampler(),
                m_Renderer->getOffscreenImageView(),
                VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);

            m_UIManager = std::make_unique<UIManager>(m_Scene.get());
            m_UIManager->setWindow(m_Window->getGLFWWindow());
            m_UIManager->setRenderer(m_Renderer.get());
            m_UIManager->setAssetManager(m_AssetManager.get());

            m_ProjectManager = std::make_unique<ProjectManager>();
            m_UIManager->setProjectManager(m_ProjectManager.get());

            // Não abrir o projeto por padrão para evitar erros de caminho em diferentes máquinas
            m_ProjectManager->openProject("MyProject");

            // Create camera
            auto cameraEntity = m_Scene->createEntity("Camera");
            m_Scene->getRegistry().emplace<Camera>(cameraEntity);

            auto &camera = m_Scene->getRegistry().get<Camera>(cameraEntity);
            camera.position = glm::vec3(0.0f, 2.0f, 5.0f);
            camera.target = glm::vec3(0.0f, 0.0f, 0.0f);

            // Procedural cube disabled - use FBX import instead
            // auto cubeEntity = m_Scene->createEntity("Cube");
            // m_Scene->getRegistry().emplace<::Mesh>(cubeEntity);
            // auto &mesh = m_Scene->getRegistry().get<::Mesh>(cubeEntity);
            // mesh.meshPath = "[procedural]";
            // MeshData meshData = ModelLoader::createCube(1.0f,
            //                                            m_Renderer->getDevice(),
            //                                            m_Renderer->getPhysicalDevice(),
            //                                            [](uint32_t typeFilter, VkMemoryPropertyFlags properties, VkPhysicalDeviceMemoryProperties *memProperties) -> uint32_t
            //                                            {
            //                                                for (uint32_t i = 0; i < memProperties->memoryTypeCount; i++)
            //                                                {
            //                                                    if ((typeFilter & (1 << i)) && (memProperties->memoryTypes[i].propertyFlags & properties) == properties)
            //                                                    {
            //                                                        return i;
            //                                                    }
            //                                                }
            //                                                return uint32_t(~0);
            //                                            });

            // mesh.vertexBuffer = meshData.vertexBuffer;
            // mesh.indexBuffer = meshData.indexBuffer;
            // mesh.vertexMemory = meshData.vertexMemory;
            // mesh.indexMemory = meshData.indexMemory;
            // mesh.vertexCount = static_cast<uint32_t>(meshData.vertices.size());
            // mesh.indexCount = meshData.indexCount;

            m_CameraController = std::make_unique<CameraController>(
                m_Window->getGLFWWindow(),
                camera.position,
                camera.target,
                camera.up);
            
            m_UIManager->setCameraController(m_CameraController.get());

            // Initialize async loader
            Atlas::AsyncLoader::getInstance().init();
            
            // Setup asset drop callback - ASYNC LOADING
            m_UIManager->setOnAssetDropped([this](const std::string &assetPath) {
                std::string fullPath = m_ProjectManager->getAssetFullPath(assetPath);
                std::filesystem::path fsPath(fullPath);
                std::string ext = fsPath.extension().string();
                
                if (ext == ".fbx" || ext == ".gltf" || ext == ".glb" || ext == ".obj" || ext == ".dae") {
                    std::string modelName = fsPath.filename().string();
                    std::string basePath = fullPath;
                    
                    auto findMemoryType = [](uint32_t typeFilter, VkMemoryPropertyFlags properties, VkPhysicalDeviceMemoryProperties* memProperties) -> uint32_t {
                        for (uint32_t i = 0; i < memProperties->memoryTypeCount; i++) {
                            if ((typeFilter & (1 << i)) && (memProperties->memoryTypes[i].propertyFlags & properties) == properties) {
                                return i;
                            }
                        }
                        return uint32_t(~0);
                    };
                    
                    // Create placeholder while loading
                    MeshData placeholderData = ModelLoader::createCube(0.1f, m_Renderer->getDevice(), m_Renderer->getPhysicalDevice(), findMemoryType);
                    
                    auto tempEntity = m_Scene->createEntity(modelName + " [Loading...]");
                    auto& tempMesh = m_Scene->getRegistry().emplace<::Mesh>(tempEntity);
                    tempMesh.vertexBuffer = placeholderData.vertexBuffer;
                    tempMesh.indexBuffer = placeholderData.indexBuffer;
                    tempMesh.vertexMemory = placeholderData.vertexMemory;
                    tempMesh.vertexCount = placeholderData.vertices.size();
                    tempMesh.indexCount = placeholderData.indexCount;
                    tempMesh.meshPath = basePath;
                    
                    std::cout << "Started loading model (async): " << basePath << std::endl;

                    std::shared_ptr<ModelData> modelDataPtr = std::make_shared<ModelData>();

                    Atlas::AsyncLoader::getInstance().loadModelAsync<ModelData>(
                        basePath,
                        [basePath, modelDataPtr]() -> std::shared_ptr<ModelData> {
                            PROFILE_SCOPE("ModelLoad");
                            ModelLoader::loadModelMultiMesh(
                                basePath,
                                VK_NULL_HANDLE,
                                VK_NULL_HANDLE,
                                nullptr,
                                modelDataPtr.get(),
                                false);
                            return modelDataPtr;
                        },
                        [this, modelName, basePath, tempEntity](Atlas::AsyncLoader::LoadResult<ModelData> result) {
                            if (result.success && result.data) {
                                PendingModel pending;
                                pending.modelData = result.data;
                                pending.modelName = modelName;
                                pending.basePath = basePath;
                                pending.placeholderEntity = tempEntity;

                                std::lock_guard<std::mutex> lock(m_PendingModelsMutex);
                                m_PendingModels.push_back(std::move(pending));

                                std::cout << "Async model data ready: " << basePath << std::endl;
                            } else {
                                std::cerr << "Async model load failed: " << result.error << std::endl;
                            }
                        });

                    std::cout << "Model loading dispatched, continuing main loop..." << std::endl;
                    std::cout.flush();

                    // We keep placeholder visible until model load completes.
                    // No immediate scene entity creation here.

                    // Skip synchronous creation block.
                    // The pending model callback will handle adding entities in processPendingModels().

                }
            });

            // Setup window resize callback
            m_Window->setResizeCallback([this](int width, int height)
            {
                m_Renderer->recreateSwapChain();
                // Recreate viewport texture after swapchain recreation
                m_ViewportTexture = (ImTextureID)ImGui_ImplVulkan_AddTexture(
                    m_Renderer->getOffscreenSampler(),
                    m_Renderer->getOffscreenImageView(),
                    VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL
                );
            });

            m_Renderer->setRenderCallback([this](VkCommandBuffer commandBuffer)
                                          { m_ImGuiManager.render(commandBuffer); });
        }

        ~Editor()
        {
            Atlas::AsyncLoader::getInstance().shutdown();
            m_ImGuiManager.cleanup(m_Renderer->getDevice());
            if (m_AssetManager) {
                m_AssetManager->shutdown();
            }
            m_Renderer->shutdown();
            glfwDestroyWindow(static_cast<GLFWwindow*>(m_Window->getNativeWindow()));
        }

        void run()
        {
            ImGuiIO &io = ImGui::GetIO();
            io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;

            double lastTime = 0.0;

            while (!m_Window->shouldClose())
            {
                double currentTime = glfwGetTime();
                float deltaTime = static_cast<float>(currentTime - lastTime);
                lastTime = currentTime;

                m_Window->update();

                // Update profile counters
                m_UIManager->updateProfiler(deltaTime);

                // Update camera first
                if (m_CameraController && !m_UIManager->isGizmoUsing())
                {
                    m_CameraController->update(deltaTime);
                }

                // Render
                m_ImGuiManager.newFrame();
                ImGuizmo::BeginFrame();
                
                // Update camera matrices for ImGuizmo AFTER updating camera
                if (m_CameraController) {
                    m_UIManager->setCameraMatrices(
                        m_CameraController->getViewMatrix(),
                        m_CameraController->getProjMatrix()
                    );
                }
                
                // Render UI (includes gizmo)
                m_UIManager->render(m_ViewportTexture);

                processPendingModels();
                m_Renderer->renderScene(m_Scene.get());
                m_Renderer->endFrame();
            }

            vkDeviceWaitIdle(m_Renderer->getDevice());
        }

        void processPendingModels() {
            std::lock_guard<std::mutex> lock(m_PendingModelsMutex);
            if (m_PendingModels.empty()) {
                return;
            }

            auto findMemoryType = [](uint32_t typeFilter, VkMemoryPropertyFlags properties, VkPhysicalDeviceMemoryProperties* memProperties) -> uint32_t {
                for (uint32_t i = 0; i < memProperties->memoryTypeCount; i++) {
                    if ((typeFilter & (1 << i)) && (memProperties->memoryTypes[i].propertyFlags & properties) == properties) {
                        return i;
                    }
                }
                return uint32_t(~0);
            };

            for (auto& item : m_PendingModels) {
                if (item.modelData && m_Scene) {
                    if (m_Scene->getRegistry().valid(item.placeholderEntity)) {
                        m_Scene->getRegistry().destroy(item.placeholderEntity);
                    }

                    // Create root entity for model (container node, not renderable)
                    auto rootEntity = m_Scene->createEntity(item.modelName);

                    // Ensure root has transform for hierarchy manipulation.
                    if (!m_Scene->hasTransform(rootEntity)) {
                        m_Scene->getRegistry().emplace<Transform>(rootEntity);
                    }
                    auto& rootTransform = m_Scene->getTransform(rootEntity);
                    rootTransform.position = glm::vec3(0.0f);
                    rootTransform.rotation = glm::vec3(0.0f);
                    rootTransform.scale = glm::vec3(1.0f);

                    // Root should be a container, not an actual renderable mesh.
                    if (m_Scene->getRegistry().all_of<Renderable>(rootEntity)) {
                        m_Scene->getRegistry().remove<Renderable>(rootEntity);
                    }
                    if (m_Scene->getRegistry().all_of<::Mesh>(rootEntity)) {
                        m_Scene->getRegistry().remove<Mesh>(rootEntity);
                    }

                    // Ensure buffers exist on render thread context
                    for (auto& meshData : item.modelData->meshes) {
                        std::cout << "  [DEBUG] Pending mesh " << meshData.name
                                  << " verts=" << meshData.vertices.size()
                                  << " idx=" << meshData.indices.size()
                                  << " vb=" << meshData.vertexBuffer
                                  << " ib=" << meshData.indexBuffer << std::endl;
                        if (meshData.vertexBuffer == VK_NULL_HANDLE || meshData.indexBuffer == VK_NULL_HANDLE) {
                            ModelLoader::createBuffers(meshData, m_Renderer->getDevice(), m_Renderer->getPhysicalDevice(), findMemoryType);
                        }

                        auto entity = m_Scene->createEntity(item.modelName + "_" + meshData.name);
                        auto& mesh = m_Scene->getRegistry().emplace<::Mesh>(entity);
                        mesh.meshPath = item.basePath + "#" + meshData.name;
                        mesh.vertexBuffer = meshData.vertexBuffer;
                        mesh.indexBuffer = meshData.indexBuffer;
                        mesh.vertexMemory = meshData.vertexMemory;
                        mesh.indexMemory = meshData.indexMemory;
                        mesh.vertexCount = static_cast<uint32_t>(meshData.vertices.size());
                        mesh.indexCount = meshData.indexCount;

                        if (m_AssetManager && m_Renderer) {
                            ECS::MaterialComponent material;
                            material.baseColor = meshData.baseColor;
                            material.metallic = meshData.metallic;
                            material.roughness = meshData.roughness;

                            if (!meshData.baseColorTexturePath.empty()) {
                                std::filesystem::path modelDir = std::filesystem::path(item.basePath).parent_path();
                                std::filesystem::path texPath = (modelDir / meshData.baseColorTexturePath).lexically_normal();
                                std::string texFullPath = texPath.string();

                                std::cout << "[Import] mesh=" << meshData.name
                                          << " baseColorTexRel=" << meshData.baseColorTexturePath
                                          << " resolved=" << texFullPath << std::endl;

                                uint32_t slot = 0;
                                if (auto it = m_TextureSlots.find(texFullPath); it != m_TextureSlots.end()) {
                                    slot = it->second;
                                } else {
                                    auto tex = m_AssetManager->loadTexture(StringID(texFullPath), texFullPath);
                                    if (tex && tex->isValid()) {
                                        slot = m_Renderer->bindTexture(tex->getImageView(), tex->getSampler());
                                        if (slot != 0) {
                                            m_TextureSlots[texFullPath] = slot;
                                        }
                                    }
                                }

                                if (slot != 0) {
                                    material.useAlbedoTexture = true;
                                    material.albedoTextureIndex = static_cast<int32_t>(slot);
                                    material.albedoTextureId = StringID(texFullPath);
                                    material.albedoTexturePath = texFullPath;
                                }
                            } else {
                                std::cout << "[Import] mesh=" << meshData.name << " has no baseColorTexture" << std::endl;
                            }

                            m_Scene->getRegistry().emplace<ECS::MaterialComponent>(entity, material);
                        }

                        meshData.freeCPUMemory();
                        m_Scene->setParent(entity, rootEntity);
                    }
                }
            }

            m_PendingModels.clear();
        }

    private:
        struct PendingModel {
            std::shared_ptr<ModelData> modelData;
            std::string modelName;
            std::string basePath;
            entt::entity placeholderEntity = entt::null;
        };

        std::unique_ptr<Window> m_Window;
        std::unique_ptr<Renderer> m_Renderer;
        std::unique_ptr<AssetManager> m_AssetManager;
        std::unique_ptr<Scene> m_Scene;
        ImGuiManager m_ImGuiManager;
        std::unique_ptr<UIManager> m_UIManager;
        std::unique_ptr<ProjectManager> m_ProjectManager;
        std::unique_ptr<CameraController> m_CameraController;
        ImTextureID m_ViewportTexture = 0;

        std::unordered_map<std::string, uint32_t> m_TextureSlots;

        std::vector<PendingModel> m_PendingModels;
        std::mutex m_PendingModelsMutex;
    };

}

int main()
{
    try
    {
        Atlas::Editor editor;
        editor.run();
    }
    catch (const std::exception &e)
    {
        std::cerr << e.what() << std::endl;
        return EXIT_FAILURE;
    }
    return EXIT_SUCCESS;
}
#include <iostream>
#include <stdexcept>

#include "platform/window.h"
#include "renderer/renderer.h"
#include "scene/scene.h"
#include "imgui/imgui_manager.h"
#include <imgui_impl_vulkan.h>
#include "ui/ui_manager.h"
#include "project/project_manager.h"
#include "utils/camera_controller.h"
#include "ecs/ecs.h"

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

            m_ProjectManager = std::make_unique<ProjectManager>();
            m_UIManager->setProjectManager(m_ProjectManager.get());

            // Não abrir o projeto por padrão para evitar erros de caminho em diferentes máquinas
            // m_ProjectManager->openProject(".");

            // Create camera
            auto cameraEntity = m_Scene->createEntity("Camera");
            m_Scene->getRegistry().emplace<Camera>(cameraEntity);

            auto &camera = m_Scene->getRegistry().get<Camera>(cameraEntity);
            camera.position = glm::vec3(0.0f, 2.0f, 5.0f);
            camera.target = glm::vec3(0.0f, 0.0f, 0.0f);

            // Create a cube (procedural - correct winding order for Vulkan)
            auto cubeEntity = m_Scene->createEntity("Cube");
            m_Scene->getRegistry().emplace<Mesh>(cubeEntity);
            auto &mesh = m_Scene->getRegistry().get<Mesh>(cubeEntity);
            mesh.meshPath = "[procedural]";
            MeshData meshData = ModelLoader::createCube(1.0f,
                                                       m_Renderer->getDevice(),
                                                       m_Renderer->getPhysicalDevice(),
                                                       [](uint32_t typeFilter, VkMemoryPropertyFlags properties, VkPhysicalDeviceMemoryProperties *memProperties) -> uint32_t
                                                       {
                                                           for (uint32_t i = 0; i < memProperties->memoryTypeCount; i++)
                                                           {
                                                               if ((typeFilter & (1 << i)) && (memProperties->memoryTypes[i].propertyFlags & properties) == properties)
                                                               {
                                                                   return i;
                                                               }
                                                           }
                                                           return uint32_t(~0);
                                                       });

            mesh.vertexBuffer = meshData.vertexBuffer;
            mesh.indexBuffer = meshData.indexBuffer;
            mesh.vertexMemory = meshData.vertexMemory;
            mesh.indexMemory = meshData.indexMemory;
            mesh.vertexCount = static_cast<uint32_t>(meshData.vertices.size());
            mesh.indexCount = meshData.indexCount;

            m_CameraController = std::make_unique<CameraController>(
                m_Window->getGLFWWindow(),
                camera.position,
                camera.target,
                camera.up);

            // Setup asset drop callback
            m_UIManager->setOnAssetDropped([this](const std::string &assetPath)
                                           {
            std::string fullPath = m_ProjectManager->getAssetFullPath(assetPath);
            std::filesystem::path fsPath(fullPath);
            std::string ext = fsPath.extension().string();
            
            if (ext == ".fbx" || ext == ".gltf" || ext == ".glb" || ext == ".obj" || ext == ".dae") {
                auto entity = m_Scene->createEntity(fsPath.filename().string());
                auto& mesh = m_Scene->getRegistry().emplace<Mesh>(entity);
                mesh.meshPath = fullPath;
                
                MeshData meshData = ModelLoader::loadModel(fullPath, 
                    m_Renderer->getDevice(), 
                    m_Renderer->getPhysicalDevice(),
                    [](uint32_t typeFilter, VkMemoryPropertyFlags properties, VkPhysicalDeviceMemoryProperties* memProperties) -> uint32_t {
                        for (uint32_t i = 0; i < memProperties->memoryTypeCount; i++) {
                            if ((typeFilter & (1 << i)) && (memProperties->memoryTypes[i].propertyFlags & properties) == properties) {
                                return i;
                            }
                        }
                        return uint32_t(~0);
                    });
                
                mesh.vertexBuffer = meshData.vertexBuffer;
                mesh.indexBuffer = meshData.indexBuffer;
                mesh.vertexMemory = meshData.vertexMemory;
                mesh.indexMemory = meshData.indexMemory;
                mesh.vertexCount = static_cast<uint32_t>(meshData.vertices.size());
                mesh.indexCount = meshData.indexCount;
                
                std::cout << "Created entity from asset: " << fullPath << std::endl;
            } });

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
            m_ImGuiManager.cleanup(m_Renderer->getDevice());
            m_Renderer->shutdown();
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

                // Update camera
                if (m_CameraController)
                {
                    m_CameraController->update(deltaTime);
                }

                // Render
                m_ImGuiManager.newFrame();
                m_UIManager->render(m_ViewportTexture);

                m_Renderer->renderScene(m_Scene.get());
                m_Renderer->endFrame();
            }

            vkDeviceWaitIdle(m_Renderer->getDevice());
        }

    private:
        std::unique_ptr<Window> m_Window;
        std::unique_ptr<Renderer> m_Renderer;
        std::unique_ptr<Scene> m_Scene;
        ImGuiManager m_ImGuiManager;
        std::unique_ptr<UIManager> m_UIManager;
        std::unique_ptr<ProjectManager> m_ProjectManager;
        std::unique_ptr<CameraController> m_CameraController;
        ImTextureID m_ViewportTexture = 0;
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
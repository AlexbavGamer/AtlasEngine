#include "editor_app.h"
#include "world_streaming_layer.h"
#include "hlod_viewer_layer.h"

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
#include <cstdlib>

#include <ImGuizmo.h>
#include <imgui.h>

#include "../assets/asset_manager.h"
#include "../core/profiler.h"
#include "../core/threading/async_loader.h"
#include "../export/package_manifest.h"
#include "../imgui/imgui_manager.h"
#include "../platform/window.h"
#include "../project/project_manager.h"
#include "../renderer/renderer.h"
#include "../scene/scene.h"
#include "../scene/scene_serializer.h"
#include "../physics/physics_system.h"
#include "../scripting/script_engine.h"
#include "../ui/ui_manager.h"
#include "../utils/camera_controller.h"
#include "../utils/model_loader.h"
#include "../utils/primitive_helpers.h"
#include "../ecs/ecs.h"
#include "../world/world_partition.h"
#include "../world/culling.h"
#include "../world/hlod.h"
#include "../world/lod.h"
#include "../utils/frustum.h"

#ifdef _WIN32
#include <windows.h>
#endif

namespace Atlas {

namespace {
std::filesystem::path getCurrentExecutablePath() {
#ifdef _WIN32
    char buffer[MAX_PATH] = {};
    DWORD len = GetModuleFileNameA(nullptr, buffer, MAX_PATH);
    if (len > 0 && len < MAX_PATH) {
        return std::filesystem::path(std::string(buffer, len));
    }
    return std::filesystem::current_path() / "AtlasEngine.exe";
#else
    std::error_code ec;
    auto exe = std::filesystem::read_symlink("/proc/self/exe", ec);
    if (!ec) {
        return exe;
    }
    return std::filesystem::current_path() / "AtlasEngine";
#endif
}

bool isPrimitiveMeshPath(const std::string& meshPath) {
    return meshPath.rfind("primitive://", 0) == 0;
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

EditorApp::EditorApp()
try {
    m_InitStep = "window";
    m_Window = std::make_unique<Window>(1280, 720, "Atlas Engine");
    m_InitStep = "renderer";
    m_Renderer = std::make_unique<Renderer>(m_Window.get());
    m_Renderer->init();

    m_InitStep = "assets/scene/world";
    m_AssetManager = std::make_unique<AssetManager>();
    m_AssetManager->setRenderer(m_Renderer.get());

    m_Scene = std::make_unique<Scene>();
    m_Scene->getRegistry().on_destroy<::Mesh>().connect<&EditorApp::onMeshDestroyed>(this);
    m_WorldPartition = std::make_unique<WorldPartition>(m_Scene.get());
    m_CullingPipeline = std::make_unique<CullingPipeline>(m_CullingConfig);
    m_HLODSystem = std::make_unique<HLODSystem>(m_HLODConfig);
    m_CityScene = m_Scene.get();

    m_InitStep = "imgui/viewport";
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

    m_InitStep = "ui/project";
    m_UIManager = std::make_unique<::UIManager>(m_Scene.get());
    m_UIManager->setWindow(m_Window->getGLFWWindow());
    m_UIManager->setRenderer(m_Renderer.get());
    m_UIManager->setAssetManager(m_AssetManager.get());

    m_ProjectManager = std::make_unique<::ProjectManager>();
    m_UIManager->setProjectManager(m_ProjectManager.get());

    m_InitStep = "physics/scripting";
    m_PhysicsSystem = std::make_unique<Atlas::Physics::PhysicsSystem>();
    m_PhysicsSystem->initialize();

    m_ScriptEngine = std::make_unique<Atlas::Scripting::ScriptEngine>();
    m_ScriptEngine->setWindow(m_Window->getGLFWWindow());
    m_ScriptEngine->initialize();

    m_InitStep = "editor camera";
    auto cameraEntity = m_Scene->createEntity("Editor Camera");
    m_Scene->getRegistry().emplace<EditorCamera>(cameraEntity);
    auto& camera = m_Scene->getRegistry().get<EditorCamera>(cameraEntity);
    camera.position = glm::vec3(0.0f, 2.0f, 5.0f);
    camera.target = glm::vec3(0.0f, 0.0f, 0.0f);

    rebindEditorCameraController();
    m_UIManager->setOnNewProject([this]() { loadProjectScene(); });
    m_UIManager->setOnOpenProject([this]() { loadProjectScene(); });
    m_UIManager->setOnSaveProject([this]() { saveProjectScene(); });
    m_UIManager->setOnExportGame([this]() { exportGamePackage(); });
    m_UIManager->setOnExportGameLinux([this]() { exportGamePackageLinux(); });
    m_UIManager->setOnNewScene([this]() { newScene(); });
    m_UIManager->setOnOpenSceneAsset([this](const std::string& assetPath) { loadSceneFromAssetPath(assetPath); });
    m_UIManager->setOnPlay([this]() { startPlayMode(); });
    m_UIManager->setOnPause([this]() { togglePausePlayMode(); });
    m_UIManager->setOnStop([this]() { stopPlayMode(); });
    m_UIManager->setOnReleaseGameFocus([this]() {
        if (m_ScriptEngine) {
            m_ScriptEngine->setMouseCaptured(false);
            m_ScriptEngine->setInputEnabled(false);
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
    // Walnut-style tool/debug layers (render on top of the editor UI).
    m_LayerStack.pushLayer<WorldStreamingLayer>(this);
    m_LayerStack.pushOverlay<HLODViewerLayer>(this);
} catch (const std::exception& e) {
    throw std::runtime_error(std::string("EditorApp startup failed at step '") + m_InitStep + "': " + e.what());
} catch (...) {
    throw std::runtime_error(std::string("EditorApp startup failed at step '") + m_InitStep + "' (unknown, non-std exception)");
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

void EditorApp::resetEditorScene(bool createEditorCamera) {
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

    if (createEditorCamera) {
        ensureEditorCamera();
    }
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

    std::string assetRel = m_CurrentSceneAssetPath;
    if (assetRel.empty()) {
        // Fall back to default.
        std::string defaultFull = m_ProjectManager->getDefaultScenePath();
        if (defaultFull.empty()) {
            return false;
        }
        assetRel = "scenes/main.scene";
    }

    const std::string scenePath = m_ProjectManager->getAssetFullPath(assetRel);
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

void EditorApp::newScene() {
    if (!m_ProjectManager || !m_ProjectManager->hasProject()) {
        return;
    }

    resetEditorScene(true);

    m_CurrentSceneAssetPath = "scenes/untitled.scene";
    if (m_Scene) {
        m_Scene->setName("Untitled");
        m_Scene->setDirty(true);
    }
}

bool EditorApp::loadProjectScene() {
    if (!m_ProjectManager || !m_ProjectManager->hasProject()) {
        return false;
    }

    return loadSceneFromAssetPath("scenes/main.scene");
}

bool EditorApp::loadSceneFromAssetPath(const std::string& assetRelativePath) {
    if (!m_ProjectManager || !m_ProjectManager->hasProject()) {
        return false;
    }

    if (assetRelativePath.empty()) {
        return false;
    }

    const std::string scenePath = m_ProjectManager->getAssetFullPath(assetRelativePath);
    if (scenePath.empty()) {
        return false;
    }

    resetEditorScene(false);

    SerializedScene data;
    if (std::filesystem::exists(scenePath) && SceneSerializer::loadFromFile(scenePath, data)) {
        std::unordered_map<uint32_t, entt::entity> entityMap;
        entityMap.reserve(data.entities.size());

        auto& registry = m_Scene->getRegistry();

        auto isLegacyEditorCamera = [&](const SerializedEntity& src) {
            return src.name == "Editor Camera" && src.hasCamera && !src.hasGameCamera;
        };

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
            if (src.hasEditorCamera) {
                registry.emplace_or_replace<EditorCamera>(entity, src.editorCamera);
                if (registry.all_of<Camera>(entity)) {
                    registry.remove<Camera>(entity);
                }
            } else if (src.hasCamera) {
                if (isLegacyEditorCamera(src)) {
                    registry.emplace_or_replace<EditorCamera>(entity, EditorCamera{src.camera});
                    if (registry.all_of<Camera>(entity)) {
                        registry.remove<Camera>(entity);
                    }
                } else {
                    registry.emplace_or_replace<Camera>(entity, src.camera);
                }
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
            if (src.hasMeshCollider) {
                registry.emplace_or_replace<ECS::MeshColliderComponent>(entity, src.meshCollider);
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
            // TDD §5: every mesh entity carries LOD state (fresh state on load;
            // variants gracefully fall back to the full mesh when absent).
            if (registry.all_of<::Mesh>(entity) && !registry.all_of<Atlas::LODComponent>(entity)) {
                registry.emplace<Atlas::LODComponent>(entity, Atlas::LODComponent{});
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

    m_CurrentSceneAssetPath = assetRelativePath;

    ensureEditorCamera();
    rebindEditorCameraController();
    m_Scene->updateWorldTransforms();
    m_Scene->setDirty(false);

    if (m_ProjectManager) {
        m_ProjectManager->invalidateAssetTreeCache();
    }

    return true;
}

bool EditorApp::exportGamePackage() {
    if (!m_ProjectManager || !m_ProjectManager->hasProject() || !m_Scene) {
        return false;
    }

    if (!saveProjectScene()) {
        std::cerr << "[Export] Failed to save project scene before export" << std::endl;
        return false;
    }

    const auto& registry = m_Scene->getRegistry();
    auto meshView = registry.view<::Mesh>();
    for (auto entity : meshView) {
        const auto& mesh = meshView.get<::Mesh>(entity);
        if (!isPrimitiveMeshPath(mesh.meshPath)) {
            std::cerr << "[Export] Unsupported non-primitive mesh in scene: " << mesh.meshPath << std::endl;
            return false;
        }
    }

    std::filesystem::path exportRoot = std::filesystem::path(m_ProjectManager->getProjectPath()) / "export" / m_ProjectManager->getCurrentProject().name;
    std::filesystem::path packageRoot = exportRoot / "game";

    // Build the game executable natively — the editor invokes g++ directly
    // on the engine source files (without editor-only code) and links against
    // the prebuilt static libraries (glfw, assimp, etc.) from `premake5 ninja`.
    std::error_code ec;
    std::filesystem::remove_all(exportRoot, ec);
    ec.clear();
    std::filesystem::create_directories(packageRoot, ec);
    if (ec) {
        std::cerr << "[Export] Failed to create export directory: " << exportRoot.string() << std::endl;
        return false;
    }

    // Build the game .exe natively.
    {
        // AtlasEngine.exe lives at <root>/bin/<cfg>/ — go up 3 levels to the repo root.
        const std::string engineRoot = getCurrentExecutablePath().parent_path().parent_path().parent_path().string();
        const std::string outputExe = (exportRoot / (m_ProjectManager->getCurrentProject().name + ".exe")).string();

        // Collect game source files (all src/ except editor-only files).
        std::vector<std::string> sources;
        for (auto& entry : std::filesystem::recursive_directory_iterator(
                 std::filesystem::path(engineRoot) / "src")) {
            if (!entry.is_regular_file()) continue;
            auto ext = entry.path().extension().string();
            if (ext != ".cpp" && ext != ".c") continue;
            // Use forward-slash normalized path so the exclusion checks below
            // work identically on Windows (\ separators) and POSIX (/).
            auto path = entry.path().lexically_normal().generic_string();
            // Exclude editor-only files.
            if (path.find("/main.cpp") != std::string::npos) continue;
            if (path.find("/editor/") != std::string::npos) continue;
            if (path.find("/ui/") != std::string::npos) continue;
            if (path.find("/imgui/") != std::string::npos) continue;
            if (path.find("/project/") != std::string::npos) continue;
            if (path.find("native_file_dialog") != std::string::npos) continue;
            if (path.find("camera_controller") != std::string::npos) continue;
            sources.push_back(std::move(path));
        }

        // Also include the generated embedded shaders.
        std::string embedCpp = (std::filesystem::path(engineRoot) / "build/generated/embedded_shaders.cpp").string();
        if (std::filesystem::exists(embedCpp)) {
            sources.push_back(std::move(embedCpp));
        }

        // Build include directories string.
        auto q = [](const std::string& s) { return "\"" + s + "\""; };
        std::string inc = "-I" + q(engineRoot + "/src");
        inc += " -I" + q(engineRoot + "/deps/src/imgui");
        inc += " -I" + q(engineRoot + "/deps/src/imgui/backends");
        inc += " -I" + q(engineRoot + "/deps/src/imguifiledialog");
        inc += " -I" + q(engineRoot + "/deps/src/imguizmo/src");
        inc += " -I" + q(engineRoot + "/deps/src/glm");
        inc += " -I" + q(engineRoot + "/deps/src/stb");
        inc += " -I" + q(engineRoot + "/deps/src/entt/src");
        inc += " -I" + q(engineRoot + "/deps/src/vma/include");
        inc += " -I" + q(engineRoot + "/deps/src/assimp/include");
        inc += " -I" + q(engineRoot + "/deps/src/lua");
        inc += " -I" + q(engineRoot + "/deps/src/glfw/include");
        inc += " -I" + q(engineRoot + "/deps/src/tracy/public");
        inc += " -I" + q(std::string(getenv("VULKAN_SDK")) + "/Include");

        // Build source files string.
        std::string srcStr;
        for (const auto& s : sources) {
            srcStr += q(s) + " ";
        }

        // Link libraries.
        std::string libDir = q(engineRoot + "/build/bin/Release");
        std::string vkLib = q(std::string(getenv("VULKAN_SDK")) + "/Lib");

        std::string cmd = "g++ -std=c++17 -O2 -DNDEBUG -DVMA_STATIC -DATLAS_EMBED_SHADERS=1 -mwindows "
            + inc + " " + srcStr
            + " -L" + libDir + " -lglfw -limgui_lib -limguizmo_lib -llua_lib -lassimp -lzlib -lTracyClient"
            + " -L" + vkLib + " -lvulkan-1"
            + " -lole32 -lshell32 -lshlwapi -luuid -lws2_32 -ldbghelp -lgdi32"
            + " -o " + q(outputExe);

        std::cout << "[Export] Building game..." << std::endl;
        const int ret = std::system(cmd.c_str());
        if (ret != 0) {
            std::cerr << "[Export] Game build failed with exit code " << ret << std::endl;
            return false;
        }
    }

    std::filesystem::copy(m_ProjectManager->getAssetsPath(), packageRoot / "assets",
        std::filesystem::copy_options::recursive | std::filesystem::copy_options::overwrite_existing, ec);
    if (ec) {
        std::cerr << "[Export] Failed to copy assets folder" << std::endl;
        return false;
    }

    // Copy the MinGW runtime DLLs next to the exported executable so the game
    // runs on machines that don't have the MinGW toolchain in PATH.
    {
        const char* dllNames[] = {"libstdc++-6.dll", "libgcc_s_seh-1.dll", "libwinpthread-1.dll"};
        std::string mingwBin = std::filesystem::path(getCurrentExecutablePath()).parent_path().parent_path().parent_path().parent_path().string();
        // The editor exe runs from <root>/bin/<cfg>/; the MinGW bin dir is not
        // derivable from there, so probe a few common locations.
        std::vector<std::string> candidates = {
            mingwBin + "/../mingw/bin",
            mingwBin + "/../../scoop/apps/mingw/current/bin",
            mingwBin + "/../../Users/" + std::string(getenv("USERNAME") ? getenv("USERNAME") : "") + "/scoop/apps/mingw/current/bin",
            "C:/Users/" + std::string(getenv("USERNAME") ? getenv("USERNAME") : "") + "/scoop/apps/mingw/current/bin",
        };
        std::string foundDir;
        for (const auto& c : candidates) {
            if (std::filesystem::exists(std::filesystem::path(c) / "libstdc++-6.dll")) {
                foundDir = c;
                break;
            }
        }
        if (!foundDir.empty()) {
            for (const char* dll : dllNames) {
                ec.clear();
                std::filesystem::copy_file(std::filesystem::path(foundDir) / dll, exportRoot / dll,
                    std::filesystem::copy_options::overwrite_existing, ec);
            }
        }
    }

    Atlas::Export::PackageManifest manifest;
    manifest.startupScene = "assets/" + (m_CurrentSceneAssetPath.empty() ? std::string("scenes/main.scene") : m_CurrentSceneAssetPath);
    manifest.assetsRoot = "assets";
    manifest.useEmbeddedShaders = true;
    manifest.shadersPath = "shaders";
    if (!Atlas::Export::savePackageManifest(manifest, (packageRoot / "package.manifest").string())) {
        std::cerr << "[Export] Failed to write package manifest" << std::endl;
        return false;
    }

    std::filesystem::path shaderDir = getCurrentExecutablePath().parent_path() / "shaders";
    if (std::filesystem::exists(shaderDir)) {
        ec.clear();
        std::filesystem::copy(shaderDir, packageRoot / "shaders",
            std::filesystem::copy_options::recursive | std::filesystem::copy_options::overwrite_existing, ec);
    }

    std::cout << "[Export] Game exported to: " << exportRoot.string() << std::endl;
    return true;
}

bool EditorApp::exportGamePackageLinux() {
    if (!m_ProjectManager || !m_ProjectManager->hasProject() || !m_Scene) {
        return false;
    }

    if (!saveProjectScene()) {
        std::cerr << "[Export] Failed to save project scene before export" << std::endl;
        return false;
    }

    const auto& registry = m_Scene->getRegistry();
    auto meshView = registry.view<::Mesh>();
    for (auto entity : meshView) {
        const auto& mesh = meshView.get<::Mesh>(entity);
        if (!isPrimitiveMeshPath(mesh.meshPath)) {
            std::cerr << "[Export] Unsupported non-primitive mesh in scene: " << mesh.meshPath << std::endl;
            return false;
        }
    }

    std::filesystem::path exportRoot =
        std::filesystem::path(m_ProjectManager->getProjectPath()) / "export" / (m_ProjectManager->getCurrentProject().name + "_linux");
    std::filesystem::path packageRoot = exportRoot / "game";

    std::error_code ec;
    std::filesystem::remove_all(exportRoot, ec);
    ec.clear();
    std::filesystem::create_directories(packageRoot, ec);
    if (ec) {
        std::cerr << "[Export] Failed to create export directory: " << exportRoot.string() << std::endl;
        return false;
    }

    // Copy assets into the package.
    std::filesystem::copy(m_ProjectManager->getAssetsPath(), packageRoot / "assets",
        std::filesystem::copy_options::recursive | std::filesystem::copy_options::overwrite_existing, ec);
    if (ec) {
        std::cerr << "[Export] Failed to copy assets folder" << std::endl;
        return false;
    }

    // Write the package manifest.
    Atlas::Export::PackageManifest manifest;
    manifest.startupScene = "assets/" + (m_CurrentSceneAssetPath.empty() ? std::string("scenes/main.scene") : m_CurrentSceneAssetPath);
    manifest.assetsRoot = "assets";
    manifest.useEmbeddedShaders = true;
    manifest.shadersPath = "shaders";
    if (!Atlas::Export::savePackageManifest(manifest, (packageRoot / "package.manifest").string())) {
        std::cerr << "[Export] Failed to write package manifest" << std::endl;
        return false;
    }

    // Copy shaders into the package (embedded shaders are used, but keep the
    // .spv files alongside for fallback / RenderDoc inspection).
    std::filesystem::path shaderDir = getCurrentExecutablePath().parent_path() / "shaders";
    if (std::filesystem::exists(shaderDir)) {
        ec.clear();
        std::filesystem::copy(shaderDir, packageRoot / "shaders",
            std::filesystem::copy_options::recursive | std::filesystem::copy_options::overwrite_existing, ec);
    }

    // Copy the Linux build script and CI workflow next to the package.
    std::filesystem::path engineRoot = getCurrentExecutablePath().parent_path().parent_path().parent_path();
    std::filesystem::path scriptSrc = engineRoot / "scripts" / "build_linux.sh";
    if (std::filesystem::exists(scriptSrc)) {
        std::filesystem::copy_file(scriptSrc, exportRoot / "build_linux.sh",
            std::filesystem::copy_options::overwrite_existing, ec);
    }

    std::filesystem::path workflowSrc = engineRoot / "scripts" / "workflows" / "build-linux.yml";
    if (std::filesystem::exists(workflowSrc)) {
        std::filesystem::create_directories(exportRoot / ".github" / "workflows", ec);
        ec.clear();
        std::filesystem::copy_file(workflowSrc, exportRoot / ".github" / "workflows" / "build-linux.yml",
            std::filesystem::copy_options::overwrite_existing, ec);
    }

    std::cout << "[Export] Linux package exported to: " << exportRoot.string() << std::endl;
    std::cout << "  To build the Linux game, run: ./build_linux.sh game ./game" << std::endl;
    std::cout << "  or push this folder to GitHub and run the 'Build Linux Game' workflow." << std::endl;
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
    copyIfPresent(Atlas::ECS::TagComponent{});
    copyIfPresent(Atlas::ECS::MaterialComponent{});
    copyIfPresent(Atlas::ECS::EditorHiddenComponent{});
    copyIfPresent(Atlas::ECS::ScriptComponent{});
    copyIfPresent(Atlas::ECS::FollowCameraComponent{});
    copyIfPresent(Atlas::ECS::GameCameraComponent{});
    copyIfPresent(Atlas::ECS::RigidBodyComponent{});
    copyIfPresent(Atlas::ECS::BoxColliderComponent{});
    copyIfPresent(Atlas::ECS::SphereColliderComponent{});
    copyIfPresent(Atlas::ECS::CapsuleColliderComponent{});
    copyIfPresent(Atlas::ECS::MeshColliderComponent{});
    copyIfPresent(Atlas::LODComponent{});
    copyIfPresent(Atlas::ECS::SkeletonComponent{});
    copyIfPresent(Atlas::ECS::AnimationPlayerComponent{});
    copyIfPresent(Atlas::ECS::BonePoseOverrideComponent{});
    copyIfPresent(Atlas::ECS::SkinnedMeshComponent{});

    // Copy meshes but mark them as non-owning so runtime clones do not free shared GPU handles.
    for (const auto& pair : remap) {
        if (src.all_of<::Mesh>(pair.first)) {
            ::Mesh meshCopy = src.get<::Mesh>(pair.first);
            meshCopy.ownsGpuResources = false;
            dst.emplace_or_replace<::Mesh>(pair.second, meshCopy);
        }
    }

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


void EditorApp::rebuildRuntimePhysics() {
    if (m_PhysicsSystem) {
        m_PhysicsSystem->rebuild(m_RuntimeScene.get());
    }
}

void EditorApp::updateRuntimePhysics(Scene* scene, float deltaTime) {
    if (m_PhysicsSystem) {
        m_PhysicsSystem->step(scene, deltaTime);
    }
}

void EditorApp::startPlayMode() {
    const bool focusGameOnPlay = (m_UIManager && m_UIManager->wasViewportFocused());

    cloneSceneToRuntime();
    m_GameModePlaying = (m_RuntimeScene != nullptr);
    m_GameModePaused = false;

    if (m_GameModePlaying && m_UIManager) {
        m_UIManager->setScene(m_RuntimeScene.get());
        m_UIManager->clearSelection();
    }

    if (m_GameModePlaying && focusGameOnPlay && m_UIManager) {
        m_UIManager->requestFocusGameViewport();
    }

    if (m_GameModePlaying) {
        rebuildRuntimePhysics();
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
        m_ScriptEngine->setInputEnabled(false);
        m_ScriptEngine->destroyScene();
    }
    if (m_PhysicsSystem) {
        m_PhysicsSystem->clear();
    }
    m_GameModePaused = false;
    m_GameModePlaying = false;
    m_RuntimeScene.reset();

    if (m_UIManager) {
        m_UIManager->setScene(m_Scene.get());
        m_UIManager->clearSelection();
    }
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
        glm::vec3 forward = glm::vec3(world * glm::vec4(0.0f, 0.0f, -1.0f, 0.0f));
        glm::vec3 up = glm::vec3(world * glm::vec4(0.0f, 1.0f, 0.0f, 0.0f));

        if (glm::length(forward) < 1e-5f) {
            forward = glm::vec3(0.0f, 0.0f, -1.0f);
        } else {
            forward = glm::normalize(forward);
        }

        if (glm::length(up) < 1e-5f) {
            up = glm::vec3(0.0f, 1.0f, 0.0f);
        } else {
            up = glm::normalize(up);
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
        meshData = Atlas::PrimitiveHelpers::createPlane(2.0f);
    } else if (primitiveType == "Sphere") {
        meshData = Atlas::PrimitiveHelpers::createSphere(0.5f);
    } else if (primitiveType == "Cylinder") {
        meshData = Atlas::PrimitiveHelpers::createCylinder(0.5f, 1.5f);
    } else if (primitiveType == "Capsule") {
        meshData = Atlas::PrimitiveHelpers::createCapsule(0.45f, 1.8f);
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

    // Create GPU buffers. createdMeshBuffers tracks whether THIS call minted a
    // fresh set (vs. reusing shared buffers) for handle registration below.
    bool createdMeshBuffers = false;
    if (meshData.vertexBuffer == VK_NULL_HANDLE || meshData.indexBuffer == VK_NULL_HANDLE) {
        ModelLoader::createBuffers(meshData, m_Renderer->getDevice(), m_Renderer->getPhysicalDevice(), PrimitiveHelpers::findMemoryType);
        createdMeshBuffers = true;
        // Auto-LOD variants while CPU data is alive (per-primitive buffers;
        // shared-geometry batching still applies via identical buffer keys).
        Atlas::cacheImportLODs(m_Renderer.get(), meshData, reinterpret_cast<uint64_t>(meshData.vertexBuffer),
                                 reinterpret_cast<uint64_t>(meshData.indexBuffer), false);
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
    if (createdMeshBuffers) {
        mesh.renderMeshId = m_Renderer->getMeshRegistry().allocateMesh();
    }
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
    // TDD §5: LOD state from birth (see applyMeshToEntity).
    registry.emplace_or_replace<Atlas::LODComponent>(entity, Atlas::LODComponent{});

    if (m_UIManager) {
        m_UIManager->setSelectedEntity(entity);
    }

    return entity;
}

Entity EditorApp::createLightEntity(ECS::LightComponent::Type type, Entity parent) {
    if (!m_Scene) {
        return entt::null;
    }

    auto entity = m_Scene->createEntity("Light");
    auto& registry = m_Scene->getRegistry();

    if (parent != entt::null && registry.valid(parent)) {
        m_Scene->setParent(entity, parent);
    }

    // Add Transform component
    if (registry.all_of<Transform>(entity)) {
        auto& transform = registry.get<Transform>(entity);
        transform.position = (parent != entt::null && registry.valid(parent)) ? glm::vec3(0.0f) : getDefaultSpawnPosition();
    }

    // Add LightComponent
    ECS::LightComponent light;
    light.type = type;
    light.color = glm::vec3(1.0f, 1.0f, 1.0f);
    light.intensity = (type == ECS::LightComponent::Type::Directional) ? 50.0f : 5.0f;
    light.castShadows = (type == ECS::LightComponent::Type::Directional);
    light.direction = (type == ECS::LightComponent::Type::Directional) ? glm::vec3(0.0f, -1.0f, 0.0f) : glm::vec3(0.0f, -1.0f, 0.0f);
    
    if (type == ECS::LightComponent::Type::Directional) {
        light.castShadows = true;
    }
    
    registry.emplace_or_replace<ECS::LightComponent>(entity, light);

    // Add a name based on type
    switch (type) {
        case ECS::LightComponent::Type::Directional: registry.emplace<Atlas::ECS::TagComponent>(entity, "Directional Light"); break;
        case ECS::LightComponent::Type::Point: registry.emplace<Atlas::ECS::TagComponent>(entity, "Point Light"); break;
        case ECS::LightComponent::Type::Spot: registry.emplace<Atlas::ECS::TagComponent>(entity, "Spot Light"); break;
    }

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

    if (!mesh.ownsGpuResources) {
        return;
    }

    VkBuffer vb = mesh.vertexBuffer;
    VkDeviceMemory vm = mesh.vertexMemory;
    VkBuffer ib = mesh.indexBuffer;
    VkDeviceMemory im = mesh.indexMemory;

    mesh.vertexBuffer = VK_NULL_HANDLE;
    mesh.vertexMemory = VK_NULL_HANDLE;
    mesh.indexBuffer = VK_NULL_HANDLE;
    mesh.indexMemory = VK_NULL_HANDLE;
    // Mirror the deferred vkDestroy above: release the registry handle under
    // the exact same ownership guard (ownsGpuResources checked on entry).
    if (mesh.renderMeshId != Atlas::kInvalidMeshHandle) {
        m_Renderer->getMeshRegistry().freeMesh(mesh.renderMeshId);
        mesh.renderMeshId = 0;
    }

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

        // Discover PBR texture sets next to the model (MTL-less OBJ fallback).
        m_ActiveImportTextureSets = Atlas::discoverPbrTextureSets(m_ActiveImportFullPath);
        {
            int best = Atlas::bestPbrSetForModel(m_ActiveImportTextureSets, m_ActiveImportModelName);
            m_ActiveImportOptions.textureSet =
                (best >= 0) ? m_ActiveImportTextureSets[static_cast<size_t>(best)].name : std::string{};
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
        ImGui::DragFloat3("Rotation (deg)", &m_ActiveImportOptions.rotationEulerDeg.x, 1.0f, -360.0f, 360.0f, "%.1f");
        ImGui::Checkbox("Import Textures", &m_ActiveImportOptions.loadTextures);
        ImGui::BeginDisabled(!m_ActiveImportOptions.loadTextures);
        {
            int texSetIdx = 0; // 0 = None
            for (size_t i = 0; i < m_ActiveImportTextureSets.size(); ++i) {
                if (m_ActiveImportTextureSets[i].name == m_ActiveImportOptions.textureSet) {
                    texSetIdx = static_cast<int>(i) + 1;
                    break;
                }
            }
            const std::string preview =
                (texSetIdx > 0) ? m_ActiveImportTextureSets[static_cast<size_t>(texSetIdx) - 1].name : "None";
            if (ImGui::BeginCombo("Texture Set (PBR)", preview.c_str())) {
                const bool noneSel = (texSetIdx == 0);
                if (ImGui::Selectable("None", noneSel)) {
                    m_ActiveImportOptions.textureSet.clear();
                }
                if (noneSel) ImGui::SetItemDefaultFocus();
                for (size_t i = 0; i < m_ActiveImportTextureSets.size(); ++i) {
                    const bool sel = (texSetIdx == static_cast<int>(i) + 1);
                    if (ImGui::Selectable(m_ActiveImportTextureSets[i].name.c_str(), sel)) {
                        m_ActiveImportOptions.textureSet = m_ActiveImportTextureSets[i].name;
                    }
                    if (sel) ImGui::SetItemDefaultFocus();
                }
                ImGui::EndCombo();
            }
            if (texSetIdx > 0) {
                const auto& s = m_ActiveImportTextureSets[static_cast<size_t>(texSetIdx) - 1];
                if (s.combined.empty() && !s.metallic.empty() && !s.roughness.empty()) {
                    ImGui::TextDisabled("metallic + roughness will be combined on import");
                }
            } else if (m_ActiveImportTextureSets.empty()) {
                ImGui::TextDisabled("No PBR texture sets found next to the model");
            }
        }
        ImGui::EndDisabled();
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
                    m_ActiveImportTextureSets = Atlas::discoverPbrTextureSets(m_ActiveImportFullPath);
                    {
                        int best = Atlas::bestPbrSetForModel(m_ActiveImportTextureSets, m_ActiveImportModelName);
                        m_ActiveImportOptions.textureSet =
                            (best >= 0) ? m_ActiveImportTextureSets[static_cast<size_t>(best)].name : std::string{};
                    }
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
        [fullPath, modelDataPtr, options]() -> std::shared_ptr<::ModelData> {
            PROFILE_SCOPE("ModelLoad");
            ModelLoader::loadModelMultiMesh(fullPath, VK_NULL_HANDLE, VK_NULL_HANDLE, nullptr, modelDataPtr.get(), false, options.loadTextures);
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

// TDD §10 runtime flow: partition streaming -> hierarchical culling ->
// LOD selection -> HLOD cross-fade, on the active scene (edit or play).
void EditorApp::updateCityRendering(Scene* scene, const glm::vec3& camPos, const glm::mat4& view,
                                    const glm::mat4& viewProj, const glm::mat4& proj, float viewportHeight, float deltaTime) {
    if (!scene || !m_WorldPartition || !m_CullingPipeline || !m_HLODSystem) {
        return;
    }

    // Retarget systems when the active scene changed (edit <-> play).
    if (m_CityScene != scene) {
        m_CityScene = scene;
        m_WorldPartition->setScene(scene);
        m_WorldPartition->markDirty();
        m_HLODSystem->clear();
    }

    // 1. Streaming + in-partition frustum culling (sets base visibility).
    m_WorldPartition->update(camPos, viewProj);

    // 2. Hierarchical culling: cells first, then objects
    //    (frustum + distance + screen-size; occlusion when enabled).
    //    Occluders come from the previous frame's LOD screen sizes (temporal).
    m_OcclusionCuller.setConfig(m_OcclusionConfig);
    m_OcclusionCuller.beginFrame(viewProj, static_cast<uint32_t>(viewportHeight), static_cast<uint32_t>(viewportHeight));
    if (m_OcclusionConfig.enable) {
        struct OccluderCandidate { float screenSize; glm::vec3 bmin; glm::vec3 bmax; };
        std::vector<OccluderCandidate> candidates;
        candidates.reserve(256);
        auto& occRegistry = scene->getRegistry();
        for (auto e : occRegistry.view<LODComponent, ::Mesh>()) {
            const auto& lod = occRegistry.get<LODComponent>(e);
            if (lod.screenSize < m_OcclusionConfig.minOccluderScreenSize) {
                continue;
            }
            const auto& mesh = occRegistry.get<::Mesh>(e);
            if (!mesh.hasBounds || !scene->hasTransform(e)) {
                continue;
            }
            glm::mat4 world = scene->getCachedWorldTransform(e);
            glm::vec3 centerLocal = (mesh.boundsMin + mesh.boundsMax) * 0.5f;
            glm::vec3 extents = (mesh.boundsMax - mesh.boundsMin) * 0.5f;
            glm::vec3 wCenter = glm::vec3(world * glm::vec4(centerLocal, 1.0f));
            // Conservative world AABB: extent scaled by max basis length.
            float maxBasis = 1.0f;
            maxBasis = std::max(maxBasis, glm::length(glm::vec3(world[0])));
            maxBasis = std::max(maxBasis, glm::length(glm::vec3(world[1])));
            maxBasis = std::max(maxBasis, glm::length(glm::vec3(world[2])));
            glm::vec3 wExt = extents * maxBasis;
            candidates.push_back({lod.screenSize, wCenter - wExt, wCenter + wExt});
        }
        const uint32_t maxOcc = m_OcclusionConfig.maxOccluders > 0 ? m_OcclusionConfig.maxOccluders : 64;
        if (candidates.size() > maxOcc) {
            std::nth_element(candidates.begin(), candidates.begin() + maxOcc, candidates.end(),
                [](const OccluderCandidate& a, const OccluderCandidate& b) { return a.screenSize > b.screenSize; });
            candidates.resize(maxOcc);
        }
        for (const auto& c : candidates) {
            m_OcclusionCuller.addOccluder(c.bmin, c.bmax);
        }
        m_CullingPipeline->setOccluder(&m_OcclusionCuller);
    } else {
        m_CullingPipeline->setOccluder(nullptr);
    }
    m_CullingPipeline->setConfig(m_CullingConfig);
    m_CullingConfig.enableOcclusion = m_OcclusionConfig.enable;
    m_CullingPipeline->setWorldConfig(m_WorldPartition->config());
    Frustum frustum = Frustum::fromViewProj(viewProj);
    m_LastCullingStats = m_CullingPipeline->cullWorldPartition(*m_WorldPartition, frustum, viewProj, camPos, viewportHeight);

    // NOTE: LOD selection runs separately every frame (see run()) so it works
    // even with chunk-file streaming, which owns visibility itself.

    // 4. HLOD cross-fade + lazy generation of HLOD0/HLOD1 actors.
    m_HLODSystem->setConfig(m_HLODConfig);
    m_HLODSystem->update(scene, camPos, *m_WorldPartition, deltaTime, m_Renderer.get(), nullptr);

    // 5. HLOD1 impostors: billboard quads for visible far actors (§4.2/§5.2).
    if (m_Renderer) {
        std::vector<Atlas::ImpostorDraw> impostors;
        const auto& cache = m_HLODSystem->getCache();
        impostors.reserve(cache.size());
        const glm::vec3 up0(0.0f, 1.0f, 0.0f);
        for (const auto& [coord, actors] : cache) {
            (void)coord;
            for (const auto& actor : actors) {
                if (actor.level != Atlas::ECS::HLODLevel::HLOD1 || !actor.isVisible || actor.transitionAlpha < 0.5f) {
                    continue;
                }
                // Frustum-test the actor bounds.
                if (!frustum.testSphere(actor.center, actor.radius)) {
                    continue;
                }
                glm::vec3 ext = (actor.boundsMax - actor.boundsMin) * 0.5f;
                const float sx = std::max({ext.x * 2.0f, ext.z * 2.0f, 1.0f});
                const float sy = std::max(ext.y * 2.0f, 1.0f);
                glm::vec3 toCam = actor.center - camPos;
                const float dist = glm::length(toCam);
                if (dist < 1e-3f) {
                    continue;
                }
                toCam /= dist;
                glm::vec3 right = glm::cross(up0, toCam);
                if (glm::length(right) < 1e-4f) {
                    right = glm::vec3(1.0f, 0.0f, 0.0f);
                } else {
                    right = glm::normalize(right);
                }
                glm::vec3 up = glm::normalize(glm::cross(toCam, right));
                glm::mat4 basis(1.0f);
                basis[0] = glm::vec4(right * sx, 0.0f);
                basis[1] = glm::vec4(up * sy, 0.0f);
                basis[2] = glm::vec4(toCam, 0.0f);
                basis[3] = glm::vec4(actor.center, 1.0f);
                Atlas::ImpostorDraw draw;
                draw.model = basis;
                draw.color = actor.avgColor;
                impostors.push_back(draw);
            }
        }
        m_Renderer->setImpostorDraws(std::move(impostors));
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

        if (m_CameraController && !m_UIManager->isGizmoUsing()) {
            bool uiAllow = (m_UIManager && m_UIManager->allowViewportCameraInput());
            bool allow = uiAllow || m_CameraController->isCapturing();
            m_CameraController->update(deltaTime, allow);
        }

        m_LayerStack.update(deltaTime);

        m_ImGuiManager->newFrame();
        ImGuizmo::BeginFrame();

        if (m_UIManager) {
            bool haveScenePreviewCamera = false;
            glm::mat4 scenePreviewView(1.0f);
            glm::mat4 scenePreviewProj(1.0f);
            glm::mat4 scenePreviewRenderProj(1.0f);
            glm::vec3 scenePreviewPos(0.0f);

            if (m_Scene) {
                auto& editorRegistry = m_Scene->getRegistry();
                auto editorCamView = editorRegistry.view<EditorCamera>();
                if (editorCamView.begin() != editorCamView.end()) {
                    auto camEnt = *editorCamView.begin();
                    auto& cam = editorRegistry.get<EditorCamera>(camEnt);
                    VkExtent2D extent = m_Renderer ? m_Renderer->getSwapChainExtent() : VkExtent2D{1280, 720};
                    if (extent.width > 0 && extent.height > 0) {
                        cam.aspectRatio = static_cast<float>(extent.width) / static_cast<float>(extent.height);
                    }
                    scenePreviewView = cam.getViewMatrix();
                    scenePreviewProj = cam.getProjectionMatrix();
                    scenePreviewRenderProj = scenePreviewProj;
                    scenePreviewRenderProj[1][1] = -scenePreviewRenderProj[1][1];
                    scenePreviewPos = cam.position;
                    haveScenePreviewCamera = true;
                    m_UIManager->setCameraMatrices(scenePreviewView, scenePreviewProj);
                }
            }

            if (!haveScenePreviewCamera && m_CameraController) {
                scenePreviewView = m_CameraController->getViewMatrix();
                scenePreviewProj = m_CameraController->getProjMatrix();
                scenePreviewRenderProj = scenePreviewProj;
                scenePreviewRenderProj[1][1] = -scenePreviewRenderProj[1][1];
                glm::mat4 invView = glm::inverse(scenePreviewView);
                scenePreviewPos = glm::vec3(invView[3]);
                haveScenePreviewCamera = true;
                m_UIManager->setCameraMatrices(scenePreviewView, scenePreviewProj);
            }

            if (m_Renderer) {
                m_Renderer->setScenePreviewCameraOverride(haveScenePreviewCamera, scenePreviewView, scenePreviewRenderProj, scenePreviewPos);
            }
        }

        m_UIManager->render(m_Viewport.getSceneTextureId(), m_Viewport.getGameTextureId(), m_GameModePlaying, m_GameModePaused);

        m_LayerStack.renderUI();

        renderImportOptionsPopup();
        processPendingModels();

        if (m_ScriptEngine) {
            const bool gameInputEnabled = m_GameModePlaying && !m_GameModePaused && m_UIManager && m_UIManager->wasGameViewportFocused();
            m_ScriptEngine->setInputEnabled(gameInputEnabled);
        }

        if (m_GameModePlaying) {
            updateGameCameras(m_RuntimeScene.get());
            if (!m_GameModePaused) {
                updateAnimationRuntime(m_RuntimeScene.get(), deltaTime);
                if (m_ScriptEngine) {
                    m_ScriptEngine->update(deltaTime);
                }
                updateRuntimePhysics(m_RuntimeScene.get(), deltaTime);
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

        // TDD §10 city pipeline (disabled while chunk-file streaming is enabled).
        // Runs on the active scene in both edit and play modes.
        if (m_WorldPartition && !m_WorldStreamingEnabled && m_WorldPartition->config().enabled) {
            Scene* cityScene = m_GameModePlaying ? m_RuntimeScene.get() : m_Scene.get();
            if (cityScene) {
                glm::vec3 camPos(0.0f);
                glm::mat4 view(1.0f);
                glm::mat4 proj(1.0f);
                bool haveCamera = false;

                auto& registry = cityScene->getRegistry();
                // Prefer primary game camera, then any game camera, then editor camera.
                {
                    auto gameView = registry.view<Camera, Atlas::ECS::GameCameraComponent>();
                    entt::entity picked = entt::null;
                    for (auto e : gameView) {
                        if (picked == entt::null) picked = e;
                        if (gameView.get<Atlas::ECS::GameCameraComponent>(e).primary) { picked = e; break; }
                    }
                    if (picked != entt::null) {
                        auto& cam = registry.get<Camera>(picked);
                        VkExtent2D extent = m_Renderer ? m_Renderer->getSwapChainExtent() : VkExtent2D{1280, 720};
                        if (extent.width > 0 && extent.height > 0) {
                            cam.aspectRatio = static_cast<float>(extent.width) / static_cast<float>(extent.height);
                        }
                        camPos = cam.position;
                        view = cam.getViewMatrix();
                        proj = cam.getProjectionMatrix();
                        proj[1][1] = -proj[1][1];
                        haveCamera = true;
                    }
                }
                if (!haveCamera) {
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
                        haveCamera = true;
                    } else if (m_CameraController) {
                        view = m_CameraController->getViewMatrix();
                        proj = m_CameraController->getProjMatrix();
                        proj[1][1] = -proj[1][1];

                        // Derive camera world position from the view matrix.
                        glm::mat4 invView = glm::inverse(view);
                        camPos = glm::vec3(invView[3]);
                        haveCamera = true;
                    }
                }

                if (haveCamera) {
                    VkExtent2D extent = m_Renderer ? m_Renderer->getSwapChainExtent() : VkExtent2D{1280, 720};
                    float viewportHeight = extent.height > 0 ? static_cast<float>(extent.height) : 720.0f;
                    updateCityRendering(cityScene, camPos, view, proj * view, proj, viewportHeight, deltaTime);
                }
                // LOD selection runs every frame regardless of streaming mode
                // (level assignment always; hiding only when the partition
                // pipeline owns visibility, otherwise chunk streaming does).
                // This must run AFTER culling so hide-only never gets undone.
            }
        }

        // LOD selection runs every frame regardless of streaming mode
        // (level assignment always; hiding only when the partition
        // pipeline owns visibility, otherwise chunk streaming does).
        // This must run AFTER culling so hide-only never gets undone.
        if (m_WorldPartition && m_WorldPartition->config().enabled) {
            Scene* cityScene = m_GameModePlaying ? m_RuntimeScene.get() : m_Scene.get();
            if (cityScene) {
                glm::vec3 camPos(0.0f);
                glm::mat4 view(1.0f);
                glm::mat4 proj(1.0f);
                bool haveCamera = false;

                auto& registry = cityScene->getRegistry();
                auto gameView = registry.view<Camera, Atlas::ECS::GameCameraComponent>();
                entt::entity picked = entt::null;
                for (auto e : gameView) {
                    if (picked == entt::null) picked = e;
                    if (gameView.get<Atlas::ECS::GameCameraComponent>(e).primary) { picked = e; break; }
                }
                if (picked != entt::null) {
                    auto& cam = registry.get<Camera>(picked);
                    VkExtent2D extent = m_Renderer ? m_Renderer->getSwapChainExtent() : VkExtent2D{1280, 720};
                    if (extent.width > 0 && extent.height > 0) {
                        cam.aspectRatio = static_cast<float>(extent.width) / static_cast<float>(extent.height);
                    }
                    camPos = cam.position;
                    view = cam.getViewMatrix();
                    proj = cam.getProjectionMatrix();
                    proj[1][1] = -proj[1][1];
                    haveCamera = true;
                }
                if (!haveCamera) {
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
                        haveCamera = true;
                    } else if (m_CameraController) {
                        view = m_CameraController->getViewMatrix();
                        proj = m_CameraController->getProjMatrix();
                        proj[1][1] = -proj[1][1];
                        glm::mat4 invView = glm::inverse(view);
                        camPos = glm::vec3(invView[3]);
                        haveCamera = true;
                    }
                }

                if (haveCamera) {
                    VkExtent2D extent = m_Renderer ? m_Renderer->getSwapChainExtent() : VkExtent2D{1280, 720};
                    float viewportHeight = extent.height > 0 ? static_cast<float>(extent.height) : 720.0f;
                    const bool partitionActive = !m_WorldStreamingEnabled && m_WorldPartition->config().enabled;
                    m_LastLODStats = updateLODSystem(cityScene, camPos, proj, viewportHeight,
                                                      m_WorldPartition->config(), m_LODConfig,
                                                      partitionActive);
                    // Drop stale impostor billboards when their producer is off.
                    if (!partitionActive && m_Renderer && m_Renderer->getImpostorDrawCount() > 0) {
                        m_Renderer->setImpostorDraws({});
                    }
                }
            }
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

        // Import options: root scale + rotation.
        if (m_Scene->getRegistry().all_of<Transform>(rootEntity)) {
            auto& tr = m_Scene->getRegistry().get<Transform>(rootEntity);
            float s = item.importOptions.uniformScale;
            if (s <= 0.0f) s = 1.0f;
            tr.scale *= glm::vec3(s);
            tr.rotation.x += item.importOptions.rotationEulerDeg.x;
            tr.rotation.y += item.importOptions.rotationEulerDeg.y;
            tr.rotation.z += item.importOptions.rotationEulerDeg.z;
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
            const bool createdMeshBuffers =
                (meshData.vertexBuffer == VK_NULL_HANDLE || meshData.indexBuffer == VK_NULL_HANDLE);
            if (createdMeshBuffers) {
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
            if (createdMeshBuffers) {
                mesh.renderMeshId = m_Renderer->getMeshRegistry().allocateMesh();
            }
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
            // PBR fallback for MTL-less imports (bare OBJs + textures/ sets).
            // Fills only paths assimp left empty; assimp/MTL results always win.
            if (item.importOptions.loadTextures && !item.importOptions.textureSet.empty()) {
                Atlas::fillEmptyPbrTexturePaths(item.basePath, item.importOptions.textureSet,
                    meshData.baseColorTexturePath, meshData.normalTexturePath,
                    meshData.metallicRoughnessTexturePath, meshData.aoTexturePath,
                    meshData.emissiveTexturePath);
            }
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
            // TDD §5: every imported mesh carries LOD state from birth
            // (screenSizeBias stays editable in Properties for hero objects).
            m_Scene->getRegistry().emplace_or_replace<Atlas::LODComponent>(entity, Atlas::LODComponent{});
            // Auto-LOD variants while CPU data is alive (skipped for skinned).
            // NOTE: each submesh caches its own variants (keyed by its buffers).
            Atlas::cacheImportLODs(m_Renderer.get(), meshData, reinterpret_cast<uint64_t>(mesh.vertexBuffer),
                reinterpret_cast<uint64_t>(mesh.indexBuffer),
                m_Scene->getRegistry().all_of<ECS::SkinnedMeshComponent>(entity));
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

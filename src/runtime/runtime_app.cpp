#include "runtime_app.h"

#include <chrono>
#include <cmath>
#include <filesystem>
#include <iostream>
#include <stdexcept>

#ifdef _WIN32
#include <windows.h>
#else
#include <unistd.h>
#endif

#include "runtime_scene_loader.h"
#include "../assets/asset_manager.h"
#include "../export/package_manifest.h"
#include "../physics/physics_system.h"
#include "../platform/window.h"
#include "../renderer/renderer.h"
#include "../scene/scene.h"
#include "../scripting/script_engine.h"

namespace Atlas::Runtime {
namespace {

constexpr int kWindowWidth = 1600;
constexpr int kWindowHeight = 900;
constexpr const char* kWindowTitle = "Atlas Runtime";

} // namespace

RuntimeApp::RuntimeApp(std::string packageRoot)
    : m_PackageRoot(std::move(packageRoot)) {
}

RuntimeApp::~RuntimeApp() {
    if (m_ScriptEngine) {
        m_ScriptEngine->destroyScene();
        m_ScriptEngine->shutdown();
    }
    if (m_PhysicsSystem) {
        m_PhysicsSystem->shutdown();
    }
    if (m_AssetManager) {
        m_AssetManager->shutdown();
    }
    if (m_Renderer) {
        m_Renderer->shutdown();
    }
}

std::string RuntimeApp::getExecutableDirectory() {
#ifdef _WIN32
    char buffer[MAX_PATH] = {};
    DWORD len = GetModuleFileNameA(nullptr, buffer, MAX_PATH);
    if (len == 0 || len == MAX_PATH) {
        return std::filesystem::current_path().string();
    }
    return std::filesystem::path(std::string(buffer, len)).parent_path().string();
#else
    std::error_code ec;
    auto exe = std::filesystem::read_symlink("/proc/self/exe", ec);
    if (ec) {
        return std::filesystem::current_path().string();
    }
    return exe.parent_path().string();
#endif
}

std::string RuntimeApp::resolvePackageRoot() const {
    if (!m_PackageRoot.empty()) {
        return std::filesystem::path(m_PackageRoot).lexically_normal().string();
    }
    return (std::filesystem::path(getExecutableDirectory()) / "game").lexically_normal().string();
}

bool RuntimeApp::initialize() {
    m_Window = std::make_unique<Window>(kWindowWidth, kWindowHeight, kWindowTitle);
    m_Renderer = std::make_unique<Renderer>(m_Window.get());
    m_Renderer->init();

    m_AssetManager = std::make_unique<AssetManager>();
    m_AssetManager->setRenderer(m_Renderer.get());

    m_Scene = std::make_unique<Scene>();

    m_PhysicsSystem = std::make_unique<Atlas::Physics::PhysicsSystem>();
    m_PhysicsSystem->initialize();

    m_ScriptEngine = std::make_unique<Atlas::Scripting::ScriptEngine>();
    m_ScriptEngine->setWindow(m_Window->getGLFWWindow());
    m_ScriptEngine->initialize();

    return true;
}

bool RuntimeApp::loadPackage() {
    m_PackageRoot = resolvePackageRoot();
    const std::filesystem::path manifestPath = std::filesystem::path(m_PackageRoot) / "package.manifest";

    Atlas::Export::PackageManifest manifest;
    if (!Atlas::Export::loadPackageManifest(manifestPath.string(), manifest)) {
        std::cerr << "Failed to load package manifest: " << manifestPath.string() << std::endl;
        return false;
    }

    m_AssetsRoot = (std::filesystem::path(m_PackageRoot) / manifest.assetsRoot).lexically_normal().string();
    m_StartupScenePath = (std::filesystem::path(m_PackageRoot) / manifest.startupScene).lexically_normal().string();

    m_Renderer->setPreferGameCamera(true);
    m_ScriptEngine->setAssetsRoot(m_AssetsRoot);
    m_ScriptEngine->setInputEnabled(true);

    if (!Atlas::Runtime::loadRuntimeSceneFromFile(*m_Scene, *m_Renderer, *m_AssetManager, m_StartupScenePath, m_AssetsRoot)) {
        std::cerr << "Failed to load runtime scene: " << m_StartupScenePath << std::endl;
        return false;
    }

    m_PhysicsSystem->rebuild(m_Scene.get());
    m_ScriptEngine->instantiateScene(m_Scene.get());
    m_ScriptEngine->callStart();
    return true;
}

void RuntimeApp::updateGameCameras(float) {
    if (!m_Scene) {
        return;
    }
    auto& registry = m_Scene->getRegistry();
    auto view = registry.view<Transform, Camera, Atlas::ECS::GameCameraComponent>(entt::exclude<Atlas::ECS::FollowCameraComponent>);
    for (auto entity : view) {
        auto& camera = view.get<Camera>(entity);
        glm::mat4 world = m_Scene->getWorldTransform(entity);
        glm::vec3 position = glm::vec3(world[3]);
        glm::vec3 forward = glm::vec3(world * glm::vec4(0.0f, 0.0f, -1.0f, 0.0f));
        glm::vec3 up = glm::vec3(world * glm::vec4(0.0f, 1.0f, 0.0f, 0.0f));
        if (glm::length(forward) > 1e-5f) {
            forward = glm::normalize(forward);
        } else {
            forward = glm::vec3(0.0f, 0.0f, -1.0f);
        }
        if (glm::length(up) > 1e-5f) {
            up = glm::normalize(up);
        } else {
            up = glm::vec3(0.0f, 1.0f, 0.0f);
        }
        camera.position = position;
        camera.target = position + forward;
        camera.up = up;
    }
}

void RuntimeApp::updateFollowCameras(float deltaTime) {
    if (!m_Scene) {
        return;
    }
    auto& registry = m_Scene->getRegistry();
    auto view = registry.view<Camera, Atlas::ECS::FollowCameraComponent>();
    for (auto entity : view) {
        auto& camera = view.get<Camera>(entity);
        auto& follow = view.get<Atlas::ECS::FollowCameraComponent>(entity);
        if (follow.target == entt::null || !registry.valid(follow.target) || !registry.all_of<Transform>(follow.target)) {
            continue;
        }
        glm::mat4 targetWorld = m_Scene->getWorldTransform(follow.target);
        glm::vec3 targetPos = glm::vec3(targetWorld[3]);
        glm::vec3 desiredPos = targetPos + follow.offset;
        float alpha = 1.0f - std::exp(-std::max(0.0f, follow.smoothness) * deltaTime);
        camera.position = glm::mix(camera.position, desiredPos, alpha);
        if (follow.lookAtTarget) {
            camera.target = targetPos;
        }
    }
}

void RuntimeApp::run() {
    if (!initialize()) {
        throw std::runtime_error("Failed to initialize runtime application");
    }
    if (!loadPackage()) {
        throw std::runtime_error("Failed to load runtime package");
    }

    auto lastFrame = std::chrono::high_resolution_clock::now();
    while (!m_Window->shouldClose()) {
        m_Window->update();
        if (m_Window->isMinimized()) {
            continue;
        }

        auto now = std::chrono::high_resolution_clock::now();
        float deltaTime = std::chrono::duration<float>(now - lastFrame).count();
        lastFrame = now;

        updateGameCameras(deltaTime);
        m_ScriptEngine->update(deltaTime);
        m_PhysicsSystem->step(m_Scene.get(), deltaTime);
        updateFollowCameras(deltaTime);
        m_Scene->updateWorldTransforms();

        m_Renderer->beginFrame();
        m_Renderer->renderScene(m_Scene.get());
        m_Renderer->endFrame();
    }
}

} // namespace Atlas::Runtime

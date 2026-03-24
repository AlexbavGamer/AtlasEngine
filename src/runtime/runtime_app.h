#pragma once

#include <memory>
#include <string>

namespace Atlas {
class AssetManager;
class Renderer;
class Scene;
class Window;
}

namespace Atlas::Physics { class PhysicsSystem; }
namespace Atlas::Scripting { class ScriptEngine; }

namespace Atlas::Runtime {

class RuntimeApp {
public:
    explicit RuntimeApp(std::string packageRoot = std::string());
    ~RuntimeApp();

    void run();

private:
    bool initialize();
    bool loadPackage();
    std::string resolvePackageRoot() const;
    void updateGameCameras(float deltaTime);
    void updateFollowCameras(float deltaTime);
    static std::string getExecutableDirectory();

    std::string m_PackageRoot;
    std::string m_AssetsRoot;
    std::string m_StartupScenePath;

    std::unique_ptr<Window> m_Window;
    std::unique_ptr<Renderer> m_Renderer;
    std::unique_ptr<AssetManager> m_AssetManager;
    std::unique_ptr<Scene> m_Scene;
    std::unique_ptr<Atlas::Physics::PhysicsSystem> m_PhysicsSystem;
    std::unique_ptr<Atlas::Scripting::ScriptEngine> m_ScriptEngine;
};

} // namespace Atlas::Runtime

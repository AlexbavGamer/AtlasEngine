#pragma once

#include <string>

namespace Atlas {
class AssetManager;
class Renderer;
class Scene;
}

namespace Atlas::Runtime {

bool loadRuntimeSceneFromFile(Scene& scene,
                              Renderer& renderer,
                              AssetManager& assetManager,
                              const std::string& scenePath,
                              const std::string& assetsRoot);

} // namespace Atlas::Runtime

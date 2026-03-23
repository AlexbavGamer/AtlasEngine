#pragma once

#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#include <entt/entt.hpp>
#include <glm/glm.hpp>

#include "../core/game_input.h"
#include "script_types.h"

struct GLFWwindow;
extern "C" {
struct lua_State;
}

namespace Atlas {
class Scene;

namespace Scripting {

class ScriptEngine {
public:
    ScriptEngine();
    ~ScriptEngine();

    bool initialize();
    void shutdown();

    void setWindow(GLFWwindow* window);
    GLFWwindow* getWindow() const { return m_Window; }
    void setAssetsRoot(const std::string& assetsRoot) { m_AssetsRoot = assetsRoot; }
    void setInputEnabled(bool enabled);
    bool isInputEnabled() const;
    const Atlas::GameInput& getGameInput() const { return m_GameInput; }

    bool instantiateScene(Scene* scene);
    void updateMouseState();
    void setMouseCaptured(bool captured);
    bool isMouseCaptured() const;
    glm::vec2 getMousePosition() const;
    glm::vec2 getMouseDelta() const;

    void callStart();
    void update(float deltaTime);
    void destroyScene();
    bool reloadScene();

    std::string getEntityError(entt::entity entity) const;

    static std::vector<ScriptFieldDefinition> inspectScript(const std::string& scriptPath, std::string* outError = nullptr);
    static void syncComponentFields(const std::vector<ScriptFieldDefinition>& defs, ScriptFieldMap& fields);

private:
    struct EntityHandle {
        Scene* scene = nullptr;
        entt::entity entity = entt::null;

        bool isValid() const;
        std::string getName() const;
        glm::vec3 getPosition() const;
        void setPosition(const glm::vec3& value) const;
        glm::vec3 getRotation() const;
        void setRotation(const glm::vec3& value) const;
        void destroy() const;
    };

    struct ScriptInstance {
        entt::entity entity = entt::null;
        size_t scriptIndex = 0;
        std::string scriptPath;
        int tableRef = -2;
        int onCreateRef = -2;
        int onStartRef = -2;
        int onUpdateRef = -2;
        int onDestroyRef = -2;
        bool created = false;
        std::string error;
    };

    bool registerBindings();
    bool createInstance(Scene* scene, entt::entity entity, size_t scriptIndex);
    std::string resolveScriptPath(const std::string& scriptPath) const;
    bool callMember(ScriptInstance& instance, int fnRef, float deltaTime = 0.0f, bool passDt = false);
    void storeEntityError(entt::entity entity, const std::string& error);

    static bool parseFieldDefinitions(lua_State* L, const std::string& absolutePath,
                                      std::vector<ScriptFieldDefinition>& outDefs, std::string& outError);
    static ScriptFieldValue readFieldValue(lua_State* L, int index, ScriptFieldType expectedType);
    static void pushFieldValue(lua_State* L, const ScriptFieldValue& value);

    lua_State* m_Lua = nullptr;
    Scene* m_RuntimeScene = nullptr;
    GLFWwindow* m_Window = nullptr;
    std::string m_AssetsRoot;
    std::vector<ScriptInstance> m_Instances;
    std::unordered_map<uint32_t, std::string> m_Errors;
    Atlas::GameInput m_GameInput;
    bool m_Initialized = false;
};

} // namespace Scripting
} // namespace Atlas

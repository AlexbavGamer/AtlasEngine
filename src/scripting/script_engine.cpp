#include "script_engine.h"

#include <GLFW/glfw3.h>

#include <algorithm>
#include <filesystem>
#include <iostream>
#include <sstream>

extern "C" {
#include <lauxlib.h>
#include <lua.h>
#include <lualib.h>
}

#include "../core/runtime_console.h"
#include "../ecs/components/components.h"
#include "../scene/scene.h"

namespace Atlas::Scripting {
namespace {

constexpr int kNoRef = LUA_NOREF;

ScriptFieldType fieldTypeFromString(const std::string& type) {
    if (type == "bool") return ScriptFieldType::Bool;
    if (type == "int") return ScriptFieldType::Int;
    if (type == "float") return ScriptFieldType::Float;
    if (type == "string") return ScriptFieldType::String;
    if (type == "vec2") return ScriptFieldType::Vec2;
    if (type == "vec3") return ScriptFieldType::Vec3;
    if (type == "vec4") return ScriptFieldType::Vec4;
    return ScriptFieldType::None;
}

ScriptFieldValue defaultValueForType(ScriptFieldType type) {
    switch (type) {
    case ScriptFieldType::Bool: return false;
    case ScriptFieldType::Int: return 0;
    case ScriptFieldType::Float: return 0.0f;
    case ScriptFieldType::String: return std::string();
    case ScriptFieldType::Vec2: return glm::vec2(0.0f);
    case ScriptFieldType::Vec3: return glm::vec3(0.0f);
    case ScriptFieldType::Vec4: return glm::vec4(0.0f);
    default: return std::monostate{};
    }
}

std::string luaErrorString(lua_State* L) {
    const char* msg = lua_tostring(L, -1);
    std::string out = msg ? std::string(msg) : std::string("Unknown Lua error");
    lua_pop(L, 1);
    return out;
}

float getTableNumberField(lua_State* L, int index, const char* key, int arrayIndex) {
    index = lua_absindex(L, index);
    float value = 0.0f;

    lua_getfield(L, index, key);
    if (lua_isnumber(L, -1)) {
        value = static_cast<float>(lua_tonumber(L, -1));
        lua_pop(L, 1);
        return value;
    }
    lua_pop(L, 1);

    lua_geti(L, index, arrayIndex);
    if (lua_isnumber(L, -1)) {
        value = static_cast<float>(lua_tonumber(L, -1));
    }
    lua_pop(L, 1);
    return value;
}

bool readVecFromTable(lua_State* L, int index, glm::vec2& out) {
    if (!lua_istable(L, index)) return false;
    out.x = getTableNumberField(L, index, "x", 1);
    out.y = getTableNumberField(L, index, "y", 2);
    return true;
}

bool readVecFromTable(lua_State* L, int index, glm::vec3& out) {
    if (!lua_istable(L, index)) return false;
    out.x = getTableNumberField(L, index, "x", 1);
    out.y = getTableNumberField(L, index, "y", 2);
    out.z = getTableNumberField(L, index, "z", 3);
    return true;
}

bool readVecFromTable(lua_State* L, int index, glm::vec4& out) {
    if (!lua_istable(L, index)) return false;
    out.x = getTableNumberField(L, index, "x", 1);
    out.y = getTableNumberField(L, index, "y", 2);
    out.z = getTableNumberField(L, index, "z", 3);
    out.w = getTableNumberField(L, index, "w", 4);
    return true;
}

void pushVec(lua_State* L, const glm::vec2& v) {
    lua_createtable(L, 2, 2);
    lua_pushnumber(L, v.x); lua_setfield(L, -2, "x");
    lua_pushnumber(L, v.y); lua_setfield(L, -2, "y");
    lua_pushnumber(L, v.x); lua_seti(L, -2, 1);
    lua_pushnumber(L, v.y); lua_seti(L, -2, 2);
}

void pushVec(lua_State* L, const glm::vec3& v) {
    lua_createtable(L, 3, 3);
    lua_pushnumber(L, v.x); lua_setfield(L, -2, "x");
    lua_pushnumber(L, v.y); lua_setfield(L, -2, "y");
    lua_pushnumber(L, v.z); lua_setfield(L, -2, "z");
    lua_pushnumber(L, v.x); lua_seti(L, -2, 1);
    lua_pushnumber(L, v.y); lua_seti(L, -2, 2);
    lua_pushnumber(L, v.z); lua_seti(L, -2, 3);
}

void pushVec(lua_State* L, const glm::vec4& v) {
    lua_createtable(L, 4, 4);
    lua_pushnumber(L, v.x); lua_setfield(L, -2, "x");
    lua_pushnumber(L, v.y); lua_setfield(L, -2, "y");
    lua_pushnumber(L, v.z); lua_setfield(L, -2, "z");
    lua_pushnumber(L, v.w); lua_setfield(L, -2, "w");
    lua_pushnumber(L, v.x); lua_seti(L, -2, 1);
    lua_pushnumber(L, v.y); lua_seti(L, -2, 2);
    lua_pushnumber(L, v.z); lua_seti(L, -2, 3);
    lua_pushnumber(L, v.w); lua_seti(L, -2, 4);
}

ScriptEngine* getEngine(lua_State* L) {
    lua_getfield(L, LUA_REGISTRYINDEX, "Atlas.ScriptEnginePtr");
    ScriptEngine* engine = static_cast<ScriptEngine*>(lua_touserdata(L, -1));
    lua_pop(L, 1);
    return engine;
}

Scene* getSceneUpvalue(lua_State* L) {
    return static_cast<Scene*>(lua_touserdata(L, lua_upvalueindex(1)));
}

entt::entity getEntityUpvalue(lua_State* L) {
    return static_cast<entt::entity>(static_cast<uint32_t>(lua_tointeger(L, lua_upvalueindex(2))));
}

int l_vec3(lua_State* L) {
    const float x = static_cast<float>(luaL_optnumber(L, 1, 0.0));
    const float y = static_cast<float>(luaL_optnumber(L, 2, 0.0));
    const float z = static_cast<float>(luaL_optnumber(L, 3, 0.0));
    pushVec(L, glm::vec3(x, y, z));
    return 1;
}

int l_log_info(lua_State* L) {
    const char* msg = luaL_optstring(L, 1, "");
    const std::string line = std::string("[Lua] ") + msg;
    Atlas::ConsoleInfo(line);
    std::cout << line << std::endl;
    return 0;
}

int l_log_warn(lua_State* L) {
    const char* msg = luaL_optstring(L, 1, "");
    const std::string line = std::string("[Lua][Warn] ") + msg;
    Atlas::ConsoleWarn(line);
    std::cout << line << std::endl;
    return 0;
}

int l_log_error(lua_State* L) {
    const char* msg = luaL_optstring(L, 1, "");
    const std::string line = std::string("[Lua][Error] ") + msg;
    Atlas::ConsoleError(line);
    std::cerr << line << std::endl;
    return 0;
}

int l_print(lua_State* L) {
    const int argc = lua_gettop(L);
    std::ostringstream oss;
    for (int i = 1; i <= argc; ++i) {
        size_t len = 0;
        const char* str = luaL_tolstring(L, i, &len);
        if (i > 1) {
            oss << ' ';
        }
        if (str) {
            oss.write(str, static_cast<std::streamsize>(len));
        }
        lua_pop(L, 1);
    }
    const std::string line = std::string("[Lua] ") + oss.str();
    Atlas::ConsoleInfo(line);
    std::cout << line << std::endl;
    return 0;
}

int l_input_is_key_down(lua_State* L) {
    ScriptEngine* engine = getEngine(L);
    const int key = static_cast<int>(luaL_checkinteger(L, 1));
    if (!engine) {
        lua_pushboolean(L, 0);
        return 1;
    }
    lua_pushboolean(L, engine->getWindow() && glfwGetKey(engine->getWindow(), key) == GLFW_PRESS);
    return 1;
}

int l_input_is_mouse_button_down(lua_State* L) {
    ScriptEngine* engine = getEngine(L);
    const int button = static_cast<int>(luaL_checkinteger(L, 1));
    if (!engine) {
        lua_pushboolean(L, 0);
        return 1;
    }
    lua_pushboolean(L, engine->getWindow() && glfwGetMouseButton(engine->getWindow(), button) == GLFW_PRESS);
    return 1;
}

int l_input_get_mouse_position(lua_State* L) {
    ScriptEngine* engine = getEngine(L);
    if (!engine) {
        pushVec(L, glm::vec2(0.0f));
        return 1;
    }
    pushVec(L, engine->getMousePosition());
    return 1;
}

int l_input_get_mouse_delta(lua_State* L) {
    ScriptEngine* engine = getEngine(L);
    if (!engine) {
        pushVec(L, glm::vec2(0.0f));
        return 1;
    }
    pushVec(L, engine->getMouseDelta());
    return 1;
}

int l_input_set_mouse_captured(lua_State* L) {
    ScriptEngine* engine = getEngine(L);
    const bool captured = lua_toboolean(L, 1) != 0;
    if (engine) {
        engine->setMouseCaptured(captured);
    }
    return 0;
}

int l_input_is_mouse_captured(lua_State* L) {
    ScriptEngine* engine = getEngine(L);
    lua_pushboolean(L, engine && engine->isMouseCaptured());
    return 1;
}

int l_entity_is_valid(lua_State* L) {
    Scene* scene = getSceneUpvalue(L);
    entt::entity entity = getEntityUpvalue(L);
    lua_pushboolean(L, scene && scene->getRegistry().valid(entity));
    return 1;
}

int l_entity_get_name(lua_State* L) {
    Scene* scene = getSceneUpvalue(L);
    entt::entity entity = getEntityUpvalue(L);
    if (!scene || !scene->getRegistry().valid(entity) || !scene->getRegistry().all_of<ECS::TagComponent>(entity)) {
        lua_pushliteral(L, "");
        return 1;
    }
    lua_pushstring(L, scene->getRegistry().get<ECS::TagComponent>(entity).name.c_str());
    return 1;
}

int l_entity_get_position(lua_State* L) {
    Scene* scene = getSceneUpvalue(L);
    entt::entity entity = getEntityUpvalue(L);
    glm::vec3 value(0.0f);
    if (scene && scene->getRegistry().valid(entity) && scene->getRegistry().all_of<Transform>(entity)) {
        value = scene->getRegistry().get<Transform>(entity).position;
    }
    pushVec(L, value);
    return 1;
}

int l_entity_set_position(lua_State* L) {
    Scene* scene = getSceneUpvalue(L);
    entt::entity entity = getEntityUpvalue(L);
    if (!scene || !scene->getRegistry().valid(entity) || !scene->getRegistry().all_of<Transform>(entity)) {
        return 0;
    }
    glm::vec3 value(0.0f);
    if (lua_istable(L, 2)) {
        readVecFromTable(L, 2, value);
    }
    scene->getRegistry().get<Transform>(entity).position = value;
    scene->setDirty(true);
    return 0;
}

int l_entity_get_rotation(lua_State* L) {
    Scene* scene = getSceneUpvalue(L);
    entt::entity entity = getEntityUpvalue(L);
    glm::vec3 value(0.0f);
    if (scene && scene->getRegistry().valid(entity) && scene->getRegistry().all_of<Transform>(entity)) {
        value = scene->getRegistry().get<Transform>(entity).rotation;
    }
    pushVec(L, value);
    return 1;
}

int l_entity_set_rotation(lua_State* L) {
    Scene* scene = getSceneUpvalue(L);
    entt::entity entity = getEntityUpvalue(L);
    if (!scene || !scene->getRegistry().valid(entity) || !scene->getRegistry().all_of<Transform>(entity)) {
        return 0;
    }
    glm::vec3 value(0.0f);
    if (lua_istable(L, 2)) {
        readVecFromTable(L, 2, value);
    }
    scene->getRegistry().get<Transform>(entity).rotation = value;
    scene->setDirty(true);
    return 0;
}

int l_entity_destroy(lua_State* L) {
    Scene* scene = getSceneUpvalue(L);
    entt::entity entity = getEntityUpvalue(L);
    if (scene && scene->getRegistry().valid(entity)) {
        scene->destroyEntity(entity);
    }
    return 0;
}

glm::vec3 getTransformBasis(Transform transform, const glm::vec3& axis) {
    transform.position = glm::vec3(0.0f);
    transform.scale = glm::vec3(1.0f);
    glm::vec3 dir = glm::vec3(transform.getModelMatrix() * glm::vec4(axis, 0.0f));
    const float len = glm::length(dir);
    if (len <= 1e-5f) {
        return axis;
    }
    return dir / len;
}

int l_entity_get_forward(lua_State* L) {
    Scene* scene = getSceneUpvalue(L);
    entt::entity entity = getEntityUpvalue(L);
    glm::vec3 value(0.0f, 0.0f, -1.0f);
    if (scene && scene->getRegistry().valid(entity) && scene->getRegistry().all_of<Transform>(entity)) {
        value = getTransformBasis(scene->getRegistry().get<Transform>(entity), glm::vec3(0.0f, 0.0f, -1.0f));
    }
    pushVec(L, value);
    return 1;
}

int l_entity_get_right(lua_State* L) {
    Scene* scene = getSceneUpvalue(L);
    entt::entity entity = getEntityUpvalue(L);
    glm::vec3 value(1.0f, 0.0f, 0.0f);
    if (scene && scene->getRegistry().valid(entity) && scene->getRegistry().all_of<Transform>(entity)) {
        value = getTransformBasis(scene->getRegistry().get<Transform>(entity), glm::vec3(1.0f, 0.0f, 0.0f));
    }
    pushVec(L, value);
    return 1;
}

int l_entity_get_up(lua_State* L) {
    Scene* scene = getSceneUpvalue(L);
    entt::entity entity = getEntityUpvalue(L);
    glm::vec3 value(0.0f, 1.0f, 0.0f);
    if (scene && scene->getRegistry().valid(entity) && scene->getRegistry().all_of<Transform>(entity)) {
        value = getTransformBasis(scene->getRegistry().get<Transform>(entity), glm::vec3(0.0f, 1.0f, 0.0f));
    }
    pushVec(L, value);
    return 1;
}

void pushEntityTable(lua_State* L, Scene* scene, entt::entity entity) {
    lua_createtable(L, 0, 10);

    lua_pushlightuserdata(L, scene);
    lua_pushinteger(L, static_cast<lua_Integer>(static_cast<uint32_t>(entity)));
    lua_pushcclosure(L, l_entity_is_valid, 2);
    lua_setfield(L, -2, "IsValid");

    lua_pushlightuserdata(L, scene);
    lua_pushinteger(L, static_cast<lua_Integer>(static_cast<uint32_t>(entity)));
    lua_pushcclosure(L, l_entity_get_name, 2);
    lua_setfield(L, -2, "GetName");

    lua_pushlightuserdata(L, scene);
    lua_pushinteger(L, static_cast<lua_Integer>(static_cast<uint32_t>(entity)));
    lua_pushcclosure(L, l_entity_get_position, 2);
    lua_setfield(L, -2, "GetPosition");

    lua_pushlightuserdata(L, scene);
    lua_pushinteger(L, static_cast<lua_Integer>(static_cast<uint32_t>(entity)));
    lua_pushcclosure(L, l_entity_set_position, 2);
    lua_setfield(L, -2, "SetPosition");

    lua_pushlightuserdata(L, scene);
    lua_pushinteger(L, static_cast<lua_Integer>(static_cast<uint32_t>(entity)));
    lua_pushcclosure(L, l_entity_get_rotation, 2);
    lua_setfield(L, -2, "GetRotation");

    lua_pushlightuserdata(L, scene);
    lua_pushinteger(L, static_cast<lua_Integer>(static_cast<uint32_t>(entity)));
    lua_pushcclosure(L, l_entity_set_rotation, 2);
    lua_setfield(L, -2, "SetRotation");

    lua_pushlightuserdata(L, scene);
    lua_pushinteger(L, static_cast<lua_Integer>(static_cast<uint32_t>(entity)));
    lua_pushcclosure(L, l_entity_get_forward, 2);
    lua_setfield(L, -2, "GetForward");

    lua_pushlightuserdata(L, scene);
    lua_pushinteger(L, static_cast<lua_Integer>(static_cast<uint32_t>(entity)));
    lua_pushcclosure(L, l_entity_get_right, 2);
    lua_setfield(L, -2, "GetRight");

    lua_pushlightuserdata(L, scene);
    lua_pushinteger(L, static_cast<lua_Integer>(static_cast<uint32_t>(entity)));
    lua_pushcclosure(L, l_entity_get_up, 2);
    lua_setfield(L, -2, "GetUp");

    lua_pushlightuserdata(L, scene);
    lua_pushinteger(L, static_cast<lua_Integer>(static_cast<uint32_t>(entity)));
    lua_pushcclosure(L, l_entity_destroy, 2);
    lua_setfield(L, -2, "Destroy");
}

int refFunctionField(lua_State* L, int tableRef, const char* name) {
    lua_rawgeti(L, LUA_REGISTRYINDEX, tableRef);
    lua_getfield(L, -1, name);
    int ref = kNoRef;
    if (lua_isfunction(L, -1)) {
        ref = luaL_ref(L, LUA_REGISTRYINDEX);
    } else {
        lua_pop(L, 1);
    }
    lua_pop(L, 1);
    return ref;
}

ScriptFieldValue readFieldValueFromLua(lua_State* L, int index, ScriptFieldType expectedType) {
    index = lua_absindex(L, index);
    if (lua_isnil(L, index)) {
        return defaultValueForType(expectedType);
    }

    switch (expectedType) {
    case ScriptFieldType::Bool:
        return ScriptFieldValue(lua_toboolean(L, index) != 0);
    case ScriptFieldType::Int:
        return ScriptFieldValue(static_cast<int>(lua_tointeger(L, index)));
    case ScriptFieldType::Float:
        return ScriptFieldValue(static_cast<float>(lua_tonumber(L, index)));
    case ScriptFieldType::String:
        return ScriptFieldValue(std::string(lua_tostring(L, index) ? lua_tostring(L, index) : ""));
    case ScriptFieldType::Vec2: {
        glm::vec2 v(0.0f);
        readVecFromTable(L, index, v);
        return ScriptFieldValue(v);
    }
    case ScriptFieldType::Vec3: {
        glm::vec3 v(0.0f);
        readVecFromTable(L, index, v);
        return ScriptFieldValue(v);
    }
    case ScriptFieldType::Vec4: {
        glm::vec4 v(0.0f);
        readVecFromTable(L, index, v);
        return ScriptFieldValue(v);
    }
    default:
        return std::monostate{};
    }
}

bool parseFieldDefinitionsFromLoadedTable(lua_State* L, int tableIndex,
    std::vector<ScriptFieldDefinition>& outDefs, std::string& outError) {
    tableIndex = lua_absindex(L, tableIndex);
    if (!lua_istable(L, tableIndex)) {
        outError = "Script must return a table";
        return false;
    }

    lua_getfield(L, tableIndex, "__fields");
    if (!lua_istable(L, -1)) {
        lua_pop(L, 1);
        outDefs.clear();
        return true;
    }

    outDefs.clear();
    lua_pushnil(L);
    while (lua_next(L, -2) != 0) {
        if (lua_type(L, -2) == LUA_TSTRING && lua_istable(L, -1)) {
            const char* key = lua_tostring(L, -2);
            lua_getfield(L, -1, "type");
            std::string typeName = lua_isstring(L, -1) ? lua_tostring(L, -1) : "";
            lua_pop(L, 1);

            ScriptFieldType type = fieldTypeFromString(typeName);
            if (type != ScriptFieldType::None) {
                ScriptFieldDefinition def;
                def.name = key ? std::string(key) : std::string();
                def.type = type;
                def.defaultValue = defaultValueForType(type);

                lua_getfield(L, -1, "default");
                if (!lua_isnil(L, -1)) {
                    def.defaultValue = readFieldValueFromLua(L, -1, type);
                }
                lua_pop(L, 1);

                outDefs.push_back(std::move(def));
            }
        }
        lua_pop(L, 1);
    }

    lua_pop(L, 1);
    return true;
}

} // namespace

ScriptEngine::ScriptEngine() = default;

ScriptEngine::~ScriptEngine() {
    shutdown();
}

bool ScriptEngine::initialize() {
    if (m_Initialized) {
        return true;
    }

    m_Lua = luaL_newstate();
    if (!m_Lua) {
        return false;
    }

    luaL_openlibs(m_Lua);
    if (!registerBindings()) {
        shutdown();
        return false;
    }

    m_Initialized = true;
    return true;
}

void ScriptEngine::shutdown() {
    destroyScene();
    if (m_Window && m_MouseCaptured) {
        glfwSetInputMode(m_Window, GLFW_CURSOR, GLFW_CURSOR_NORMAL);
    }
    m_MouseCaptured = false;
    m_HasMousePosition = false;
    m_MouseDelta = glm::vec2(0.0f);
    if (m_Lua) {
        lua_close(m_Lua);
        m_Lua = nullptr;
    }
    m_Initialized = false;
}

bool ScriptEngine::registerBindings() {
    lua_pushlightuserdata(m_Lua, this);
    lua_setfield(m_Lua, LUA_REGISTRYINDEX, "Atlas.ScriptEnginePtr");

    lua_pushcfunction(m_Lua, l_vec3);
    lua_setglobal(m_Lua, "vec3");

    lua_pushcfunction(m_Lua, l_print);
    lua_setglobal(m_Lua, "print");

    lua_newtable(m_Lua);
    lua_pushcfunction(m_Lua, l_log_info); lua_setfield(m_Lua, -2, "Info");
    lua_pushcfunction(m_Lua, l_log_warn); lua_setfield(m_Lua, -2, "Warn");
    lua_pushcfunction(m_Lua, l_log_error); lua_setfield(m_Lua, -2, "Error");
    lua_setglobal(m_Lua, "Log");

    lua_newtable(m_Lua);
    lua_pushcfunction(m_Lua, l_input_is_key_down); lua_setfield(m_Lua, -2, "IsKeyDown");
    lua_pushcfunction(m_Lua, l_input_is_mouse_button_down); lua_setfield(m_Lua, -2, "IsMouseButtonDown");
    lua_pushcfunction(m_Lua, l_input_get_mouse_position); lua_setfield(m_Lua, -2, "GetMousePosition");
    lua_pushcfunction(m_Lua, l_input_get_mouse_delta); lua_setfield(m_Lua, -2, "GetMouseDelta");
    lua_pushcfunction(m_Lua, l_input_set_mouse_captured); lua_setfield(m_Lua, -2, "SetMouseCaptured");
    lua_pushcfunction(m_Lua, l_input_is_mouse_captured); lua_setfield(m_Lua, -2, "IsMouseCaptured");
    lua_setglobal(m_Lua, "Input");

    lua_newtable(m_Lua);
    lua_pushinteger(m_Lua, GLFW_KEY_W); lua_setfield(m_Lua, -2, "W");
    lua_pushinteger(m_Lua, GLFW_KEY_A); lua_setfield(m_Lua, -2, "A");
    lua_pushinteger(m_Lua, GLFW_KEY_S); lua_setfield(m_Lua, -2, "S");
    lua_pushinteger(m_Lua, GLFW_KEY_D); lua_setfield(m_Lua, -2, "D");
    lua_pushinteger(m_Lua, GLFW_KEY_SPACE); lua_setfield(m_Lua, -2, "Space");
    lua_pushinteger(m_Lua, GLFW_KEY_LEFT_SHIFT); lua_setfield(m_Lua, -2, "LeftShift");
    lua_pushinteger(m_Lua, GLFW_KEY_LEFT_CONTROL); lua_setfield(m_Lua, -2, "LeftControl");
    lua_pushinteger(m_Lua, GLFW_KEY_ESCAPE); lua_setfield(m_Lua, -2, "Escape");
    lua_setglobal(m_Lua, "Key");

    lua_newtable(m_Lua);
    lua_pushinteger(m_Lua, GLFW_MOUSE_BUTTON_LEFT); lua_setfield(m_Lua, -2, "Left");
    lua_pushinteger(m_Lua, GLFW_MOUSE_BUTTON_RIGHT); lua_setfield(m_Lua, -2, "Right");
    lua_pushinteger(m_Lua, GLFW_MOUSE_BUTTON_MIDDLE); lua_setfield(m_Lua, -2, "Middle");
    lua_setglobal(m_Lua, "Mouse");

    return true;
}

bool ScriptEngine::instantiateScene(Scene* scene) {
    destroyScene();

    if (!scene) {
        return false;
    }
    if (!m_Initialized && !initialize()) {
        return false;
    }

    m_RuntimeScene = scene;
    auto& registry = scene->getRegistry();
    auto view = registry.view<ECS::ScriptComponent>();
    for (auto entity : view) {
        const auto& sc = view.get<ECS::ScriptComponent>(entity);
        for (size_t scriptIndex = 0; scriptIndex < sc.scripts.size(); ++scriptIndex) {
            createInstance(scene, entity, scriptIndex);
        }
    }

    for (auto& instance : m_Instances) {
        if (instance.onCreateRef != kNoRef) {
            callMember(instance, instance.onCreateRef);
            instance.created = true;
        }
    }

    return true;
}

void ScriptEngine::callStart() {
    for (auto& instance : m_Instances) {
        if (instance.onStartRef != kNoRef) {
            callMember(instance, instance.onStartRef);
        }
    }
}

void ScriptEngine::updateMouseState() {
    if (!m_Window) {
        m_MousePosition = glm::vec2(0.0f);
        m_MouseDelta = glm::vec2(0.0f);
        m_HasMousePosition = false;
        return;
    }

    double mouseX = 0.0;
    double mouseY = 0.0;
    glfwGetCursorPos(m_Window, &mouseX, &mouseY);
    glm::vec2 current(static_cast<float>(mouseX), static_cast<float>(mouseY));

    if (!m_HasMousePosition) {
        m_MousePosition = current;
        m_MouseDelta = glm::vec2(0.0f);
        m_HasMousePosition = true;
        return;
    }

    m_MouseDelta = current - m_MousePosition;
    m_MousePosition = current;
}

void ScriptEngine::setMouseCaptured(bool captured) {
    m_MouseCaptured = captured;
    if (!m_Window) {
        return;
    }

    glfwSetInputMode(m_Window, GLFW_CURSOR, captured ? GLFW_CURSOR_DISABLED : GLFW_CURSOR_NORMAL);

    double mouseX = 0.0;
    double mouseY = 0.0;
    glfwGetCursorPos(m_Window, &mouseX, &mouseY);
    m_MousePosition = glm::vec2(static_cast<float>(mouseX), static_cast<float>(mouseY));
    m_MouseDelta = glm::vec2(0.0f);
    m_HasMousePosition = true;
}

void ScriptEngine::update(float deltaTime) {
    updateMouseState();

    if (!m_RuntimeScene) {
        return;
    }

    auto& registry = m_RuntimeScene->getRegistry();
    for (auto& instance : m_Instances) {
        entt::entity entity = instance.entity;
        if (!registry.valid(entity) || !registry.all_of<ECS::ScriptComponent>(entity)) {
            continue;
        }
        auto& sc = registry.get<ECS::ScriptComponent>(entity);
        if (instance.scriptIndex >= sc.scripts.size()) {
            continue;
        }
        const auto& script = sc.scripts[instance.scriptIndex];
        if (!script.enabled || instance.onUpdateRef == kNoRef) {
            continue;
        }
        callMember(instance, instance.onUpdateRef, deltaTime, true);
    }
}

void ScriptEngine::destroyScene() {
    if (!m_Lua) {
        m_Instances.clear();
        m_Errors.clear();
        m_RuntimeScene = nullptr;
        return;
    }

    for (auto it = m_Instances.rbegin(); it != m_Instances.rend(); ++it) {
        if (it->onDestroyRef != kNoRef) {
            callMember(*it, it->onDestroyRef);
        }

        if (it->tableRef != kNoRef) luaL_unref(m_Lua, LUA_REGISTRYINDEX, it->tableRef);
        if (it->onCreateRef != kNoRef) luaL_unref(m_Lua, LUA_REGISTRYINDEX, it->onCreateRef);
        if (it->onStartRef != kNoRef) luaL_unref(m_Lua, LUA_REGISTRYINDEX, it->onStartRef);
        if (it->onUpdateRef != kNoRef) luaL_unref(m_Lua, LUA_REGISTRYINDEX, it->onUpdateRef);
        if (it->onDestroyRef != kNoRef) luaL_unref(m_Lua, LUA_REGISTRYINDEX, it->onDestroyRef);
    }

    m_Instances.clear();
    m_Errors.clear();
    m_RuntimeScene = nullptr;
    setMouseCaptured(false);
}

bool ScriptEngine::reloadScene() {
    Scene* scene = m_RuntimeScene;
    destroyScene();
    if (!scene) {
        return false;
    }
    bool ok = instantiateScene(scene);
    if (ok) {
        callStart();
    }
    return ok;
}

std::string ScriptEngine::getEntityError(entt::entity entity) const {
    auto it = m_Errors.find(static_cast<uint32_t>(entity));
    return (it != m_Errors.end()) ? it->second : std::string();
}

std::vector<ScriptFieldDefinition> ScriptEngine::inspectScript(const std::string& scriptPath, std::string* outError) {
    std::vector<ScriptFieldDefinition> defs;
    lua_State* L = luaL_newstate();
    if (!L) {
        if (outError) *outError = "Failed to create Lua state";
        return defs;
    }
    luaL_openlibs(L);

    std::string error;
    if (!parseFieldDefinitions(L, scriptPath, defs, error) && outError) {
        *outError = error;
    }

    lua_close(L);
    return defs;
}

void ScriptEngine::syncComponentFields(const std::vector<ScriptFieldDefinition>& defs, ScriptFieldMap& fields) {
    ScriptFieldMap next;
    for (const auto& def : defs) {
        auto it = fields.find(def.name);
        if (it != fields.end() && getFieldType(it->second) == def.type) {
            next.emplace(def.name, it->second);
        } else {
            next.emplace(def.name, def.defaultValue);
        }
    }
    fields = std::move(next);
}

bool ScriptEngine::EntityHandle::isValid() const {
    return scene && scene->getRegistry().valid(entity);
}

std::string ScriptEngine::EntityHandle::getName() const {
    if (!isValid()) return {};
    auto& registry = scene->getRegistry();
    if (!registry.all_of<ECS::TagComponent>(entity)) return {};
    return registry.get<ECS::TagComponent>(entity).name;
}

glm::vec3 ScriptEngine::EntityHandle::getPosition() const {
    if (!isValid()) return glm::vec3(0.0f);
    auto& registry = scene->getRegistry();
    if (!registry.all_of<Transform>(entity)) return glm::vec3(0.0f);
    return registry.get<Transform>(entity).position;
}

void ScriptEngine::EntityHandle::setPosition(const glm::vec3& value) const {
    if (!isValid()) return;
    auto& registry = scene->getRegistry();
    if (!registry.all_of<Transform>(entity)) return;
    registry.get<Transform>(entity).position = value;
    scene->setDirty(true);
}

glm::vec3 ScriptEngine::EntityHandle::getRotation() const {
    if (!isValid()) return glm::vec3(0.0f);
    auto& registry = scene->getRegistry();
    if (!registry.all_of<Transform>(entity)) return glm::vec3(0.0f);
    return registry.get<Transform>(entity).rotation;
}

void ScriptEngine::EntityHandle::setRotation(const glm::vec3& value) const {
    if (!isValid()) return;
    auto& registry = scene->getRegistry();
    if (!registry.all_of<Transform>(entity)) return;
    registry.get<Transform>(entity).rotation = value;
    scene->setDirty(true);
}

void ScriptEngine::EntityHandle::destroy() const {
    if (!isValid()) return;
    scene->destroyEntity(entity);
}

bool ScriptEngine::createInstance(Scene* scene, entt::entity entity, size_t scriptIndex) {
    auto& registry = scene->getRegistry();
    auto& sc = registry.get<ECS::ScriptComponent>(entity);
    if (scriptIndex >= sc.scripts.size()) {
        return false;
    }

    auto& script = sc.scripts[scriptIndex];
    if (!script.enabled || script.scriptPath.empty()) {
        return false;
    }

    const std::string absolutePath = resolveScriptPath(script.scriptPath);
    if (luaL_loadfile(m_Lua, absolutePath.c_str()) != LUA_OK) {
        storeEntityError(entity, luaErrorString(m_Lua));
        return false;
    }
    if (lua_pcall(m_Lua, 0, 1, 0) != LUA_OK) {
        storeEntityError(entity, luaErrorString(m_Lua));
        return false;
    }
    if (!lua_istable(m_Lua, -1)) {
        lua_pop(m_Lua, 1);
        storeEntityError(entity, "Script must return a table");
        return false;
    }

    std::vector<ScriptFieldDefinition> defs;
    std::string error;
    parseFieldDefinitions(m_Lua, absolutePath, defs, error);
    syncComponentFields(defs, script.fields);

    pushEntityTable(m_Lua, scene, entity);
    lua_setfield(m_Lua, -2, "entity");

    for (const auto& [name, value] : script.fields) {
        pushFieldValue(m_Lua, value);
        lua_setfield(m_Lua, -2, name.c_str());
    }

    ScriptInstance instance;
    instance.entity = entity;
    instance.scriptIndex = scriptIndex;
    instance.scriptPath = script.scriptPath;
    instance.tableRef = luaL_ref(m_Lua, LUA_REGISTRYINDEX);
    instance.onCreateRef = refFunctionField(m_Lua, instance.tableRef, "OnCreate");
    instance.onStartRef = refFunctionField(m_Lua, instance.tableRef, "OnStart");
    instance.onUpdateRef = refFunctionField(m_Lua, instance.tableRef, "OnUpdate");
    instance.onDestroyRef = refFunctionField(m_Lua, instance.tableRef, "OnDestroy");

    m_Instances.push_back(std::move(instance));
    storeEntityError(entity, std::string());
    return true;
}

std::string ScriptEngine::resolveScriptPath(const std::string& scriptPath) const {
    std::filesystem::path path(scriptPath);
    if (path.is_absolute()) {
        return path.lexically_normal().string();
    }

    if (!m_AssetsRoot.empty()) {
        std::filesystem::path assets(m_AssetsRoot);
        return (assets / path).lexically_normal().string();
    }

    return path.lexically_normal().string();
}

bool ScriptEngine::callMember(ScriptInstance& instance, int fnRef, float deltaTime, bool passDt) {
    if (!m_Lua || fnRef == kNoRef || instance.tableRef == kNoRef) {
        return true;
    }

    lua_rawgeti(m_Lua, LUA_REGISTRYINDEX, fnRef);
    lua_rawgeti(m_Lua, LUA_REGISTRYINDEX, instance.tableRef);
    int nargs = 1;
    if (passDt) {
        lua_pushnumber(m_Lua, deltaTime);
        nargs = 2;
    }

    if (lua_pcall(m_Lua, nargs, 0, 0) != LUA_OK) {
        std::string error = luaErrorString(m_Lua);
        instance.error = error;
        storeEntityError(instance.entity, error);
        return false;
    }

    if (m_RuntimeScene) {
        auto& registry = m_RuntimeScene->getRegistry();
        if (registry.valid(instance.entity) && registry.all_of<ECS::ScriptComponent>(instance.entity)) {
            auto& sc = registry.get<ECS::ScriptComponent>(instance.entity);
            if (instance.scriptIndex < sc.scripts.size()) {
                auto& script = sc.scripts[instance.scriptIndex];
                lua_rawgeti(m_Lua, LUA_REGISTRYINDEX, instance.tableRef);
                for (auto& [name, value] : script.fields) {
                    lua_getfield(m_Lua, -1, name.c_str());
                    value = readFieldValue(m_Lua, -1, getFieldType(value));
                    lua_pop(m_Lua, 1);
                }
                lua_pop(m_Lua, 1);
            }
        }
    }

    storeEntityError(instance.entity, std::string());
    return true;
}

void ScriptEngine::storeEntityError(entt::entity entity, const std::string& error) {
    uint32_t id = static_cast<uint32_t>(entity);
    if (error.empty()) {
        m_Errors.erase(id);
    } else {
        m_Errors[id] = error;
    }
}

bool ScriptEngine::parseFieldDefinitions(lua_State* L, const std::string& absolutePath,
    std::vector<ScriptFieldDefinition>& outDefs, std::string& outError) {
    if (luaL_loadfile(L, absolutePath.c_str()) != LUA_OK) {
        outError = luaErrorString(L);
        return false;
    }
    if (lua_pcall(L, 0, 1, 0) != LUA_OK) {
        outError = luaErrorString(L);
        return false;
    }
    const bool ok = parseFieldDefinitionsFromLoadedTable(L, -1, outDefs, outError);
    lua_pop(L, 1);
    return ok;
}

ScriptFieldValue ScriptEngine::readFieldValue(lua_State* L, int index, ScriptFieldType expectedType) {
    return readFieldValueFromLua(L, index, expectedType);
}

void ScriptEngine::pushFieldValue(lua_State* L, const ScriptFieldValue& value) {
    if (std::holds_alternative<bool>(value)) {
        lua_pushboolean(L, std::get<bool>(value));
    } else if (std::holds_alternative<int>(value)) {
        lua_pushinteger(L, std::get<int>(value));
    } else if (std::holds_alternative<float>(value)) {
        lua_pushnumber(L, std::get<float>(value));
    } else if (std::holds_alternative<std::string>(value)) {
        lua_pushstring(L, std::get<std::string>(value).c_str());
    } else if (std::holds_alternative<glm::vec2>(value)) {
        pushVec(L, std::get<glm::vec2>(value));
    } else if (std::holds_alternative<glm::vec3>(value)) {
        pushVec(L, std::get<glm::vec3>(value));
    } else if (std::holds_alternative<glm::vec4>(value)) {
        pushVec(L, std::get<glm::vec4>(value));
    } else {
        lua_pushnil(L);
    }
}

} // namespace Atlas::Scripting
